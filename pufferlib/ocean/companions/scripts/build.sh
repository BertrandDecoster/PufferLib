#!/bin/bash
# Unified build script for Companions
#
# Usage: build.sh [OPTIONS]
#
# Options:
#   --release    Build Release config (default: Debug)
#   --clean      Force full rebuild
#   --python     Also build and test Python bindings
#   --pytest     Also run pytest on tests/python/
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
#   macOS/Linux: build/bin/libcompanions_api.{dylib,so} + build/bin/data/
#   Windows:     build/bin/<Config>/companions_api.dll + build/bin/<Config>/data/

set -o pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Defaults
CONFIG="Debug"
CLEAN_BUILD=false
BUILD_PYTHON=false
RUN_PYTEST=false
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
        --pytest)
            RUN_PYTEST=true
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
REPO_ROOT="$SCRIPT_DIR/../../../.."

# Resolve venv python (Windows: .venv/Scripts, Unix: .venv/bin)
if [ -x "$REPO_ROOT/.venv/Scripts/python.exe" ]; then
    VENV_PYTHON="$REPO_ROOT/.venv/Scripts/python.exe"
elif [ -x "$REPO_ROOT/.venv/bin/python" ]; then
    VENV_PYTHON="$REPO_ROOT/.venv/bin/python"
else
    VENV_PYTHON=""
fi

# Configure CMake (always pass BUILD_DLL=ON to ensure it's set)
if [ ! -d "$BUILD_DIR" ]; then
    echo "=== Configuring CMake ==="
    mkdir -p "$BUILD_DIR"
fi
if ! cmake -S "$SCRIPT_DIR/.." -B "$BUILD_DIR" -DBUILD_DLL=ON 2>&1; then
    echo -e "\n${RED}CMAKE CONFIGURE FAILED${NC}"
    exit 1
fi
echo ""

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

# Python bindings (optional) — skipped on Windows: setup.py raises on line 144
# because the C extensions don't support MSVC. Treat --python as a no-op there.
if [ "$BUILD_PYTHON" = true ]; then
    if [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]] || [[ "$OSTYPE" == "win32"* ]]; then
        echo ""
        echo -e "${YELLOW}=== Skipping --python (setup.py does not support Windows) ===${NC}"
    else
        if [ -z "$VENV_PYTHON" ]; then
            echo -e "\n${RED}--python given but no venv found at .venv/{Scripts,bin}${NC}"
            exit 1
        fi
        echo ""
        echo "=== Building Python Bindings ==="
        cd "$REPO_ROOT"
        if ! "$VENV_PYTHON" setup.py build_companions --inplace --force 2>&1 | grep -v "SetuptoolsDeprecationWarning" | grep -v "^\!\!" | grep -v "project.license" | grep -v "By 2026" | grep -v "See https://packaging" | grep -v "tool.setuptools" | grep -v "^\*\*\*"; then
            echo -e "\n${RED}PYTHON BUILD FAILED${NC}"
            exit 1
        fi

        if [ "$RUN_TESTS" = true ]; then
            echo ""
            echo "=== Testing Python Integration ==="
            if ! "$VENV_PYTHON" -m pufferlib.ocean.companions.synchro 2>&1; then
                echo -e "\n${RED}PYTHON TESTS FAILED${NC}"
                exit 1
            fi
        fi
    fi
fi

# pytest on tests/python/ (optional)
if [ "$RUN_PYTEST" = true ] && [ "$RUN_TESTS" = true ]; then
    if [ -z "$VENV_PYTHON" ]; then
        echo -e "\n${RED}--pytest given but no venv found at .venv/{Scripts,bin}${NC}"
        exit 1
    fi
    if ! "$VENV_PYTHON" -c "import pytest" 2>/dev/null; then
        echo -e "\n${RED}--pytest given but pytest is not installed in .venv${NC}"
        echo -e "${YELLOW}Install with:  uv pip install pytest${NC}"
        exit 1
    fi
    echo ""
    echo "=== Running pytest (tests/python/) ==="
    cd "$REPO_ROOT"
    if ! "$VENV_PYTHON" -m pytest pufferlib/ocean/companions/tests/python/ -v 2>&1; then
        echo -e "\n${RED}PYTEST FAILED${NC}"
        exit 1
    fi
fi

# Verify shared library and data folder (OS-aware)
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: single-config generator, output to bin/
    LIB_NAME="libcompanions_api.dylib"
    LIB_DIR="$BUILD_DIR/bin"
elif [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]] || [[ "$OSTYPE" == "win32"* ]]; then
    # Windows: multi-config generator, output to bin/$CONFIG/
    LIB_NAME="companions_api.dll"
    LIB_DIR="$BUILD_DIR/bin/$CONFIG"
else
    # Linux: single-config generator, output to bin/
    LIB_NAME="libcompanions_api.so"
    LIB_DIR="$BUILD_DIR/bin"
fi

LIB_PATH="$LIB_DIR/$LIB_NAME"
DATA_DIR="$LIB_DIR/data"

echo ""
if [ -f "$LIB_PATH" ] && [ -d "$DATA_DIR" ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build succeeded! ($CONFIG)${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Output:"
    echo "  Library: $LIB_PATH"
    echo "  Data:    $DATA_DIR/"
    ls "$DATA_DIR" | sed 's/^/           /'
else
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Build succeeded, but library/data not found${NC}"
    echo -e "${YELLOW}========================================${NC}"
    if [ ! -f "$LIB_PATH" ]; then
        echo -e "${YELLOW}  Missing: $LIB_PATH${NC}"
    fi
    if [ ! -d "$DATA_DIR" ]; then
        echo -e "${YELLOW}  Missing: $DATA_DIR${NC}"
    fi
fi
