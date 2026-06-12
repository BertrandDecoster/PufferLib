# Snapshot System

## Concept

Each env is self contained and can be reset with new layout and actor positions. This is the default mode in RL.

But this repo also handles video game logic: the player is presented a level, and a plan decomposes it into simple tasks ("companions go to these spots", "aggro this mob"). Each task has a trained RL model.


In detail:
Each env is self contained and can be reseted with new layout and actor positions. This is the default mode in RL and this repo supports it
But another aspect of this repo is that it's supposed to handle the logic of a video game. Here is a high level overview
The player is presented a level, which can be fully described, and  "stepped in" by BaseEnv. The player has to solve a problem to finish the level. 
A plan is generated to decompose
the level in a list of easy tasks, such as "companions go to these spots at the same time" or "aggro this mob and bring it there"
There are single Envs for each of these tasks, and the associated trained RL models.
This means that BaseEnv contains the entire game logic, the RL envs are here to provide observations (only observe what is necessary
for the current task), rewards for RL, termination, and initial setup if the init has to be done (it doesn't come from a snapshot)
So, ingame, the relevant model is loaded, and the relevant env loads the snapshot of the game. As the player and the
RL agents that control the companions succeed in that simple task, we continue to the next task in the plan. A snapshot of the end of
the env is taken, and it is loaded in the env that matches the next task in the plan.

### TaskLens vs Snapshots

**Task switching now uses TaskLens, not snapshots:**
```cpp
// OLD: Serialize snapshot, create new env, deserialize (slow)
// NEW: Just swap the lens pointer (fast)
env.SetTaskLens(std::make_unique<SynchroLens>());
// ... complete task ...
env.SetTaskLens(std::make_unique<AggroLens>());
```

**Snapshots are still used for:**
- Save/load game state
- Loading premade levels from editors
- Transferring state between processes (DLL boundary)

**TaskLens handles:**
- Runtime task switching within same BaseEnv
- Task-specific rewards, termination, goal-plane contents + vector obs tail
- No serialization overhead

### Architecture

```
BaseEnv (physical world)        TaskLens (task interpretation)
├── grid, agents, effects       ├── CanOperateOn()
├── annotations                 ├── IsDone(), IsSuccess()
├── SaveSnapshot()              ├── ComputeReward()
├── LoadSnapshot()              ├── IsGoalCell(), GetGoalCells()
└── SetTaskLens() ──────────────├── WriteVectorObs(), AdditionalVectorObsSize()
                                └── Activate(), Deactivate()
```

Snapshots capture physical state only (grid, agents, effects, annotations,
tick, RNG). TaskLens is stateless - computed on-demand from BaseEnv state.
Goal cells are SemanticTag annotations (e.g. SynchroGoal, AggroTarget) read
via `IsGoalCell()`; there is no cell masking/rewriting.

## DLL API

```c
// Two-call pattern: get size, then save
int32_t companions_get_snapshot_size(const Companions_Env* env);
bool companions_save_snapshot(const Companions_Env* env, uint8_t* out_buffer, int32_t buffer_size);

// Load from buffer
bool companions_load_snapshot(Companions_Env* env, const uint8_t* data, int32_t data_size);
```

**Usage:**
```cpp
// Save
int32_t size = companions_get_snapshot_size(env);
std::vector<uint8_t> buffer(size);
companions_save_snapshot(env, buffer.data(), size);

// Load
companions_load_snapshot(env, buffer.data(), buffer.size());
```

**Notes:**
- Returns `false`/`0` on error, check `companions_get_error()`
- Contains: grid, agents, effects, annotations, tick, RNG state (~2-5 KB)
- Binary format: magic `0x534E4150` ("SNAP"), version 2 (v2 adds annotations; v1 payloads still load via MigrateV1)
- Format is locked by golden fixtures (`tests/data/golden_snapshot_v2.{bin,json}`, `test_snapshot_golden.cc`)

## JSON API

Human-readable JSON serialization for external tools (Claude Code game playing, editors).

```c
// String-based (caller frees with companions_free_string)
const char* companions_snapshot_to_json(const Companions_Env* env);
void companions_free_string(const char* str);
bool companions_load_snapshot_json(Companions_Env* env, const char* json_str);

// File-based
bool companions_save_snapshot_json(const Companions_Env* env, const char* filepath);
bool companions_load_snapshot_json_file(Companions_Env* env, const char* filepath);
```

**JSON Structure:**
```json
{
  "magic": "SNAP",
  "version": 2,
  "grid": {"rows": 10, "cols": 10, "cells": [...]},
  "agents": [{"id": 1, "agent_type": "Player", "position": {"row": 3, "col": 5}, ...}],
  "annotations": [{"target": "Cell", "pos": {"row": 2, "col": 4}, "tag": "SynchroGoal", "owner_lens_id": -1, "params": {...}}, ...],
  "effects": [...],
  "tick": 42,
  "horizon": 100,
  "rng_state": {"state": 12345, "inc": 67890},
  "d4_value": 0,
  "patrol_path": [...]
}
```

**Notes:**
- Enums serialize as strings (e.g., `"Floor"`, `"COMPANION"`, `"Patrol"`)
- Pretty-printed with 2-space indent
- Full round-trip fidelity with binary format

## FSM State Persistence

Enemy FSM state is fully preserved across snapshots, enabling CLI stepping and mid-combat save/load.

**What's saved (FSMSnapshot struct in `snapshot.h`):**
- `state_type` - Current FSM state as `FSMStateType` enum (Patrol, Aggro, Telegraph, Attack, Recovery, ReturnToPatrol)
- `target_id` - Currently targeted companion
- `patrol_path`, `patrol_index`, `patrol_forward` - Patrol waypoints and position
- `detection_range`, `lose_target_range` - Aggro configuration
- `rng_state`, `rng_inc` - RNG state for deterministic pathfinding tie-breaking
- `attack_tick_counter` - Progress through attack phases
- `attack_target_position`, `attack_area_*`, `attack_damage`, `attack_filter` - Locked attack intent

**How restoration works (`base_env.cc` LoadSnapshot):**
1. State pointer restored via `GetFSMStateByType()` registry lookup
2. FSMContext fields restored (target, patrol, ranges)
3. Attack runtime state restored (preserves mid-attack position)
4. RNG state restored via `pcg32::SetState()`

**Key files:**
- `fsm_state.h` - `FSMStateType` enum, `FSMState::GetType()` virtual
- `fsm_states.h` - `GetFSMStateByType()` registry function
- `object.h` - `AgentFSM::SetCurrentState()`, `SetTick()` methods
- `snapshot.h` - `FSMSnapshot` struct with all persisted fields