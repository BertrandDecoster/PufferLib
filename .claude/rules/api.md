---
paths: pufferlib/ocean/companions/src/api/**
---

# C API Integration

The companions game is built as a Windows DLL with a C API for external integration (game engines, FFI bindings).

## Architecture

```
Consumer (UE5, ASCII, etc) → companions_api.dll (C API) → SynchroEnv (C++ game logic)
```

The C API wraps the pure C++17 game logic with POD-only types for safe DLL boundary crossing.

## Key Files

| File | Purpose |
|------|---------|
| `src/api/companions_api.h` | C API header with POD structs |
| `src/api/companions_api.cc` | Implementation wrapping SynchroEnv |
| `tests/test_api.cc` | Unit tests for C API |
| `tests/test_api_parity.cc` | Parity test vs direct C++ |
| `tests/test_api_reactions.cc` | 1.5 / 1.6: reports (turn health, odd motions included), outcome previews, level data, parity |
| `tests/test_revive_lens.cc` | 1.6: `Companions_Lens_Revive` (after its C++ tests) |

## Core API

### Lifecycle

```c
Companions_Env* companions_create(const Companions_EnvConfig* config);
void companions_destroy(Companions_Env* env);
bool companions_reset(Companions_Env* env, uint32_t seed);  // 1.5: false on error
```

### Game Loop

```c
bool companions_step(                // 1.5: false with the error (cleared first)
    Companions_Env* env,
    const Companions_Action* actions,  // [num_agents] movement + interact
    int32_t action_count,              // Must be the agent count
    Companions_StepResult* out_result  // State + transition events
);
```

A refused step leaves `out_result` untouched; a step that throws inside the env fills it
with the env's current state and no event. No exception crosses the boundary.

### State Queries

```c
void companions_get_state(const Companions_Env* env, Companions_GameState* out);
Companions_CellKind companions_get_cell(const Companions_Env* env, int row, int col);
void companions_get_grid(const Companions_Env* env, Companions_CellKind* out_grid);
bool companions_get_agent_by_index(const Companions_Env* env, int index, Companions_AgentState* out);
```

## Key Structs

| Struct | Contents |
|--------|----------|
| `Companions_EnvConfig` | rows, cols, num_companions, num_synchro, map_complexity, horizon, seed |
| `Companions_Action` | movement (0-4), interact (0-1) |
| `Companions_AgentState` | id, position, prev_position, health, facing, fsm_state, statuses |
| `Companions_GameState` | agents[], special_cells[], effects[], done, success, rewards[] |
| `Companions_StepResult` | state + events[] for animation |
| `Companions_Event` | type, tick, subject_id, position data, event-specific fields |

## Event System

Events returned by `companions_step()` for animation:

| Event | Description |
|-------|-------------|
| `Companions_Event_AgentMoved` | Agent successfully moved to new position |
| `Companions_Event_AgentBlocked` | Agent tried to move but was blocked |
| `Companions_Event_HealthChanged` | 1.6: one agent's turn health (`health_amount` = change, `health_new` = HP, `effect_id` = `Companions_TurnOutcome`, `report_index` = its `companions_get_turn_health` entry); a state-change event. Supersedes `AgentDamaged` / `AgentHealed` (declared, never emitted) |
| `Companions_Event_AgentDowned` | A companion went down (1.3) |
| `Companions_Event_AgentRevived` | A downed companion got up (1.4: reviver, HP) |
| `Companions_Event_AgentDefeated` | 1.5: a weakness (P, S) defeated an agent (S, its landing's source / `tag_kind` / `tag_reaction`); a state-change event |
| `Companions_Event_SkillUsed` | A companion used a skill slot (caster, centre, slot, skill) |
| `Companions_Event_TagApplied` | A tag landed on an agent (a skill's, a zone's or a reaction's result; 1.5: `tag_kind`, `tag_reaction`, `health_amount` = zone damage) |
| `Companions_Event_ReactionFired` | 1.5: a reaction fired (trigger, rule, result, the triggering landing's source / kind) |
| `Companions_Event_EpisodeEnd` | Episode ended (`effect_id` = the end reason): each time done becomes true, and (1.6) once more when TeamDown / Horizon upgrades an Interrupted end |

Order in a step: moved / blocked, health changed, downed, revived, defeated, skill used,
tag applied, reaction fired, episode end (grouped by kind, not in time order; the state
changes first, so the cap drops skill / tag / reaction events first). Since 1.5 every
event carries `report_index` (its entry in the uncapped report query, -1 for none): the
events are capped, the reports are not.

The header's "Versioning" and "Event System" comments are the reference (order,
event cap, what each version changed); the companions `CLAUDE.md` summarizes them.

## Skills and tags

Setters: `companions_set_agent_skill` (the equipped skill), `companions_apply_tag` /
`remove_tag`, `companions_set_cell_tag`. Queries (1.4): the skill book
(`companions_get_skill_count` / `get_skill` / `find_skill` → `Companions_SkillInfo`),
`companions_preview_skill` (→ `Companions_SkillPreview`: what a slot would do now) and
the last step's uses (`companions_get_last_skill_use_count` / `get_last_skill_use` →
`Companions_SkillUseInfo`, the affected agents with `Companions_SkillEffect` flags and,
since 1.6, `affected_damage`: the raw share each use dealt each agent, 0 without the
Damage effect).
`Companions_AgentState.skills` are the effective skills (context rules applied),
`equipped_skills` the slots' own.

## Rules as data (1.5)

Report queries with a `Companions_ReportSource` (`LastStep`, or `Preview`: the last
`companions_preview_skill_outcome`, one held at a time): skill uses, tag landings,
reactions (with the cells a `zone_becomes` (re)set), defeats, downs, revives. The level
data, read-only: zone table, cell zones, reaction rules, tag statuses, weaknesses,
immunities. The header's "Reports" and "Level data" sections are the reference.

## The phased turn (1.6)

A step is one turn resolved in phases (intents from the world as the turn begins,
motion layers, one tag phase, the HP ledger, downs / deaths / defeats / revives at the
end of the turn; effects planned into the turn). Reports: the tag landings list the
zones' first, then the skills', then the reactions' results; every report's `damage` is
the source's raw share (before Marked) and the HP truth is the turn health
(`Companions_TurnHealth`, `companions_get_turn_health_count` / `get_turn_health`, both
sources; `Companions_TurnOutcome` None / Downed / Died / Defeated / Revived). A reaction
or a defeat credits the first non-zone landing of its tag. Odd motions (an off-axis sum
of forced moves): `companions_get_odd_motion_count` / `get_odd_motion` (both sources),
`companions_get_odd_motion_total` (the env's life). `companions_preview_skill_outcome`
runs a whole turn on a copy with that use alone (everyone else stays, no FSM, no effect
activation); `Companions_SkillOutcome` gained `turn_health_count` / `odd_motion_count`
(1.6.0 amended in place: rebuild both sides). `companions_spawn_effect` between steps
applies an effect without a wind-up at once; the env's own spawns during a step wait
for the next turn (`in_telegraph`, `ticks_remaining` 1). The header's "The Turn"
section is the reference.

## Interruptions (1.6)

Done is the same for every env and lens: the success, the team down, the horizon, or
an interruption; only the team down or the horizon fail a task (`Companions_End_TaskFailed`
is never produced). A down interrupts the task: done as `Companions_End_Interrupted`, a done that can end
later (a host that plays on keeps stepping; the episode ends for real on Success /
TeamDown / Horizon, each reported by its own EpisodeEnd). The step that revives the last
downed companion already reads done false and `Companions_End_None`; the task rewards
again from the next step. The down cost (default -0.5, runtime, not in snapshots):
`companions_set_down_cost` (finite, between -1e6 and 0) / `companions_get_down_cost`. The header's
"Versioning" (1.6) and `companions_get_end_reason` comments are the reference.

`Companions_Lens_Revive` (4): get the downed allies up. Refused while nobody is down;
params: the downed allies' cells (each must hold one), none = every downed ally.
Success once nobody is down, after someone was down in its episode (a reset or
snapshot load keeping the lens starts a new one); never interrupted (a down pays the
cost and joins its goal); -0.01 per step, +1.0 on success. One lens at a time: no lens
stack (push / pop) yet.

## Thread Safety

- Each `Companions_Env` instance is NOT thread-safe
- Different instances can be used from different threads
- `companions_get_error()` uses thread-local storage

## Build

```bash
# Configure with DLL build enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_DLL=ON

# Build the DLL
cmake --build build --target companions_api --config Release

# Output files
# - build/bin/Release/companions_api.dll
# - build/lib/Release/companions_api.lib (import library)
```

## Test

```bash
# Run C API tests
ctest -C Release -R api

# Individual tests
./build/Release/companions_api_test.exe        # Unit tests
./build/Release/companions_api_parity_test.exe # Parity vs direct C++
```

## Parity Test

The parity test (`test_api_parity.cc`) verifies that the C API produces **identical results** to directly calling `SynchroEnv` C++ methods:

- Same agent positions after reset with same seed
- Same rewards after each step with same actions
- Same done/success flags at episode boundaries
- Same grid state (cell kinds)
- Determinism across multiple runs
