#!/bin/bash
# Profile PufferLib training with samply (profiles Python + C code)
# Usage: ./scripts/profile_train.sh [env] [duration] [grid_size]
#
# Examples:
#   ./scripts/profile_train.sh                  # Profile synchro (10x10) for 30s
#   ./scripts/profile_train.sh synchro 60 20    # Profile synchro (20x20) for 60s
#
# Output: Opens Firefox Profiler in browser

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPANIONS_DIR="$(dirname "$SCRIPT_DIR")"
PUFFERLIB_DIR="$(cd "$COMPANIONS_DIR/../../.." && pwd)"
BUILD_DIR="$COMPANIONS_DIR/build"

GAME_NAME="${1:-synchro}"
DURATION="${2:-30}"
GRID_SIZE="${3:-10}"

PYTHON="$PUFFERLIB_DIR/.venv/bin/python"
PUFFER="$PUFFERLIB_DIR/.venv/bin/puffer"

# Check if samply is installed
if ! command -v samply &> /dev/null; then
    echo "samply not found. Install with: brew install samply"
    exit 1
fi

echo "==> Profiling puffer_${GAME_NAME} (${GRID_SIZE}x${GRID_SIZE}) for ${DURATION}s..."
mkdir -p "$BUILD_DIR"
cd "$PUFFERLIB_DIR"

PROFILE_PATH="$BUILD_DIR/profile_train_${GAME_NAME}.json.gz"
SAMPLE_RATE=1000  # 1000 samples/sec (Python is slower than C++)
echo "==> Profile will be saved to: $PROFILE_PATH"
echo "==> Sampling at ${SAMPLE_RATE} Hz for ${DURATION}s..."

# Run puffer train with samply in background
# Use Serial backend so everything runs in one process
samply record --rate "$SAMPLE_RATE" --save-only -o "$PROFILE_PATH" -- \
    "$PYTHON" "$PUFFER" train "puffer_${GAME_NAME}" \
    --env.rows="$GRID_SIZE" --env.cols="$GRID_SIZE" \
    --vec.backend=Serial > /dev/null 2>&1 &
SAMPLY_PID=$!

# Progress bar with time display
for i in $(seq 1 "$DURATION"); do
    # Check if samply is still running
    if ! kill -0 "$SAMPLY_PID" 2>/dev/null; then
        break
    fi
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

# Kill the Python process to stop training
pkill -f "puffer_${GAME_NAME}" 2>/dev/null || true

# Wait for samply to finish saving the profile
wait "$SAMPLY_PID" 2>/dev/null || true

echo ""
if [ -f "$PROFILE_PATH" ]; then
    echo "==> Profile saved to: $PROFILE_PATH"
    echo "==> Opening in Firefox Profiler..."
    samply load "$PROFILE_PATH"
else
    echo "==> Warning: Profile was not saved"
fi
