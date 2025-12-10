---
paths: pufferlib/ocean/**
---

# Ocean Environment Architecture

High-performance C/C++ environments achieving 1M+ steps/second.

## Standard C Environment Structure

Each environment in `pufferlib/ocean/<env>/` requires:

| File | Purpose |
|------|---------|
| `<env>.h` | C struct definitions (`Env`, `Log`) and lifecycle functions |
| `<env>.c` | Main game logic (optional, can be in .h) |
| `binding.c` | Python binding using `env_binding.h` macros |
| `<env>.py` | Python wrapper extending `pufferlib.PufferEnv` |

### Required C Functions

```c
void c_reset(Env* env);           // Reset environment state
void c_step(Env* env);            // Execute one step
void c_render(Env* env);          // Render to buffer (optional)
void c_close(Env* env);           // Cleanup resources
```

### Binding Pattern

```c
#include "<env>.h"
#define Env C<EnvName>
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) { ... }
static int my_log(PyObject* dict, Log* log) { ... }
```

## Companions Exception

The `companions/` environment uses a different architecture:
- **C++17** instead of C
- **CMake** build system (not setup.py extensions)
- **Static libraries** linked at install time
- See [companions.md](companions.md) for details

## Key Files

| File | Purpose |
|------|---------|
| `pufferlib/pufferl.py` | Main training loop (PPO) |
| `pufferlib/pufferlib.py` | `PufferEnv` base class |
| `pufferlib/ocean/env_binding.h` | C binding macros |
| `pufferlib/ocean/environment.py` | Environment registry (`make_<env>` functions) |
| `pufferlib/ocean/torch.py` | Policy network definitions |
| `pufferlib/vector.py` | Vectorized environment wrapper |
| `setup.py` | Build system (raylib/box2d downloads, C extensions) |
