#!/bin/bash
# Build and test C++ in Release mode
# For fast iteration on game logic with optimized builds

set -o pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

# Configure CMake if build directory doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "=== Configuring CMake ==="
    mkdir -p "$BUILD_DIR"
    if ! cmake -S "$SCRIPT_DIR/.." -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release 2>&1; then
        echo -e "\n${RED}CMAKE CONFIGURE FAILED${NC}"
        exit 1
    fi
    echo ""
fi

echo "=== Building C++ (Release) ==="
if ! cmake --build "$BUILD_DIR" --config Release -j4 2>&1; then
    echo -e "\n${RED}BUILD FAILED${NC}"
    exit 1
fi

echo ""
echo "=== Running C++ Tests (Release) ==="
# Use -C Release for multi-config generators (MSVC)
if ! ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure 2>&1; then
    echo -e "\n${RED}TESTS FAILED${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}C++ Release Build and Tests succeeded!${NC}"
echo -e "${GREEN}========================================${NC}"
