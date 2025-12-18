#!/bin/bash
# Generate expected outputs for all FIT fixtures
# Run this once to create baseline expected outputs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARSER="${SCRIPT_DIR}/../build/fit-parser"
FIXTURES_DIR="${SCRIPT_DIR}/fixtures"
EXPECTED_DIR="${SCRIPT_DIR}/expected"

# Create expected directory
mkdir -p "$EXPECTED_DIR"

echo "Generating expected outputs..."
echo ""

count=0
for fit_file in "$FIXTURES_DIR"/*.fit; do
    if [ ! -f "$fit_file" ]; then
        continue
    fi
    
    filename=$(basename "$fit_file")
    expected_file="$EXPECTED_DIR/${filename%.fit}.json"
    
    echo "Processing: $filename"
    
    # Run parser and save output
    if "$PARSER" "$fit_file" > "$expected_file" 2>&1; then
        # Get point count
        points=$(grep -o '"points": [0-9]*' "$expected_file" | grep -o '[0-9]*')
        echo "  → Saved $points points to ${filename%.fit}.json"
        count=$((count + 1))
    else
        echo "  → ERROR: Failed to parse"
        rm -f "$expected_file"
    fi
done

echo ""
echo "Generated $count expected output files in tests/expected/"
