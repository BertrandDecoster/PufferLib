#!/bin/bash
# Run parity test for Companions Python wrapper
# Usage: ./run_parity_test.sh [--rebuild]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPANIONS_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PUFFERLIB_ROOT="$(cd "$COMPANIONS_DIR/../../.." && pwd)"
BUILD_DIR="$COMPANIONS_DIR/build"
DATA_DIR="$SCRIPT_DIR/reference_data"

# Parse arguments
REBUILD=false
for arg in "$@"; do
    case $arg in
        --rebuild)
            REBUILD=true
            shift
            ;;
    esac
done

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$DATA_DIR"

# Build C++ components if needed
if [ "$REBUILD" = true ] || [ ! -f "$BUILD_DIR/bin/parity_generator" ]; then
    echo "Building C++ components..."
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . -j4 --target parity_generator
    cd "$SCRIPT_DIR"
fi

# Generate reference data
REFERENCE_FILE="$DATA_DIR/synchro_parity_42.bin"
echo "Generating reference data..."
"$BUILD_DIR/bin/parity_generator" \
    --env-seed 42 \
    --action-seed 123 \
    --steps 1000 \
    --rows 12 --cols 12 \
    --agents 3 \
    --synchro 3 \
    --complexity 0 \
    --horizon 100 \
    --output "$REFERENCE_FILE"

# Activate virtual environment
cd "$PUFFERLIB_ROOT"
source .venv/bin/activate

# Build Python extension if needed
echo "Ensuring Python extension is built..."
python setup.py build_companions --inplace 2>/dev/null || {
    echo "Building Python extension..."
    python setup.py build_companions --inplace --force
}

# Run parity test
echo ""
echo "Running parity test..."
python "$SCRIPT_DIR/parity_test.py" "$REFERENCE_FILE" -v

echo ""
echo "All tests passed!"
