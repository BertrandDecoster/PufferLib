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
int32_t ue_companions_get_snapshot_size(const UE_CompanionsEnv* env);
bool ue_companions_save_snapshot(const UE_CompanionsEnv* env, uint8_t* out_buffer, int32_t buffer_size);

// Load from buffer
bool ue_companions_load_snapshot(UE_CompanionsEnv* env, const uint8_t* data, int32_t data_size);
```

**Usage:**
```cpp
// Save
int32_t size = ue_companions_get_snapshot_size(env);
TArray<uint8> buffer;
buffer.SetNum(size);
ue_companions_save_snapshot(env, buffer.GetData(), size);

// Load
ue_companions_load_snapshot(env, buffer.GetData(), buffer.Num());
```

**Notes:**
- Returns `false`/`0` on error, check `ue_companions_get_error()`
- Contains: grid, agents, effects, tick, RNG state (~2-5 KB)
- Binary format: magic `0x534E4150` ("SNAP"), version 1