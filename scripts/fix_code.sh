#!/bin/bash
# Applies code formatting and static analysis fixes (Linux).

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json"

if [ ! -f "$COMPILE_COMMANDS" ]; then
    echo -e "\033[0;31mcompile_commands.json not found in $BUILD_DIR.\033[0m"
    echo "Please run: ./scripts/build_dev.sh"
    exit 1
fi

# 1. Format (Style)
echo -e "\n\033[0;36m[1/2] Running clang-format (Style)...\033[0m"
find "$PROJECT_ROOT/src" "$PROJECT_ROOT/include" -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 | while IFS= read -r -d '' file; do
   echo "Formatting $(basename "$file")"
   clang-format -i "$file"
done

# 2. Fix Static Analysis Issues
echo -e "\n\033[0;36m[2/2] Running clang-tidy --fix (Logic/Modernize)...\033[0m"
find "$PROJECT_ROOT/src" -type f -name "*.cpp" -print0 | while IFS= read -r -d '' file; do
    echo -e "\033[0;36mAnalyzing $(basename "$file")...\033[0m"
    clang-tidy -p "$BUILD_DIR" --fix --format-style=file "$file"
done

echo -e "\n\033[0;32mDone! Please review changes before committing.\033[0m"
