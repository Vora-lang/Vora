#!/usr/bin/env bash
# run_tests.sh — Vora test suite runner (Linux / macOS / WSL)
# Usage:
#   bash tests/run_tests.sh                 # VM mode (default)
#   bash tests/run_tests.sh --interpreter   # Interpreter mode
#
# On Windows, use tests/run_tests.ps1 instead.

set -e

# ──────────────────────────────────────────────────
# Locate the Vora binary
# ──────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Try common locations (single-config first, then multi-config)
VORA=""
for candidate in \
    "$PROJECT_DIR/build/Vora" \
    "$PROJECT_DIR/build/Debug/Vora" \
    "$PROJECT_DIR/build/Release/Vora" \
    "$PROJECT_DIR/build/vora" \
    "$PROJECT_DIR/build/Debug/vora" \
    "$PROJECT_DIR/build/Release/vora" \
    "$PROJECT_DIR/build/linux-x64-debug/Vora" \
    "$PROJECT_DIR/build/linux-x64-release/Vora" \
    "$PROJECT_DIR/build/macos-universal-debug/Vora" \
    "$PROJECT_DIR/build/macos-universal-release/Vora"; do
    if [ -x "$candidate" ]; then
        VORA="$candidate"
        break
    fi
done

if [ -z "$VORA" ]; then
    echo "Error: Vora binary not found. Build the project first:"
    echo "  cmake -S . -B build && cmake --build build"
    exit 1
fi

echo "Using: $VORA"
echo ""

# ──────────────────────────────────────────────────
# Mode
# ──────────────────────────────────────────────────

MODE_FLAG=""
MODE="VM"
if [[ "$1" == "--interpreter" ]]; then
    MODE_FLAG="--interpreter"
    MODE="Interpreter"
fi

# ──────────────────────────────────────────────────
# Run tests
# ──────────────────────────────────────────────────

PASS=0
FAIL=0
ERRORS=""

echo "============================================"
echo "  Vora Test Suite ($MODE mode)"
echo "============================================"
echo ""

for file in "$PROJECT_DIR"/tests/lexer/*.va \
            "$PROJECT_DIR"/tests/parser/*.va \
            "$PROJECT_DIR"/tests/runtime/*.va \
            "$PROJECT_DIR"/tests/interpreter/*.va; do
    [ -f "$file" ] || continue
    name="${file#$PROJECT_DIR/tests/}"
    printf "  %-45s " "$name"

    if output=$("$VORA" $MODE_FLAG "$file" 2>&1); then
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
