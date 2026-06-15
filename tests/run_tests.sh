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

# Locate the Vora binary — preset paths first (consistent with build.sh),
# then legacy generic paths as fallback.
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
    "$PROJECT_DIR/build/macos-universal-release/Vora" \
    "$PROJECT_DIR/build/Vora" \
    "$PROJECT_DIR/build/vora" \
    "$PROJECT_DIR/build/Debug/Vora" \
    "$PROJECT_DIR/build/Release/Vora" \
    "$PROJECT_DIR/build/Debug/vora" \
    "$PROJECT_DIR/build/Release/vora"; do
    if [ -x "$candidate" ]; then
        VORA="$candidate"
        break
    fi
done

if [ -z "$VORA" ]; then
    echo "Error: Vora binary not found. Build the project first:"
    echo "  ./build.sh                       (recommended)"
    echo "  cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug"
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

    if output=$("$VORA" "$file" 2>&1); then
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
