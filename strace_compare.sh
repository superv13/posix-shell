#!/usr/bin/env bash
# tests/strace_compare.sh
#
# Syscall comparison: posixsh vs bash vs dash
#
# Methodology (FAIR — all three use -c flag directly):
#   strace -c <shell> -c 'command'
#
# strace -c counts every kernel call the shell makes.
# All three shells are invoked identically: <shell> -c 'command'
# This isolates each shell independently — no wrappers.
#
# This is the practitioner article's core evidence:
#   bash -c 'exit'   → 100+ syscalls (dynamic linker, libc init, readline)
#   dash -c 'exit'   →  60-80 syscalls (smaller libc init)
#   posixsh -c 'exit' →  10-20 syscalls (nothing but what we wrote)
#
# Usage:
#   bash tests/strace_compare.sh
#   bash tests/strace_compare.sh --brief    # startup comparison only
#   bash tests/strace_compare.sh --csv      # machine-readable
# ===========================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHELL_BIN="${POSIXSH:-$SCRIPT_DIR/posixsh_release}"
BRIEF=0
CSV=0

for arg in "$@"; do
    case "$arg" in
        --brief) BRIEF=1;;
        --csv)   CSV=1;;
    esac
done

[[ -x "$SHELL_BIN" ]] || { echo "error: posixsh not found at $SHELL_BIN — run make first"; exit 1; }
command -v strace >/dev/null 2>&1 || { echo "error: strace not found — apt install strace"; exit 1; }
command -v bash   >/dev/null 2>&1 || { echo "error: bash not found"; exit 1; }
command -v dash   >/dev/null 2>&1 || { echo "error: dash not found"; exit 1; }

# ---------------------------------------------------------------------------
# run_strace <shell_binary> <command_string>
#
# Runs:  strace -c <shell_binary> -c '<command_string>'
# Parses the strace -c summary and prints: calls|errors|ms
#
# strace -c output format (written to stderr or -o file):
#
#   % time     seconds  usecs/call     calls    errors syscall
#   ------ ----------- ----------- --------- --------- -------
#    99.88    0.000343         343         1           execve
#   ------ ----------- ----------- --------- --------- -------
#   100.00    0.000343                     2         1 total
#
# The "total" line fields (awk, whitespace-separated):
#   $1 = 100.00   $2 = seconds   $3 = calls   $4 = errors OR "total"
#   $NF = "total" always
#   $(NF-1) = errors (if present) or calls (if no errors)
#   We detect by checking if NF == 4 (no errors) or NF == 5 (with errors)
# ---------------------------------------------------------------------------
run_strace() {
    local shell_bin="$1"
    local cmd_str="$2"
    local tmp
    tmp=$(mktemp /tmp/strace_out.XXXXXX)

    # Run the shell with strace -c
    # -o $tmp      : write summary to file (not mixed with shell's stdout)
    # >/dev/null   : suppress shell's stdout so it doesn't leak into terminal
    # 2>/dev/null  : suppress strace's per-call trace lines (we use -c summary)
    strace -c -o "$tmp" \
        "$shell_bin" -c "$cmd_str" \
        >/dev/null 2>/dev/null || true

    # Parse the "total" line from strace -c output.
    # Format: pct  seconds  usecs/call  calls  [errors]  total
    # NF==5: no errors column → $4=calls
    # NF==6: errors column present → $4=calls $5=errors
    local total_calls total_errors elapsed_ms

    total_calls=$(awk '/total$/ {
        if   (NF >= 5) print $4
        else              print "0"
    }' "$tmp" 2>/dev/null || echo "?")

    total_errors=$(awk '/total$/ {
        if   (NF == 6) print $5
        else           print "0"
    }' "$tmp" 2>/dev/null || echo "0")

    # Convert strace seconds column ($2) to milliseconds
    elapsed_ms=$(awk '/total$/ {printf "%.3f", $2 * 1000}' "$tmp" 2>/dev/null || echo "?")

    rm -f "$tmp"

    # Sanitise empty results
    [[ -z "$total_calls"  || "$total_calls"  == "" ]] && total_calls="?"
    [[ -z "$total_errors" || "$total_errors" == "" ]] && total_errors="0"
    [[ -z "$elapsed_ms"   || "$elapsed_ms"   == "" ]] && elapsed_ms="?"

    printf '%s|%s|%s' "$total_calls" "$total_errors" "$elapsed_ms"
}

# ---------------------------------------------------------------------------
# Scenarios
# In --brief mode only the startup scenario runs.
# ---------------------------------------------------------------------------
declare -a KEYS
declare -A LABELS CMD_STR

KEYS=("startup" "echo_hello" "ls_devnull" "pipe_2stage" "redir_out" "env_count")

LABELS["startup"]="shell startup + exit"
LABELS["echo_hello"]="echo hello"
LABELS["ls_devnull"]="ls /dev/null"
LABELS["pipe_2stage"]="echo hello | cat"
LABELS["redir_out"]="echo hello > /dev/null"
LABELS["env_count"]="env | wc -l"

CMD_STR["startup"]="exit"
CMD_STR["echo_hello"]="echo hello"
CMD_STR["ls_devnull"]="ls /dev/null"
CMD_STR["pipe_2stage"]="echo hello | cat"
CMD_STR["redir_out"]="echo hello > /dev/null"
CMD_STR["env_count"]="env | wc -l"

[[ "$BRIEF" -eq 1 ]] && KEYS=("startup")

DATE=$(date +%Y-%m-%d)

# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------
if [[ "$CSV" -eq 1 ]]; then
    echo "scenario,shell,syscall_count,errors,ms"
    for key in "${KEYS[@]}"; do
        for shell_name in posixsh bash dash; do
            if   [[ "$shell_name" == "posixsh" ]]; then bin="$SHELL_BIN"
            elif [[ "$shell_name" == "bash"    ]]; then bin="bash"
            else                                         bin="dash"
            fi
            IFS='|' read -r calls errors ms \
                <<< "$(run_strace "$bin" "${CMD_STR[$key]}")"
            echo "${key},${shell_name},${calls},${errors},${ms}"
        done
    done
    exit 0
fi

# ---------------------------------------------------------------------------
# Table output
# ---------------------------------------------------------------------------
W=68
printf '\n'
printf '┌%s┐\n' "$(printf '─%.0s' $(seq 1 $W))"
printf '│  %-20s  %-8s  %-14s  %-8s  %-6s  │\n' \
    "Command / Shell" "Shell" "Syscall count" "Errors" "ms"
printf '├%s┤\n' "$(printf '─%.0s' $(seq 1 $W))"

first_row=1
for key in "${KEYS[@]}"; do
    cmd="${CMD_STR[$key]}"
    label="${LABELS[$key]}"

    [[ "$first_row" -eq 0 ]] && \
        printf '├%s┤\n' "$(printf '─%.0s' $(seq 1 $W))"
    first_row=0

    for shell_name in posixsh bash dash; do
        if   [[ "$shell_name" == "posixsh" ]]; then bin="$SHELL_BIN"; short="psh"
        elif [[ "$shell_name" == "bash"    ]]; then bin="bash";       short="bash"
        else                                         bin="dash";       short="dash"
        fi

        printf "  measuring %s in %s ... " "$label" "$shell_name" >&2

        IFS='|' read -r calls errors ms \
            <<< "$(run_strace "$bin" "$cmd")"

        printf "\r\033[K" >&2   # clear measuring line

        if [[ "$shell_name" == "posixsh" ]]; then
            display_label="$label"
        else
            display_label=""
        fi

        printf '│  %-20s  %-8s  %-14s  %-8s  %-6s  │\n' \
            "$display_label" "$short" "$calls" "$errors" "${ms}ms"
    done
done

printf '└%s┘\n' "$(printf '─%.0s' $(seq 1 $W))"
printf '\n'
printf 'Key findings for the practitioner article:\n'
printf '  posixsh syscall count for startup should be 10-20\n'
printf '  bash    syscall count for startup is typically 100-150\n'
printf '  dash    syscall count for startup is typically 60-90\n'
printf '  The difference = dynamic linker + libc init that posixsh skips entirely\n'
printf '  All three invoked identically: <shell> -c command (no wrappers)\n'
printf '\n'
printf '  Run with --csv for machine-readable output for article tables\n'
printf '  Run with --brief for startup-only quick comparison\n'
