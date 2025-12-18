#!/bin/bash
# Test script for FIT parser
# Runs parser on all fixtures and validates output

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARSER="${SCRIPT_DIR}/../build/fit-parser"
FIXTURES_DIR="${SCRIPT_DIR}/fixtures"
EXPECTED_DIR="${SCRIPT_DIR}/expected"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
TOTAL=0
PASSED=0
FAILED=0

echo "========================================="
echo "FIT Parser Test Suite"
echo "========================================="
echo ""

# Test each FIT file
for fit_file in "$FIXTURES_DIR"/*.fit; do
    if [ ! -f "$fit_file" ]; then
        continue
    fi
    
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$fit_file")
    expected_file="$EXPECTED_DIR/${filename%.fit}.json"
    
    echo -n "Testing: $filename ... "
    
    # Run parser
    if output=$("$PARSER" "$fit_file" 2>&1); then
        # Check if expected output exists
        if [ -f "$expected_file" ]; then
            # Compare outputs
            if echo "$output" | diff -q - "$expected_file" > /dev/null 2>&1; then
                echo -e "${GREEN}PASS${NC}"
                PASSED=$((PASSED + 1))
            else
                echo -e "${RED}FAIL${NC} (output mismatch)"
                FAILED=$((FAILED + 1))
                
                # Show diff if requested
                if [ "$1" == "-v" ] || [ "$1" == "--verbose" ]; then
                    echo "$output" | diff - "$expected_file" | head -20
                fi
            fi
        else
            # No expected output - just validate JSON
            if echo "$output" | python3 -m json.tool > /dev/null 2>&1; then
                echo -e "${YELLOW}SKIP${NC} (no expected output, but valid JSON)"
                PASSED=$((PASSED + 1))
            else
                echo -e "${RED}FAIL${NC} (invalid JSON)"
                FAILED=$((FAILED + 1))
            fi
        fi
    else
        echo -e "${RED}FAIL${NC} (parser error)"
        FAILED=$((FAILED + 1))
        if [ "$1" == "-v" ] || [ "$1" == "--verbose" ]; then
            echo "$output"
        fi
    fi
done

echo ""
echo "========================================="
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================="

if [ $FAILED -gt 0 ]; then
    exit 1
fi
