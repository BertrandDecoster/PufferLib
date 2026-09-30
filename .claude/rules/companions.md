---
paths: pufferlib/ocean/companions/**
---

# Companions Integration (C++ Game)

The companions game differs from standard Ocean envs:
- Uses CMake build system (static libs linked by setup.py)
- Written in C++17 (vs C for other Ocean envs)
- Modular architecture: `src/core/`, `src/env/`, `src/viz/`
- Currently integrated: `synchro_env`, two RL tasks: `puffer_synchro` and `puffer_revive`

## Key Files

| File | Purpose |
|------|---------|
| `synchro.h` | C interface header (extern "C" wrapper) |
| `synchro_wrapper.cc` | C++ implementation of C interface |
| `binding.c` | Python C extension |
| `synchro.py` | PufferLib wrapper class |
| `pufferlib/config/ocean/synchro.ini` | Training config (`puffer_synchro`, `task = synchro`) |
| `pufferlib/config/ocean/revive.ini` | Training config (`puffer_revive`, `task = revive`) |
| `pufferlib/ocean/environment.py` | `synchro` and `revive` both map to `make_synchro` |

## Observation Space

Flattened tensor + vector: `[5*rows*cols + 12]` floats

**Tensor (5 channels × rows × cols):**
| Plane | Content |
|-------|---------|
| 0 | Floor cells (1.0 if walkable) |
| 1 | Wall cells (1.0 if wall) |
| 2 | The lens's goal cells (1.0 if goal): SynchroGoal cells in the synchro task, ReviveLens goal cells in the revive task |
| 3 | Current player position |
| 4 | Other agents positions |

**Vector (12 features appended after tensor; `BaseEnv::kVectorObs*` in `src/env/base_env.h`):**
| Index | Content |
|-------|---------|
| 0-1 | Position (row, col) normalized to [0,1] |
| 2 | Health ratio |
| 3 | Self downed (1 while down) |
| 4 | Distance to goal (normalized) |
| 5-7 | Other 1: relative row, relative col, downed |
| 8-10 | Other 2: relative row, relative col, downed |
| 11 | Steps left / 100 |

The others are the first two other agents in agent order, an enemy included. An
enemy or anyone standing has flag 0 next to its real deltas; a missing or dead one
reads 0 throughout (a dead one keeps its slot: slots follow agent order, never
reshuffled). The layout is fixed whatever the agent count. Aggro / Dodge append
their features after index 11 (20 / 22 floats). Known limits (12 floats since phase 3,
2 flagged others, steps left above 1.0, the zero-copy gap): see the companions
`CLAUDE.md`, "Observations".

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
| `task` | `'synchro'` | `'synchro'` (SynchroLens) or `'revive'` (one random companion starts each episode down, under the ReviveLens; `num_agents >= 2`) |
| `down_cost` | -0.5 | Added to every agent's reward once per new down; finite, in [-1e6, 0]; `pufferl.py` clamps rewards to [-1, 1] |

`task` stays a string through `process_config` (`ast.literal_eval` fails on a bare
word, so the raw string is kept). The tasks, the down cost, terminals vs truncations:
see the companions `CLAUDE.md`, "RL binding".

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
├── objects_                   ├── IsSuccess()
├── effects_                   ├── IsInterruptible()
├── tick_, done, end reason    ├── ComputeReward()
└── task_lens_ ────────────────└── WriteGoalPlane() / GetGoalCells()
```

Done is BaseEnv's, the same for every lens; no lens fails its task. See the companions
`CLAUDE.md`, "Why an episode ended" and "Interruptions".

**Key files:**
| File | Purpose |
|------|---------|
| `src/env/task_lens.h` | Abstract TaskLens interface |
| `src/env/synchro_lens.h/.cc` | SynchroLens - all on synchro cells |
| `src/env/aggro_lens.h/.cc` | AggroLens - lure enemy to target |
| `src/env/dodge_lens.h/.cc` | DodgeLens - survival task |
| `src/env/revive_lens.h/.cc` | ReviveLens - get the downed allies up (not interruptible) |

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
- `ReviveLens`: requires someone down

Goal cells per lens, ReviveLens, one lens at a time (no lens stack yet): see the
companions `CLAUDE.md`, "Environments & TaskLens".

## Architecture Layers

### 1. C++ Game (`src/env/synchro_env.cc`)

Pure game logic. **No auto-reset.**

- `Reset(seed)` - Creates new procedural map
- `Step(actions)` - One turn, resolved in phases over every agent at once (see the companions `CLAUDE.md`, "Step order"); returns `StepResult{rewards, done}`
- `IsInterrupted()` - done because a down interrupted the task (provisional)
- `SetStartDowned(n)` - each Reset generates n companions down (the revive task); they count as team downs
- `IsSuccess()`, `NumAgentsOnSynchroCells()` - Query state
- `SetTaskLens()` - Swap task interpretation at runtime

### 2. C Wrapper (`synchro_wrapper.cc`)

Bridges C++ to Python. **Has auto-reset.**

- `synchro_init()` - Creates SynchroEnv (the task, the down cost), allocates the render buffer; non-zero with `error` set on a refused config
- `c_reset()` - Calls `Reset(seed++)`, writes observations to buffers
- `c_step()` - Calls `Step()`, sets terminals / truncations, **auto-resets on done**, updates log
- `c_reset_seed(seed)` - Explicit seed reset (for parity testing)

**Terminals and truncations:** Interrupted is a truncation, every other end a terminal;
see the companions `CLAUDE.md`, "RL binding".

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

Truncations are not handled (latent: no RL task truncates today): see the companions
`CLAUDE.md`, "RL binding", and `.claude/rules/training.md`.

## Map Generation

Procedural maps based on `map_complexity`:
- **0:** Empty rectangle
- **1:** Scattered obstacles (maintains connectivity)
- **2+:** Rooms connected by corridors

All maps guarantee full connectivity via flood-fill validation.
