#!/bin/bash
# Profile C++ random rollouts with samply (5 seconds)
# Usage: ./scripts/profile_cpp.sh [grid_size]
#
# Examples:
#   ./scripts/profile_cpp.sh        # Profile with 10x10 grid
#   ./scripts/profile_cpp.sh 20     # Profile with 20x20 grid
#
# Profile saved to: build/profile_cpp.json.gz

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPANIONS_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$COMPANIONS_DIR/build"

GRID_SIZE="${1:-10}"
DURATION=5

# Check if samply is installed
if ! command -v samply &> /dev/null; then
    echo "samply not found. Install with: brew install samply"
    exit 1
fi

echo "==> Profiling C++ random rollouts (${GRID_SIZE}x${GRID_SIZE}) for ${DURATION}s..."
mkdir -p "$BUILD_DIR"

# Configure with debug symbols
echo "==> Configuring with RelWithDebInfo..."
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build the benchmark
echo "==> Building companions_fsm_benchmark..."
cmake --build . --target companions_fsm_benchmark -j4

# Run with samply, save profile
PROFILE_PATH="$BUILD_DIR/profile_cpp.json.gz"
SAMPLE_RATE=8000  # 8000 samples/sec for better resolution
echo "==> Saving profile to: $PROFILE_PATH"
echo "==> Sampling at ${SAMPLE_RATE} Hz for ${DURATION}s..."

# Run samply in background with higher sample rate, suppress benchmark output
samply record --rate "$SAMPLE_RATE" --save-only -o "$PROFILE_PATH" -- ./companions_fsm_benchmark --grid-size "$GRID_SIZE" --iterations 999999999 > /dev/null 2>&1 &
SAMPLY_PID=$!

# Progress bar with time display
for i in $(seq 1 "$DURATION"); do
    filled=$(printf '█%.0s' $(seq 1 "$i"))
    remaining=$((DURATION - i))
    if [ "$remaining" -gt 0 ]; then
        empty=$(printf '░%.0s' $(seq 1 "$remaining"))
    else
        empty=""
    fi
    printf "\r==> Collecting: [%d/%ds] %s%s" "$i" "$DURATION" "$filled" "$empty"
    sleep 1
done
echo " done"

# Kill only the benchmark process (exact name match, not samply)
pkill -x "companions_fsm_benchmark" 2>/dev/null || true

# Wait for samply to finish saving the profile
wait "$SAMPLY_PID" 2>/dev/null || true

echo ""
echo "==> Profile saved to: $PROFILE_PATH"
echo "==> Opening profile in Firefox Profiler..."
samply load "$PROFILE_PATH"
