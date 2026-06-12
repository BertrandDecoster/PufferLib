---
paths: pufferlib/ocean/companions/**
---

# Companions Integration (C++ Game)

The companions game differs from standard Ocean envs:
- Uses CMake build system (static libs linked by setup.py)
- Written in C++17 (vs C for other Ocean envs)
- Modular architecture: `src/core/`, `src/env/`, `src/viz/`
- Currently integrated: `synchro_env` (via `puffer_synchro`)

## Key Files

| File | Purpose |
|------|---------|
| `synchro.h` | C interface header (extern "C" wrapper) |
| `synchro_wrapper.cc` | C++ implementation of C interface |
| `binding.c` | Python C extension |
| `synchro.py` | PufferLib wrapper class |
| `pufferlib/config/ocean/synchro.ini` | Training config |

## Observation Space

Flattened tensor + vector: `[7*rows*cols + 9 + lens_tail]` floats.
The layout is universal: BaseEnv owns the tensor planes and the 9 base vector
features; the active TaskLens owns the goal plane contents and the vector tail
(`WriteVectorObs`/`AdditionalVectorObsSize`). SynchroLens tail = 0,
AggroLens = 8, DodgeLens = 10.

**Tensor (7 channels × rows × cols, `BaseEnv::kNumObservationPlanes`):**
| Plane | Content |
|-------|---------|
| 0 | Floor cells (1.0 if walkable) |
| 1 | Wall cells (1.0 if wall) |
| 2 | Goal cells (1.0 where the active lens reports a goal) |
| 3 | Current player position |
| 4 | Other agents positions |
| 5 | Telegraphed hazard zones |
| 6 | Active hazard zones |

**Vector (9 base features appended after tensor, then the lens tail):**
| Index | Content |
|-------|---------|
| 0-1 | Position (row, col) normalized to [0,1] |
| 2 | Health ratio |
| 3 | Distance to goal (normalized) |
| 4-7 | Relative positions to 2 other companions |
| 8 | Steps left / 100 |
| 9+ | Task-specific lens tail (`TaskLens::WriteVectorObs`) |

## Action Space

MultiDiscrete: `[5, 2]`
- **Movement (5):** 0=Stay, 1=Up, 2=Down, 3=Left, 4=Right
- **Interact (2):** 0=None, 1=Attack

## Environment Configuration

Constructor parameters (set via `synchro.ini` or directly):

| Parameter | Default | Description |
|-----------|---------|-------------|
| `rows`, `cols` | 12 | Grid dimensions |
| `num_agents` | 3 | Companions per environment |
| `num_synchro` | 3 | Goal cells to reach simultaneously |
| `map_complexity` | 0 | 0=empty, 1=obstacles, 2+=rooms |
| `horizon` | 100 | Max steps per episode |
| `d4_transform` | 0 | D4 symmetry transform (0-7) |
| `overfit` | false | Always reset to same seed (for testing) |

## D4 Symmetry Transforms

The environment supports D4 group transforms (rotations + reflections) for data augmentation and equivariance testing:

| Value | Transform | Description |
|-------|-----------|-------------|
| 0 | Identity | No transform |
| 1 | Rot90 | 90° counter-clockwise |
| 2 | Rot180 | 180° rotation |
| 3 | Rot270 | 270° counter-clockwise |
| 4 | FlipH | Horizontal flip |
| 5 | FlipV | Vertical flip |
| 6 | FlipD | Diagonal flip (transpose) |
| 7 | FlipA | Anti-diagonal flip |

Used by D4-equivariant networks in `networks/d4.py`.

## TaskLens Architecture

Task-specific behavior is separated from world state via composition:

```
BaseEnv (physical reality)     TaskLens (mental construct)
├── grid_                      ├── CanOperateOn()
├── objects_                   ├── IsDone()
├── effects_                   ├── IsSuccess()
├── tick_                      ├── ComputeReward()
└── task_lens_ ────────────────└── MaskCell()
```

**Key files:**
| File | Purpose |
|------|---------|
| `src/env/task_lens.h` | Abstract TaskLens interface |
| `src/env/synchro_lens.h/.cc` | SynchroLens - all on synchro cells |
| `src/env/aggro_lens.h/.cc` | AggroLens - lure enemy to target |
| `src/env/dodge_lens.h/.cc` | DodgeLens - survival task |

**Runtime task switching:**
```cpp
// No snapshot needed - just swap the lens!
env.SetTaskLens(std::make_unique<SynchroLens>());
// ... complete synchro task ...
env.SetTaskLens(std::make_unique<DodgeLens>());
// World state preserved, only interpretation changes
```

**Validation:** Each lens validates via `CanOperateOn()`:
- `SynchroLens`: requires synchro cells in grid
- `AggroLens`: requires target cell + patrol path
- `DodgeLens`: accepts any env

**Observation masking:** Each lens hides irrelevant cells:
- `SynchroLens`: hides Target → Floor
- `AggroLens`: hides Synchro → Floor
- `DodgeLens`: hides both → Floor

## Architecture Layers

### 1. C++ Game (`src/env/synchro_env.cc`)

Pure game logic. **No auto-reset.**

- `Reset(seed)` - Creates new procedural map
- `Step(actions)` - Returns `StepResult{rewards, done}`
- `IsSuccess()`, `NumAgentsOnSynchroCells()` - Query state
- `SetTaskLens()` - Swap task interpretation at runtime

### 2. C Wrapper (`synchro_wrapper.cc`)

Bridges C++ to Python. **Has auto-reset.**

- `synchro_init()` - Creates SynchroEnv, allocates observation buffers
- `c_reset()` - Calls `Reset(seed++)`, writes observations to buffers
- `c_step()` - Calls `Step()`, **auto-resets on done**, updates log
- `c_reset_seed(seed)` - Explicit seed reset (for parity testing)

**Auto-reset behavior in `c_step()`:**
```cpp
if (result.done) {
    // Log episode stats
    if (env->overfit) {
        cpp_env->Reset(env->seed);    // Same seed every time
    } else {
        cpp_env->Reset(env->seed++);  // Increment seed
    }
}
```

### 3. Python Wrapper (`synchro.py`)

PufferLib integration. Delegates to C wrapper.

- Creates vectorized environments with unique seeds: `env_seed = i + seed * num_envs`
- `reset()` → `binding.vec_reset()`
- `step()` → `binding.vec_step()` (auto-reset handled by C wrapper)

### 4. Training (`pufferl.py`)

Uses PufferLib's vectorized interface. No reset handling needed - C wrapper auto-resets.

## Map Generation

Procedural maps based on `map_complexity`:
- **0:** Empty rectangle
- **1:** Scattered obstacles (maintains connectivity)
- **2+:** Rooms connected by corridors

All maps guarantee full connectivity via flood-fill validation.
