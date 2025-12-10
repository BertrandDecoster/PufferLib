#!/bin/bash
# Profile PufferLib training with cProfile + snakeviz
# Usage: ./scripts/profile_train.sh [env] [duration] [grid_size]
#
# Examples:
#   ./scripts/profile_train.sh                  # Profile synchro (10x10) for 30s
#   ./scripts/profile_train.sh synchro 60 20    # Profile synchro (20x20) for 60s
#
# Output: Opens snakeviz in browser

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

echo "==> Profiling puffer_${GAME_NAME} (${GRID_SIZE}x${GRID_SIZE}) for ${DURATION}s..."
mkdir -p "$BUILD_DIR"
cd "$PUFFERLIB_DIR"

PROFILE_PSTATS="$BUILD_DIR/profile_train_${GAME_NAME}.pstats"
echo "==> Profile will be saved to: $PROFILE_PSTATS"

# Run puffer train with cProfile in background
# Use Serial backend so cProfile captures all training (no multiprocessing)
"$PYTHON" -m cProfile -o "$PROFILE_PSTATS" "$PUFFER" train "puffer_${GAME_NAME}" --env.rows="$GRID_SIZE" --env.cols="$GRID_SIZE" --vec.backend=Serial > /dev/null 2>&1 &
PROFILE_PID=$!

# Progress bar with time display
for i in $(seq 1 "$DURATION"); do
    # Check if profiler is still running
    if ! kill -0 "$PROFILE_PID" 2>/dev/null; then
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

# Kill the training process to stop and save profile
kill -INT "$PROFILE_PID" 2>/dev/null || true

# Wait for it to save the profile
sleep 2
kill "$PROFILE_PID" 2>/dev/null || true
wait "$PROFILE_PID" 2>/dev/null || true

# Kill any remaining training processes
pkill -f "puffer_${GAME_NAME}" 2>/dev/null || true

echo ""
if [ -f "$PROFILE_PSTATS" ]; then
    echo "==> Profile saved to: $PROFILE_PSTATS"

    # Check if snakeviz is installed
    if "$PYTHON" -c "import snakeviz" 2>/dev/null; then
        echo "==> Opening in snakeviz..."
        "$PYTHON" -m snakeviz "$PROFILE_PSTATS"
    elif "$PYTHON" -c "import flameprof" 2>/dev/null; then
        PROFILE_SVG="$BUILD_DIR/profile_train_${GAME_NAME}.svg"
        "$PYTHON" -m flameprof "$PROFILE_PSTATS" -o "$PROFILE_SVG"
        echo "==> Flamegraph saved to: $PROFILE_SVG"
        open "$PROFILE_SVG" 2>/dev/null || echo "==> Open: $PROFILE_SVG"
    else
        echo "==> Install snakeviz for visualization:"
        echo "    uv pip install snakeviz"
        echo ""
        echo "==> Or view with pstats:"
        "$PYTHON" -c "import pstats; p = pstats.Stats('$PROFILE_PSTATS'); p.sort_stats('cumulative').print_stats(30)"
    fi
else
    echo "==> Warning: Profile was not saved"
fi
