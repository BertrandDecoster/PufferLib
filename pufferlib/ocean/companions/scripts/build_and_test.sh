#!/bin/bash
# Build and test script for Companions
# Provides clear summary of first failure or success

set -o pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

FIRST_ERROR=""

fail() {
    if [ -z "$FIRST_ERROR" ]; then
        FIRST_ERROR="$1"
    fi
    echo -e "${RED}FAILED:${NC} $1"
    return 1
}

echo "=== Building C++ ==="
if ! cmake --build pufferlib/ocean/companions/build -j4 2>&1; then
    fail "C++ build failed"
    echo -e "\n${RED}BUILD FAILED:${NC} $FIRST_ERROR"
    exit 1
fi

echo ""
echo "=== Running C++ Tests ==="
cd pufferlib/ocean/companions/build
if ! ctest --output-on-failure 2>&1; then
    fail "C++ tests failed"
    cd ../../../..
    echo -e "\n${RED}TEST FAILED:${NC} $FIRST_ERROR"
    exit 1
fi
cd ../../../..

echo ""
echo "=== Building Python Bindings ==="
if ! .venv/bin/python setup.py build_companions --inplace --force 2>&1 | grep -v "SetuptoolsDeprecationWarning" | grep -v "^\!\!" | grep -v "project.license" | grep -v "By 2026" | grep -v "See https://packaging" | grep -v "tool.setuptools" | grep -v "^\*\*\*"; then
    fail "Python bindings build failed"
    echo -e "\n${RED}BUILD FAILED:${NC} $FIRST_ERROR"
    exit 1
fi

echo ""
echo "=== Testing Python Integration ==="
if ! .venv/bin/python -m pufferlib.ocean.companions.synchro 2>&1; then
    fail "Python integration test failed"
    echo -e "\n${RED}TEST FAILED:${NC} $FIRST_ERROR"
    exit 1
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Compilation and Tests all succeeded!${NC}"
echo -e "${GREEN}========================================${NC}"
