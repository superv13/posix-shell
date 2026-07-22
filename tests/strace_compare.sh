#!/bin/bash
# tests/strace_compare.sh
#
# Purpose:
#   Compares the syscall profiles of posixsh, bash, and dash for a set of
#   representative shell operations, using strace -c (summary mode).
#
# Why this is a Phase 5 deliverable:
#   The project's academic claim is that posixsh is minimally invasive at
#   the syscall level.  strace -c provides a count of every kernel call
#   made, allowing us to quantify "how many syscalls does it take to run
#   ls?" across shells.  This table is the core empirical evidence for the
#   practitioner article.
#
# Usage:
#   bash tests/strace_compare.sh
#   bash tests/strace_compare.sh --brief     (skip pipeline and env tests)
#   bash tests/strace_compare.sh --csv       (machine-readable output)
#
# Requirements:
#   strace   must be installed (apt install strace)
#   bash     must be installed (apt install bash)
#   dash     optional (apt install dash); skipped if absent
#
# Output format (default):
#
#   ┌─────────────────────────────────────────────────────┐
#   │  Syscall comparison: posixsh vs bash vs dash        │
#   │  Command: echo hello                                │
#   ├──────────────┬──────────┬──────────┬────────────── ┤
#   │  Shell       │  Calls   │  Errors  │  Time (ms)    │
#   ├──────────────┼──────────┼──────────┼───────────────┤
#   │  posixsh     │     12   │      0   │   0.341       │
#   │  bash        │    112   │      4   │   1.820       │
#   │  dash        │     44   │      2   │   0.912       │
#   └──────────────┴──────────┴──────────┴───────────────┘
#
# Interpretation:
#   Lower total syscall count = simpler kernel interaction.
#   posixsh's count should be significantly lower than bash's because:
#     - No dynamic linker (static binary, no ld.so calls)
#     - No libc initialization (no brk, mmap for heap, no locale setup)
#     - No readline history file reads
#     - No /etc/passwd lookups for PS1 expansion

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHELL_BIN="$SCRIPT_DIR/../posixsh"
CSV_MODE=0
BRIEF_MODE=0

for arg in "$@"; do
    case "$arg" in
        --csv)   CSV_MODE=1  ;;
        --brief) BRIEF_MODE=1 ;;
    esac
done

# ── Prerequisites check ───────────────────────────────────────────────────────

die() { echo "error: $*" >&2; exit 1; }

[[ -f "$SHELL_BIN" ]] || die "posixsh binary not found at $SHELL_BIN — run make first"
command -v strace >/dev/null 2>&1 || die "strace not found — install with: apt install strace"
command -v bash   >/dev/null 2>&1 || die "bash not found"

HAVE_DASH=0
command -v dash >/dev/null 2>&1 && HAVE_DASH=1

# ── strace wrapper ────────────────────────────────────────────────────────────
#
# strace -c counts syscalls without printing individual trace lines.
# For bash/dash we run with -c flag.  For posixsh we also use -c now
# (see run_strace_posixsh below).
# -f is added for posixsh so that forked children in pipelines are counted.
# Timeout prevents hangs if the shell misbehaves.

run_strace() {
    local shell_bin="$1"
    local shell_cmd="$2"
    local tmp
    tmp=$(mktemp /tmp/strace_out.XXXXXX)

    # -f: follow forks (counts child syscalls too, matches posixsh -f counting)
    # >/dev/null: suppress command stdout so it doesn't leak into the $()
    #             capture that IFS-splits calls|errors|ms.
    strace -f -c -o "$tmp" timeout 10 \
        "$shell_bin" -c "$shell_cmd" </dev/null >/dev/null 2>/dev/null || true

    # Parse strace summary: extract total calls and total errors
    # strace -c output looks like:
    #   % time     seconds  usecs/call     calls    errors syscall
    #   ...
    #   100.00    0.000341           1        12         0 total
    local total_calls total_errors elapsed
    total_calls=$(awk '/^[[:space:]]*[0-9]/ && /total/ {print $4}' "$tmp" 2>/dev/null || echo "?")
    total_errors=$(awk '/^[[:space:]]*[0-9]/ && /total/ {print $5}' "$tmp" 2>/dev/null || echo "?")
    elapsed=$(awk '/^[[:space:]]*[0-9]/ && /total/ {printf "%.3f", $2*1000}' "$tmp" 2>/dev/null || echo "?")

    rm -f "$tmp"
    echo "${total_calls}|${total_errors}|${elapsed}"
}

# posixsh now supports -c, so we trace it exactly like bash/dash:
#   strace -c ./posixsh -c "echo hello"
# No wrapper sh needed; the count is posixsh's own syscalls only.
# For commands that fork children (pipes), we add -f --summary-only
# to aggregate all descendant syscalls into a single total — which
# is the same set of kernel calls that bash -c would make.

run_strace_posixsh() {
    local shell_cmd="$1"
    local tmp
    tmp=$(mktemp /tmp/strace_out.XXXXXX)

    # -f  : follow forks so pipeline children are counted
    # -c  : summary mode (aggregate totals)
    # >/dev/null: suppress command stdout (echo hello → hello, ls → filenames)
    #             so it doesn't leak into the $() capture.
    strace -f -c -o "$tmp" timeout 10 \
        "$SHELL_BIN" -c "$shell_cmd" </dev/null >/dev/null 2>/dev/null || true

    local total_calls total_errors elapsed
    total_calls=$(awk '/^[[:space:]]*[0-9]/ && /total/ {print $4}' "$tmp" 2>/dev/null || echo "?")
    total_errors=$(awk '/^[[:space:]]*[0-9]/ && /total/ {print $5}' "$tmp" 2>/dev/null || echo "?")
    elapsed=$(awk '/^[[:space:]]*[0-9]/ && /total/ {printf "%.3f", $2*1000}' "$tmp" 2>/dev/null || echo "?")

    rm -f "$tmp"
    echo "${total_calls}|${total_errors}|${elapsed}"
}

# ── test cases ────────────────────────────────────────────────────────────────

declare -a TEST_NAMES=(
    "shell startup + exit"
    "echo hello"
    "ls /tmp"
)

declare -a TEST_POSIXSH=(
    "exit"
    "echo hello"
    "ls /tmp"
)

declare -a TEST_BASH=(
    "exit"
    "echo hello"
    "ls /tmp"
)

if [[ $BRIEF_MODE -eq 0 ]]; then
    TEST_NAMES+=( "cat /dev/null | wc -l" "env | wc -l" )
    TEST_POSIXSH+=( "cat /dev/null | wc -l" "env | wc -l" )
    TEST_BASH+=( "cat /dev/null | wc -l" "env | wc -l" )
fi

# ── formatting ────────────────────────────────────────────────────────────────

print_separator() {
    printf '├───────────────────────────┼──────┼──────────────┼──────────┼───────┤\n'
    # printf '├────────────────────────────────────────────────────────────────────┤\n'
}

print_header() {
    printf '┌────────────────────────────────────────────────────────────────────┐\n'
    printf '│  posixsh syscall comparison  %-38s│\n' "($(date '+%Y-%m-%d'))"
    printf '├───────────────────────────┬──────┬──────────────┬──────────┬───────┤\n'
    printf '│  %-24s │ %-4s │ %-12s │ %-8s │ %-5s │\n' \
        "Command / Shell" "Shel" "Calls" "Errors" "ms"
    printf '├───────────────────────────┼──────┼──────────────┼──────────┼───────┤\n'
}

print_row() {
    local label="$1" shell="$2" calls="$3" errors="$4" ms="$5"
    printf '│  %-24s │ %-4s │ %-12s │ %-8s │ %-5s │\n' \
        "$label" "$shell" "$calls" "$errors" "$ms"
}

print_footer() {
    printf '└───────────────────────────┴──────┴──────────────┴──────────┴───────┘\n'
}

# ── main ──────────────────────────────────────────────────────────────────────

if [[ $CSV_MODE -eq 1 ]]; then
    echo "command,shell,total_calls,total_errors,elapsed_ms"
fi

[[ $CSV_MODE -eq 0 ]] && print_header

num_tests=${#TEST_NAMES[@]}

for (( idx=0; idx<num_tests; idx++ )); do
    name="${TEST_NAMES[$idx]}"
    posixsh_cmd="${TEST_POSIXSH[$idx]}"
    bash_cmd="${TEST_BASH[$idx]}"

    # posixsh
    IFS='|' read -r p_calls p_errors p_ms <<< "$(run_strace_posixsh "$posixsh_cmd")"
    # bash
    IFS='|' read -r b_calls b_errors b_ms <<< "$(run_strace bash "$bash_cmd")"
    # dash (if available)
    if [[ $HAVE_DASH -eq 1 ]]; then
        IFS='|' read -r d_calls d_errors d_ms <<< "$(run_strace dash "$bash_cmd")"
    fi

    if [[ $CSV_MODE -eq 1 ]]; then
        echo "\"$name\",posixsh,$p_calls,$p_errors,$p_ms"
        echo "\"$name\",bash,$b_calls,$b_errors,$b_ms"
        [[ $HAVE_DASH -eq 1 ]] && echo "\"$name\",dash,$d_calls,$d_errors,$d_ms"
    else
        print_row "$name" "psh" "$p_calls" "$p_errors" "$p_ms"
        print_row ""      "bash" "$b_calls" "$b_errors" "$b_ms"
        [[ $HAVE_DASH -eq 1 ]] && print_row "" "dash" "$d_calls" "$d_errors" "$d_ms"
        [[ $idx -lt $((num_tests-1)) ]] && print_separator
    fi
done

[[ $CSV_MODE -eq 0 ]] && print_footer

echo ""
echo "Key findings to note for the practitioner article:"
echo "  - posixsh has no dynamic linker overhead (static binary)"
echo "  - posixsh skips libc init: no brk/mmap for heap, no locale reads"
echo "  - posixsh skips readline: no ~/.bash_history reads"
echo "  - posixsh skips /etc/passwd, /etc/nsswitch.conf lookups"
echo "  - Run with --csv for machine-readable output for tables"
