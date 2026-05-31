#!/usr/bin/env bash
# run_tests.sh — Vora test suite runner
# Usage:
#   bash tests/run_tests.sh          # Interpreter mode (default)
#   bash tests/run_tests.sh --vm     # VM mode

set -e

VORA="./build/Debug/Vora.exe"
PASS=0
FAIL=0
ERRORS=""

VM_FLAG=""
MODE="Interpreter"
if [[ "$1" == "--vm" ]]; then
    VM_FLAG="--vm"
    MODE="VM"
fi

echo "============================================"
echo "  Vora Test Suite ($MODE mode)"
echo "============================================"
echo ""

run_test() {
    local file="$1"
    local name="${file#tests/}"
    printf "  %-45s " "$name"
    if output=$("$VORA" $VM_FLAG "$file" 2>&1); then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  $name: $output"
    fi
}

for file in tests/lexer/*.va tests/parser/*.va tests/runtime/*.va tests/interpreter/*.va; do
    [ -f "$file" ] || continue
    run_test "$file"
done

echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "============================================"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    printf "%b" "$ERRORS"
    echo ""
    exit 1
fi
