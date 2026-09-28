# The Companions - PufferLib Game Implementation

## Project Overview

Implementations of "The Companions" cooperative grid game for MARL training:

1. **Standalone Pure C++** (`companions/`) - Complete, tested
2. **Documentation** (`companions/docs/`) - Extensive doc, only read if you need to

**CRITICAL** This repo follows TDD principles. Tests are first class citizen.

## Repository Structure

```
companions/                    # Standalone pure C++ implementation
├── CMakeLists.txt
├── src/
│   ├── core/                  # Types, Grid, Cells, Objects, Pathfinder
│   │   └── fsm/               # Enemy FSM AI (fsm_state.h, fsm_states.cc, enemies.cc)
│   ├── demo/                  # Interactive play
│   ├── env/                   # BaseEnv, Envs, TaskLens
│   │   ├── task_lens.h        # Abstract TaskLens interface
│   │   ├── synchro_lens.h/cc  # SynchroLens implementation
│   │   ├── aggro_lens.h/cc    # AggroLens implementation
│   │   └── dodge_lens.h/cc    # DodgeLens implementation
│   └── viz/                   # ASCII/ANSI renderer
├── tests
└── benchmarks/
    ├── cpp/fsm_benchmark.cc   # C++ FSM performance benchmark
    └── python/                # RL algorithm comparison scripts
```

## Tests
- One executable per area, registered with ctest (`CMakeLists.txt`); on Windows (VS
  multi-config) they land in `build/bin/Release/` (e.g. `companions_skills_test.exe`)
- Skills, tags, zones, Rooted, friendly fire, SkillBook, line / landing rules:
  `companions_skills_test` (`tests/test_skills.cc`); their snapshot v4 JSON and C API
  sides live in `tests/test_snapshot_json.cc`, `tests/test_snapshot.cc`, `tests/test_api.cc`

## Game design
**DOCUMENTATION** look at `docs/GDD.md` to know more
Only read it if you need to develop new environments so you can follow
the spirit of the game.
The companions is a multi agent cooperative env played on a 2D grid.
All the dynamics are defined in BaseEnv. Task-specific behavior (rewards, termination,
observation masking) is handled by **TaskLens** objects that can be swapped at runtime.





## Key Technical Details

### Grid & Movement
- 4-connected movement (Up/Down/Left/Right + Stay)
- The world is a rectangular 2D grid, and there is a Cell in each square of the grid
- Actors are Objects that have a position on the grid
- There can be at most a single living Actor in a Cell. Dead agents stay where they
  died, and living ones may walk / land onto their cell (collisions, `IsOccupied`,
  `CanLand` ignore the dead)
- `ObjectManager`'s actor grid (`GetActorAt`) holds one actor per cell: a dead actor
  never takes a cell from a living one (`PlaceInGrid`, used by `CreateActor`,
  `UpdatePosition` and `RebuildGrid`, which the copy ctor / `operator=`, the D4
  transform and `LoadSnapshot` use), so `GetActorAt` returns the living one. A corpse
  walked over and left drops out of the grid: read dead agents from `GetAllAgents()`,
  never by cell

### Action space
 - Multidiscrete action space: movement X interaction
 - Movement has 5 options (Stay, Up, Down, Left, Right)
 - Interaction (`InteractAction`, `core/types.h`):
   - `None`
   - `Skill1`: use the skill in slot 0 (`Attack` is its historical alias)
   - `Skill2`: slot 1. Declared (data, snapshots, C API all carry 2 slots) but not
     in the flat space until `kEnabledSkillSlots = 2` (the space then grows from
     10 to 15). The C API accepts `Skill2` and treats it as `None`
 - Flat action = `movement * kNumInteractActions + interact` (10 today);
   `EncodeAction` / `DecodeAction` flatten / unflatten for other frameworks
 - Using a skill: the companion stays put, the movement only aims (sets its
   facing; Stay keeps it)
 - Slots are never empty: without another skill a slot holds the fixed default
   `attack` (`kDefaultSkill`), so every companion, RL envs' included, strikes the
   faced cell on `Skill1` (1 damage, allies spared) instead of moving
 - A skill that can't be used (a name the book lacks, cooldown, disabled slot,
   rooted + self-moving skill, dead caster) is dropped and the movement applies as with `None`
 - `BaseEnv::LegalActions`: Stay and each move onto an in-bounds walkable cell (interact
   `None`), plus, for a companion that is not stunned and `CanUseSkill(slot 0)`, `Skill1`
   with each of the 5 aims (a wall-facing aim included). FSM / plain agents: movement
   only; the dead: Stay only. Only the C++ tests call it today
 - The legacy generic companion cast (an on/off flag that cast an effect for an EMPTY
   slot 0) is gone, with its C API setter / getter and EffectSpawned events (C API 1.2.0)

### Collision Resolution
Fixed-point iteration algorithm in `base_env.cc:ResolveCollisions()`:
1. Invalidate moves into walls/out-of-bounds
2. Cancel swap conflicts (A→B, B→A)
3. Cancel same-cell conflicts (A→X, B→X)
4. Validate chase moves (only if target is vacating)

### Skills, tags, zones
The env knows no MEANING: skill and tag names ("fireball", "burning") are opaque data
(at most `kMaxNameLength` = 31 bytes), a level may retune or add skills, and what a tag
*does* is the host's business. But the env simulates every mechanic: targeting, motion,
tags, roots, cooldowns. Code: `core/skill_config.{h,cc}`, `core/tag_table.h`,
`env/skill_motion.{h,cc}`, `env/base_env.cc` (`ResolveSkills`, `UseSkill`, `Affects`, `AreaMotion`).

**Step order** (`BaseEnv::Step`):
1. Clear the per-step reports, tick tags and cooldowns
2. `PreStep` (enemy FSM) → `GatherIntentions` → `ResolveCollisions` → `ExecuteValidatedMovements`
3. `ApplyZoneTags` (every living agent on a zone cell)
4. `ResolveInteractions` → `ResolveSkills`
5. Effects tick, statuses tick, `tick_++`, `PostStep`, rewards (TaskLens)

**Line and landing rules** (`env/skill_motion.h`):
- Line rule: a line travels along the facing; a wall (not pathable) or the grid edge
  stops it on the cell before; holes (`CellKind::Hazard`: pathable, not walkable) and
  actors are crossed
- Landing rule: something moved never ends on a hole or on another living actor
- Ground target: the cell `range` away by the line rule (may be a hole)
- Projectile: the first living agent the skill affects within `range`, else the last cell reached
- Dash: up to `distance` by the line rule, lands on the furthest valid cell (else stays)
- Teleport: exactly `distance`, else `distance - 1`, ... 1 (ignores what lies between,
  walls included), else stays
- Push: each ring thing `distance` away from the centre, as a dash. Pull: exactly one
  cell, only into a free, walkable centre

**Builtins** (`SkillBook`, `skill_config.cc`). Tags are permanent (-1).

| Skill | Targeting | Area | Motion | Effect | Cooldown |
|-------|-----------|------|--------|--------|----------|
| `fireball` | ground, range 3 | cross | push_out 1 (the ring) | `burning` | 3 |
| `lightningStep` | self | cross around the landing cell | dash 4, `tag_path` | `electrified` on agents crossed on the path and on the 4 cells orthogonal to the landing cell; `self_tags=false` | 3 |
| `teleport` | self | single | teleport 3, else 2, else 1 | - | 4 |
| `vortex` | ground, range 3 | cross | pull_in: one ring thing, priority up/right/down/left | roots the affected agents on the cross (pulled one included) for the next step; `self_root=false` | 4 |
| `attack` | projectile, range 1 (the faced cell; a wall stops it) | single | - | `damage` 1; `friendly_fire=false` (allies and caster spared) | 0 |

**Default skill `attack`** (`kDefaultSkill`, `core/types.h`; its config is a
`SkillBook` builtin): every companion slot holds a skill, and one with nothing else in
it holds `attack`. It is FIXED:
- `SkillBook::Define` of a skill named `attack` throws ("'attack' is the fixed default
  skill", `RejectDefaultSkillName`, shared with snapshot validation); `Reset` keeps it;
  a snapshot whose `skills` contain it is rejected; `SaveSnapshot` never writes it
- Clearing a slot puts it back: `Companion` starts with it in both slots,
  `SetCompanionSkill(id, slot, "")`, a snapshot slot `""` (as v3 / v4 files wrote for
  an empty one) or a missing slot, `companions_set_agent_skill(..., "" or NULL)`;
  agent state reports `"attack"`, never `""`

Cooldown: used at step t, usable again at step t + cooldown (0 and 1 both mean every step).

**SkillConfig** (JSON key in parentheses when it differs):

| Field | Default | Meaning |
|-------|---------|---------|
| `name` | required | Opaque, unique in the book (`Define` replaces a same-name skill) |
| `targeting` | projectile | `self` (centre = caster after its motion) / `ground` / `projectile` |
| `range` | 1 | Ground / projectile distance |
| `filter` | all | `all` / `companion` / `enemy` / `neutral`: who can be affected |
| `area` | single | `single` (centre) / `cross` (centre + in-bounds ring, order up, right, down, left) |
| `motion` | none | `none` / `dash` / `teleport` (caster) / `push_out` / `pull_in` (ring things; need a cross) |
| `motion_distance` (`distance`) | 0 | Dash / teleport / push distance (pull is always 1) |
| `tag_path` | false | Dash: agents crossed on the way are affected too |
| `tags` | [] | `{tag, duration}` landed on every affected agent |
| `damage` | 0 | Health every affected agent (area and dash path) loses, via `Agent::TakeDamage` (Marked ×1.5, truncated: 1 stays 1, 2 → 3; 0 HP = dead, as with effects) |
| `root_steps` | 0 | Affected agents on the area are rooted for this many next steps |
| `cooldown` | 0 | See above |
| `friendly_fire` | true | Off: only agents NOT of the caster's faction are affected (allies and caster get no tag, damage, push/pull or root; a projectile flies past them) |
| `self_tags` / `self_motion` / `self_root` / `self_damage` | true | With friendly fire, the caster is affected when it stands in its own area (after its own motion); each flag can spare it that effect |

- "Affected" (`BaseEnv::Affects`) = living, passes `filter`, and `friendly_fire` or not of
  the caster's faction. Push / pull also move living non-agent actors (no faction check)
- Resolution of one skill (`UseSkill`): caster motion and centre → affected agents
  (area, then dash path) → tags → damage → root (area only, before anything moves) →
  push / pull. An agent the damage kills keeps the tags (reported) but is neither
  rooted nor moved, and is no longer affected by anything (dead)
- Damage is not reported as events yet (`Companions_Event_AgentDamaged` is declared,
  not implemented): read it from the agents' health
- `ValidateSkillConfig`: non-empty name ≤ 31 bytes; range, distance, damage, root_steps,
  cooldown ≥ 0; tag names non-empty ≤ 31 bytes with duration -1 or > 0; enums in range

**Multiple casters** resolve sequentially, in agent-index order, each from its CURRENT
position: an earlier push / pull can move a later caster before it acts (aim, range and
area start from its new cell), and earlier casters claim landing cells first. A caster
rooted earlier in the pass still resolves its skill this step (usability is decided in
`GatherIntentions`; the root blocks from the next step).

**Tags** (`TagTable`, `Agent::ApplyTag`):
- Opaque names interned per env; ids stay stable (the table only grows, never cleared
  by `LoadSnapshot` / `Reset`). Persist names, not ids
- Duration in steps, or -1 (`kPermanentTag`); re-applying keeps the longer (permanent wins)
- Durations tick at the START of `Step`: landed during step t with duration d, the tag
  is present after steps t .. t+d-1
- No gameplay effect in the env. Host primitives: `ApplyTagTo` / `RemoveTagFrom`

**Zones** (`SetCellTag` / `GetCellTag` / `ClearCellTags`):
- One tag per cell ("" clears), with the duration it lands with; the zone itself never expires
- Landed (cause `"zone"`, source -1) on every living agent standing there after regular
  movement (before casts and skills), and on any agent a skill motion lands there
  (landing cell only: cells a dash crosses do not apply). Effect pushes do not apply zones
- World state: copied with the env, saved in snapshots, replaced by `LoadSnapshot`
  (generated levels have none, so every `Reset` clears them)

**Statuses** (`StatusType`, `core/object.h`): `Stunned`(1) forces Stay, `Marked`(3)
(damage ×1.5 in `Agent::TakeDamage`, truncated toward zero: 1 damage stays 1),
`Rooted`(4). `Slowed` was removed: value 2 is reserved (never reused, a snapshot carrying
it is rejected); the C API `Companions_Status_*` keeps the same numbers.
- Rooted: can't move by itself (walking becomes Stay, dash / teleport skills are
  unusable, `CanMoveItself`), can still use non-moving skills (a rooted FSM enemy still
  attacks), can be pushed / pulled
- Statuses tick at the END of `Step`, so `root_steps = n` is applied as n + 1

**Per-step reports** (cleared at the start of every `Step` and by `LoadSnapshot`, hence every `Reset`):

| BaseEnv | Content | C API event |
|---------|---------|-------------|
| `GetLastSkillUses()` | caster, skill, target (centre; landing cell for a self skill), slot | `Companions_Event_SkillUsed` (effect_id = slot, effect_name = skill) |
| `GetLastTagsApplied()` | agent, tag id, duration, source (caster / -1), cause (skill / `"zone"`), `fresh` | `Companions_Event_TagApplied` (effect_id = tag id, status_duration, health_source_id = source, tag_fresh) |

- `fresh` = the agent did not carry the tag just before this landing (so an agent on a
  duration-1 zone is re-landed fresh every step)
- Event order in a step: AgentMoved, AgentBlocked, SkillUsed, TagApplied,
  EpisodeEnd; at most `Companions_MAX_EVENTS` (64), EpisodeEnd always kept, `events_dropped` counts the rest
- C API (`src/api/companions_api.h`, version 1.2.1: 1.2 removed the legacy cast,
  1.2.1 added `companions_get_end_reason`):
  `companions_set_agent_skill` (`""` / NULL = `attack`), `companions_apply_tag` /
  `remove_tag`, `companions_set_cell_tag` / `get_cell_tag`, `companions_get_tag_name` /
  `find_tag`; `Companions_AgentState` carries tags (first 8), 2 skill slots and cooldowns;
  name buffers are 32 bytes (31 + NUL)

**Levels** bring their skills, zones and slots through snapshot JSON v4 (`core/snapshot_json.cc`):
- Top level `"skills"`: SkillConfig objects (keys above; all but `name` optional)
- Top level `"cell_tags"`: `{row, col, tag, duration}` (duration absent = -1)
- Per agent: `"tags"` (`{tag, duration}`), `"skills"` (slot names; `""` or missing =
  `attack`), `"cooldowns"`;
  statuses as `"status_type": "stunned" | "marked" | "rooted"`
- Unknown keys in a skill / tag / zone are rejected; `Snapshot::ValidateSkillsTagsZones`
  runs before any change (and when JSON / binary snapshots are parsed)
- Slots always hold a real skill: a non-empty slot naming neither a builtin nor one of
  the snapshot's `skills` is rejected (`LoadSnapshot` throws, the C API returns false;
  the message names the agent, the slot and the skill). `""` still means `attack`
- `LoadSnapshot` resets the SkillBook to the builtins, then defines the snapshot's skills
  (they may retune builtins, but not `attack`), so a level's skills never leak into
  the next load. `SaveSnapshot` writes the whole book, builtins included, but `attack`

### Pathfinding
A* with Euclidean heuristic in `pathfinder.cc`:
- Euclidean heuristic naturally prioritizes reducing the larger axis first (long axis)
- Random tie-breaking for equal distances via optional RNG (`SetRng()`)
- FSM enemies pass `FSMContext::rng` to pathfinder for varied but optimal paths

### Enemy FSM AI
Location: `companions/src/core/fsm/`

**Architecture** (Flyweight pattern):
- `FSMState` - Singleton states with no per-actor data
- `FSMContext` - Per-actor runtime data (target_id, patrol_path, detection_range, etc.)
- States call `enemy.MoveTo()` directly to set movement intentions

**States**: PatrolState → AggroState → ReturnToPatrolState → PatrolState

**Enemy Types** (in `enemies.h/.cc`):
- `Zombie`: cadence [1,0] (moves every 2 turns), A* pathfinding, detection=3, lose=5
- `Goblin`: no cadence (always moves), A* pathfinding, detection=4, lose=6
- `Dragon`: flying (ignores walls), direct movement, detection=5, lose=8. Not implemented yet

**Integration**: `BaseEnv::PreStep()` calls `UpdateEnemyFSM()` before gathering intentions

**A dead attacker's pending attacks are cancelled**:
- A dead (or stunned) agent's FSM does not run, so a wind-up in `TelegraphState`
  never reaches `AttackState`, which is what spawns the strike effect
- `EffectSystem::Tick` removes an effect still in its telegraph phase whose source
  agent exists and is dead, before it can activate. An effect already active when its
  source dies runs its course, but a looping one stops at its next restart, with or
  without a wind-up (`telegraph_ticks = 0` loops too).
  Effects without a source (`kInvalidObjectId`, host-spawned) are never cancelled
- `source_id` is an ObjectId: `LoadSnapshot` re-issues agent ids (0, 1, ... in the
  saved order) and maps effect sources, effect actor targets and FSM `target_id`
  through the snapshot's own agent `id`s (first wins on a duplicate; an id naming no
  saved agent loads as `kInvalidObjectId`), so gaps from removed objects or
  hand-authored ids never misattribute an effect
- A companion that kills the attacker during step t cancels a strike due at the end
  of step t: skills resolve before effects tick

### Environments & TaskLens

Each environment uses a **TaskLens** to define task-specific behavior:

| Env | Lens | Goal | Masks |
|-----|------|------|-------|
| SynchroEnv | SynchroLens | All companions on synchro cells | Target→Floor |
| AggroEnv | AggroLens | Lure enemy to target cell | Synchro→Floor |
| DodgeEnv | DodgeLens | Survive until horizon | Both→Floor |

**Runtime task switching** (no snapshot needed):
```cpp
env.SetTaskLens(std::make_unique<SynchroLens>());
// ... complete task ...
env.SetTaskLens(std::make_unique<AggroLens>());  // World state preserved
```

**AggroEnv details:**
- 1-3 companions, 1 Goblin enemy with FSM
- 3x3 patrol square (8 cells perimeter, clockwise)
- Aggro range: 3, Return range: 5
- Smart spawning: companions and target outside aggro range
- Episode end: success when a living FSM enemy stands on the target; failure at the
  horizon or as soon as no living FSM enemy remains (killed: the lure can't succeed
  any more, `AggroLens::IsFailed`). `AggroEnv::IsDone` applies the dead-enemy rule only
  while the active lens is the Aggro lens; it never consults another lens's `IsDone`
- Rewards: +1 win, `kTimePenalty` (-0.01) per other step. The failure is terminal:
  `BaseEnv::Step` latches it after the rewards (`IsTaskFailed`, like `success_`; the
  first outcome is final; `SetTaskLens` / `LoadSnapshot` / `Reset` clear both with
  `ResetOutcome`), so only the killing step pays `AggroLens::FailurePenalty(horizon,
  tick)` = `kTimePenalty * (horizon - tick + 1) + kEnemyDeadPenalty` (the rest of the
  episode's time cost, that step included, plus -1); later steps pay 0. A kill at any
  step returns `horizon * kTimePenalty - 1`, one below timing out: killing is never
  a shortcut, whatever the horizon. `AggroEnv::MinUtility` is that return
- `done` is a verdict for RL episodes, never a stop: the env keeps stepping. A host
  that keeps playing after a kill (a game layer) ignores `done` for that reason:
  `EndReason::TaskFailed`, see below

**Why an episode ended** (`BaseEnv::GetEndReason`, `EndReason` in `base_env.h`):
`None` while `IsDone()` is false, else `Success` (latched), `TaskFailed` (the latched
lens failure), `Horizon` (tick >= horizon), else `TaskFailed` (an env's own end rule:
a Dodge companion died, an Aggro enemy killed between steps). C API (1.2.1, additive,
no struct layout change): `Companions_EndReason` (same values: None 0, Success 1,
Horizon 2, TaskFailed 3) from `companions_get_end_reason(env)`, fixed on the step or
lens change where done becomes true, kept while a host plays on, cleared by reset and
snapshot loads; the EpisodeEnd event carries it in `effect_id`

### Known issue: D4 transform
- `SaveSnapshot` writes the TRANSFORMED world (current rows/cols, positions, zones)
  together with `d4_transform` (`base_env.cc:1136`, `:1251`), but `LoadSnapshot` treats a
  snapshot as pre-transform and transforms it again (`base_env.cc:1461-1465`): Save→Load
  is not a round trip when `d4 != 0`
- `ApplyD4Transform` swaps `rows_` / `cols_` for rotations / transposes
  (`base_env.cc:197-199`), and each `Reset` generates a level with the current
  `rows_` / `cols_` then transforms it (e.g. `synchro_env.cc:96-113`): on non-square
  grids, rows/cols alternate on every Reset

## Exporting the game
We want to integrate the pure C++ game in `companions/` into PufferLib


### Observations
5-plane tensor [5 × grid_size × grid_size]:
- Plane 0: Walkable cells
- Plane 1: Walls
- Plane 2: Synchro/goal cells
- Plane 3: Current player
- Plane 4: Other agents

### Curriculum Learning

For this repo, we don't use the curriculum learning at all. But it is present
so that the training scripts (another repo) can use the levers provided here. 

Four main levers for difficulty progression:

| Lever | Range | Effect |
|-------|-------|--------|
| **Grid Size** | 6→16 | Larger grids = longer paths, more exploration |
| **Num Companions** | 1→3 | More agents = harder coordination |
| **Map Complexity** | 0→5 | Procedural obstacles, rooms, corridors |
| **Level Difficulty** | 0→5 | Depend on the env. Not implemented |


**MapGenerator** (`core/map_generator.cc`) creates procedural maps:
- Complexity 0: Empty rectangle (backward compatible with original SynchroEnv)
- Complexity 1: Scattered obstacles while maintaining connectivity
- Complexity 2-5: Rooms connected by corridors with increasing density

All generated maps guarantee full connectivity via flood-fill validation.
See `map_generator.h` for detailed visual examples at each complexity level.

