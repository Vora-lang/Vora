#!/usr/bin/env bash
# run_tests.sh — Vora test suite runner (Linux / macOS / WSL)
# Usage:
#   bash tests/run_tests.sh
#
# On Windows, use tests/run_tests.ps1 instead.

set -e

# ──────────────────────────────────────────────────
# Locate the Vora binary
# ──────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Locate the Vora binary — all builds use CMake presets only.
VORA=""
for candidate in \
    "$PROJECT_DIR/build/linux-x64-debug/Vora" \
    "$PROJECT_DIR/build/linux-x64-release/Vora" \
    "$PROJECT_DIR/build/linux-x86-debug/Vora" \
    "$PROJECT_DIR/build/linux-x86-release/Vora" \
    "$PROJECT_DIR/build/linux-aarch64-debug/Vora" \
    "$PROJECT_DIR/build/linux-aarch64-release/Vora" \
    "$PROJECT_DIR/build/linux-armhf-debug/Vora" \
    "$PROJECT_DIR/build/linux-armhf-release/Vora" \
    "$PROJECT_DIR/build/macos-universal-debug/Vora" \
    "$PROJECT_DIR/build/macos-universal-release/Vora"; do
    if [ -x "$candidate" ]; then
        VORA="$candidate"
        break
    fi
done

if [ -z "$VORA" ]; then
    echo "Error: Vora binary not found. Build the project first:"
    echo "  ./build.sh"
    exit 1
fi

echo "Using: $VORA"
echo ""

# ──────────────────────────────────────────────────
# Run tests
# ──────────────────────────────────────────────────

PASS=0
FAIL=0
ERRORS=""

echo "============================================"
echo "  Vora Test Suite"
echo "============================================"
echo ""

for file in "$PROJECT_DIR"/tests/lexer/*.va \
            "$PROJECT_DIR"/tests/parser/*.va \
            "$PROJECT_DIR"/tests/runtime/*.va \
            "$PROJECT_DIR"/tests/interpreter/*.va \
            "$PROJECT_DIR"/tests/formatter/*.va; do
    [ -f "$file" ] || continue
    name="${file#$PROJECT_DIR/tests/}"
    printf "  %-45s " "$name"

    # Pipe stdin from /dev/null so input() hits EOF (returns null) in
    # non-interactive tests. test_input.va gets real data for non-EOF path.
    if [[ "$name" == *"test_input.va" ]]; then
        output=$(printf 'hello\n\n42' | "$VORA" "$file" 2>&1)
    else
        output=$("$VORA" "$file" < /dev/null 2>&1)
    fi
    if [ $? -eq 0 ]; then
        echo "PASS"
        ((PASS++)) || true
    else
        echo "FAIL"
        ((FAIL++)) || true
        ERRORS+=$'\n'"  $name: $output"
    fi
done

echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "============================================"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo "$ERRORS"
    echo ""
    exit 1
fi
