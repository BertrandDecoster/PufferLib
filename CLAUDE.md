# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PufferLib is a high-performance reinforcement learning library with C-based environments ("Ocean" environments) that achieve 1M+ steps/second. The `pufferlib/ocean/companions` directory contains a custom C++ game being integrated into PufferLib's training infrastructure.

## Build Commands

### Full PufferLib Installation
```bash
uv pip install -e .
# Or with specific environment extras
uv pip install -e ".[atari,procgen]"
```

### Build C Extensions Only
```bash
python setup.py build_ext --inplace
# Debug build with sanitizers
DEBUG=1 python setup.py build_ext --inplace --force
# Build specific environment
python setup.py build_snake --inplace
```

### Build Individual Ocean Environment (standalone binary)
```bash
./scripts/build_ocean.sh <env_name> [local|fast|web]
# Example: ./scripts/build_ocean.sh snake local
```

### Build Companions Game (CMake C++ project)
```bash
cd pufferlib/ocean/companions
mkdir -p build && cd build
cmake ..
make
```

## Training & Evaluation

```bash
# Train an ocean environment
puffer train puffer_snake

# Evaluate a trained model
puffer eval puffer_synchro --render-mode ansi --train.device mps --load-model-path latest

# Hyperparameter sweep
puffer sweep puffer_snake

# Distributed training
torchrun --standalone --nnodes=1 --nproc-per-node=6 -m pufferlib.pufferl train puffer_nmmo3
```

Environment names use `puffer_` prefix (e.g., `puffer_snake`, `puffer_breakout`).

## Testing

```bash
# PufferLib tests
pytest tests/

# Companions C++ tests (after cmake build)
cd pufferlib/ocean/companions/build
ctest
# Or run individual test
./companions_test
./companions_fsm_test
```

## Architecture

### Ocean Environment Structure

Each C environment in `pufferlib/ocean/<env>/` requires:
- `<env>.h` - C struct definitions (`Env`, `Log`) and `c_reset`, `c_step`, `c_render`, `c_close` functions
- `<env>.c` - Main game logic (optional, can be in .h)
- `binding.c` - Python binding using `env_binding.h` macros
- `<env>.py` - Python wrapper extending `pufferlib.PufferEnv`

The binding pattern:
```c
#include "<env>.h"
#define Env C<EnvName>
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) { ... }
static int my_log(PyObject* dict, Log* log) { ... }
```

### Companions Integration (C++ Game)

The companions game differs from standard Ocean envs:
- Uses CMake build system instead of setup.py C extensions
- Written in C++17 (vs C for other Ocean envs)
- Modular architecture: `src/core/`, `src/env/`, `src/viz/`
- Multiple environment variants: `base_env`, `aggro_env`, `dodge_env`, `synchro_env`

To integrate companions with PufferLib training:
1. Create `binding.c` with C wrapper around C++ code
2. Add to `pufferlib/ocean/environment.py` MAKE_FUNCTIONS dict
3. Create config at `pufferlib/config/ocean/companions.ini`

The environments auto-reset, and change at each reset
Python wrapper computes per-env seed: env_seed = i + seed * num_envs
The C wrapper passes seed=0 at init, and vec_reset() calls c_reset() which does NOT re-seed - it just advances the existing RNG state. There's a c_reset_seed() function that properly seeds, but it's not used by the vectorized reset

### Configuration System

Environment configs in `pufferlib/config/ocean/<env>.ini`:
```ini
[base]
package = ocean
env_name = puffer_<env>
policy_name = <PolicyClass>

[env]
# Environment constructor kwargs

[train]
# PPO hyperparameters
```

### Policy Models

Custom policies in `pufferlib/ocean/<env>/<env>.py` or `pufferlib/models.py`. Must implement:
- `encode_observations(observations)` → hidden state
- `decode_actions(hidden)` → (logits, values)

## Key Files

- `pufferlib/pufferl.py` - Main training loop (PPO)
- `pufferlib/pufferlib.py` - `PufferEnv` base class
- `pufferlib/ocean/env_binding.h` - C binding macros
- `pufferlib/ocean/environment.py` - Environment registry
- `pufferlib/vector.py` - Vectorized environment wrapper
- `setup.py` - Build system with raylib/box2d downloads

## Models
cx9x30s2 : env synchro, old policy, size 5, companions 2, synchro 2, complexity 0
7212qjtt : env synchro, old policy, size 10, companions 3, synchro 2, complexity 2

5n85iosj : env synchro, d4 policy, size 10, companions 3, synchro 2, complexity 2

The results of a run are saved at 3 places with the same ID (8 characters, like cx9x30s2)

/wandb/run-DATE_TIME-ID
/experiments/puffer_synchro_ID.pt
/experiments/puffer_synchro_ID/
