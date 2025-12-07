#!/bin/bash
# Profile companions with samply
# Usage: ./scripts/profile_benchmark.sh [--cpp|--train] [duration_seconds]
#
# Examples:
#   ./scripts/profile_benchmark.sh --cpp      # Profile C++ benchmark
#   ./scripts/profile_benchmark.sh --train    # Profile Python training loop
#   ./scripts/profile_benchmark.sh --train 30 # Profile training for 30 seconds
#
# Profiles are saved to: pufferlib/ocean/companions/build/
#   - profile_cpp.json.gz   (C++ benchmark)
#   - profile_train.json.gz (Python training)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPANIONS_DIR="$(dirname "$SCRIPT_DIR")"
PUFFERLIB_DIR="$(cd "$COMPANIONS_DIR/../../.." && pwd)"
BUILD_DIR="$COMPANIONS_DIR/build"

MODE="${1:---cpp}"
DURATION="${2:-30}"

# Check if samply is installed
if ! command -v samply &> /dev/null; then
    echo "samply not found. Install with: brew install samply"
    exit 1
fi

profile_cpp() {
    echo "==> Profiling C++ benchmark..."
    mkdir -p "$BUILD_DIR"

    # Configure with debug symbols if needed
    echo "==> Configuring with RelWithDebInfo..."
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

    # Build the benchmark
    echo "==> Building companions_fsm_benchmark..."
    cmake --build . --target companions_fsm_benchmark -j4

    # Run with samply, save profile
    PROFILE_PATH="$BUILD_DIR/profile_cpp.json.gz"
    echo "==> Saving profile to: $PROFILE_PATH"
    echo "==> Firefox Profiler will open automatically"
    samply record --save-only -o "$PROFILE_PATH" -- ./companions_fsm_benchmark

    echo ""
    echo "==> Profile saved to: $PROFILE_PATH"
    echo "==> To view later: samply load $PROFILE_PATH"
}

profile_train() {
    echo "==> Profiling Python training loop for ${DURATION}s..."
    mkdir -p "$BUILD_DIR"
    cd "$PUFFERLIB_DIR"

    PROFILE_PATH="$BUILD_DIR/profile_train.json.gz"
    echo "==> Saving profile to: $PROFILE_PATH"
    echo "==> Will stop after ${DURATION}s (press Ctrl+C to stop earlier)..."

    # Set up timer to kill only the Python process (not samply)
    (
        sleep "$DURATION"
        # Kill Python processes running puffer (but not samply which also matches)
        # Use pgrep to find and kill only python processes
        for pid in $(pgrep -f "python.*puffer train"); do
            # Check it's actually python, not samply
            if ps -p "$pid" -o comm= 2>/dev/null | grep -q python; then
                kill -TERM "$pid" 2>/dev/null || true
            fi
        done
    ) &
    TIMER_PID=$!

    # Run samply in foreground - it will save profile when child exits
    samply record --save-only -o "$PROFILE_PATH" -- \
        .venv/bin/puffer train puffer_synchro || true

    # Clean up timer if samply finished early
    kill "$TIMER_PID" 2>/dev/null || true

    echo ""
    if [ -f "$PROFILE_PATH" ]; then
        echo "==> Profile saved to: $PROFILE_PATH"
        echo "==> To view: samply load $PROFILE_PATH"
    else
        echo "==> Warning: Profile may not have been saved (samply was killed too quickly)"
        echo "==> Try running with a longer duration"
    fi
}

case "$MODE" in
    --cpp|-c)
        profile_cpp
        ;;
    --train|-t)
        profile_train
        ;;
    *)
        echo "Usage: $0 [--cpp|--train] [duration_seconds]"
        echo ""
        echo "Options:"
        echo "  --cpp, -c     Profile C++ benchmark (default)"
        echo "  --train, -t   Profile Python training loop"
        echo ""
        echo "Examples:"
        echo "  $0 --cpp          # Profile C++ benchmark"
        echo "  $0 --train        # Profile training for 30s (default)"
        echo "  $0 --train 60     # Profile training for 60s"
        echo ""
        echo "Profiles are saved to:"
        echo "  build/profile_cpp.json.gz   - C++ benchmark"
        echo "  build/profile_train.json.gz - Python training"
        echo ""
        echo "To view a saved profile:"
        echo "  samply load build/profile_train.json.gz"
        exit 1
        ;;
esac
