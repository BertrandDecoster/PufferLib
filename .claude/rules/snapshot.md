# Snapshot System

## Concept

Each env is self contained and can be reset with new layout and actor positions. This is the default mode in RL.

But this repo also handles video game logic: the player is presented a level, and a plan decomposes it into simple tasks ("companions go to these spots", "aggro this mob"). Each task has a trained RL model. The game loads the relevant model/env, and snapshots transfer state between tasks as players progress through the plan.

In detail:
Each env is self contained and can be reseted with new layout and actor positions. This is the default mode in RL and this repo supports it
But another aspect of this repo is that it's supposed to handle the logic of a video game. Here is a high level overview
The player is presented a level, which can be fully described in BaseEnv. The player has to solve a problem. A plan is generated to decompose
the level in a list of easy tasks, such as "companions go to these spots at the same time" or "aggro this mob and bring it there"
There are single Envs for each of these tasks, and the associated trained RL models.
This means that BaseEnv contains the entire game logic, the RL envs are here to provide observations (only observe what is necessary
for the current task), rewards for RL, termination, and initial setup if it doesn't come from a snapshot
So, ingame, the relevant model is loaded, and the relevant env loads the snapshot of thee game. As the player and the RL agents that 
control the companions succeed in that simple task, we continue to the next task in the plan. A snapshot of the end of the env is taken,
and it is loaded in the env that matches the next task in the plan.

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
- Contains: grid, agents, effects, tick, RNG state (~2-5 KB)
- Binary format: magic `0x534E4150` ("SNAP"), version 1

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
  "grid": {"rows": 10, "cols": 10, "cells": [...]},
  "agents": [{"id": 1, "agent_type": "Companion", "position": {"row": 3, "col": 5}, ...}],
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