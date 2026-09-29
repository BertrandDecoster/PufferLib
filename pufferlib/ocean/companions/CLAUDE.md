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
- Zones' lifetime, successor, damage per landing and the zone table: `companions_zones_test`
  (`tests/test_zones.cc`)
- The turn's health (the ledger, Marked on the total, heals, downs / deaths / defeats /
  revives at the end of the turn, `GetLastTurnHealth`): `companions_turn_test`
  (`tests/test_turn.cc`)
- Reactions, weaknesses, immunities, tag statuses, their reports and the landing order,
  outcome previews (`PreviewSkillOutcome`), a step that throws:
  `companions_reactions_test` (`tests/test_reactions.cc`); their C API side (1.5: report
  queries of both sources, events, level data, outcome previews, parity with the C++
  env): `companions_api_reactions_test` (`tests/test_api_reactions.cc`)
- v7 snapshots (the zone table, the zone cells' fields, reactions, tag statuses,
  weaknesses, immunities): binary in `tests/test_snapshot.cc`, JSON in
  `tests/test_snapshot_json.cc`, a zone's timer across a load in `tests/test_zones.cc`, a
  saved world playing the same reactions in `tests/test_reactions.cc`
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

### The motion phase
Every motion of the turn (`BaseEnv::MotionPhase`; the header documents the algorithm;
tests: `tests/test_turn.cc`): teleports, dashes, walks and forced moves (the skills' pushes
and pulls; an effect's push still goes through the effect system until effects are
planned). It resolves in **layers**, each seeing the FINAL result of the layers before it,
while the actors whose motion is in a later layer still stand where they were:
1. **Teleports** (the casters', as planned: 3, else 2, else 1; only the landing matters)
2. **Dashes** (as planned: an agent on the line as the turn begins stops the plan, and an
   agent that only walks later still stands there; a teleport landed on the line holds
   it). A dash jumps holes, never ends on one
3. **Walks**: every walker, companions and enemies together, by the walk rules as they
   always were (fuzzed identical to the old `ResolveCollisions`): two walks onto one cell
   both stay, a swap cancels, a walk into a cell succeeds only if its occupant leaves
   (chains and rings of 3+ move), a walk into a wall or into one that stays does not
4. **Forced moves, last** (so a walk dodges one): every use's pushes and pulls are found
   again (`FindForcedMoves`) on whoever stands on its cells after layers 1-3: a walker
   stepping into a ring is pushed, one walking out dodges, a dasher landing on it is
   pushed; a pull takes the first ring thing by its priority from there. An actor's
   forced moves add up as vectors (opposed ones cancel); a sum off the axes travels the
   rounded line toward its end (`ForcedMovePath`), and every such sum is recorded:
   `GetLastOddMotions()` (actor, dr, dc; a step report) and `GetOddMotionCount()` (the
   env's life: copied with it, kept by `Reset` / `LoadSnapshot`)

- **Within a layer** the motions are simultaneous (`SolveLayer`; walks: `SolveWalkLayer`):
  a cell whose occupant leaves in the same layer is free; a dash / push / pull stops
  before the first cell of its path held at the END of the layer (a wall, a hole for a
  forced move, a living actor), never contesting it; two motions ending on one cell: the
  lower rank wins (agent index; things, living non-agent actors, after every agent by
  ObjectId), the only use of the indices. Each round judges every motion against the same
  choices: firm blockers first (an actor the layer does not move, a motion of it that
  stays), then the secure motions (none may still end on its path, none better on its
  end, whoever it passes or lands on surely leaves) take their end; a verdict never rests
  on a motion that may still change, so nobody falls back for a blocker that goes away.
  With nothing sure left: settled if the choices agree (rings, crossings, swaps of
  non-walks all move), else one verdict of the worst-ranked motion (a tie-break of a
  cycle). Choices only advance: it ends
- A body down as the turn begins and a thing no push / pull moves never move and block
  every motion (a walk into a thing is blocked)
- Executed action (`GetExecutedAction`, the C API's): the walk a walker made (a walker then
  pushed keeps it; its final cell is past it), Stay if it did not walk. Events unchanged:
  AgentMoved for any change of cell (`move_action` = that walk, Stay for a teleport / dash
  / push), AgentBlocked for an original walk that did not move

### Skills, tags, zones
The env knows no MEANING: skill and tag names ("fireball", "burning") are opaque data
(at most `kMaxNameLength` = 31 bytes), a level may retune or add skills, and what a tag
*does* is the level's data (reactions, weaknesses, immunities, tag statuses) or the
host's business. But the env simulates every mechanic: targeting, motion, tags, roots,
cooldowns, revives, reactions. Code: `core/skill_config.{h,cc}`, `core/tag_table.h`,
`core/context_skill.{h,cc}`, `env/skill_motion.{h,cc}`, `env/base_env.cc` (`PlanSkillUse`,
`ResolveSkillTargets`, `MotionPhase`, `ResolveSkills`, `UseSkill`,
`PreviewSkill`, `EffectiveSkill`, `Affects`).

**Step order** (`BaseEnv::Step`):
1. Clear the per-step reports, `BeginStep` on every agent
2. `PreStep` (enemy FSM) → `GatherIntentions`, the intents phase: every skill use is
   PLANNED here from the world as the turn begins (`AddSkillPlan`: its effective skill,
   the context rules read once, its cells and targets, its cooldown spent; see "Multiple
   casters") → the motion phase (`MotionPhase`, in layers: teleports, dashes, walks,
   then pushes / pulls on whoever stands on their cells; see "The motion phase")
3. `ApplyZoneTags`, the zone phase (every affectable agent on a zone cell: alive, not
   downed; one landing each, on its FINAL cell), in sub-phases, each over ALL the landings: a. immunity,
   the tag and its status; b. weaknesses; c. reactions, gathered then applied (every
   trigger found, every firing's affected agents computed, then the outcomes applied
   per agent); d. the zone's damage on those still affectable (see Zones and
   Reactions). From here to the end of the step the zone map is READ-ONLY: a
   reaction's `zone_becomes` waits in a pending buffer
4. `ResolveInteractions` → `ResolveSkills`: every use's hits (`UseSkill`), in caster
   order (see "Resolution of one skill")
5. Effects tick, `CommitPendingZones` (the step's reaction zones, in the order they were
   recorded: a later write to the same cell wins), `ApplyTurnOutcomes` (the turn's
   health, see below), `EndStep` on every agent (tags,
   statuses, cooldowns tick), `TickZones` (zone lifetimes tick, expired zones become
   their successor), the downs since the last report (`GetLastDowns`, after `EndStep`),
   `tick_++`, `PostStep`, rewards (TaskLens)

**The turn's health** (`BaseEnv::GetLastTurnHealth`, `turn_`; tests: `tests/test_turn.cc`):
nothing changes HP, alive or down DURING a step. Every hit and heal (zones, reactions,
skills, effects, the enemies' strikes: `HurtInStep` / `HealInStep`, the effect system
through its `HealthSink`), every weakness defeat and every revive goes into a per-agent
ledger (seeded after `BeginStep`: Marked as the turn began), applied once at the end of
the turn (`ApplyTurnOutcomes`, after `CommitPendingZones`): the damage total, x1.5
rounded down once if Marked as the turn BEGAN (a Marked landed this turn acts next
turn), minus the heals, clamped (`Agent::ApplyTurnHealth`: at 0 a companion goes down,
another agent dies); a defeat: 0 whatever the heals; a revive (an ally down as the turn
began): up with the planned HP (two revivers: once, the highest HP, credited to the
lowest agent index giving it; the other use keeps the ally without `Revive`); then the
dead's strikes still winding up are cancelled (`EffectSystem::CancelDeadSources`). So
during the turn everyone stays as it began: an agent the turn takes to 0 (or defeats)
still acts, is still tagged / hit / pushed / rooted and still triggers reactions (one
defeat per agent per turn); a heal can save it; an enemy killed this turn still lands
this turn's strike; a revived ally cannot be downed the turn it gets up. The reports keep
each hit's raw share (a landing's `damage`, a reaction outcome's `damage`, the `Damage`
effect). `TurnHealth {agent, damage, marked_bonus, heal, change, health, outcome}`
(`TurnOutcome`: None, Downed, Died, Defeated, Revived), one per agent with ledger
activity, in agent-index order; copied with the env, cleared by the next step and
`LoadSnapshot` (not in the C API yet). Between steps the host's primitives stay
immediate (`TakeDamage` with Marked per hit, `Heal`, `Defeat`, `Revive`, a `kill` / `hit`
it spawns). A step that throws applies its ledger and planned revives (`AbortStep`,
agent-local, no report). `PreviewSkillOutcome` applies them on its clone.

**A step reads one zone map** (`BaseEnv::pending_zones_`): the map as the step began
(after `PreStep`) is the map every read of the step sees: zone landings (the zone phase
and a skill motion's landing), weaknesses (P), spread regions. A reaction's
`zone_becomes` during a step is recorded, and the map changes at its end, before the
timers (stored n + 1, so it covers its n next steps): a zone a reaction creates first
lands NEXT step. So the order of the agents does not change an outcome of the zone
phase, only the order of the reports, with one exception: two firings writing different
`zone_becomes` to the same cells, the later one (firing order: trigger agent index)
wins. Report-only effects of the order: a result's `DefeatReport::reaction` names the
first firing whose result defeated, only the first of identical result landings on an
agent is `fresh`. And it cannot cascade within a step: a spread fires
once per trigger (N triggers in one region = N firings, each reaching everyone), and
waits a step before its zone lands. Between two steps (a host's `ApplyTagTo`) there is
no phase: a landing resolves at once and its `zone_becomes` applies at once. A host
call from a hook DURING a step: `SetCellTag` writes at once (in `PreStep`, before the
zone phase, it is part of the map the step reads), while an `ApplyTagTo` sees
`in_step_` set, so its reaction's `zone_becomes` waits for the end of the step like any
other. A step that throws commits what it recorded (`AbortStep`; a map cleared during
the step drops them instead: the abort runs while the throw unwinds and must not
allocate)

**Step timers** (`Agent::BeginStep` / `EndStep`; zones: `BaseEnv::TickZones`): tag and
status durations, cooldowns and zone lifetimes all count steps and all tick at the END of
`Step`. A timer of n is in effect for the n next steps: set between two steps (host
primitives, snapshots), it reads n; set during a step, it also covers the rest of that
step (kept as n + 1 inside the step) and reads n after it. What a host reads between
steps is always the number of steps to come it covers.

**Line and landing rules** (`env/skill_motion.h`):
- Line rule: a line travels along the facing; a wall (not pathable) or the grid edge
  stops it on the cell before; holes (`CellKind::Hazard`: pathable, not walkable) and
  actors are crossed
- Landing rule: something moved never ends on a hole or on another living actor
- Ground target: the cell `range` away by the line rule (may be a hole)
- Projectile: the first agent the skill affects within `range` (a downed one is passed
  over; by an `affects_downed` skill, a standing one), else the last cell reached
- Dash: up to `distance`; a wall or the first living actor stops it, holes are jumped;
  lands on the furthest walkable cell before that (else stays)
- Teleport: exactly `distance`, else `distance - 1`, ... 1 (ignores what lies between,
  walls included), else stays
- Push: each ring thing `distance` away from the centre; a wall, a hole or a living
  actor stops it on the cell before (the motion phase: a cell held at the end of its
  layer; pushes on one thing add up; on whoever stands on the ring after the teleports,
  dashes and walks). Pull: exactly one cell, only into a free, walkable
  centre

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
| `damage` | 0 | Health every affected agent (area and dash path) loses, into the turn's ledger (see The turn's health: Marked ×1.5 on the turn's total; 0 HP at the end of the turn = dead, a companion down, as with effects) |
| `root_steps` | 0 | Affected agents on the area are rooted for this many next steps |
| `cooldown` | 0 | See above |
| `friendly_fire` | true | Off: only agents NOT of the caster's faction are affected (allies and caster get no tag, damage, push/pull or root; a projectile flies past them) |
| `self_tags` / `self_motion` / `self_root` / `self_damage` | true | With friendly fire, the caster is affected when it stands in its own area (after its own motion); each flag can spare it that effect |
| `affects_downed` | false | Off: affects the standing only (alive, not downed). On: the downed only (alive and down), which it can only revive: no tags, damage, `root_steps` or motion, and `friendly_fire` on (validated) |
| `revive_percent` | 0 | A downed companion it affects gets up (`Companion::Revive`) with this percent of its max HP, rounded up (at least 1); in [0, 100], > 0 needs `affects_downed` |

- "Affected" (`BaseEnv::Affects`) = affectable (alive, not downed; for an
  `affects_downed` skill: alive and downed), passes `filter`, and `friendly_fire` or not
  of the caster's faction. Push / pull also move living non-agent actors (no faction check)
- Resolution of one skill: its PLAN (`PlanSkillUse` → `ResolveSkillTargets`, pure,
  shared with `PreviewSkill`, from the world as the turn begins, the caster read on its
  planned landing cell: the landing and its fallbacks (the closer walkable cells of the
  line), the centre, its CELLS (the area; a `tag_path` dash's crossed path), the
  predicted affected agents (area in cell order, then the path) and what it will do to
  each, the forced moves (each ring thing a push moves away from the centre by
  `motion_distance`, the one thing a pull takes into the centre, as offsets), the
  revives (allies down as the turn began)) → the motion phase: the caster's dash /
  teleport and the forced moves, with every other motion of the turn (see "The motion
  phase") → the zone phase → `UseSkill`: the hits land on the planned cells, on whoever
  stands there after the motion phase, plus whom its own push / pull moved (walking out
  dodges, walking in gets
  hit; a projectile's line is traced once, as the turn begins: its stop cell is fixed, an
  agent walking into the line before it is not hit) → tags → damage → revive → root
  (area only). The damage goes into the turn's ledger: an agent it takes to 0 is still
  affected by anything else this turn (down or dead at its end)
- Damage is not reported as events yet (`Companions_Event_AgentDamaged` is declared,
  not implemented): read it from the agents' health
- `ValidateSkillConfig`: non-empty name ≤ 31 bytes; range, distance, damage, root_steps,
  cooldown ≥ 0 (root_steps, cooldown ≤ 1,000,000); tag names non-empty ≤ 31 bytes with
  duration -1 or in 1..1,000,000 (`kMaxTimerSteps`), at most
  `kMaxSkillTags` (32, the C API's `Companions_MAX_SKILL_TAGS`) tags; enums in range;
  `revive_percent` and `affects_downed` as above

**Multiple casters** are simultaneous: every use is planned from the world as the turn
BEGINS (`GatherIntentions`), so a caster aims from the cell it began the turn on even if
another use pulls it this turn, and its cooldown is spent when the use is planned (the
use happened, whatever it then reaches; a step that throws before the use applies keeps
it spent). A caster down as the turn begins cannot cast; one going down this turn still
casts. A caster rooted this turn still resolves its skill (the root blocks from the next
step). The indices order the reports (uses in caster index order); the motion phase
depends on them only through its ties (two motions of one layer onto one cell: the
lower index), and
they still change outcomes in one TEMPORARY way (until the one tag phase): a skill's
landing fires its reaction at once (`LandTag`), in caster order: an agent carrying wet
hit by electrified (index 0) and chilled (index 1) reacts by the rule the first landing
completes.

A dash / teleport losing its landing falls back along its line and keeps its planned
area, centre and whole path (a `tag_path` dash still hits every planned path cell).

**Revive** (a skill with `revive_percent` > 0; the builtin `revive`):
- `Companion::Revive(health)`: the downed companion gets up where it lies, with
  `ceil(max_health * revive_percent / 100)` HP (clamped to [1, max]); its statuses are
  already clear (cleared as it went down), its tags kept, `times_downed` unchanged (the
  team's counter never goes back). It acts from the next step, and its cell's zone lands
  on it from the next step (zones apply before skills)
- A revive is a skill use: a `SkillUse` (the revived agent with the `Revive` effect) plus
  a `GetLastRevives()` entry (reviver, revived, health). It applies at the end of the turn
  (see The turn's health): the ally is down all turn, so nothing downs it again that
  turn. Several revivers of one ally in a step: one revive, the highest HP, credited to
  the lowest agent index giving it; the other uses keep it in `affected` without
  `Revive`

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
  `caster_landing`, `affected`. It is the use's plan (`PlanSkillUse`), as the step would
  make it now, so preview and plan cannot drift; computed whatever `usable` says. What
  the step DOES may still differ: the hits land on the planned cells on whoever stands
  there after the motion phase (everyone, enemies too), and a dash / teleport
  whose landing another motion took falls back; a use whose movement is Stay keeps the
  caster's facing. It says whom the skill affects, not where a push / pull then moves them
- `SkillUse::affected`: what the use DID, those it predicted still on its cells (or moved
  by its own push / pull) in the preview's order, then those who walked or were moved
  onto its cells, in cell order (nobody moved: the preview's agents and effects)
- `PreviewSkillOutcome(caster id, slot, aim)`: what the use would DO, reports included:
  resolved on a `Clone()` (nothing in the env changes or is interned) by the step's own
  code for a use (`AddSkillPlan`, the motion phase with only its motions, then
  `ResolveSkills`), as the next step would resolve it were it the step's only change:
  nobody else moves, zones land only on those its motions moved (their final cell;
  TEMPORARY until the preview runs a whole turn: a caster standing in a fire gets no
  landing in its preview), no
  enemy acting, no end-of-step timers. The clone is mid-step
  around the use (`in_step_`, `Agent::BeginStep`), so what the use sets (a
  `zone_becomes` zone, a status) is stored as the step stores it (the zones its
  reactions set, their reports' `cells`, are committed after the use as the end of the
  step would, timers not ticked). `SkillOutcome {usable,
  world}`: the clone after the use, whose `GetLast*` are the use's reports (skill use,
  landings, reactions, defeats, revives, and the downs it caused, not the unreported ones
  from between steps); tag ids are the clone's (read names in its table). Unusable (as
  `PreviewSkill`): nothing resolved, empty reports; `world` null for an id naming no
  companion. Costs a copy of the env (a UI query). The step may differ as `PreviewSkill`
  says, and also lands the zones on those standing on them first (a tag they did not
  carry yet can react or defeat before the use)
- `AffectedAgent { id, effects }`, `SkillEffect` bit flags: `Tags` 1, `Damage` 2,
  `Root` 4, `Motion` 8 (it really changes cell: a push against a wall is no Motion),
  `Revive` 16; 0 = affected, nothing applies (the caster gets only what its `self_*`
  flags allow). In a preview they are a prediction; in a `SkillUse` they are what the
  use DID (damage downs and kills only at the end of the turn, so an agent the use takes
  to 0 keeps its Root / Motion; an ally another caster also revives keeps `Revive` on
  the credited use only). Tags and Damage agree. Tags means at least one of the skill's
  tags lands: an agent immune to all of them gets no Tags (preview and use); a use whose
  tag defeated an agent (a weakness) keeps Tags and Damage (its raw share)

**Tags** (`TagTable`, `Agent::ApplyTag`):
- Opaque names interned per env; ids stay stable (the table only grows, never cleared
  by `LoadSnapshot` / `Reset`). Persist names, not ids
- Duration in steps, or -1 (`kPermanentTag`); re-applying keeps the longer (permanent wins)
- Durations are step timers (see Step timers): landed during step t with duration d,
  the tag is present after steps t .. t+d-1 (and during step t+d, until its end)
- No gameplay effect by themselves; the level's rules give them one (see Reactions). Host
  primitives: `ApplyTagTo` / `RemoveTagFrom` (`ApplyTagTo` refuses a downed or dead agent
  (interning nothing) and an agent immune to the tag). `ApplyTagTo`
  is a landing like any other (`TagSource::Host`, cause `"host"`, source -1): tag
  statuses, weaknesses and reactions apply, and it is reported (`GetLastTagsApplied`,
  `GetLastReactions`, `GetLastDefeats`) until the next `Step` clears the reports (the C
  API's events are built by the step: a host landing is not among them)

**Zones** (`SetCellTag` / `GetCellTag` / `ClearCellTags`, `DefineZone` / `GetZoneDefs`;
tests: `tests/test_zones.cc`):
- One zone per cell (`CellTag`, a resolved copy of a `ZoneDef`, `core/types.h`): `tag`,
  `duration` (what it lands with), `steps` (its own lifetime), `then` (its successor),
  `damage` (per landing)
- **The zone table** (level data, keyed by tag): `DefineZone(tag, ZoneDef)` (re)defines
  what a zone of that tag is; `GetZoneDefs()` / `GetZoneDef(tag)` (an undefined tag gets
  the defaults: permanent, landing a permanent tag, harmless, without successor).
  Anything that creates a zone BY NAME takes its fields from it: `SetCellTag(cell, tag)`,
  `SetCellTag(cell, tag, duration)` (that landing duration, the rest from the table: the
  C API setter), a successor, a reaction's `zone_becomes`, a snapshot zone cell's missing
  fields.
  `SetCellTag(cell, tag, ZoneDef)` is the explicit per-cell override. A cell keeps its
  resolved copy (no lookup on the hot path): redefining a tag changes later zones only.
  Like max_downs: copied with the env, kept across a generated `Reset`, saved in
  snapshots (v7 `"zones"`) and replaced by `LoadSnapshot` with the snapshot's (none in
  older ones), set before the snapshot's zone cells load
- Refused (cell / table unchanged, nothing interned): out of bounds, an empty tag
  (`DefineZone`; `SetCellTag` "" clears the cell), a tag or `then` over 31 bytes, a
  duration or steps of 0, below -1 or above `kMaxTimerSteps` (1,000,000: every step
  timer level data or the host sets is capped, `IsValidTimer`, so the n + 1 of a timer
  set during a step never overflows), a negative damage
- Landed (cause `"zone"`, source -1) once per turn on every affectable agent standing
  there after the motion phase: its FINAL cell, moved or not (before the skills' hits;
  the cell it began on and the cells a dash or a push crosses land nothing). Effect
  pushes (still outside the motion phase) do not apply zones
- **One landing, in order** (see Reactions): immunity (nothing lands, no damage), the
  tag and its tag status (`PutTag`), weakness (a defeated agent stays in play until the
  end of the turn), reaction, then the zone's own `damage`, into the turn's ledger
  (Marked on the turn's total; a companion taken to 0 goes down at the end of the turn
  and keeps the tag). Reported as the landing's `damage` (the zone's raw share, before
  Marked; 0 for a harmless zone).
  The downed and the dead get no landing at all
- **The zone phase** (`ApplyZoneTags` → `CollectZoneLanding` per agent, then
  `ResolveZoneLandings`) runs those steps as sub-phases, each over every landing (agent
  order only orders the reports): a. every agent's landing (immunity, tag, status);
  b. the weaknesses; c. the reactions, gathered then applied: c1 every landing's
  trigger (the first rule pairing the landed tag with one the agent carries, on the
  state after a and b); every trigger fires, a weakness-defeated one included (it
  stays in play until the end of the turn); c2 every firing's affected
  agents (the phase-start region's affectable agents, or the trigger alone) and its
  report, nothing applied (`StartReaction`); c3 the outcomes, agent-local
  (`ApplyReactionHits`): per agent, every firing's removals (originals not kept), then
  every result landing (immunity, tag, tag status; firing order: trigger agent index),
  then ONE weakness check over the results that landed (its first `(P, S)` with P under
  it and S among them: defeated, every firing whose result is S reports `defeated`),
  then each firing's damage into the turn's ledger, each firing reporting its raw
  share; then the zones, in firing order. No firing's outcome cancels
  another's (a firing that would down B does not stop B's own reaction), so the
  agents' final state does not depend on the order (the zones and the reports: see
  "A step reads one zone map"); d. each zone's damage. The outcome preview runs the
  same phases over the landings of those its motions moved (`ApplyMovedZoneTags`)
- **A step reads one map** (see Step order): a zone a reaction creates during a step
  (`zone_becomes`) is written at the end of the step and first lands next step, tag and
  damage; everything in the step (the zone phase, weaknesses, spread regions) reads the
  zone as the step began
- A caster its landing zone takes to 0 still gets its own use (tags, damage: reported)
  and the others it affects are tagged, hurt, rooted, pushed; it goes down at the end of
  the turn
- Lifetime: `steps` is a step timer (see Step timers): set between two steps, the zone
  lands during the n next steps; set by a host call during a step (e.g. a `PreStep`
  hook), it also covers the rest of that step (kept as n + 1, `BaseEnv::in_step_`) and
  reads n after it (a
  reaction's `zone_becomes` is kept as n + 1 too but written at the end of the step, so
  it lands during exactly its n next steps). `-1` =
  never expires. Zones tick in `TickZones`, right after the agents' `EndStep`; an
  expired zone becomes its successor `then`, a zone by name (the table's fields; created
  after the step, it lands from the next step and lasts its n next steps), or the cell
  loses its zone. Cycles are legal (`a -> a`, `a -> b -> a`): one zone per tick
- World state: copied with the env (timers included), saved in snapshots, replaced by
  `LoadSnapshot` (generated levels have none, so every `Reset` clears them).
  `SaveSnapshot` writes each zone with every field as its cell holds it (v7: `steps`,
  the steps to come, `then`, `damage`, and the landing `duration`), so a zone mid-life
  expires on the same step after a load, and a per-cell override or a zone created
  before its tag was redefined loads as it was. A snapshot cell that lacks a field (a
  hand-written level's `{row, col, tag}`, a file before v7) takes the snapshot's zone
  table's (`Snapshot::CellZone`; an undefined tag: the defaults)

**Reactions, weaknesses, immunities, tag statuses** (`core/reaction.{h,cc}`,
`BaseEnv::LandTag` / `ResolveWeakness` / `ResolveReaction`; tests: `tests/test_reactions.cc`).
The combo rules as data: two tags react into a third, an agent is defeated by a tag
landing where the map provides another, a tag never lands on some agents, a tag carries
a status.
- **Every tag landing** (a skill's, a zone's, a reaction's result, the host's
  `ApplyTagTo`) resolves in this order, on an affectable agent (the downed and the dead
  get nothing):
  1. immunity: an agent `immune` to the tag gets nothing (no report, no zone damage);
  2. the tag lands (reported, with its `TagSource`) and the status `tag_statuses` binds
     to it applies (`Agent::ApplyStatus`, a step timer: n + 1 during a step);
  3. weakness: for one of the agent's `(P, S)` with S = the tag, the zone of the cell it
     stands on NOW provides P: defeated (the env's `kill`: 0 HP at the end of the turn
     whatever its health, heals or Marked, an agent dies, a companion goes down;
     between two steps `Agent::Defeat` at once), reported once per turn; during a step it
     stays in play until the end of the turn (reaction outcomes, zone damage, pushes).
     P is asked of the map, never of
     the tags it carries: a wet imp on dry land is not defeated by a spark; an oiled gob
     walking into fire is not defeated by (oil, burning) (fire is not oil)
  4. reaction (never for a reaction's result: no chains): the FIRST rule, in level
     order, with the tag as `a` and `b` carried, or the reverse (unordered). One reaction
     per landing. Who is affected: when the rule `spread`s and the agent stands on a cell
     whose zone provides `a` or `b`, every affectable agent on that zone's connected
     region (4-neighbour flood fill over the cells carrying that zone tag, from the
     agent's cell), in agent-index order, the trigger included; otherwise the agent
     alone. A trigger step 3 defeated still starts it (like the HTN rules, which
     spread AND defeat the vulnerable) and, during a step, is still affected by it (a
     gob weak to (oil, burning) burnt on the oil: it is defeated, it and the others on
     the oil burn, the oil catches fire). Between two steps (a host landing) the defeat
     is at once: spreading, it fires without the trigger; alone, nothing fires, nothing
     is reported. Each affected agent, in turn: loses `a` and `b` but those in `keep`,
     gets `result` (a permanent tag, through steps 1-3: an immune agent does not get it,
     a weakness defeats it), then takes `damage` (the turn's ledger).
     Every outcome reads the map as the step began: the spread region becomes
     `zone_becomes` (a zone by name: the table's fields, a step timer) at the END of the
     step (recorded after every outcome; `ReactionReport::cells` lists the cells); a
     host landing between steps changes it at once
  5. (a zone's landing) the zone's damage, see Zones
- Whatever a skill's or the host's landing sets off happens at once, inside it (a
  reaction during a skill's tags, before its damage; inside `ApplyTagTo`), but for the
  zone change (end of step). The zone phase resolves its landings together, in
  sub-phases, its reactions gathered then applied (see Zones): there, a result's
  weakness is checked once all of an agent's results landed, then each firing's
  damage applies
- Consequences of these rules worth knowing when writing a level: a zone re-lands its
  tag every step, so an agent carrying a reaction's result that is also one of its
  originals (`wet + electrified -> electrified`) reacts again with every landing of the
  other one (standing in the lake: every step). Spreading with `zone_becomes` = the
  other original (wet), everyone in the lake keeps the result, so every step each
  agent's wet landing is a trigger (all fire, gathered then applied): N agents in the lake,
  N firings per step, each over the whole region (N x `damage` to each), each one
  re-setting the lake at the end of the step (it never runs out). A result in {a, b} is a trap: the level
  should give the result another name (`wet + electrified -> shocked`); the env keeps
  the rule as it is. A spread from an agent standing on a zone providing the TRIGGER
  tag (an oiled agent walking into the `burning` zone) spreads over that zone's region,
  and `zone_becomes` then re-sets it at the end of the step (its lifetime starts
  again). Two casters igniting the same oil in one step both fire (the oil is still
  oil for the second). A tag status re-lands
  with every landing of its tag, so a zone whose tag carries Rooted or Stunned holds an
  agent indefinitely (a rooted agent can't walk out)
- **Level data** (`SetReactions(rules, error)` / `GetReactions()`,
  `SetTagStatuses(rules, error)` / `GetTagStatuses()`): like the zone table, copied with
  the env, kept across a generated `Reset` (`LoadGeneratedLevel`), saved in snapshots
  (v7) and replaced by `LoadSnapshot` with the snapshot's (none in older ones). Tags are
  interned when set (the table only grows, so the resolved ids stay valid)
- **Per agent** (`SetWeaknesses(agent, weak_to, error)` / `GetWeaknesses`,
  `SetImmunities(agent, immune, error)` / `GetImmunities`; `Agent::GetWeakTo` /
  `GetImmune` / `IsImmuneTo`, tag ids): on the agent, copied and saved (v7) with it,
  gone when a generated `Reset` re-creates the agents (they have none). An immunity
  blocks landings only: a tag the agent already carries stays until it expires or a
  reaction removes it
- **Validation** (each setter: false, nothing changed or interned, the reason in `error`):
  every name non-empty, at most 31 bytes; a reaction: `a` != `b`, a `result`, `keep` a
  subset of {a, b} without repeats, `damage` >= 0, `zone_becomes` only with `spread`, one
  rule per unordered pair; a tag status: stunned / marked / rooted (not none), `steps`
  in 1..1,000,000, one per tag; weaknesses: no pair twice; immunities: no tag twice; an unknown agent id
- **Reports** (per step, see Per-step reports): `GetLastReactions()` (`ReactionReport`:
  rule index, the trigger agent, the triggering tag, its landing's source / cause /
  kind, `spread`, the affected agents in order with `result_landed` / `defeated` /
  `damage`; `rule` indexes the current reactions, so a `SetReactions` between the step
  and the read makes it stale), `GetLastDefeats()` (`DefeatReport`: agent, P, S, the
  landing of S: source / cause / kind / `reaction`, the index of the reaction whose
  result S is, else -1), and each `TagApplication`'s `kind` (`TagSource`: Skill 0, Zone
  1, Reaction 2, Host 3; no default, every landing path sets it) and `reaction` (a
  result: its reaction's index in `GetLastReactions()`). A result keeps the source and
  cause of the landing that triggered its reaction; its `fresh` reads the agent before
  the reaction removed the originals (a result that is one of them is not fresh on an
  agent that carried it). A `ReactionReport`'s `cells`: the cells it (re)set to
  `zone_becomes`, row-major (empty without a spread or a `zone_becomes`; a
  `zone_becomes` equal to the region's zone still lists them, its lifetime restarting),
  so a host sees "the oil caught fire" without diffing; during a step they change at
  its end (two reactions writing one cell: the later one wins), between steps at once.
  C API: 1.5 (see Per-step reports)

**Statuses** (`StatusType`, `core/object.h`): `Stunned`(1) forces Stay, `Marked`(3)
(damage ×1.5: on a step's damage total, rounded down once, if Marked as the step began;
per hit between steps, in `Agent::TakeDamage`, truncated toward zero: 1 damage stays 1),
`Rooted`(4). `Slowed` was removed: value 2 is reserved (never reused, a snapshot carrying
it is rejected); the C API `Companions_Status_*` keeps the same numbers.
- Rooted: can't move by itself (walking becomes Stay, dash / teleport skills are
  unusable, `CanMoveItself`), can still use non-moving skills (a rooted FSM enemy still
  attacks), can be pushed / pulled
- Statuses are step timers (see Step timers): `root_steps = n` roots for the n next steps
- Going down clears a companion's statuses, and a downed one accepts none (see Downs)

**Per-step reports** (cleared at the start of every `Step` and by `LoadSnapshot`, hence every `Reset`;
also `GetLastOddMotions()`, the forced-move sums off the axes, C++ only, see "The motion
phase"):

| BaseEnv | Content | C API event |
|---------|---------|-------------|
| `GetLastSkillUses()` | caster, skill (the effective one), target (centre; landing cell for a self skill), slot, `affected` (the agents it affected, in processing order, with what it did to each: see Previews) | `Companions_Event_SkillUsed` (effect_id = slot, effect_name = skill); the whole use, `affected` included: `companions_get_last_skill_use_count` / `companions_get_last_skill_use` (not an event: never cut by the event cap) |
| `GetLastTagsApplied()` | agent, tag id, duration, source (caster / -1), cause (skill / `"zone"` / `"host"`; a result: its trigger's), `fresh`, `damage` (the zone damage's raw share, into the turn's ledger; 0 for a skill's landing or a harmless zone), `kind` (`TagSource`), `reaction` (a result: its index in `GetLastReactions()`, else -1) | `Companions_Event_TagApplied` (effect_id = tag id, status_duration, health_source_id = source, tag_fresh; 1.5: tag_kind, tag_reaction, health_amount = damage, report_index); the whole report: `companions_get_tag_landing*` |
| `GetLastReactions()` | `ReactionReport`: rule, trigger, tag, source / cause / kind of the triggering landing, `spread`, `affected` (agent, `result_landed`, `defeated`, `damage`), `cells` (what became `zone_becomes`), in the order they fired | `Companions_Event_ReactionFired` (1.5: subject = trigger, effect_id = rule, effect_name = result, health_source_id / tag_kind, report_index); `companions_get_reaction*` |
| `GetLastDefeats()` | `DefeatReport`: agent, zone (P), tag (S), source / cause / kind / `reaction` of the landing of S | `Companions_Event_AgentDefeated` (1.5: effect_id / effect_name = S, health_source_id / tag_kind / tag_reaction, report_index); `companions_get_defeat*` |
| `GetLastDowns()` | one companion id per down (a down between steps: the next step's) | `Companions_Event_AgentDowned` (subject_id, position = its cell) |
| `GetLastRevives()` | reviver, revived, health (the HP it got up with), at the end of the turn, in the revived's agent-index order | `Companions_Event_AgentRevived` (subject_id = revived, health_source_id = reviver, health_new = health_amount = health, position = its cell after the step) |

- `fresh` = the agent did not carry the tag just before this landing (an agent standing on
  a duration-1 zone still carries its tag when the zone lands it again: not fresh)
- Report order in a step: the zone phase first (every zone landing in agent-index
  order; its landing defeats; its reactions in trigger agent-index order; their result
  landings in the same firing order, each firing's in its affected order; then the
  result defeats, in first-hit order: firing, then affected order), then the skill phase:
  each use's hits and skill use, in caster order. The C API reads the same vectors (its
  `report_index` fields and event order follow them)
- Event order in a step: movement events (AgentMoved / AgentBlocked, per agent), then
  AgentDowned, AgentRevived, AgentDefeated, SkillUsed, TagApplied, ReactionFired, EpisodeEnd (grouped by kind, not in time order: a down from between the
  steps comes before the step's AgentRevived; a companion revived in a step cannot be
  downed in it); at most
  `Companions_MAX_EVENTS` (64), EpisodeEnd always kept, `events_dropped` counts the rest.
  The state changes (movement, down, revive) come first, so SkillUsed / TagApplied are cut
  first. They always fit when no companion is revived twice in the step (per agent: one
  movement, one revive, two downs = 4 events; fits for up to 15 agents). Reviving the same
  companion twice in a step needs a skill downing it between the two revives; only then
  can state-change events be dropped
- C API (`src/api/companions_api.h`, version 1.6.0 (1.5 and 1.6 below): 1.2 removed the legacy cast,
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
- C API 1.5.0 (`Companions_Event` layout changed: consumers rebuild): the rules as data.
  Report queries with a `Companions_ReportSource` (`LastStep`: the env's `GetLast*`, the
  host's landings since the step included; `Preview`: the last outcome preview's world,
  dropped by a step, reset or load), never capped: `companions_get_skill_use*`,
  `_tag_landing*` (`Companions_TagLanding`: names, cause, `Companions_TagSource` kind,
  reaction, fresh, damage), `_reaction*` (`Companions_ReactionInfo`: the rule's a / b /
  result, trigger, triggering landing, spread, affected with outcomes, `zone_becomes` and
  `cell_count`; `companions_get_reaction_cell`), `_defeat*`, `_down*`, `_revive*`;
  `companions_preview_skill_outcome` (`PreviewSkillOutcome`, the clone kept in the
  wrapper: one preview at a time, read each aim's reports before the next); level data, read-only: `companions_get_zone_def*` / `_find_zone_def` (the
  table, sorted by tag), `companions_get_cell_zone` (a cell's resolved zone, remaining
  steps included), `_reaction_rule*`, `_tag_status*`, `_agent_weakness*`,
  `_agent_immunity*`; events `ReactionFired` (19, after TagApplied) / `AgentDefeated`
  (20, a state-change event after AgentRevived: the cap keeps it before the skills);
  `Companions_Event.tag_kind` / `tag_reaction` / `report_index` (-1 when none).
  `companions_step` / `companions_reset` return bool (false with the error; the step
  clears the error first and refuses an action count other than the agent count).
  No exception crosses the boundary: a step that throws returns false with the env's
  current state in `out_result` and no event (`BaseEnv::Step` aborts the step,
  `AbortStep`, so `SaveSnapshot`, which throws inside a step, works after it; a throw
  after `TickZones` keeps the incremented tick). A copy of an env (Clone, copy,
  assignment) re-points its FSM agents' `FSMContext::rng` at its own RNG
  (`RepointFsmRng`)
- C API 1.6.0 (behaviour only, struct layouts unchanged): only a team down or the
  horizon fails a task (see "Why an episode ended"). Aggro no longer fails when no
  enemy lives, Dodge no longer fails on a down; `Companions_End_TaskFailed` (3) is no
  longer produced (kept: the values are append-only). A down interrupts the task:
  `Companions_End_Interrupted` (5, provisional), a one-time down cost per new down, the
  task paused until nobody is down (see "Why an episode ended")

**Levels** bring their skills, zones, slots, downs, context skills and combo rules
through snapshot JSON v7 (`core/snapshot_json.cc`; versions 2..7 load, binary snapshots
follow the same number, binary 1..7):
- Top level `"skills"`: SkillConfig objects (keys above; all but `name` optional; v6
  adds `affects_downed` / `revive_percent`, absent = false / 0)
- Top level `"zones"` (v7): the zone table, an object keyed by tag,
  `{"burning": {"duration": 3, "steps": 4, "then": "smoke", "damage": 1}}`; every
  field optional (absent: the ZoneDef default: -1, -1, `""`, 0). Absent = no table
- Top level `"cell_tags"`: `{row, col, tag}` plus the fields the cell has: `duration`
  (v4), `steps` (the steps to come), `then` (`""` = no successor), `damage` (v7). A
  field absent is the table's for that tag (the defaults when it does not define it),
  a field present is the cell's own (a per-cell override); the table loads first,
  wherever the file lists it. A level tool writes `{row, col, tag}` and the table;
  `SaveSnapshot` writes every field (resolved)
- Top level `"reactions"` (v7): `[{a, b, result, keep, damage, spread, zone_becomes}]`
  (`a`, `b`, `result` required; absent: `[]`, 0, false, `""`), in level order
- Top level `"tag_statuses"` (v7): `[{tag, status, steps}]` (`status` by name like the
  agents' statuses, `"stunned" | "marked" | "rooted"`, case-insensitive; `steps` absent
  = 1)
- v7 validation (`ValidateSkillsTagsZones`, reusing `core/reaction.h`): the table's
  tags and every name non-empty, at most 31 bytes; durations and steps in 1..1,000,000
  or -1,
  damages >= 0 (`ValidateZoneTable` / `ValidateZoneDef`, a cell's present fields
  too); the reactions and tag statuses as their setters check them (`keep` a subset
  of {a, b}, an unknown status rejected, ...); each agent's `weak_to` / `immune` (no
  repeats). A `then` or `zone_becomes` only needs a valid name: defined or not
  (an undefined tag gets the defaults), cycles (`a -> b -> a`) are legal. Messages
  name `zones['tag']`, `zone at (r, c)`, `reactions[i]`, `tag_statuses[i]`, the agent
  and its `weak_to[i]` / `immune[i]`. Older files, and JSON without the keys, load with
  none of it
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
  `"downed"`, `"times_downed"` (absent = false, 0; see Downs for their rules); any
  agent (v7, written when it has some): `"weak_to": [{"zone": "wet", "tag":
  "electrified"}]`, `"immune": ["burning"]` (absent = none)
- Unknown keys are rejected at the root, in an agent, and in a skill / tag / zone /
  zone table entry / reaction / tag status / weakness (a misspelt `"reaction"` or
  `"weakTo"` is an error, not a level silently without its rules; grid cells, FSMs,
  effects and annotations are not checked). Every int is a JSON integer in int's range:
  a bool, a float or `4294967295` is an error naming the key, never converted. The
  `"version"` is a lower bound for the reader, not a gate: every key is read whatever
  the declared version. A tag written twice in `"zones"` is not detected (the JSON
  parser keeps the last one). A v7 binary file with bytes after its last block is
  rejected. `Snapshot::ValidateSkillsTagsZones`
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

- **Going down** (`OnZeroHealth`: an agent dies, a `Companion` goes down): alive
  (`IsAlive()`), 0 HP, `IsDowned()`, `times_downed + 1`; its statuses are cleared, its
  tags and skills kept; its timers keep running (`EndStep` ticks it: tags expire,
  cooldowns recover). During a step only at the end of the turn (`ApplyTurnOutcomes` →
  `Agent::ApplyTurnHealth` / `Defeat`: skill damage, zones, reactions, effects and enemy
  strikes all go through the turn's ledger, see The turn's health); between steps at
  once (`Agent::TakeDamage`, the host's `kill` / `hit`). It stays down until a skill revives it (see Revive: by
  default, an ally beside it finds its slot 0 is `revive`, a context skill) or
  `Reset` / `LoadSnapshot`
- **Inert and untouched.** `Agent::IsAffectable()` = alive and not downed: the check
  for everything that hits, heals, tags, statuses, pushes / pulls or targets an agent
  (`TakeDamage`, `Heal`, `ApplyTag` / `LandTag` / `ApplyTagTo` (false, interns nothing),
  `ApplyStatus`, zones, `Affects`, forced moves, effects and effect pushes, projectiles
  pass over it). The one exception: an `affects_downed` skill reaches the downed only
  (and can only revive them). A body down as the turn begins is a static blocker of the
  motion phase: never moved, it blocks every walk, push, dash path and landing onto it.
  The downed does not act (`GatherIntentions`: Stay;
  `CanUseSkill` false; `LegalActions`: Stay only; no context skill).
  Enemies ignore it (`FindClosestCompanion`) and drop it as a target (`AggroState`, a
  wind-up locks no downed target). It still blocks its cell (collisions, landing)
- **Objectives: only standing companions control them; physical presence (occupancy)
  counts the downed.** A downed body on a goal cell does not cover it
  (`SynchroLens::CountAgentsOnSynchroCells` / `SynchroEnv::NumAgentsOnSynchroCells`
  count affectable agents only); occupancy checks (collisions, landing, `IsOccupied`)
  stay on `IsAlive()`. Anything else is the host's rules (e.g. a game layer's plates)
- `Agent::IsDead()` stays health-based (a downed companion `IsDead()`). DodgeLens
  counts a companion that is not affectable (down or dead) as fallen: no survival bonus
  that step, no success at the horizon; no failure either (only a team down or the
  horizon fails a task)
- **Team counter**: `GetDowns()` = the sum of `Companion::GetTimesDowned()` (every down
  counts). `GetMaxDowns()` / `SetMaxDowns(n)` (n >= 1, else false; `kDefaultMaxDowns` =
  3, `core/types.h`). `IsTeamDown()` = downs >= max_downs, or no companion is affectable
  (a dead companion does not stand; an env without companions is never team down)
- **Done**: `BaseEnv::IsDone()` is non-virtual and the same for every env and lens:
  `success_ || tick_ >= horizon_ || interrupted_ || IsTeamDown()`.
  `EndReason::TeamDown` (4), `EndReason::Interrupted` (5): see "Why an episode ended"
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
  agent exists and is dead, before it can activate; the end of a turn
  (`EffectSystem::CancelDeadSources`) removes those of the agents that died in it. An effect already active when its
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
- Deaths come at the end of the turn: an attacker a companion kills during step t
  still lands a strike activating in step t; its strikes still winding up at the end
  of step t never land

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
- Episode end: success when a living FSM enemy stands on the target, else the horizon,
  or the team down (every env, see Downs). A dead enemy fails nothing (only a team down
  or the horizon fails a task): the episode runs on to the horizon
- Rewards: +1 win, `kTimePenalty` (-0.01) per other step, a kill's included: a
  killed-enemy episode returns what timing out does (`horizon * kTimePenalty`;
  `AggroEnv::MinUtility` adds the downs' worst cost, `WorstDownCost`). A step
  paused by a down (see Interruptions) pays 0. A kill after a latched success pays `kTimePenalty` (the
  enemy is off the target, as after any success)
- `done` is a verdict for RL episodes, never a stop: the env keeps stepping

**Why an episode ended** (`BaseEnv::GetEndReason`, `EndReason` in `base_env.h`):
what ended the episode. `None` while `IsDone()` is false, else `Success` (latched by
`Step` from the lens's `IsSuccess`: a success on the step the team goes down is a
success), else `TeamDown` (`IsTeamDown()`, any env: the level is lost), else `Horizon`
(tick >= horizon), else `Interrupted` (a down paused the task: provisional, see
Interruptions below). Only a team down or the horizon fails a task (a bad situation stays
salvageable): no lens has a failure of its own (Aggro's dead enemy, a Dodge companion
down), and `TaskFailed` (3) is no longer produced (since C API 1.6; the value stays,
the enums are append-only). No success after the episode ended otherwise: `Step`
latches one only while `BaseEnv::SuccessCounts()` (success latched, or no end reason
latched yet: the horizon step itself still can; never while paused), and the lenses pay their win reward
only then (a goal reached, or a Dodge companion revived, past the horizon wins
nothing: the level is lost). A lens's own `IsDone` (success or horizon) is for tests and
tools; `BaseEnv` never calls it. The reason is fixed when done first
becomes true (latched by `Step`, `SetTaskLens*` and `LoadSnapshot`, cleared by
`ResetOutcome`; `Interrupted` excepted, upgraded by a later reason): a team down after the horizon, or after loading a snapshot at
the horizon, keeps `Horizon` (`IsTeamDown()` / the C API's `team_down` still tell the
team is down). C API (1.2.1, additive, no struct layout change; 1.3 added `TeamDown` 4):
`Companions_EndReason` (same values: None 0, Success 1, Horizon 2, TaskFailed 3 (not
produced since 1.6), TeamDown 4, Interrupted 5 (since 1.6)) from `companions_get_end_reason(env)`, fixed on the step or
lens change where done becomes true, kept while a host plays on; reset and snapshot
loads take the env's (done / success / reason: a snapshot loaded at the horizon is done
at once, as `Horizon`, and the next step reports EpisodeEnd); the EpisodeEnd event
carries it in `effect_id`. A derived `Reset` / `LoadSnapshot` that changes state after
`BaseEnv::LoadSnapshot` latched calls `RelatchEndReasonAfterLoad` (AggroEnv's `Reset`
spawns its enemy and companions then; it also takes the loaded downs as seen)

**Interruptions** (a down interrupts the task; `EndReason::Interrupted` 5, C API
`Companions_End_Interrupted`, since 1.6). `Step` reads the team's downs from the state
(`GetDowns()` vs `downs_seen_`: a down the host caused between steps is caught by the
next step). The step they grow, the running lens's task is interrupted (`interrupted_`,
`IsInterrupted()`): done as `Interrupted`, and that step pays every agent the lens's
reward plus the down cost once per new down (`down_cost_ * new_downs`). From the next
step the lens is paused while anyone is down (`AnyCompanionDowned()`): rewards 0, no
success latched; downs meanwhile pay nothing, then or later (`downs_seen_` counts them).
The pause clears once nobody is down (a revive; the step that revives the last one is
still paused: the lens rewards and decides again from the next step, done false and
the reason `None` again), and on any lens change, `Reset` or `LoadSnapshot`
(`ResetOutcome` clears it and takes the current downs as seen: a snapshot loaded with
someone down loads not interrupted). Priority Success > TeamDown > Horizon >
Interrupted: `Interrupted` is the only provisional reason (`LatchEndReason` upgrades
it, `GetEndReason` computes it live): a team down while paused is `TeamDown`, a pause
reaching the horizon `Horizon`, and a final verdict ends the pause (`IsInterrupted()`
is true exactly when `GetEndReason()` is `Interrupted`; steps played on after it pay
again). A down only pauses while the episode goes on: the horizon never pauses (a down
on the horizon step ends as `Horizon`; a Dodge companion revived on the horizon step:
`Horizon`, no success), a success and a down on one step is a `Success`, and a down
after the episode ended pauses nothing. Every step latches the verdict as it starts, so
a team the host downed between steps is not turned into a success by the next step (no
win reward either), and a host raising max_downs after a `TeamDown` reopens the episode
before the step. A host reopening the episode that way (or reviving between steps
after an all-down) with someone still down does not pause it: that down was already
seen. A step paused as it starts pays nothing, even if its start latch ends the pause
(a team the host downed while paused). A lens opts out with `TaskLens::IsInterruptible()` (default true):
its downs pay the cost, nothing pauses. Without a lens the rewards are all 0 (no cost,
no pause). **Down cost**: `kDefaultDownCost` = -0.5, `SetDownCost(c)` (false for a
non-finite or positive `c`, the cost unchanged; 0 allowed) / `GetDownCost()`; runtime,
not in snapshots, copied with the env, kept across `Reset` / `LoadSnapshot`. The envs'
`MinUtility` adds `WorstDownCost()` (a loose bound: every down paid up to the team
down, max_downs - 1 downs, then every companion at once)

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

