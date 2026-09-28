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
  `companions_skills_test` (`tests/test_skills.cc`); their snapshot JSON and C API
  sides live in `tests/test_snapshot_json.cc`, `tests/test_snapshot.cc`, `tests/test_api.cc`
- Downs, `IsAffectable`, the team counter, `TeamDown`, down reports, v5 snapshots:
  `companions_downs_test` (`tests/test_downs.cc`)
- Revive (`affects_downed`, `revive_percent`, the `revive` builtin), context skills,
  the revive report, v6 context rules: `companions_revive_test` (`tests/test_revive.cc`);
  `PreviewSkill` and `SkillUse::affected`: `companions_skills_test`; their C API side:
  `tests/test_api.cc`
- A test that registers its own effects holds a `ScopedEffectRegistry`
  (`tests/effect_registry_guard.h`), declared before its envs: it clears the global
  `EffectConfigRegistry` back to the builtins on entry and on exit, even when an
  assertion throws

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
  `CanLand` ignore the dead). A downed companion (see Downs) is alive: it keeps its
  cell and blocks it
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
   - `Skill1`: use slot 0's effective skill (see Context skills; `Attack` is its
     historical alias)
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
 - A skill that can't be used (a name the book lacks, cooldown (an equipped skill's
   only), disabled slot, rooted + self-moving skill, dead or downed caster) is dropped
   and the movement applies as with `None`
 - `BaseEnv::LegalActions`: Stay and each move onto an in-bounds walkable cell (interact
   `None`), plus, for a companion, each enabled slot (`slot < kEnabledSkillSlots`) it
   `CanUseSkill`, as `Skill1` / `Skill2` with each of the 5 aims (a wall-facing aim
   included). FSM / plain agents: movement only; the dead, the downed and the stunned
   (any kind): Stay only (`GatherIntentions` forces the downed and the stunned to
   Stay); the rooted (any kind): no moves (Stay, and a companion's skills). Only the
   C++ tests call it today
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
tags, roots, cooldowns, revives. Code: `core/skill_config.{h,cc}`, `core/tag_table.h`,
`core/context_skill.{h,cc}`, `env/skill_motion.{h,cc}`, `env/base_env.cc` (`ResolveSkills`,
`UseSkill`, `ResolveSkillTargets`, `PreviewSkill`, `EffectiveSkill`, `Affects`, `AreaMotion`).

**Step order** (`BaseEnv::Step`):
1. Clear the per-step reports, `BeginStep` on every agent
2. `PreStep` (enemy FSM) → `GatherIntentions` (fixes each skill use's effective skill:
   the context rules are read here, once, before anyone moves) → `ResolveCollisions` →
   `ExecuteValidatedMovements`
3. `ApplyZoneTags` (every affectable agent on a zone cell: alive, not downed)
4. `ResolveInteractions` → `ResolveSkills` (one `UseSkill` per caster, in agent-index
   order: `ResolveSkillTargets`, then the effects, see "Resolution of one skill")
5. Effects tick, `EndStep` on every agent (tags, statuses, cooldowns tick), the downs
   since the last report (`GetLastDowns`, after `EndStep`), `tick_++`, `PostStep`,
   rewards (TaskLens)

**Step timers** (`Agent::BeginStep` / `EndStep`): tag and status durations and cooldowns all
count steps and all tick at the END of `Step`. A timer of n is in effect for the n next
steps: set between two steps (host primitives, snapshots), it reads n; set during a step,
it also covers the rest of that step (kept as n + 1 inside the step) and reads n after it.
What a host reads between steps is always the number of steps to come it covers.

**Line and landing rules** (`env/skill_motion.h`):
- Line rule: a line travels along the facing; a wall (not pathable) or the grid edge
  stops it on the cell before; holes (`CellKind::Hazard`: pathable, not walkable) and
  actors are crossed
- Landing rule: something moved never ends on a hole or on another living actor
- Ground target: the cell `range` away by the line rule (may be a hole)
- Projectile: the first agent the skill affects within `range` (a downed one is passed
  over; by an `affects_downed` skill, a standing one), else the last cell reached
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
| `revive` | projectile, range 1, filter `companion` | single | - | `affects_downed`, `revive_percent` 50: a downed ally on the faced cell gets up with half its max HP, rounded up. Retunable by a level (unlike `attack`); the default context rule gives it (see Context skills) | 0 |
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

Cooldown: 0 = no cooldown (every step); n = blocked for the n steps after the one it was
used in (used at step t, usable again at step t + n + 1). Reads n after the use, then
n - 1, ...; `LegalActions` and `GatherIntentions` read the same value.

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
| `damage` | 0 | Health every affected agent (area and dash path) loses, via `Agent::TakeDamage` (Marked ×1.5, truncated: 1 stays 1, 2 → 3; 0 HP = dead, a companion down, as with effects) |
| `root_steps` | 0 | Affected agents on the area are rooted for this many next steps |
| `cooldown` | 0 | See above |
| `friendly_fire` | true | Off: only agents NOT of the caster's faction are affected (allies and caster get no tag, damage, push/pull or root; a projectile flies past them) |
| `self_tags` / `self_motion` / `self_root` / `self_damage` | true | With friendly fire, the caster is affected when it stands in its own area (after its own motion); each flag can spare it that effect |
| `affects_downed` | false | Off: affects the standing only (alive, not downed). On: the downed only (alive and down), which it can only revive: no tags, damage, `root_steps` or motion, and `friendly_fire` on (validated) |
| `revive_percent` | 0 | A downed companion it affects gets up (`Companion::Revive`) with this percent of its max HP, rounded up (at least 1); in [0, 100], > 0 needs `affects_downed` |

- "Affected" (`BaseEnv::Affects`) = affectable (alive, not downed; for an
  `affects_downed` skill: alive and downed), passes `filter`, and `friendly_fire` or not
  of the caster's faction. Push / pull also move living non-agent actors (no faction check)
- Resolution of one skill (`UseSkill`): `ResolveSkillTargets` (pure, shared with
  `PreviewSkill`: the caster's landing, the centre, the affected agents (area in cell
  order, then a `tag_path` dash's path) and what the use will do to each, read with the
  caster already on its landing cell) → caster motion → tags → damage → revive → root
  (area only, before anything moves) → push / pull. An agent the damage kills or downs
  keeps the tags (reported) but is neither rooted nor moved, and is no longer affected by
  anything
- Damage is not reported as events yet (`Companions_Event_AgentDamaged` is declared,
  not implemented): read it from the agents' health
- `ValidateSkillConfig`: non-empty name ≤ 31 bytes; range, distance, damage, root_steps,
  cooldown ≥ 0; tag names non-empty ≤ 31 bytes with duration -1 or > 0, at most
  `kMaxSkillTags` (32, the C API's `Companions_MAX_SKILL_TAGS`) tags; enums in range;
  `revive_percent` and `affects_downed` as above

**Multiple casters** resolve sequentially, in agent-index order, each from its CURRENT
position: an earlier push / pull can move a later caster before it acts (aim, range and
area start from its new cell), and earlier casters claim landing cells first. A caster
rooted earlier in the pass still resolves its skill this step (usability is decided in
`GatherIntentions`; the root blocks from the next step).

**Revive** (a skill with `revive_percent` > 0; the builtin `revive`):
- `Companion::Revive(health)`: the downed companion gets up where it lies, with
  `ceil(max_health * revive_percent / 100)` HP (clamped to [1, max]); its statuses are
  already clear (cleared as it went down), its tags kept, `times_downed` unchanged (the
  team's counter never goes back). It acts from the next step, and its cell's zone lands
  on it from the next step (zones apply before skills)
- A revive is a skill use: a `SkillUse` (the revived agent with the `Revive` effect) plus
  a `GetLastRevives()` entry (reviver, revived, health). Several revivers of one ally in
  a step: the first in agent-index order gets it up, the later ones find it standing and
  affect nobody. Revived then downed again in the same step: a revive, then a down

**Context skills** (`core/context_skill.{h,cc}`; `BaseEnv::EffectiveSkill`,
`IsContextSkill`, `GetContextSkills` / `SetContextSkills`):
- A slot holds its EQUIPPED skill (what `SetCompanionSkill`, snapshots and the C API
  setter write: `Companion::GetSkill`) and an EFFECTIVE one: the skill of the first rule
  `{condition, slot, skill}` for that slot whose condition holds for the companion, else
  the equipped one. Computed whenever read, never stored or swapped. A companion that
  cannot act (downed, dead) has no context (effective = equipped); a slot outside
  [0, `kMaxSkillSlots`) has `""`
- Conditions (`ContextCondition`, evaluated by `BaseEnv::ContextHolds`):
  `adjacent_downed_ally` = a downed agent of the companion's faction on one of its 4
  neighbours. A new condition is an enum value, its name in `context_skill.cc`'s table
  and its case in `ContextHolds`
- Default rules (`DefaultContextSkills()`: a fresh env, a level without
  `"context_skills"`): `[{adjacent_downed_ally, slot 0, revive}]`, so every level has revive
- Cooldowns belong to the equipped skill: a rule's skill must have cooldown 0
  (`ValidateContextSkills`), and a context use neither reads nor spends the slot's
  cooldown (usable whatever it says; it keeps ticking), even when the rule names the
  equipped skill itself (the origin decides, not the name)
- `GatherIntentions` fixes each use's skill (the rule, if any): an ally revived or downed
  later in the step does not change it
- Validation: `SetContextSkills` returns false (rules unchanged, the reason in `error`)
  unless `ValidateContextSkills` accepts them with the current book (a known condition,
  a slot in range, a skill of the book with cooldown 0); snapshots, see Levels. A rule
  the book no longer allows (`IsUsableWith`: its skill gone, or given a cooldown later
  through `GetMutableSkillBook` / a generated Reset reloading the builtins) is skipped at
  run time (it never disables a slot) and left out by `SaveSnapshot`
- Level data, like `max_downs`: see Levels

**Previews and what a use did** (`BaseEnv::PreviewSkill`, `SkillUse::affected`):
- `PreviewSkill(caster, slot, aim)`: what that slot's effective skill, aimed `aim`, would
  do NOW (a pure query: nothing moves, lands, hurts, revives or is interned): `usable`
  (the step would use it: not stunned, `CanUseSkill`), `skill`, `centre`,
  `caster_landing`, `affected`. Same code as the step (`ResolveSkillTargets`), so
  preview and use cannot drift; computed whatever `usable` says. The step may still
  differ: it moves everyone first (enemies too), then resolves casters one by one, so an
  earlier caster's push / pull / damage / revive changes what a later one reaches; a use
  whose movement is Stay keeps the caster's facing. It says whom the skill affects, not
  where a push / pull then moves them
- `AffectedAgent { id, effects }`, `SkillEffect` bit flags: `Tags` 1, `Damage` 2,
  `Root` 4, `Motion` 8 (it really changes cell: a push against a wall is no Motion),
  `Revive` 16; 0 = affected, nothing applies (the caster gets only what its `self_*`
  flags allow). In a preview they are a prediction made before the damage; in a
  `SkillUse` they are what the use DID: an agent its own damage downed or killed has no
  Root / Motion, a pull that then took the next ring thing reports that one with Motion,
  and an ally an earlier caster got up first is not revived again (standing, it is not
  affected at all). Tags and Damage agree

**Tags** (`TagTable`, `Agent::ApplyTag`):
- Opaque names interned per env; ids stay stable (the table only grows, never cleared
  by `LoadSnapshot` / `Reset`). Persist names, not ids
- Duration in steps, or -1 (`kPermanentTag`); re-applying keeps the longer (permanent wins)
- Durations are step timers (see Step timers): landed during step t with duration d,
  the tag is present after steps t .. t+d-1 (and during step t+d, until its end)
- No gameplay effect in the env. Host primitives: `ApplyTagTo` / `RemoveTagFrom`
  (`ApplyTagTo` refuses a downed or dead agent and then interns nothing, like `LandTag`)

**Zones** (`SetCellTag` / `GetCellTag` / `ClearCellTags`):
- One tag per cell ("" clears), with the duration it lands with; the zone itself never expires
- Landed (cause `"zone"`, source -1) on every affectable agent standing there after regular
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
- Statuses are step timers (see Step timers): `root_steps = n` roots for the n next steps
- Going down clears a companion's statuses, and a downed one accepts none (see Downs)

**Per-step reports** (cleared at the start of every `Step` and by `LoadSnapshot`, hence every `Reset`):

| BaseEnv | Content | C API event |
|---------|---------|-------------|
| `GetLastSkillUses()` | caster, skill (the effective one), target (centre; landing cell for a self skill), slot, `affected` (the agents it affected, in processing order, with what it did to each: see Previews) | `Companions_Event_SkillUsed` (effect_id = slot, effect_name = skill); the whole use, `affected` included: `companions_get_last_skill_use_count` / `companions_get_last_skill_use` (not an event: never cut by the event cap) |
| `GetLastTagsApplied()` | agent, tag id, duration, source (caster / -1), cause (skill / `"zone"`), `fresh` | `Companions_Event_TagApplied` (effect_id = tag id, status_duration, health_source_id = source, tag_fresh) |
| `GetLastDowns()` | one companion id per down (a down between steps: the next step's) | `Companions_Event_AgentDowned` (subject_id, position = its cell) |
| `GetLastRevives()` | reviver, revived, health (the HP it got up with), in resolution order | `Companions_Event_AgentRevived` (subject_id = revived, health_source_id = reviver, health_new = health_amount = health, position = its cell after the step) |

- `fresh` = the agent did not carry the tag just before this landing (an agent standing on
  a duration-1 zone still carries its tag when the zone lands it again: not fresh)
- Event order in a step: movement events (AgentMoved / AgentBlocked, per agent), then
  AgentDowned, AgentRevived, SkillUsed, TagApplied, EpisodeEnd (grouped by kind, not in time order: a companion revived then
  downed again in one step has its second AgentDowned before its AgentRevived); at most
  `Companions_MAX_EVENTS` (64), EpisodeEnd always kept, `events_dropped` counts the rest.
  The state changes (movement, down, revive) come first, so SkillUsed / TagApplied are cut
  first. They always fit when no companion is revived twice in the step (per agent: one
  movement, one revive, two downs = 4 events; fits for up to 15 agents). Reviving the same
  companion twice in a step needs a skill downing it between the two revives; only then
  can state-change events be dropped
- C API (`src/api/companions_api.h`, version 1.4.0: 1.2 removed the legacy cast,
  1.2.1 added `companions_get_end_reason`, 1.3 added downs: `Companions_AgentState.downed`,
  `Companions_GameState.downs` / `max_downs` / `team_down`, `Companions_End_TeamDown` (4),
  `Companions_Event_AgentDowned` (17); 1.4 added revives and context skills:
  `Companions_AgentState.skills` = the effective skills (`EffectiveSkill`: the equipped
  ones for a downed companion, "" for a slot out of range),
  `equipped_skills` = the slots' own (`GetSkill`), `skill_cooldowns` = the equipped skills',
  `Companions_Event_AgentRevived` (18), and three queries: the skill book
  (`companions_get_skill_count` / `companions_get_skill` / `companions_find_skill` fill a
  `Companions_SkillInfo`, every SkillConfig field, tags up to `Companions_MAX_SKILL_TAGS`
  = `kMaxSkillTags` (32, so never truncated); builtins first, `attack` and `revive`
  included, then the level's own in snapshot order, a retuned builtin in place; changes
  only when a snapshot loads, a generated Reset's included), the preview (`companions_preview_skill` →
  `Companions_SkillPreview`: `PreviewSkill`) and the last step's uses
  (`companions_get_last_skill_use_count` / `companions_get_last_skill_use` →
  `Companions_SkillUseInfo`); both list the affected agents with their
  `Companions_SkillEffect` flags (same values as `SkillEffect`), the first
  `Companions_MAX_AGENTS` (8) in `affected_count`, all of them in `affected_total`.
  1.3 and 1.4 changed struct layouts, consumers rebuild; 1.4.0 was amended in place
  before release (the queries), so a consumer built against the final header refuses,
  or mis-reads, a DLL from an earlier 1.4.0 commit: rebuild both sides):
  `companions_set_agent_skill` (`""` / NULL = `attack`; sets the equipped skill),
  `companions_apply_tag` / `remove_tag`, `companions_set_cell_tag` / `get_cell_tag`,
  `companions_get_tag_name` / `find_tag`; `Companions_AgentState` carries tags (first 8),
  2 skill slots (effective and equipped) and cooldowns; name buffers are 32 bytes
  (31 + NUL). The context rules themselves are not exposed (a host reads their effect in
  `skills`, or in a preview's `skill`; level tools read them in the snapshot JSON)

**Levels** bring their skills, zones, slots, downs and context skills through snapshot
JSON v6 (`core/snapshot_json.cc`; versions 2..6 load, binary snapshots follow the same
number, binary 1..6):
- Top level `"skills"`: SkillConfig objects (keys above; all but `name` optional; v6
  adds `affects_downed` / `revive_percent`, absent = false / 0)
- Top level `"cell_tags"`: `{row, col, tag, duration}` (duration absent = -1)
- Top level `"max_downs"` (v5; absent = 3, must be >= 1)
- Top level `"context_skills"` (v6): `[{condition, slot, skill}]` (all required;
  condition `"adjacent_downed_ally"`). Absent = the default rules (next to a downed
  ally, slot 0 is `revive`), so every older level has revive; `[]` = no rule.
  `ValidateSkillsTagsZones` checks them (the default ones too) against the level's
  book (builtins + its skills): a known condition, slot in range, a known skill with
  cooldown 0 (a level may retune `revive`, not give it a cooldown while a rule uses
  it); messages name `context_skills[i]`. `LoadSnapshot` sets them; `SaveSnapshot`
  always writes the env's (but those the book no longer allows, `IsUsableWith`: they
  are inert, and the loader would reject them); a generated `Reset` keeps the env's
  (`LoadGeneratedLevel`, unchecked against the builtins). A rule whose skill the book
  lacks or gave a cooldown is skipped at run time
- A saved level pins its context rules (SaveSnapshot writes them explicitly): delete
  `"context_skills"` from a level's JSON for it to follow the default rules
- Every agent's `max_health` >= 1 (a revive brings back a percent of it)
- Per agent: `"tags"` (`{tag, duration}`), `"skills"` (slot names; `""` or missing =
  `attack`), `"cooldowns"`;
  statuses as `"status_type": "stunned" | "marked" | "rooted"`; companions only (v5):
  `"downed"`, `"times_downed"` (absent = false, 0; see Downs for their rules)
- Unknown keys in a skill / tag / zone are rejected; `Snapshot::ValidateSkillsTagsZones`
  runs before any change (and when JSON / binary snapshots are parsed)
- Slots always hold a real skill: a non-empty slot naming neither a builtin nor one of
  the snapshot's `skills` is rejected (`LoadSnapshot` throws, the C API returns false;
  the message names the agent, the slot and the skill). `""` still means `attack`.
  Older saved levels with such slots no longer load: regenerate them (noted in the
  C API Versioning section)
- Validation builds the level's book once: `SkillBook::Define` per skill (it validates
  and rejects `attack`), plus an explicit check for an unnamed skill (which `Define`
  silently skips)
- `LoadSnapshot` resets the SkillBook to the builtins, then defines the snapshot's skills
  (they may retune builtins, but not `attack`), so a level's skills never leak into
  the next load. `SaveSnapshot` writes the whole book, builtins included, but `attack`

### Downs
A companion at 0 HP goes DOWN instead of dying; the team's downs can lose the level.
Code: `core/object.{h,cc}` (`Companion`), `env/base_env.{h,cc}`. Tests: `tests/test_downs.cc`
(revive: `tests/test_revive.cc`).

- **Going down** (`Agent::TakeDamage` → `OnZeroHealth`: an agent dies, a `Companion`
  goes down): alive (`IsAlive()`), 0 HP, `IsDowned()`, `times_downed + 1`; its statuses
  are cleared, its tags and skills kept; its timers keep running (`EndStep` ticks it: tags
  expire, cooldowns recover). Skill damage, effects (enemy strikes, the host's `kill` /
  `hit`) all go through it. It stays down until a skill revives it (see Revive: by
  default, an ally beside it finds its slot 0 is `revive`, a context skill) or
  `Reset` / `LoadSnapshot`
- **Inert and untouched.** `Agent::IsAffectable()` = alive and not downed: the check
  for everything that hits, heals, tags, statuses, pushes / pulls or targets an agent
  (`TakeDamage`, `Heal`, `ApplyTag` / `LandTag` / `ApplyTagTo` (false, interns nothing),
  `ApplyStatus`, zones, `Affects`, `AreaMotion`, effects and effect pushes, projectiles
  pass over it). The one exception: an `affects_downed` skill reaches the downed only
  (and can only revive them). The downed does not act (`GatherIntentions`: Stay;
  `CanUseSkill` false; `LegalActions`: Stay only; no context skill).
  Enemies ignore it (`FindClosestCompanion`) and drop it as a target (`AggroState`, a
  wind-up locks no downed target). It still blocks its cell (collisions, landing)
- **Objectives: only standing companions control them; physical presence (occupancy)
  counts the downed.** A downed body on a goal cell does not cover it
  (`SynchroLens::CountAgentsOnSynchroCells` / `SynchroEnv::NumAgentsOnSynchroCells`
  count affectable agents only); occupancy checks (collisions, landing, `IsOccupied`)
  stay on `IsAlive()`. Anything else is the host's rules (e.g. a game layer's plates)
- `Agent::IsDead()` stays health-based (a downed companion `IsDead()`): DodgeEnv /
  DodgeLens count it as fallen (DodgeEnv: done, `TaskFailed`, unless the team is down)
- **Team counter**: `GetDowns()` = the sum of `Companion::GetTimesDowned()` (every down
  counts). `GetMaxDowns()` / `SetMaxDowns(n)` (n >= 1, else false; `kDefaultMaxDowns` =
  3, `core/types.h`). `IsTeamDown()` = downs >= max_downs, or no companion is affectable
  (a dead companion does not stand; an env without companions is never team down)
- **Done**: `BaseEnv::IsDone()` is non-virtual, `IsEnvDone() || IsTeamDown()`; envs
  implement the protected `IsEnvDone()` (success, horizon, a failure they honour), so
  every env gets the team verdict. `EndReason::TeamDown` (4): see "Why an episode ended"
- **max_downs is level data**: snapshots own it. `LoadSnapshot` sets it from the
  snapshot (absent = 3), so a host that wants another value sets it AFTER a load. A
  generated `Reset` loads through `LoadGeneratedLevel`, which keeps the env's current
  max_downs. A mid-episode `SetMaxDowns` re-evaluates the verdict (raised above the
  downs, `IsDone()` can turn false again)
- **Reports**: `GetLastDowns()`, one companion id per down, filled after `EndStep`
  (`Companion::TakeUnreportedDowns`): a down between two steps (a host effect) is
  reported once, by the next step. Downs a snapshot loads count as reported. Getting up:
  `GetLastRevives()` (see Revive)
- **Snapshots** (v5): per companion `downed` / `times_downed` (companion types only:
  Companion, Player, NPCCompanion; `IsCompanionType`), top-level `max_downs`.
  `ValidateSkillsTagsZones` rejects max_downs < 1, downs on a non-companion,
  `times_downed` < 0, and a downed companion with HP > 0, `times_downed` < 1 or
  statuses. `LoadSnapshot` restores health with `RestoreHealth` (no damage: a companion
  saved at 0 HP and not downed does not go down on load), then the downs last
- **C API 1.3.0**: `Companions_AgentState.downed`; `Companions_GameState.downs`,
  `max_downs` and `team_down` (the live `IsTeamDown()`, independent of the latched end
  reason: a team down after the horizon stays `Horizon`, `team_down` says it);
  `Companions_End_TeamDown` (4); `Companions_Event_AgentDowned` (17, subject_id,
  position), after the movement events and before AgentRevived / SkillUsed (the event cap
  drops it only in a step reviving a companion twice: see Per-step reports)

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
- Snapshots save and restore the class by name (`"kind"`) through one registry,
  `EnemyKinds()` in `enemies.cc`: a new class is added there, nowhere else. A snapshot
  naming an unknown kind, or a kind on a non-FSM agent, is rejected

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
  saved order) and maps effect sources, effect actor targets, FSM `target_id` and
  agent annotations (`AnnotationTarget::Agent`) through the snapshot's own agent
  `id`s (first wins on a duplicate; an id naming no saved agent loads as
  `kInvalidObjectId`, and an ActorList entry or agent annotation naming none is
  dropped), so gaps from removed objects or hand-authored ids never misattribute an
  effect or a tag
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
  any more, `AggroLens::IsFailed`). `AggroEnv::IsEnvDone` applies the dead-enemy rule
  only while the active lens is the Aggro lens; it never consults another lens's `IsDone`.
  As in every env, the team being down also ends it (`TeamDown`, see Downs)
- Rewards: +1 win, `kTimePenalty` (-0.01) per other step. The failure is terminal:
  `BaseEnv::Step` latches it after the rewards (`IsTaskFailed`, like `success_`; the
  first outcome is final; `SetTaskLens` / `LoadSnapshot` / `Reset` clear both with
  `ResetOutcome`), so only the killing step pays `AggroLens::FailurePenalty(horizon,
  tick)` = `kTimePenalty * (horizon - tick + 1) + kEnemyDeadPenalty` (the rest of the
  episode's time cost, that step included, plus -1); later steps pay 0. A kill at any
  step returns `horizon * kTimePenalty - 1`, one below timing out: killing is never
  a shortcut, whatever the horizon. `AggroEnv::MinUtility` is that return. A kill
  after a latched success is no failure: it pays `kTimePenalty` (the enemy is off the
  target, as after any success)
- The reward design assumes UNCLIPPED returns. `pufferl.py` clamps rewards to [-1, 1]
  (`torch.clamp(r, -1, 1)`): the failure penalty flattens to -1, and an early kill
  then beats timing out once the horizon exceeds ~100 steps. Training Aggro through
  pufferl needs either no clamp or a smaller time penalty (e.g. `kTimePenalty =
  -0.5 / horizon`); neither is done today
- `done` is a verdict for RL episodes, never a stop: the env keeps stepping. A host
  that keeps playing after a kill (a game layer) ignores `done` for that reason:
  `EndReason::TaskFailed`, see below

**Why an episode ended** (`BaseEnv::GetEndReason`, `EndReason` in `base_env.h`):
what ended the episode. `None` while `IsDone()` is false, else `Success` (latched: a
success on the step the team goes down is a success), else `TeamDown` (`IsTeamDown()`,
any env: the level is lost), else `TaskFailed` when the env is done even without the
horizon (`IsDoneWithoutHorizon`, even on the horizon step): a latched lens failure the
env's `IsEnvDone` honours (AggroEnv's dead enemy under the Aggro lens) or the env's own
end rule (a Dodge companion at 0 HP in DodgeEnv, an Aggro enemy killed between steps),
else `Horizon` (tick >= horizon). A latched failure the env's `IsEnvDone` ignores did
not end the episode: a Dodge lens on SynchroEnv, or on AggroEnv (whose `IsEnvDone`
honours a latched failure only under the Aggro lens), with a companion at 0 HP (and the
team not down) ends at the horizon, as `Horizon`. The reason is fixed when done first
becomes true (latched by `Step`, `SetTaskLens*` and `LoadSnapshot`, cleared by
`ResetOutcome`): a kill or a team down after the horizon, or after loading a snapshot at
the horizon, keeps `Horizon` (`IsTeamDown()` / the C API's `team_down` still tell the
team is down). C API (1.2.1, additive, no struct layout change; 1.3 added `TeamDown` 4):
`Companions_EndReason` (same values: None 0, Success 1, Horizon 2, TaskFailed 3,
TeamDown 4) from `companions_get_end_reason(env)`, fixed on the step or
lens change where done becomes true, kept while a host plays on; reset and snapshot
loads take the env's (done / success / reason: a snapshot loaded at the horizon is done
at once, as `Horizon`, and the next step reports EpisodeEnd); the EpisodeEnd event
carries it in `effect_id`. A derived `Reset` / `LoadSnapshot` that changes state after
`BaseEnv::LoadSnapshot` latched calls `RelatchEndReasonAfterLoad` (AggroEnv's `Reset`
spawns its enemy then: under the Aggro lens, no stale `TaskFailed`)

### Known issue: D4 transform
- `SaveSnapshot` writes the TRANSFORMED world (current rows/cols, positions, zones)
  together with `d4_transform` (`snap.d4_transform` in `BaseEnv::SaveSnapshot`), but
  `LoadSnapshot` treats a snapshot as pre-transform and transforms it again (its
  `ApplyD4Transform()` call): Save→Load is not a round trip when `d4 != 0`
- `ApplyD4Transform` swaps `rows_` / `cols_` for rotations / transposes, and each
  `Reset` generates a level with the current `rows_` / `cols_` then transforms it (e.g.
  `SynchroEnv::Reset`): on non-square
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

