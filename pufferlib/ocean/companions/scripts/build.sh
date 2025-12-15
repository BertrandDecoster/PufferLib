#!/bin/bash
# Unified build script for Companions
#
# Usage: build.sh [OPTIONS]
#
# Options:
#   --release    Build Release config (default: Debug)
#   --clean      Force full rebuild
#   --python     Also build and test Python bindings
#   --no-test    Skip running tests
#   --help       Show this help
#
# Examples:
#   build.sh                    # Debug build + tests
#   build.sh --release          # Release build + tests
#   build.sh --release --clean  # Clean Release build + tests
#   build.sh --release --python # Release + Python bindings + all tests
#
# Output:
#   The DLL and data/ folder are built to: build/bin/<Config>/
#   - companions_ue.dll
#   - data/agents.csv, data/effects.csv, ...

set -o pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Defaults
CONFIG="Debug"
CLEAN_BUILD=false
BUILD_PYTHON=false
RUN_TESTS=true

# Parse arguments
for arg in "$@"; do
    case $arg in
        --release)
            CONFIG="Release"
            ;;
        --clean)
            CLEAN_BUILD=true
            ;;
        --python)
            BUILD_PYTHON=true
            ;;
        --no-test)
            RUN_TESTS=false
            ;;
        --help)
            head -23 "$0" | tail -21
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use --help for usage"
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
REPO_ROOT="$SCRIPT_DIR/../../../../.."

# Configure CMake if build directory doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "=== Configuring CMake ==="
    mkdir -p "$BUILD_DIR"
    if ! cmake -S "$SCRIPT_DIR/.." -B "$BUILD_DIR" -DBUILD_UE5_DLL=ON 2>&1; then
        echo -e "\n${RED}CMAKE CONFIGURE FAILED${NC}"
        exit 1
    fi
    echo ""
fi

# Build C++
BUILD_ARGS="--config $CONFIG -j4"
if [ "$CLEAN_BUILD" = true ]; then
    echo "=== Building C++ ($CONFIG, clean) ==="
    BUILD_ARGS="$BUILD_ARGS --clean-first"
else
    echo "=== Building C++ ($CONFIG) ==="
fi

if ! cmake --build "$BUILD_DIR" $BUILD_ARGS 2>&1; then
    echo -e "\n${RED}C++ BUILD FAILED${NC}"
    exit 1
fi

# Run C++ tests
if [ "$RUN_TESTS" = true ]; then
    echo ""
    echo "=== Running C++ Tests ($CONFIG) ==="
    if ! ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure 2>&1; then
        echo -e "\n${RED}C++ TESTS FAILED${NC}"
        exit 1
    fi
fi

# Python bindings (optional)
if [ "$BUILD_PYTHON" = true ]; then
    echo ""
    echo "=== Building Python Bindings ==="
    cd "$REPO_ROOT"
    if ! .venv/bin/python setup.py build_companions --inplace --force 2>&1 | grep -v "SetuptoolsDeprecationWarning" | grep -v "^\!\!" | grep -v "project.license" | grep -v "By 2026" | grep -v "See https://packaging" | grep -v "tool.setuptools" | grep -v "^\*\*\*"; then
        echo -e "\n${RED}PYTHON BUILD FAILED${NC}"
        exit 1
    fi

    if [ "$RUN_TESTS" = true ]; then
        echo ""
        echo "=== Testing Python Integration ==="
        if ! .venv/bin/python -m pufferlib.ocean.companions.synchro 2>&1; then
            echo -e "\n${RED}PYTHON TESTS FAILED${NC}"
            exit 1
        fi
    fi
fi

# Verify DLL and data folder
DLL_DIR="$(cd "$BUILD_DIR/bin/$CONFIG" 2>/dev/null && pwd)"
DLL_PATH="$DLL_DIR/companions_ue.dll"
DATA_DIR="$DLL_DIR/data"

echo ""
if [ -f "$DLL_PATH" ] && [ -d "$DATA_DIR" ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build succeeded! ($CONFIG)${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Output:"
    echo "  DLL:  $DLL_PATH"
    echo "  Data: $DATA_DIR/"
    ls "$DATA_DIR" | sed 's/^/        /'
else
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Build succeeded, but DLL/data not found${NC}"
    echo -e "${YELLOW}========================================${NC}"
    if [ ! -f "$DLL_PATH" ]; then
        echo -e "${YELLOW}  Missing: $DLL_PATH${NC}"
    fi
    if [ ! -d "$DATA_DIR" ]; then
        echo -e "${YELLOW}  Missing: $DATA_DIR${NC}"
    fi
fi
