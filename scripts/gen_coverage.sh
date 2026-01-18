#!/bin/bash
# Generates code coverage report using lcov/gcov (Linux).
# Requires build to be configured with coverage support (usually -fprofile-arcs -ftest-coverage).

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
COVERAGE_DIR="$PROJECT_ROOT/coverage"

# Ensure lcov is installed
if ! command -v lcov &> /dev/null; then
    echo -e "\033[0;31mlcov not found.\033[0m Please install it (sudo apt install lcov)."
    exit 1
fi

# Clean previous coverage
mkdir -p "$COVERAGE_DIR"
lcov --directory "$BUILD_DIR" --zerocounters

# Run tests
echo -e "\033[0;36mRunning Tests...\033[0m"
"$BUILD_DIR/unit_tests"

# Capture coverage
echo -e "\n\033[0;36mCapturing Coverage...\033[0m"
lcov --directory "$BUILD_DIR" --capture --output-file "$COVERAGE_DIR/coverage.info"

# Filter out system and external headers
lcov --remove "$COVERAGE_DIR/coverage.info" '/usr/*' '*/_deps/*' '*/test/*' --output-file "$COVERAGE_DIR/coverage_filtered.info"

# Generate HTML
echo -e "\n\033[0;36mGenerating HTML Report...\033[0m"
genhtml "$COVERAGE_DIR/coverage_filtered.info" --output-directory "$COVERAGE_DIR/report"

echo -e "\n\033[0;32mCoverage report generated at $COVERAGE_DIR/report/index.html\033[0m"
