#!/bin/bash
# tests/perf_measure.sh
#
# Purpose:
#   Measures and compares the performance of posixsh vs bash vs dash across
#   key scenarios.  Results feed the performance section of the practitioner
#   article (A3 — Tighten Wall-Clock Methodology).
#
# Scenarios:
#   1. Startup + exit        — time to start, do nothing, and exit
#   2. 100 pipelines         — 100 two-stage pipelines in one session
#   3. 1 echo (fair)         — single echo, identical -c string for all shells
#   4. 1 redirect (fair)     — single redirect, identical -c string
#   5. 100 echo (loop*)      — 100 echo commands; bash/dash use native for-loop
#   6. 100 redirect (loop*)  — 100 appends; bash/dash use native for-loop
#
# Methodology (A3):
#   Each scenario is run REPS times (default 50; use --reps 100 for paper).
#   One warm-up run is discarded before the measurement loop to ensure the
#   page cache is hot.  Statistics reported:
#     - Median   : main result, robust to outliers
#     - Mean     : secondary; compare with median to spot skew
#     - 95% CI   : mean +/- 1.96 x (std / sqrt(N))   -- what sir asked for
#     - IQR      : Q3 - Q1, non-parametric spread
#     - Std, Min, Max : full diagnostic picture
#
#   Timing uses date +%s%N (nanosecond precision).
#   Statistics are computed by an inline Python 3 block (no extra deps).
#
# CPU pinning (--pin-cpu):
#   Attempts to set the governor to "performance" and disable turbo boost.
#   Requires sudo.  Skipped gracefully if root is not available.
#
# Usage:
#   bash tests/perf_measure.sh                          # 50 reps, human table
#   bash tests/perf_measure.sh --reps 100               # paper-quality run
#   bash tests/perf_measure.sh --pin-cpu                # set governor + no turbo
#   bash tests/perf_measure.sh --csv                    # machine-readable CSV
#   bash tests/perf_measure.sh --reps 100 --pin-cpu --csv   # full paper run

set -euo pipefail

# ── defaults ──────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHELL_BIN="$SCRIPT_DIR/../posixsh"
REPS=50
CSV_MODE=0
PIN_CPU=0

# ── argument parsing ──────────────────────────────────────────────────────────

for (( i=1; i<=$#; i++ )); do
    case "${!i}" in
        --reps)
            j=$((i+1))
            REPS="${!j}"
            ;;
        --csv)
            CSV_MODE=1
            ;;
        --pin-cpu)
            PIN_CPU=1
            ;;
        --help|-h)
            grep '^#' "$0" | grep -v '#!/' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
    esac
done

# ── sanity checks ─────────────────────────────────────────────────────────────

die() { echo "error: $*" >&2; exit 1; }
[[ -f "$SHELL_BIN" ]] || die "posixsh not found at $SHELL_BIN — run make first"
command -v bash   >/dev/null 2>&1 || die "bash not found"
command -v python3 >/dev/null 2>&1 || die "python3 not found (needed for statistics)"

HAVE_DASH=0
command -v dash >/dev/null 2>&1 && HAVE_DASH=1

TMPFILE=$(mktemp /tmp/posixsh_perf.XXXXXX)
trap 'rm -f "$TMPFILE"' EXIT

# ── CPU pinning ───────────────────────────────────────────────────────────────
#
# Attempts to:
#   1. Set the scaling governor to "performance" on all CPUs
#   2. Disable Intel Turbo Boost (or AMD boost)
# Falls back gracefully if cpufreq is missing or sudo is not available.

GOVERNOR_STATUS="unknown"
TURBO_STATUS="unknown"

pin_cpu() {
    # Governor
    if ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1; then
        if echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1; then
            GOVERNOR_STATUS="performance  [pinned]"
        else
            local cur_gov
            cur_gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
            GOVERNOR_STATUS="${cur_gov}  [not pinned — sudo failed; CIs may be wider]"
        fi
    else
        GOVERNOR_STATUS="N/A  [cpufreq not available]"
    fi

    # Turbo — Intel
    if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
        if echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo >/dev/null 2>&1; then
            TURBO_STATUS="disabled  [pinned]"
        else
            TURBO_STATUS="unknown  [not controlled — sudo failed]"
        fi
    # Turbo — AMD
    elif [[ -f /sys/devices/system/cpu/cpufreq/boost ]]; then
        if echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null 2>&1; then
            TURBO_STATUS="disabled  [pinned]"
        else
            TURBO_STATUS="unknown  [not controlled — sudo failed]"
        fi
    else
        TURBO_STATUS="N/A  [no turbo control found]"
    fi
}

if [[ $PIN_CPU -eq 1 ]]; then
    pin_cpu
else
    # Read current governor/turbo state for the environment header (read-only)
    if [[ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        cur_gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
        GOVERNOR_STATUS="${cur_gov}  [not pinned — use --pin-cpu for tighter CIs]"
    else
        GOVERNOR_STATUS="N/A  [cpufreq not available]"
    fi
    if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
        cur_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo "?")
        [[ "$cur_turbo" == "1" ]] && TURBO_STATUS="disabled" || TURBO_STATUS="enabled  [not controlled — use --pin-cpu]"
    elif [[ -f /sys/devices/system/cpu/cpufreq/boost ]]; then
        cur_boost=$(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo "?")
        [[ "$cur_boost" == "0" ]] && TURBO_STATUS="disabled" || TURBO_STATUS="enabled  [not controlled — use --pin-cpu]"
    else
        TURBO_STATUS="N/A"
    fi
fi

# ── timing helpers ────────────────────────────────────────────────────────────

time_it_posixsh() {
    local script="$1"
    local start end elapsed
    start=$(date +%s%N)
    "$SHELL_BIN" -c "$script" >/dev/null 2>&1 || true
    end=$(date +%s%N)
    echo $(( end - start ))
}

time_it_shell() {
    local shell_bin="$1"
    local script="$2"
    local start end elapsed
    start=$(date +%s%N)
    "$shell_bin" -c "$script" >/dev/null 2>&1 || true
    end=$(date +%s%N)
    echo $(( end - start ))
}

# ── statistics block ──────────────────────────────────────────────────────────
#
# compute_stats <nanosecond-integer> [<nanosecond-integer>...]
#
# Prints 9 values (space-separated), all in milliseconds:
#   median  mean  ci_low  ci_high  iqr_low(q1)  iqr_high(q3)  std  min  max

compute_stats() {
    python3 - "$@" <<'PYEOF'
import sys, math

ns_vals = [int(x) for x in sys.argv[1:]]
ms_vals = [v / 1_000_000.0 for v in ns_vals]
n = len(ms_vals)

if n == 0:
    print("0 0 0 0 0 0 0 0 0")
    sys.exit(0)

ms_sorted = sorted(ms_vals)

# Median
if n % 2 == 1:
    med = ms_sorted[n // 2]
else:
    med = (ms_sorted[n // 2 - 1] + ms_sorted[n // 2]) / 2.0

# Mean, std dev (sample, ddof=1)
mean = sum(ms_vals) / n
if n > 1:
    variance = sum((x - mean) ** 2 for x in ms_vals) / (n - 1)
    std = math.sqrt(variance)
else:
    std = 0.0

# 95% CI: mean +/- 1.96 * std / sqrt(N)
half = 1.96 * std / math.sqrt(n)
ci_low  = mean - half
ci_high = mean + half

# IQR via linear interpolation
def pct(data, p):
    idx  = (len(data) - 1) * p / 100.0
    lo   = int(idx)
    hi   = lo + 1
    frac = idx - lo
    if hi >= len(data):
        return data[lo]
    return data[lo] * (1 - frac) + data[hi] * frac

q1 = pct(ms_sorted, 25)
q3 = pct(ms_sorted, 75)

half = (ci_high - ci_low) / 2
print(f"{med:.4f} {mean:.4f} {ci_low:.4f} {ci_high:.4f} {half:.4f} {q1:.4f} {q3:.4f} {std:.4f} {ms_sorted[0]:.4f} {ms_sorted[-1]:.4f}")
PYEOF
}



# ── benchmark runner ──────────────────────────────────────────────────────────
#
# run_benchmark <display_name> <csv_key> <posixsh_script> <bash_script>

run_benchmark() {
    local name="$1"
    local csv_key="$2"
    local posixsh_script="$3"
    local bash_script="$4"

    local p_ns=()
    local b_ns=()
    local d_ns=()

    # posixsh — warm-up then REPS timed runs
    "$SHELL_BIN" -c "$posixsh_script" >/dev/null 2>&1 || true
    for (( r=0; r<REPS; r++ )); do
        p_ns+=( "$(time_it_posixsh "$posixsh_script")" )
    done

    # bash
    bash -c "$bash_script" >/dev/null 2>&1 || true
    for (( r=0; r<REPS; r++ )); do
        b_ns+=( "$(time_it_shell bash "$bash_script")" )
    done

    # dash
    if [[ $HAVE_DASH -eq 1 ]]; then
        dash -c "$bash_script" >/dev/null 2>&1 || true
        for (( r=0; r<REPS; r++ )); do
            d_ns+=( "$(time_it_shell dash "$bash_script")" )
        done
    fi

    # Compute stats
    local p_stats b_stats d_stats
    p_stats=$(compute_stats "${p_ns[@]}")
    b_stats=$(compute_stats "${b_ns[@]}")
    if [[ $HAVE_DASH -eq 1 ]]; then
        d_stats=$(compute_stats "${d_ns[@]}")
    else
        d_stats="N/A N/A N/A N/A N/A N/A N/A N/A N/A N/A"

    fi

    # Unpack — order: median mean ci_low ci_high ci_half q1 q3 std min max
    read -r p_med p_mean p_cilo p_cihi p_half p_q1 p_q3 p_std p_min p_max <<< "$p_stats"
    read -r b_med b_mean b_cilo b_cihi b_half b_q1 b_q3 b_std b_min b_max <<< "$b_stats"
    read -r d_med d_mean d_cilo d_cihi d_half d_q1 d_q3 d_std d_min d_max <<< "$d_stats"

    if [[ $CSV_MODE -eq 1 ]]; then
        # scenario,shell,reps,median_ms,mean_ms,ci_low_ms,ci_high_ms,iqr_low_ms,iqr_high_ms,std_ms,min_ms,max_ms
        echo "${csv_key},posixsh,${REPS},${p_med},${p_mean},${p_cilo},${p_cihi},${p_q1},${p_q3},${p_std},${p_min},${p_max}"
        echo "${csv_key},bash,${REPS},${b_med},${b_mean},${b_cilo},${b_cihi},${b_q1},${b_q3},${b_std},${b_min},${b_max}"
        [[ $HAVE_DASH -eq 1 ]] && \
            echo "${csv_key},dash,${REPS},${d_med},${d_mean},${d_cilo},${d_cihi},${d_q1},${d_q3},${d_std},${d_min},${d_max}"
    else
        # Two lines per shell: line 1 = median + CI, line 2 = IQR + std
        # Format designed to fit ~100 columns.
        printf '│  %-18s  posixsh  median %7s ms  +/-%s ms  CI [%s - %s]\n' \
            "$name" "$p_med" "$p_half" "$p_cilo" "$p_cihi"
        printf '│  %-18s           mean   %7s ms   IQR [%s - %s]   std %s ms\n' \
            "" "$p_mean" "$p_q1" "$p_q3" "$p_std"

        printf '│  %-18s  bash     median %7s ms  +/-%s ms  CI [%s - %s]\n' \
            "" "$b_med" "$b_half" "$b_cilo" "$b_cihi"
        printf '│  %-18s           mean   %7s ms   IQR [%s - %s]   std %s ms\n' \
            "" "$b_mean" "$b_q1" "$b_q3" "$b_std"

        if [[ $HAVE_DASH -eq 1 ]]; then
            printf '│  %-18s  dash     median %7s ms  +/-%s ms  CI [%s - %s]\n' \
                "" "$d_med" "$d_half" "$d_cilo" "$d_cihi"
            printf '│  %-18s           mean   %7s ms   IQR [%s - %s]   std %s ms\n' \
                "" "$d_mean" "$d_q1" "$d_q3" "$d_std"
        fi

        echo "│"
    fi
}

# ── scenario definitions ──────────────────────────────────────────────────────

build_script() {
    local cmd="$1" n="$2" script=""
    for (( i=0; i<n; i++ )); do script+="${cmd}"$'\n'; done
    echo "$script"
}

STARTUP_POSIXSH="exit"
STARTUP_BASH="exit 0"

PIPELINE_POSIXSH="$(build_script "echo x | cat > /dev/null" 100)"
PIPELINE_BASH='for i in $(seq 100); do echo x | cat > /dev/null; done'

ECHO_POSIXSH="$(build_script "echo hello_world" 100)"
ECHO_BASH='for i in $(seq 100); do echo hello_world; done'

REDIR_POSIXSH="$(build_script "echo line >> $TMPFILE" 100)"
REDIR_BASH="for i in \$(seq 100); do echo line >> $TMPFILE; done"

ECHO1_POSIXSH="echo hello_world"
ECHO1_BASH="echo hello_world"
REDIR1_POSIXSH="echo line >> $TMPFILE"
REDIR1_BASH="echo line >> $TMPFILE"

# ── environment header ────────────────────────────────────────────────────────

CPU_MODEL=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' | xargs || echo "unknown")
KERNEL=$(uname -r 2>/dev/null || echo "unknown")
OS_DESC=$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || echo "unknown")

if [[ $CSV_MODE -eq 0 ]]; then
    echo ""
    echo "┌────────────────────────────────────────────────────────────────────────────┐"
    printf "│  posixsh A3 wall-clock measurement  %s  %s │\n" "$(date '+%Y-%m-%d %H:%M')" "              "
    echo "├────────────────────────────────────────────────────────────────────────────┤"
    printf "│  CPU:      %-63s│\n" "$CPU_MODEL"
    printf "│  OS:       %-63s│\n" "$OS_DESC"
    printf "│  Kernel:   %-63s│\n" "$KERNEL"
    printf "│  Governor: %-63s│\n" "$GOVERNOR_STATUS"
    printf "│  Turbo:    %-63s│\n" "$TURBO_STATUS"
    printf "│  Reps:     %-63s│\n" "$REPS  (+ 1 warm-up discarded per scenario)"
    printf "│  Method:   %-63s│\n" "date +%s%N, Python3 stats inline"
    echo "├────────────────────────────────────────────────────────────────────────────┤"
    printf "│  %-74s│\n" "Columns: median  +/-95%CI  [CI range]  IQR [Q1-Q3]  std (all ms)"
    echo "├────────────────────────────────────────────────────────────────────────────┤"
    echo "│"
fi

if [[ $CSV_MODE -eq 1 ]]; then
    echo "scenario,shell,reps,median_ms,mean_ms,ci_low_ms,ci_high_ms,iqr_low_ms,iqr_high_ms,std_ms,min_ms,max_ms"
fi

# ── run all benchmarks ────────────────────────────────────────────────────────

run_benchmark "Startup + exit"     "startup"   "$STARTUP_POSIXSH"  "$STARTUP_BASH"
run_benchmark "100 pipelines"      "pipelines" "$PIPELINE_POSIXSH" "$PIPELINE_BASH"
run_benchmark "1 echo (fair)"      "echo1"     "$ECHO1_POSIXSH"    "$ECHO1_BASH"
run_benchmark "1 redirect (fair)"  "redir1"    "$REDIR1_POSIXSH"   "$REDIR1_BASH"
run_benchmark "100 echo (loop*)"   "echo100"   "$ECHO_POSIXSH"     "$ECHO_BASH"
run_benchmark "100 redir (loop*)"  "redir100"  "$REDIR_POSIXSH"    "$REDIR_BASH"

# ── footer ────────────────────────────────────────────────────────────────────

if [[ $CSV_MODE -eq 0 ]]; then
    echo "└────────────────────────────────────────────────────────────────────────────┘"
    echo ""
    echo "Notes:"
    echo "  - Median is the main result; mean and CI are shown for transparency."
    echo "  - 95% CI = mean +/- 1.96 x (std / sqrt(N)).  Wider => more noise."
    echo "  - IQR = Q3 - Q1  (non-parametric spread, robust to extreme outliers)."
    echo "  - FAIR rows: identical -c string across all shells — true apples-to-apples."
    echo "  - loop* rows: bash/dash use a native for-loop (0 extra execs); posixsh"
    echo "    runs 100 separate parse+execute cycles — these rows favour bash/dash."
    echo "  - Warm-up: 1 discarded run before each scenario ensures hot page cache."
    if [[ $PIN_CPU -eq 0 ]]; then
        echo "  - CPU not pinned: run with --pin-cpu (needs sudo) for tighter CIs."
        echo "    Paper note: 'CPU governor was not pinned; 100-rep CI accounts for variation.'"
    fi
    echo ""
    echo "  Paper run:   bash tests/perf_measure.sh --reps 100 --pin-cpu --csv > a3_results.csv"
    echo "  Quick check: bash tests/perf_measure.sh --reps 50"
    echo "  Syscalls:    bash tests/strace_compare.sh   (deterministic; no CI needed)"
fi
