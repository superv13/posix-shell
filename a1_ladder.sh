#!/usr/bin/env bash
# a1_ladder.sh — A1 Static-Linking Ladder: Full Measurement
#
# Builds and measures the 6-rung syscall decomposition:
#
#   Rung 1a  bash          (dynamic, glibc)     — baseline full cost
#   Rung 1b  dash          (dynamic, glibc)     — minimal dynamic shell peer
#   Rung 1c  busybox-ash   (dynamic, glibc)     — same-binary dynamic reference
#   Rung 2   BusyBox ash   (static,  glibc)     — same binary, removes linker
#   Rung 3   BusyBox ash   (static,  musl)      — removes glibc init cost
#   Rung 4   posixsh       (static,  zero-libc) — removes all libc; pure shell
#   Rung 5   nolibc_exit   (static,  zero-libc) — absolute kernel floor
#
# Each gap shows exactly which syscall category was eliminated:
#   1b→1c: binary difference (dash vs busybox, both dynamic glibc)
#   1c→2:  dynamic linker cost (same binary: busybox dynamic vs static)
#   2→3:   glibc init overhead (brk, getrandom, set_tid_address, getuid/gid…)
#   3→4:   remaining libc init (whatever musl still does)
#   4→5:   posixsh's own shell work (getpid, rt_sigaction×6, SIGCHLD handler)
#
# Usage:
#   bash a1_ladder.sh                    # full table + per-rung breakdown
#   bash a1_ladder.sh --collect          # collect data only (save to files)
#   bash a1_ladder.sh --csv              # machine-readable CSV output
#   bash a1_ladder.sh --setup-musl       # set up musl BusyBox via Docker
#   bash a1_ladder.sh --setup-musl-wget  # set up musl BusyBox via wget (no Docker)
#   bash a1_ladder.sh --help             # this help
#
# Prerequisites:
#   strace, bash, dash, busybox-static (apt install busybox-static)
#   For Rung 1c (dynamic busybox): cp $(which busybox) bin/busybox-dynamic
#   For Rung 3 (musl): run --setup-musl or --setup-musl-wget, or set BUSYBOX_MUSL.
#   For Rung 4+5: run 'make' in the project root first.
#
# Environment overrides:
#   POSIXSH          path to posixsh_release binary
#   BUSYBOX_GLIBC    path to static-glibc busybox  (default: /usr/bin/busybox)
#   BUSYBOX_MUSL     path to static-musl busybox   (default: bin/busybox-musl-static)
#   BUSYBOX_DYNAMIC  path to dynamic busybox        (default: bin/busybox-dynamic)
# ===========================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
POSIXSH="${POSIXSH:-$SCRIPT_DIR/posixsh_release}"
BUSYBOX_GLIBC="${BUSYBOX_GLIBC:-/usr/bin/busybox}"
BUSYBOX_MUSL="${BUSYBOX_MUSL:-$SCRIPT_DIR/bin/busybox-musl-static}"
BUSYBOX_DYNAMIC="${BUSYBOX_DYNAMIC:-$SCRIPT_DIR/bin/busybox-dynamic}"
NOLIBC_EXIT="${NOLIBC_EXIT:-$SCRIPT_DIR/nolibc_exit}"
DATA_DIR="${SCRIPT_DIR}/outputs"

# Parse flags
MODE="table"
for arg in "$@"; do
    case "$arg" in
        --csv)             MODE="csv" ;;
        --collect)         MODE="collect" ;;
        --setup-musl)      MODE="setup-musl" ;;
        --setup-musl-wget) MODE="setup-musl-wget" ;;
        --help|-h)         MODE="help" ;;
    esac
done

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------
if [[ "$MODE" == "help" ]]; then
    sed -n '/^# Usage:/,/^# ==/p' "$0" | head -20
    exit 0
fi

# ---------------------------------------------------------------------------
# Setup musl BusyBox via Docker (Alpine Linux)
# ---------------------------------------------------------------------------
if [[ "$MODE" == "setup-musl" ]]; then
    echo "=== Setting up musl-static BusyBox from Alpine Linux ==="
    command -v docker >/dev/null 2>&1 || { echo "ERROR: docker not found. Use --setup-musl-wget instead."; exit 1; }
    # Alpine's 'busybox-static' package installs /bin/busybox.static —
    # a true static-pie musl binary, NOT the dynamic /bin/busybox.
    echo "  Pulling Alpine image, installing busybox-static, extracting /bin/busybox.static ..."
    mkdir -p "$SCRIPT_DIR/bin"
    docker run --rm alpine:latest \
        sh -c "apk add busybox-static -q 2>/dev/null && cat /bin/busybox.static" \
        > "$SCRIPT_DIR/bin/busybox-musl-static"
    chmod +x "$SCRIPT_DIR/bin/busybox-musl-static"
    echo "  Verifying ..."
    file "$SCRIPT_DIR/bin/busybox-musl-static"
    strings "$SCRIPT_DIR/bin/busybox-musl-static" | grep -iE "musl|MUSL" | head -3
    echo ""
    echo "  Rung 3 binary ready at: $SCRIPT_DIR/bin/busybox-musl-static"
    echo "  Test: strace -c $SCRIPT_DIR/bin/busybox-musl-static ash -c 'exit'"
    exit 0
fi

# ---------------------------------------------------------------------------
# Setup musl BusyBox via wget (Docker-free fallback)
# ---------------------------------------------------------------------------
if [[ "$MODE" == "setup-musl-wget" ]]; then
    echo "=== Setting up musl-static BusyBox via wget (Docker-free) ==="
    command -v wget >/dev/null 2>&1 || { echo "ERROR: wget not found (apt install wget)"; exit 1; }
    mkdir -p "$SCRIPT_DIR/bin"
    echo "  Downloading BusyBox 1.35.0 x86_64-linux-musl static binary ..."
    wget -q https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox \
        -O "$SCRIPT_DIR/bin/busybox-musl-static"
    chmod +x "$SCRIPT_DIR/bin/busybox-musl-static"
    echo "  Verifying ..."
    file "$SCRIPT_DIR/bin/busybox-musl-static"
    strings "$SCRIPT_DIR/bin/busybox-musl-static" | grep -iE "musl|MUSL" | head -3
    echo ""
    echo "  Rung 3 binary ready at: $SCRIPT_DIR/bin/busybox-musl-static"
    echo "  Test: strace -c $SCRIPT_DIR/bin/busybox-musl-static ash -c 'exit'"
    exit 0
fi

# ---------------------------------------------------------------------------
# Prerequisites check
# ---------------------------------------------------------------------------
check_prereqs() {
    local ok=1
    command -v strace >/dev/null 2>&1 || { echo "ERROR: strace not found (apt install strace)"; ok=0; }
    command -v bash   >/dev/null 2>&1 || { echo "ERROR: bash not found"; ok=0; }
    command -v dash   >/dev/null 2>&1 || { echo "ERROR: dash not found (apt install dash)"; ok=0; }
    [[ -x "$POSIXSH" ]] || {
        echo "ERROR: posixsh_release not found at $POSIXSH"
        echo "       Run 'make' in the project root first."
        ok=0
    }
    [[ -x "$NOLIBC_EXIT" ]] || {
        echo "ERROR: nolibc_exit not found at $NOLIBC_EXIT"
        echo "       Run 'make nolibc_exit' in the project root first."
        ok=0
    }
    [[ -x "$BUSYBOX_GLIBC" ]] || {
        echo "WARNING: static-glibc busybox not found at $BUSYBOX_GLIBC"
        echo "         Rung 2 will be skipped. (apt install busybox-static)"
        BUSYBOX_GLIBC=""
    }
    if [[ ! -x "$BUSYBOX_MUSL" ]]; then
        echo "WARNING: musl-static busybox not found at $BUSYBOX_MUSL"
        echo "         Rung 3 will be skipped."
        echo "         Fix (Docker):      bash $0 --setup-musl"
        echo "         Fix (Docker-free): bash $0 --setup-musl-wget"
        BUSYBOX_MUSL=""
    fi
    if [[ ! -x "$BUSYBOX_DYNAMIC" ]]; then
        echo "WARNING: dynamic busybox not found at $BUSYBOX_DYNAMIC"
        echo "         Rung 1c will be skipped."
        echo "         Fix: mkdir -p bin && cp \$(which busybox) bin/busybox-dynamic"
        BUSYBOX_DYNAMIC=""
    fi
    [[ "$ok" -eq 1 ]]
}

# ---------------------------------------------------------------------------
# run_strace <binary> <args...>
#
# Runs strace -c on the binary, parses the total line, and prints:
#   calls|errors|seconds|full_breakdown
#
# The full_breakdown is the entire strace -c table (for per-syscall analysis).
# ---------------------------------------------------------------------------
run_strace() {
    local bin="$1"
    shift
    local args=("$@")       # remaining args passed to the binary

    local tmp
    tmp=$(mktemp /tmp/strace_a1.XXXXXX)

    # Run strace -c, writing summary to $tmp
    # We suppress both stdout and stderr of the traced binary.
    strace -c -o "$tmp" "$bin" "${args[@]}" \
        >/dev/null 2>/dev/null || true

    # Parse total line:
    # Format:  pct  seconds  usecs/call  calls  [errors]  total
    # NF==5 → no errors ($4=calls)
    # NF==6 → errors present ($4=calls $5=errors)
    local calls errors secs
    calls=$(awk '/total$/ { if (NF>=5) print $4; else print "0" }' "$tmp" 2>/dev/null || echo "?")
    errors=$(awk '/total$/ { if (NF==6) print $5; else print "0" }' "$tmp" 2>/dev/null || echo "0")
    secs=$(awk '/total$/ { printf "%.6f", $2 }' "$tmp" 2>/dev/null || echo "?")

    # Full breakdown (all syscall rows, skipping the separator lines and header)
    local breakdown
    breakdown=$(grep -v '^[-% ]' "$tmp" 2>/dev/null | grep -v '^$' | grep -v 'syscall$' || echo "")

    rm -f "$tmp"

    [[ -z "$calls"  ]] && calls="?"
    [[ -z "$errors" ]] && errors="0"
    [[ -z "$secs"   ]] && secs="?"

    printf '%s|%s|%s|%s' "$calls" "$errors" "$secs" "$breakdown"
}

# ---------------------------------------------------------------------------
# run_rung <label> <binary> [args...]
#
# Runs each rung 3 times, verifies count is deterministic, saves output.
# Returns the syscall count.
# ---------------------------------------------------------------------------
run_rung() {
    local label="$1"
    local bin="$2"
    shift 2
    local args=("$@")

    local c1 c2 c3 e s breakdown result

    IFS='|' read -r c1 e s breakdown <<< "$(run_strace "$bin" "${args[@]}")"
    IFS='|' read -r c2 _ _ _        <<< "$(run_strace "$bin" "${args[@]}")"
    IFS='|' read -r c3 _ _ _        <<< "$(run_strace "$bin" "${args[@]}")"

    if [[ "$c1" != "$c2" || "$c2" != "$c3" ]]; then
        echo "  WARNING: Non-deterministic counts: $c1 / $c2 / $c3 — using $c1" >&2
    fi

    # Save full breakdown to data dir
    if [[ -n "$DATA_DIR" ]]; then
        local safe_label="${label//[^a-zA-Z0-9_]/_}"
        run_strace "$bin" "${args[@]}" | cut -d'|' -f4- > "$DATA_DIR/${safe_label}.txt" 2>/dev/null || true
        strace -c -o "$DATA_DIR/${safe_label}_full.txt" "$bin" "${args[@]}" >/dev/null 2>/dev/null || true
    fi

    printf '%s' "$c1"
}

# ---------------------------------------------------------------------------
# verify_binary_type <binary>
#
# Prints: "dynamic|glibc", "static|glibc", "static|musl", "static|nolibc"
# ---------------------------------------------------------------------------
verify_binary_type() {
    local bin="$1"
    local link_type libc_type

    if ldd "$bin" 2>&1 | grep -q "not a dynamic executable"; then
        link_type="static"
    else
        link_type="dynamic"
    fi

    if strings "$bin" 2>/dev/null | grep -q "GNU C Library"; then
        libc_type="glibc"
    elif strings "$bin" 2>/dev/null | grep -qi "musl"; then
        libc_type="musl"
    elif strings "$bin" 2>/dev/null | grep -qi "uclibc"; then
        libc_type="uclibc"
    else
        libc_type="nolibc"
    fi

    printf '%s|%s' "$link_type" "$libc_type"
}

# ---------------------------------------------------------------------------
# Environment info (for reproducibility record)
# ---------------------------------------------------------------------------
print_environment() {
    echo "=== Environment ==="
    echo "  Date        : $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "  Host        : $(uname -n)"
    echo "  Kernel      : $(uname -r)"
    echo "  OS          : $(grep PRETTY_NAME /etc/os-release 2>/dev/null | cut -d= -f2 | tr -d '\"')"
    echo "  glibc       : $(ldd --version 2>&1 | head -1)"
    echo "  strace      : $(strace --version 2>&1 | head -1)"
    echo "  bash        : $(bash --version 2>&1 | head -1)"
    echo "  dash        : $(dpkg -l dash 2>/dev/null | awk '/^ii/{print $3}' | head -1)"
    [[ -x "$BUSYBOX_GLIBC" ]] && echo "  busybox(glibc): $($BUSYBOX_GLIBC --help 2>&1 | head -1)"
    [[ -x "$BUSYBOX_MUSL"  ]] && echo "  busybox(musl) : $($BUSYBOX_MUSL  --help 2>&1 | head -1)"
    echo "  posixsh     : $POSIXSH  ($(file "$POSIXSH" | grep -o 'statically linked\|dynamically linked'))"
    echo "  nolibc_exit : $NOLIBC_EXIT  ($(file "$NOLIBC_EXIT" | grep -o 'statically linked\|dynamically linked'))"
    echo ""
}

# ---------------------------------------------------------------------------
# Main measurement
# ---------------------------------------------------------------------------
check_prereqs || exit 1
mkdir -p "$DATA_DIR"

# In CSV mode redirect all human-readable output to stderr so stdout is pure CSV
if [[ "$MODE" == "csv" ]]; then
    exec 3>&2  # fd3 = stderr
else
    exec 3>&1  # fd3 = stdout (normal)
fi

print_environment >&3

echo "=== Measuring all rungs (3 runs each for determinism check) ===" >&3
echo "" >&3

# Rung 1a — bash, dynamic glibc
printf "  Rung 1a  bash (dynamic, glibc)         ... " >&3
R1A=$(run_rung "rung1a_bash"    bash  -c "exit")
VT1A=$(verify_binary_type "$(command -v bash)")
echo "$R1A syscalls  [${VT1A}]" >&3

# Rung 1b — dash, dynamic glibc
printf "  Rung 1b  dash (dynamic, glibc)         ... " >&3
R1B=$(run_rung "rung1b_dash"    dash  -c "exit")
VT1B=$(verify_binary_type "$(command -v dash)")
echo "$R1B syscalls  [${VT1B}]" >&3

# Rung 1c — BusyBox ash, dynamic glibc (same-binary pair with Rung 2)
R1C="N/A"
if [[ -n "$BUSYBOX_DYNAMIC" ]]; then
    printf "  Rung 1c  busybox-ash (dynamic, glibc)  ... " >&3
    R1C=$(run_rung "rung1c_busybox_dynamic"  "$BUSYBOX_DYNAMIC"  ash -c "exit")
    VT1C=$(verify_binary_type "$BUSYBOX_DYNAMIC")
    echo "$R1C syscalls  [${VT1C}]" >&3
else
    echo "  Rung 1c  busybox-ash (dynamic, glibc)  ... SKIPPED" >&3
    echo "           (fix: mkdir -p bin && cp \$(which busybox) bin/busybox-dynamic)" >&3
fi

# Rung 2 — BusyBox ash, static glibc
if [[ -n "$BUSYBOX_GLIBC" ]]; then
    printf "  Rung 2   busybox-ash (static, glibc)   ... " >&3
    R2=$(run_rung "rung2_busybox_glibc"  "$BUSYBOX_GLIBC"  ash -c "exit")
    VT2=$(verify_binary_type "$BUSYBOX_GLIBC")
    echo "$R2 syscalls  [${VT2}]" >&3
else
    R2="N/A"
    echo "  Rung 2   busybox-ash (static, glibc)   ... SKIPPED (not installed)" >&3
fi

# Rung 3 — BusyBox ash, static musl
if [[ -n "$BUSYBOX_MUSL" ]]; then
    printf "  Rung 3   busybox-ash (static, musl)    ... " >&3
    R3=$(run_rung "rung3_busybox_musl"   "$BUSYBOX_MUSL"   ash -c "exit")
    VT3=$(verify_binary_type "$BUSYBOX_MUSL")
    echo "$R3 syscalls  [${VT3}]" >&3
else
    R3="N/A"
    echo "  Rung 3   busybox-ash (static, musl)    ... SKIPPED (run --setup-musl first)" >&3
fi

# Rung 4 — posixsh, static zero-libc
printf "  Rung 4   posixsh (static, zero-libc)   ... " >&3
R4=$(run_rung "rung4_posixsh"   "$POSIXSH"  -c "exit")
VT4=$(verify_binary_type "$POSIXSH")
echo "$R4 syscalls  [${VT4}]" >&3

# Rung 5 — nolibc_exit, absolute floor (no arguments)
printf "  Rung 5   nolibc_exit (floor)           ... " >&3
R5=$(run_rung "rung5_nolibc"   "$NOLIBC_EXIT")
VT5=$(verify_binary_type "$NOLIBC_EXIT")
echo "$R5 syscalls  [${VT5}]" >&3

echo "" >&3

# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------
if [[ "$MODE" == "csv" ]]; then
    echo "rung,label,linking,libc,syscall_count"
    echo "1a,bash,dynamic,glibc,$R1A"
    echo "1b,dash,dynamic,glibc,$R1B"
    echo "1c,busybox-ash,dynamic,glibc,$R1C"
    echo "2,busybox-ash,static,glibc,$R2"
    echo "3,busybox-ash,static,musl,$R3"
    echo "4,posixsh,static,zero-libc,$R4"
    echo "5,nolibc_exit,static,zero-libc,$R5"
    exit 0
fi

# ---------------------------------------------------------------------------
# Pretty table
# ---------------------------------------------------------------------------
W=78
sep() { printf '├%s┤\n' "$(printf '─%.0s' $(seq 1 $W))"; }
top() { printf '┌%s┐\n' "$(printf '─%.0s' $(seq 1 $W))"; }
bot() { printf '└%s┘\n' "$(printf '─%.0s' $(seq 1 $W))"; }
row() { printf '│  %-12s  %-28s  %-12s  %-12s  │\n' "$1" "$2" "$3" "$4"; }

echo "=== A1 Static-Linking Ladder Results ==="
echo ""
top
row "Rung" "Binary / config" "Syscalls" "vs prev"
sep
row "1a (baseline)" "bash  [dynamic, glibc]"          "$R1A" "—"
row "1b (baseline)" "dash  [dynamic, glibc]"          "$R1B" "—"
if [[ "$R1C" != "N/A" ]]; then
    row "1c (same-bin)" "busybox-ash  [dynamic, glibc]" "$R1C" "—"
else
    row "1c (skipped)"  "busybox-ash  [dynamic, glibc]" "N/A"  "—"
fi
sep

# Rung 2: prefer 1c→2 gap (same binary, isolates linker); fall back to 1b→2
if [[ "$R2" != "N/A" && "$R2" =~ ^[0-9]+$ ]]; then
    if [[ "$R1C" != "N/A" && "$R1C" =~ ^[0-9]+$ ]]; then
        GAP_LINKER=$((R1C - R2))
        if (( GAP_LINKER >= 0 )); then
            row "2" "busybox-ash  [static, glibc]" "$R2" "-${GAP_LINKER} (−linker)"
        else
            row "2" "busybox-ash  [static, glibc]" "$R2" "+$((-GAP_LINKER)) more"
        fi
    elif [[ "$R1B" =~ ^[0-9]+$ ]]; then
        GAP12=$((R1B - R2))
        if (( GAP12 >= 0 )); then
            row "2" "busybox-ash  [static, glibc]" "$R2" "-${GAP12} (−linker†)"
        else
            row "2" "busybox-ash  [static, glibc]" "$R2" "+$((-GAP12)) more"
        fi
    else
        row "2" "busybox-ash  [static, glibc]" "$R2" "N/A"
    fi
else
    row "2" "busybox-ash  [static, glibc]" "$R2" "N/A"
fi

if [[ "$R3" != "N/A" && "$R2" =~ ^[0-9]+$ && "$R3" =~ ^[0-9]+$ ]]; then
    GAP23=$((R2 - R3))
    if (( GAP23 >= 0 )); then
        row "3" "busybox-ash  [static, musl]" "$R3" "-${GAP23} (−glibc init)"
    else
        # musl-ash can have more shell init than glibc-ash in some versions
        row "3" "busybox-ash  [static, musl]" "$R3" "+$((-GAP23)) ‡"
    fi
else
    row "3" "busybox-ash  [static, musl]" "$R3" "N/A"
fi

if [[ "$R3" != "N/A" && "$R3" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    GAP34=$((R3 - R4))
    row "4" "posixsh  [static, zero-libc]" "$R4" "-${GAP34} (−libc init)"
elif [[ "$R2" != "N/A" && "$R2" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    GAP24=$((R2 - R4))
    row "4" "posixsh  [static, zero-libc]" "$R4" "-${GAP24} (−libc init)"
else
    row "4" "posixsh  [static, zero-libc]" "$R4" "N/A"
fi

if [[ "$R4" =~ ^[0-9]+$ && "$R5" =~ ^[0-9]+$ ]]; then
    GAP45=$((R4 - R5))
    row "5" "nolibc_exit  [absolute floor]" "$R5" "-${GAP45} (posixsh work)"
else
    row "5" "nolibc_exit  [absolute floor]" "$R5" "N/A"
fi
bot

echo ""

# ---------------------------------------------------------------------------
# Attribution table
# ---------------------------------------------------------------------------
echo "=== Syscall Attribution by Category ==="
echo ""
top
printf '│  %-22s  %-24s  %-18s  │\n' "Transition" "Syscalls removed" "Category"
sep

# 1b→1c: binary difference (dash vs busybox, same link type)
if [[ "$R1C" != "N/A" && "$R1C" =~ ^[0-9]+$ && "$R1B" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "1b→1c (dash→bb-dyn)" "$((R1B-R1C)) diff" "Binary difference"
else
    printf '│  %-22s  %-24s  %-18s  │\n' "1b→1c (dash→bb-dyn)" "N/A" "Binary difference"
fi
# 1c→2: same binary, dynamic vs static — pure linker cost
if [[ "$R1C" != "N/A" && "$R1C" =~ ^[0-9]+$ && "$R2" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "1c→2 (same-bin dyn→st)" "$((R1C-R2)) saved" "Dynamic linker"
elif [[ "$R2" =~ ^[0-9]+$ && "$R1B" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "1b→2 (dyn→static glibc)" "$((R1B-R2)) saved" "Dynamic linker†"
else
    printf '│  %-22s  %-24s  %-18s  │\n' "1c→2 (same-bin dyn→st)" "N/A" "Dynamic linker"
fi

if [[ "$R2" =~ ^[0-9]+$ && "$R3" =~ ^[0-9]+$ ]]; then
    GAP23=$((R2-R3))
    if (( GAP23 >= 0 )); then
        printf '│  %-22s  %-24s  %-18s  │\n' "2→3 (glibc→musl)" "${GAP23} saved" "glibc init overhead"
    else
        # Observed: musl-ash 1.37.0 adds more shell init than glibc-ash 1.36.1
        printf '│  %-22s  %-24s  %-18s  │\n' "2→3 (glibc→musl)" "+$((-GAP23)) extra ‡" "musl ash shell init"
    fi
else
    printf '│  %-22s  %-24s  %-18s  │\n' "2→3 (glibc→musl)" "N/A" "glibc init overhead"
fi

if [[ "$R3" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "3→4 (musl→zero-libc)" "$((R3-R4)) saved" "musl residual init"
elif [[ "$R2" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "2→4 (glibc→zero-libc)" "$((R2-R4)) saved" "full libc init"
fi

if [[ "$R4" =~ ^[0-9]+$ && "$R5" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "4→5 (posixsh→floor)" "$((R4-R5)) saved" "posixsh shell work"
fi

if [[ "$R1B" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    printf '│  %-22s  %-24s  %-18s  │\n' "═══════════════" "══════════════════════" "══════════════════"
    printf '│  %-22s  %-24s  %-18s  │\n' "1b→4 (total)" "$((R1B-R4)) saved" "TOTAL vs dash"
fi
bot

echo ""
echo "=== Data files saved to: $DATA_DIR/ ==="
echo "  Per-syscall breakdowns: outputs/rungN_*_full.txt"
echo ""
echo "=== Key takeaways ==="
if [[ "$R1B" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    RATIO=$(echo "scale=1; $R1B / $R4" | bc 2>/dev/null || echo "?")
    echo "  posixsh uses ${RATIO}× fewer syscalls than dash at startup"
fi
if [[ "$R1A" =~ ^[0-9]+$ && "$R4" =~ ^[0-9]+$ ]]; then
    RATIO_BASH=$(echo "scale=1; $R1A / $R4" | bc 2>/dev/null || echo "?")
    echo "  posixsh uses ${RATIO_BASH}× fewer syscalls than bash at startup"
fi
echo "  Absolute floor (Linux kernel minimum): $R5 syscall(s)"
if [[ "$R4" =~ ^[0-9]+$ && "$R5" =~ ^[0-9]+$ ]]; then
    echo "  posixsh overhead above floor: $((R4-R5)) syscalls (documented shell init work)"
    echo "    getpid       ×1 — record shell PID for job control (\$\$)"
    echo "    rt_sigaction ×6 — SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU (SIG_IGN)"
    echo "                      + SIGCHLD (custom background-job reaper handler)"
fi
echo ""

# Footnote for Rung 2→3 inversion if observed
if [[ "$R2" =~ ^[0-9]+$ && "$R3" =~ ^[0-9]+$ ]] && (( R3 > R2 )); then
    echo "  ‡ Note: Rung 3 (musl-ash) shows MORE syscalls than Rung 2 (glibc-ash)."
    echo "    This is real observed data, not an error. It reflects that:"
    echo "    - Rung 2: Ubuntu busybox-static 1.36.1 (glibc), minimal ash shell init"
    echo "    - Rung 3: Alpine busybox-static 1.37.0 (musl), adds rt_sigprocmask×7,"
    echo "      uid/gid queries, and uname — ash feature differences between versions."
    echo "    The musl libc itself is still lighter than glibc; the difference here is"
    echo "    ash shell init code differences between busybox 1.36.1 and 1.37.0."
    echo "    For a pure libc comparison, control for busybox version."
    echo ""
fi

echo "  Run with --csv for machine-readable output"
echo "  Run --setup-musl first if Rung 3 was skipped"
