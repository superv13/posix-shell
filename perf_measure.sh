#!/usr/bin/env bash
# tests/perf_measure.sh
#
# Performance comparison: posixsh vs bash vs dash
#
# Methodology (FAIR — all three use -c flag directly):
#   time posixsh -c 'command'
#   time bash    -c 'command'
#   time dash    -c 'command'
#
# All three shells are invoked identically as a fresh subprocess.
# No wrappers, no pty harness, no intermediate Python.
# This is what the supervisor asked for: independent measurement.
#
# Usage:
#   bash tests/perf_measure.sh
#   bash tests/perf_measure.sh --reps 20     # tighter medians
#   bash tests/perf_measure.sh --csv          # spreadsheet output
# ===========================================================================

set -euo pipefail

SHELL_BIN="${POSIXSH:-$(dirname "$0")/posixsh_release}"
REPS=5
CSV=0

while (( $# > 0 )); do
    case "$1" in
        --reps)  shift; REPS="${1:?'--reps requires a number'}";;
        --csv)   CSV=1;;
    esac
    shift
done

[[ -x "$SHELL_BIN" ]] || { echo "error: posixsh not found at $SHELL_BIN — run make first"; exit 1; }
command -v bash >/dev/null 2>&1 || { echo "error: bash not found"; exit 1; }
command -v dash >/dev/null 2>&1 || { echo "error: dash not found"; exit 1; }

# ---------------------------------------------------------------------------
# time_cmd <shell_binary> <command_string>
#
# Runs:  <shell_binary> -c '<command_string>'
# Returns elapsed real time in seconds (e.g. 0.00312)
#
# Uses /usr/bin/time -f "%e" for portable sub-millisecond timing.
# Falls back to bash TIMEFORMAT if /usr/bin/time is absent.
# ---------------------------------------------------------------------------
time_cmd() {
    local shell_bin="$1"
    local cmd_str="$2"
    local t0 t1 ns

    # Use date +%s%N for nanosecond-precision wall-clock timing.
    # /usr/bin/time -f "%e" only gives centisecond resolution (2 decimal
    # places), so all sub-10ms commands show as 0.00.  date +%s%N works
    # on Linux and gives true sub-millisecond accuracy.
    t0=$(date +%s%N)
    "$shell_bin" -c "$cmd_str" >/dev/null 2>&1
    t1=$(date +%s%N)
    ns=$(( t1 - t0 ))
    # Return elapsed time in seconds with 6 decimal places (microsecond res)
    printf '%s' "$(awk -v ns="$ns" 'BEGIN{printf "%.6f", ns/1e9}')"
}

# ---------------------------------------------------------------------------
# median of N runs
# ---------------------------------------------------------------------------
median_time() {
    local shell_bin="$1"
    local cmd_str="$2"
    local times=()

    for _ in $(seq 1 "$REPS"); do
        times+=("$(time_cmd "$shell_bin" "$cmd_str")")
    done

    # Sort numerically and pick the middle value
    printf '%s\n' "${times[@]}" | sort -n | awk -v n="$REPS" 'NR==int(n/2)+1{print}'
}

# ---------------------------------------------------------------------------
# format seconds → "  4.72 ms"
# Display in milliseconds — much more readable at shell-startup timescales.
# ---------------------------------------------------------------------------
fmt() { printf '%7.3f ms' "$(awk -v s="$1" 'BEGIN{printf "%.3f", s*1000}')"; }

# ---------------------------------------------------------------------------
# Scenarios — identical command string given to all three shells via -c
# ---------------------------------------------------------------------------
declare -A SCENARIOS
declare -a SCENARIO_ORDER=(
    "startup"
    "echo_hello"
    "ls_devnull"
    "pipe_2stage"
    "pipe_3stage"
    "redir_out"
    "redir_append"
)

SCENARIOS["startup"]="exit"
SCENARIOS["echo_hello"]="echo hello"
SCENARIOS["ls_devnull"]="ls /dev/null"
SCENARIOS["pipe_2stage"]="echo hello | cat"
SCENARIOS["pipe_3stage"]="echo hello | cat | cat"
SCENARIOS["redir_out"]="echo hello > /dev/null"
SCENARIOS["redir_append"]="echo hello >> /dev/null"

declare -A LABELS=(
    ["startup"]="Startup + exit"
    ["echo_hello"]="echo hello"
    ["ls_devnull"]="ls /dev/null"
    ["pipe_2stage"]="echo | cat (2-stage)"
    ["pipe_3stage"]="echo | cat | cat (3-stage)"
    ["redir_out"]="echo > /dev/null"
    ["redir_append"]="echo >> /dev/null"
)

# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------
if [[ "$CSV" -eq 1 ]]; then
    echo "scenario,posixsh_s,bash_s,dash_s"
    for key in "${SCENARIO_ORDER[@]}"; do
        cmd="${SCENARIOS[$key]}"
        p=$(median_time "$SHELL_BIN" "$cmd")
        b=$(median_time "bash"        "$cmd")
        d=$(median_time "dash"        "$cmd")
        echo "$key,$p,$b,$d"
    done
    exit 0
fi

# ---------------------------------------------------------------------------
# Table output
# ---------------------------------------------------------------------------
SEP="├────────────────────────┼─────────────┼─────────────┼─────────────┼────────┤"
TOP="┌────────────────────────┬─────────────┬─────────────┬─────────────┬────────┐"
BOT="└────────────────────────┴─────────────┴─────────────┴─────────────┴────────┘"
HDR="│  Scenario               │   posixsh   │    bash     │    dash     │ p/bash │"
DIV="│                         │             │             │             │        │"

echo ""
printf "  posixsh performance vs bash vs dash\n"
printf "  Methodology: each shell invoked as  <shell> -c 'command'  (fair, independent)\n"
printf "  Median of %d runs. Lower = faster.\n\n" "$REPS"
echo "$TOP"
echo "$HDR"
echo "$SEP"

for key in "${SCENARIO_ORDER[@]}"; do
    cmd="${SCENARIOS[$key]}"
    label="${LABELS[$key]}"

    printf "  measuring: %s ... " "$label" >&2

    p=$(median_time "$SHELL_BIN" "$cmd")
    b=$(median_time "bash"        "$cmd")
    d=$(median_time "dash"        "$cmd")

    printf "\r\033[K" >&2   # clear the "measuring" line

    # Compute speedup ratio posixsh vs bash
    ratio=$(awk -v p="$p" -v b="$b" 'BEGIN{if(b>0) printf "%.1f", p/b; else print "N/A"}')

    printf "│  %-23s │  %s  │  %s  │  %s  │ %-6s │\n" \
        "$label" "$(fmt "$p")" "$(fmt "$b")" "$(fmt "$d")" "${ratio}x"
done

echo "$BOT"

echo ""
echo "Notes:"
echo "  - Each shell invoked identically: <shell> -c 'command'"
echo "  - No wrappers, no pty harness — all three measured independently"
echo "  - Run with --reps 20 for tighter medians on a loaded system"
echo "  - Run with --csv for spreadsheet-importable output"
echo "  - 'Startup + exit' is the cleanest measure of shell bootstrap cost"
echo "  - posixsh has no dynamic linker, no libc init, no readline setup"
echo "  - See strace_compare.sh for syscall-level explanation of differences"
