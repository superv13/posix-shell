#!/usr/bin/env bash
# tests/osh_spec/download.sh — Fetch OSH spec test files (A2 POSIX compliance)
#
# Strategy:
#   1. Sparse git clone of oils-for-unix/oils (depth=1, blob filter) — gets ALL
#      spec/*.test.sh in one shot without downloading 600 MB of source.
#   2. Per-file curl fallback for any files missing after the clone (e.g. in
#      restricted environments where git filters are unavailable).
#
# Output: tests/osh_spec/spec/*.test.sh
#
# Usage (from repo root):
#   bash tests/osh_spec/download.sh
# ===========================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SPEC_DIR="$SCRIPT_DIR/spec"
TMP_REPO="/tmp/oils-repo-a2"
RAW_BASE="https://raw.githubusercontent.com/oils-for-unix/oils/master/spec"

mkdir -p "$SPEC_DIR"

echo "=== A2: Fetching OSH Spec Test Suite ==="
echo "    Target: $SPEC_DIR"
echo ""

# ---------------------------------------------------------------------------
# Step 1: Sparse git clone (preferred — gets all spec files at once)
# ---------------------------------------------------------------------------
echo "[1/2] Attempting sparse git clone from oils-for-unix/oils ..."
rm -rf "$TMP_REPO"

CLONE_OK=0
if command -v git &>/dev/null; then
    git clone \
        --depth=1 \
        --filter=blob:none \
        --sparse \
        https://github.com/oils-for-unix/oils.git \
        "$TMP_REPO" 2>&1 && CLONE_OK=1 || true
fi

if [[ "$CLONE_OK" -eq 1 && -d "$TMP_REPO" ]]; then
    cd "$TMP_REPO"
    git sparse-checkout set spec/ 2>&1 || true
    cd "$REPO_ROOT"

    COPIED=0
    if compgen -G "$TMP_REPO/spec/*.test.sh" > /dev/null 2>&1; then
        cp "$TMP_REPO"/spec/*.test.sh "$SPEC_DIR"/
        COPIED=$(ls -1 "$SPEC_DIR"/*.test.sh 2>/dev/null | wc -l)
        echo "    Sparse clone succeeded: $COPIED spec files copied."
    else
        echo "    Sparse clone ran but spec/ has no .test.sh files — will use curl fallback."
    fi
    rm -rf "$TMP_REPO"
else
    echo "    Sparse clone unavailable or failed — using curl fallback."
    rm -rf "$TMP_REPO"
fi

# ---------------------------------------------------------------------------
# Step 2: Per-file curl/wget fallback for any missing files
# ---------------------------------------------------------------------------
TEST_FILES=(
    "pipeline.test.sh"
    "redirect.test.sh"
    "quote.test.sh"
    "var-sub.test.sh"
    "var-op-len.test.sh"
    "word-split.test.sh"
    "builtin-special.test.sh"
    "background.test.sh"
    "job-control.test.sh"
    "if_.test.sh"
    "for-expr.test.sh"
    "command-sub.test.sh"
    "here-doc.test.sh"
    "arith.test.sh"
)

echo "[2/2] Verifying and fetching any missing spec files via curl ..."
FETCHED=0
SKIPPED=0
FAILED=0

for file in "${TEST_FILES[@]}"; do
    target="$SPEC_DIR/$file"
    if [[ -f "$target" && -s "$target" ]]; then
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    url="$RAW_BASE/$file"
    if command -v curl &>/dev/null; then
        if curl -sSL --fail "$url" -o "$target" 2>/dev/null; then
            echo "    + Fetched $file"
            FETCHED=$((FETCHED + 1))
        else
            echo "    ! Could not fetch $file (not found or no network)"
            FAILED=$((FAILED + 1))
        fi
    elif command -v wget &>/dev/null; then
        if wget -q "$url" -O "$target" 2>/dev/null; then
            echo "    + Fetched $file (wget)"
            FETCHED=$((FETCHED + 1))
        else
            echo "    ! Could not fetch $file (wget)"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "    ! Neither curl nor wget available — cannot fetch $file"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
COUNT=$(ls -1 "$SPEC_DIR"/*.test.sh 2>/dev/null | wc -l)
echo "=== Setup complete ==="
echo "    $COUNT test files in $SPEC_DIR"
[[ "$FETCHED"  -gt 0 ]] && echo "    Fetched via curl/wget: $FETCHED"
[[ "$SKIPPED"  -gt 0 ]] && echo "    Already present:       $SKIPPED"
[[ "$FAILED"   -gt 0 ]] && echo "    Could not fetch:       $FAILED"
echo ""
echo "Next steps:"
echo "  Sanity check : python3 tests/osh_spec/runner.py tests/osh_spec/spec/pipeline.test.sh ./posixsh --verbose"
echo "  Full report  : python3 tests/osh_spec/compliance_report.py --target ./posixsh"
