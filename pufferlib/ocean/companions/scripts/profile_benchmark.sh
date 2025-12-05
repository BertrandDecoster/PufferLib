#!/bin/bash
# Profile companions benchmark with samply
# Usage: ./scripts/profile_benchmark.sh [iterations]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPANIONS_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$COMPANIONS_DIR/build"

ITERATIONS="${1:-100000}"

# Check if samply is installed
if ! command -v samply &> /dev/null; then
    echo "samply not found. Install with: brew install samply"
    exit 1
fi

# Configure with debug symbols if needed
echo "==> Configuring with RelWithDebInfo..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build the benchmark
echo "==> Building companions_fsm_benchmark..."
cmake --build . --target companions_fsm_benchmark -j4

# Run with samply
echo "==> Profiling with samply (${ITERATIONS} iterations)..."
echo "    Firefox Profiler will open automatically"
samply record ./companions_fsm_benchmark
