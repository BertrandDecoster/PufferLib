// Copyright 2024
// C API for external integration (game engines, FFI bindings)
// This is a SEPARATE API from the Python wrapper (synchro.h)
// All types are POD-only for safe DLL boundary crossing
//
// =============================================================================
// Versioning
// =============================================================================
// companions_version() is "1.4.0". 1.1 changed struct layouts
// (Companions_AgentState, Companions_Event, Companions_StepResult): consumers
// must be rebuilt against this header, never mixed with a 1.0 DLL or header.
// 1.2 removed the legacy generic companion cast (its on/off setter and
// getter, and its EffectSpawned events): every companion skill slot now holds
// a skill, the fixed "attack" by default, so Companions_Interact_Attack
// strikes the faced cell (struct layouts unchanged).
// 1.2.1 (additive, struct layouts unchanged): Companions_EndReason and
// companions_get_end_reason; the EpisodeEnd event carries the reason in
// effect_id.
// 1.2.2 (behaviour, struct layouts unchanged): every timer (tag and status
// durations, skill cooldowns) ticks at the end of a step; a cooldown of n
// blocks the n steps after the use (it blocked n - 1). A lens change starts
// a new episode, which reports its own EpisodeEnd. A snapshot naming an
// unknown enemy "kind" is rejected.
// 1.3 added downs, and changed struct layouts (Companions_AgentState,
// Companions_GameState): consumers must rebuild against this header. A
// companion at 0 HP goes down instead of dying: Companions_AgentState.downed
// (alive stays true); Companions_GameState.downs / max_downs count the team's
// downs and Companions_GameState.team_down says the team is down now;
// Companions_End_TeamDown (the level is lost) and
// Companions_Event_AgentDowned.
// 1.4 added revives and context skills, and changed struct layouts
// (Companions_AgentState): consumers must rebuild against this header.
// Companions_AgentState.skills now holds the EFFECTIVE skills (what Skill1 /
// Skill2 use now: a level's context rule may replace the equipped skill, by
// default "revive" in slot 0 next to a downed ally); the new equipped_skills
// holds what 1.3's skills did (what companions_set_agent_skill or the level
// put there); skill_cooldowns stay the equipped skills'. The builtin
// "revive" gets a downed companion up; Companions_Event_AgentRevived reports
// it. 1.4 also added (additive, amended into the unreleased 1.4.0) the skill
// book query: companions_get_skill_count / companions_get_skill /
// companions_find_skill fill a Companions_SkillInfo with every field of a
// skill (the builtins, "attack" and "revive" included, and the level's own or
// retuned ones), so a host reads the env's skills instead of copying them. A
// skill lands at most Companions_MAX_SKILL_TAGS tags (the env refuses more).
// Also amended in: companions_preview_skill says where a slot's skill would
// land and whom it would affect now, by the step's own targeting code, and
// companions_get_last_skill_use_count / companions_get_last_skill_use say
// whom each of the last step's skill uses affected, each with what the use
// does to it (Companions_SkillEffect). 1.4.0 was amended in
// place before its release: an app built against the amended header refuses
// an earlier 1.4.0 DLL at load (the DLL lacks these exports). DLLs built from
// earlier 1.4.0 commits of this branch are NOT compatible with this header
// either, even when they load: the struct sizes changed
// (Companions_SkillPreview, Companions_SkillUseInfo). Rebuild both sides.
// Snapshots: since 1.2, a snapshot whose agent skill slot names a skill that
// is neither a builtin nor one of the snapshot's own "skills" is rejected
// (companions_load_snapshot / _json return false, the error names the agent,
// slot and skill; "" still means "attack"). Levels saved earlier with such
// slots load no more: regenerate them.
// A minor bump may break the ABI (1.1 did): consumers pin major.minor, not
// just major, and rebuild against the matching header.
//
// =============================================================================
// Thread Safety
// =============================================================================
// - Each Companions_Env instance is NOT thread-safe, const getters included
//   (some fill internal caches: companions_get_tag_name,
//   companions_get_snapshot_size). Do not call functions on the same
//   environment from multiple threads simultaneously, even read-only ones.
// - Different Companions_Env instances can be used concurrently from
//   different threads.
// - companions_get_error() uses thread-local storage and is thread-safe.
// - companions_reset() and companions_step() must not be called
//   concurrently on the same environment.
//
// =============================================================================
// Error Handling
// =============================================================================
// On error, functions return false/zero/default values and set an error
// string retrievable via companions_get_error().
//
// =============================================================================
// Event System (Partial Implementation)
// =============================================================================
// Currently implemented events, in this order within a step: the movement
// events (AgentMoved / AgentBlocked, per agent in agent order), then
// AgentDowned, AgentRevived, SkillUsed, TagApplied, EpisodeEnd:
// - Companions_Event_AgentMoved: Agent moved to a new position (by walking,
//   or by a skill: a teleport, dash, push or pull)
// - Companions_Event_AgentBlocked: Agent tried to move but was blocked
// - Companions_Event_AgentDowned: a companion went down (0 HP: alive, inert,
//   untouchable): subject_id = the companion, position = its cell. One per
//   down; a down between two steps (a host effect, companions_spawn_effect)
//   is reported by the next step. Since 1.3.
// - Companions_Event_AgentRevived: a downed companion got up (a skill that
//   revives, such as "revive", reported as a SkillUsed too): subject_id = the
//   revived companion, health_source_id = the reviver (the skill's caster),
//   health_new = health_amount = the HP it got up with (gained from 0),
//   position = its cell after the step
//   (a later skill may push it). One per revive, in resolution order. A
//   companion revived and downed again in the same step (by a later skill or
//   an effect) has its AgentRevived here and its second AgentDowned among the
//   downs above: within a step the events are grouped by kind, not in time
//   order. Since 1.4.
// - Companions_Event_SkillUsed: a companion used the skill in a slot:
//   subject_id = caster, position = the skill's centre (the landing cell for
//   a self-targeted skill such as teleport), effect_id = the slot (0-based),
//   effect_name = skill name.
// - Companions_Event_TagApplied: a tag landed on an agent (from a skill or a
//   zone, see companions_set_cell_tag): subject_id = agent, position = its
//   cell after the step, effect_id = the tag id (see companions_get_tag_name),
//   effect_name = tag name, status_duration = duration (-1 = permanent),
//   health_source_id = caster (-1 for a zone), tag_fresh = the agent did not
//   carry the tag just before this landing.
// - Companions_Event_EpisodeEnd: Episode completed (success or failure),
//   reported once per false->true transition of done, on the step where it
//   happens: the steps a host keeps playing afterwards (done stays true) do
//   not repeat it; reset and snapshot loads start a new episode.
//   effect_id = the Companions_EndReason (see companions_get_end_reason).
// A step reports at most Companions_MAX_EVENTS events, in the order above.
// When there are more, the ones past the cap are dropped, except EpisodeEnd:
// a step that ends the episode always reports it, as the last event (the
// others are then cut to Companions_MAX_EVENTS - 1). events_dropped counts
// the events not reported. The state itself (agents' tags, skills,
// statuses, downed, health, downs) is always complete.
// The state-change events (moved / blocked, downed, revived) come first, so
// skill and tag events are dropped before them. They always fit when no
// companion is revived twice in the step: an agent then has at most one
// movement event, one revive and two downs (down between two steps, revived,
// down again), 4 events, so with up to 15 agents (states report at most
// Companions_MAX_AGENTS = 8) they fit next to EpisodeEnd. Reviving the same
// companion twice in a step takes a skill downing it between two revives
// (skills resolve one caster at a time); each such extra revive adds one
// revive and one down, and in such a step the last state-change events may
// be dropped too.
//
// Not yet implemented (will be added as needed):
// - Companions_Event_AgentDamaged, Companions_Event_AgentHealed, Companions_Event_AgentDied
// - Companions_Event_FSMTransition, Companions_Event_EffectSpawned, etc.
//   (skill damage is visible in the agents' health, not yet as events)

#ifndef COMPANIONS_API_H_
#define COMPANIONS_API_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// DLL Export Macros
// =============================================================================
#ifdef _WIN32
#ifdef COMPANIONS_EXPORTS
#define COMPANIONS_API __declspec(dllexport)
#else
#define COMPANIONS_API __declspec(dllimport)
#endif
#else
#define COMPANIONS_API __attribute__((visibility("default")))
#endif

// =============================================================================
// Constants
// =============================================================================
#define Companions_INVALID_ID (-1)
#define Companions_MAX_AGENTS 8
#define Companions_MAX_EFFECTS 32
#define Companions_MAX_EVENTS 64
#define Companions_MAX_STATUSES 4
#define Companions_MAX_SPECIAL_CELLS 64
#define Companions_MAX_EFFECT_CELLS 16
#define Companions_EFFECT_NAME_LEN 32
#define Companions_MAX_RENDER_SIZE 4096
#define Companions_MAX_TAGS 8          // Tags per Companions_AgentState
#define Companions_MAX_SKILL_SLOTS 2   // Skill slots per companion
// Skill and tag names are at most Companions_SKILL_NAME_LEN - 1 (31) bytes:
// the env refuses longer ones, so the name buffers below never truncate.
#define Companions_SKILL_NAME_LEN 32   // Including the terminating '\0'
#define Companions_MAX_SKILL_TAGS 32   // Tags a skill lands (the env refuses more)

// =============================================================================
// Basic Types
// =============================================================================
typedef int32_t Companions_ObjectId;

typedef struct {
  int32_t row;
  int32_t col;
} Companions_Position;

// =============================================================================
// Enums (matching companions::* enums)
// =============================================================================
typedef enum {
  Companions_Direction_Up = 0,
  Companions_Direction_Down = 1,
  Companions_Direction_Left = 2,
  Companions_Direction_Right = 3,
} Companions_Direction;

typedef enum {
  Companions_Movement_Stay = 0,
  Companions_Movement_Up = 1,
  Companions_Movement_Down = 2,
  Companions_Movement_Left = 3,
  Companions_Movement_Right = 4,
} Companions_MovementAction;

// Skill1 / Skill2 use the skill in slot 0 / 1, the effective one
// (Companions_AgentState.skills; see companions_set_agent_skill): the
// companion stays put and its movement only aims. A slot is never empty:
// without another skill it holds "attack" (1 damage to the agent on the faced
// cell, allies spared), so every companion strikes on Skill1, RL envs'
// included. A skill that cannot be used (unknown skill, cooldown, slot not
// enabled yet (Skill2), rooted for a self-moving skill, stunned or dead
// caster) is dropped and the movement applies as with None. Any other value
// refuses the step (see companions_step).
typedef enum {
  Companions_Interact_None = 0,
  Companions_Interact_Skill1 = 1,
  Companions_Interact_Skill2 = 2,  // Accepted but ignored until slot 2 is enabled
  Companions_Interact_Attack = Companions_Interact_Skill1,  // Historical name
} Companions_InteractAction;

typedef enum {
  Companions_Faction_Companion = 0,
  Companions_Faction_Enemy = 1,
  Companions_Faction_Neutral = 2,
} Companions_Faction;

typedef enum {
  Companions_ObjectType_Object = 0,
  Companions_ObjectType_Actor = 1,
  Companions_ObjectType_Agent = 2,
  Companions_ObjectType_AgentFSM = 3,
  Companions_ObjectType_Companion = 4,
  Companions_ObjectType_Player = 5,
  Companions_ObjectType_NPCCompanion = 6,
} Companions_ObjectType;

typedef enum {
  Companions_CellKind_Floor = 0,
  Companions_CellKind_Wall = 1,
  Companions_CellKind_Hazard = 2,
  Companions_CellKind_HealArea = 3,
} Companions_CellKind;

typedef enum {
  Companions_FSMState_None = 0,
  Companions_FSMState_Patrol = 1,
  Companions_FSMState_Aggro = 2,
  Companions_FSMState_ReturnToPatrol = 3,
  Companions_FSMState_Telegraph = 4,
  Companions_FSMState_Attack = 5,
  Companions_FSMState_Recovery = 6,
} Companions_FSMStateType;

typedef enum {
  Companions_Status_None = 0,
  Companions_Status_Stunned = 1,
  // 2 was Companions_Status_Slowed (removed); reserved, never reused.
  Companions_Status_Marked = 3,
  Companions_Status_Rooted = 4,  // Cannot move by itself (walking, self-moving skills)
} Companions_StatusType;

typedef enum {
  Companions_Enemy_Zombie = 0,
  Companions_Enemy_Archer = 1,
  Companions_Enemy_Mage = 2,
} Companions_EnemyType;

// Concrete per-agent archetype. Richer than Companions_ObjectType (which only
// distinguishes the C++ base class). Consumers that need to pick a mesh,
// glyph, or animation set for an NPC key off this field.
//
// Numerically 1:1 with companions::AgentKind (core/object.h); the mapping is
// a static_cast guarded by static_asserts in companions_api.cc. Kept
// numerically in sync with Pure::NpcKind in companions-loop. When adding a
// new enemy/NPC class: add one entry in each of the three enums and override
// Agent::GetAgentKind() on the new class.
typedef enum {
  Companions_AgentKind_Unknown     = 0,
  Companions_AgentKind_Companion   = 1,   // generic player companion
  Companions_AgentKind_NeutralNpc  = 2,   // generic friendly/quest NPC
  Companions_AgentKind_EnemyZombie = 10,
  Companions_AgentKind_EnemyGoblin = 11,
  Companions_AgentKind_EnemyDragon = 12,
} Companions_AgentKind;

// =============================================================================
// Composite Structs
// =============================================================================

// Action input
typedef struct {
  Companions_MovementAction movement;
  Companions_InteractAction interact;
} Companions_Action;

// Status effect on an agent
typedef struct {
  Companions_StatusType type;
  int32_t duration;  // ticks remaining
} Companions_StatusEffect;

// A tag an agent carries ("burning", "wet", ...: opaque names, see
// companions_get_tag_name)
typedef struct {
  int32_t tag_id;    // See companions_get_tag_name
  int32_t duration;  // Ticks left, -1 = permanent
} Companions_AgentTag;

// Agent state snapshot
typedef struct {
  Companions_ObjectId id;
  Companions_ObjectType type;
  // Concrete archetype. Unlike `type` (which only tells you "AgentFSM" vs
  // "Companion"), this carries the actual C++ class: Zombie / Goblin /
  // Dragon / Companion / NeutralNpc. Sourced from Agent::GetAgentKind() (a
  // virtual on the C++ side). Consumers that render meshes / glyphs /
  // animation sets per NPC kind key off this field.
  Companions_AgentKind kind;
  Companions_Position position;
  Companions_Position prev_position;  // Position before this step (for animation)
  Companions_Direction facing;
  Companions_Faction faction;
  int32_t health;
  int32_t max_health;
  bool alive;
  bool downed;  // A companion at 0 HP: alive, inert, untouchable (since 1.3)
  int32_t agent_index;  // Index in action array

  // FSM state (for AgentFSM types)
  Companions_FSMStateType fsm_state;
  Companions_ObjectId fsm_target_id;  // Who is being chased (if aggro)

  // Status effects
  Companions_StatusEffect statuses[Companions_MAX_STATUSES];
  int32_t status_count;

  // Tags, oldest first (the first Companions_MAX_TAGS only)
  Companions_AgentTag tags[Companions_MAX_TAGS];
  int32_t tag_count;

  // Skill slots (companions only, never "": "attack" when nothing else is
  // there; all "" / 0 for other agents). Names always fit: the env refuses
  // longer ones. Since 1.4 a slot has two skills:
  // - skills: the EFFECTIVE skill, the one Skill1 / Skill2 use now. A level's
  //   context rules may replace the equipped skill while their condition
  //   holds (by default: next to a downed ally, slot 0 is "revive"); computed
  //   for the state as it is now, so it can change between two steps (a host
  //   effect downing an ally). A companion that cannot act (downed) has no
  //   context: its effective skills are its equipped ones.
  // - equipped_skills: what companions_set_agent_skill, a snapshot or a level
  //   put in the slot (what skills held before 1.4). Equal to skills when
  //   no rule applies.
  char skills[Companions_MAX_SKILL_SLOTS][Companions_SKILL_NAME_LEN];
  char equipped_skills[Companions_MAX_SKILL_SLOTS][Companions_SKILL_NAME_LEN];
  // The EQUIPPED skills' cooldowns: next steps it stays unusable, 0 = ready.
  // A skill a context rule gives has no cooldown and neither reads nor
  // spends this one: while a rule applies, the slot is usable whatever its
  // cooldown, and the cooldown keeps ticking.
  int32_t skill_cooldowns[Companions_MAX_SKILL_SLOTS];

  // Actions - intent (before collision resolution) vs actual (after)
  Companions_Action action_intent;   // What the agent wanted to do
  Companions_Action action_actual;   // What actually happened after validation
  bool action_succeeded;     // Did movement succeed? (intent == actual)
} Companions_AgentState;

// Cell state snapshot
typedef struct {
  Companions_Position position;
  Companions_CellKind kind;
  // The living occupant, or a dead one not walked over (a corpse walked over
  // and left drops out: read the dead from the agents, not by cell);
  // Companions_INVALID_ID if empty
  Companions_ObjectId occupant_id;
} Companions_CellState;

// Active effect instance
typedef struct {
  int32_t effect_id;               // Runtime instance ID
  char effect_name[Companions_EFFECT_NAME_LEN];
  Companions_Position center;
  Companions_Direction direction;
  int32_t ticks_remaining;
  bool in_telegraph;  // true = warning, false = active damage
  Companions_ObjectId source_id;

  // Affected area (relative positions from center)
  Companions_Position affected_cells[Companions_MAX_EFFECT_CELLS];
  int32_t affected_count;
} Companions_ActiveEffect;

// =============================================================================
// Event Types (for transition/animation events)
// =============================================================================
typedef enum {
  Companions_Event_None = 0,
  Companions_Event_AgentMoved = 1,
  Companions_Event_AgentBlocked = 2,  // Movement was blocked
  Companions_Event_AgentDamaged = 3,
  Companions_Event_AgentHealed = 4,
  Companions_Event_AgentDied = 5,
  Companions_Event_AgentSpawned = 6,
  Companions_Event_FSMTransition = 7,
  Companions_Event_EffectSpawned = 8,
  Companions_Event_EffectActivated = 9,
  Companions_Event_EffectEnded = 10,
  Companions_Event_StatusApplied = 11,
  Companions_Event_StatusRemoved = 12,
  Companions_Event_GoalReached = 13,
  Companions_Event_EpisodeEnd = 14,
  Companions_Event_SkillUsed = 15,   // See "Event System" at the top
  Companions_Event_TagApplied = 16,  // See "Event System" at the top
  Companions_Event_AgentDowned = 17,  // A companion went down (subject_id, position; see "Event System")
  Companions_Event_AgentRevived = 18,  // A downed companion got up (see "Event System")
} Companions_EventType;

// Why an episode ended (companions_get_end_reason, EpisodeEnd's effect_id)
typedef enum {
  Companions_End_None = 0,        // Not done
  Companions_End_Success = 1,     // The task succeeded
  Companions_End_Horizon = 2,     // The horizon was reached
  Companions_End_TaskFailed = 3,  // A task failure ended the episode (e.g. Aggro: the enemy is dead)
  Companions_End_TeamDown = 4,    // The team is down: max_downs reached or every companion down (the level is lost). Since 1.3
} Companions_EndReason;

// Transition event (delta information for animations)
typedef struct {
  Companions_EventType type;
  int32_t tick;  // When it happened

  // Subject of the event
  Companions_ObjectId subject_id;
  Companions_Position position;

  // Event-specific data (union-like via fields, C99 compatible)
  // For AgentMoved/AgentBlocked:
  Companions_Position from_pos;
  Companions_Position to_pos;
  Companions_MovementAction move_action;

  // For AgentDamaged/AgentHealed (and AgentRevived: health_new = the HP it
  // got up with, health_amount = the same (gained from 0), health_source_id
  // = the reviver):
  int32_t health_amount;
  int32_t health_new;
  Companions_ObjectId health_source_id;

  // For FSMTransition:
  Companions_FSMStateType fsm_from;
  Companions_FSMStateType fsm_to;

  // For Effect events (and SkillUsed: slot / skill name; TagApplied: tag id /
  // tag name; EpisodeEnd: the Companions_EndReason, see "Event System" at the
  // top):
  int32_t effect_id;
  char effect_name[Companions_EFFECT_NAME_LEN];

  // For Status events (and TagApplied: the tag's duration, -1 = permanent):
  Companions_StatusType status_type;
  int32_t status_duration;

  // For TagApplied (health_source_id = caster, -1 for a zone):
  bool tag_fresh;  // The agent did not carry the tag just before this landing

  // For EpisodeEnd (effect_id = the Companions_EndReason):
  bool episode_success;
  float episode_reward;
  int32_t episode_steps;
} Companions_Event;

// =============================================================================
// Game State Snapshot
// =============================================================================
typedef struct {
  // Grid dimensions
  int32_t rows;
  int32_t cols;
  int32_t tick;

  // Agents
  Companions_AgentState agents[Companions_MAX_AGENTS];
  int32_t agent_count;

  // Special cells (non-Floor cells for efficient iteration)
  Companions_CellState special_cells[Companions_MAX_SPECIAL_CELLS];
  int32_t special_cell_count;

  // Active effects
  Companions_ActiveEffect effects[Companions_MAX_EFFECTS];
  int32_t effect_count;

  // Episode status
  bool done;
  bool success;
  int32_t downs;      // The team's downs so far (every down counts)
  int32_t max_downs;  // The level is lost at this many (or every companion down)
  // The team is down now: max_downs reached or every companion down;
  // independent of the end reason, which keeps the first reason the episode
  // ended with (a team down after the horizon stays Horizon). Since 1.3
  bool team_down;
  float rewards[Companions_MAX_AGENTS];
} Companions_GameState;

// =============================================================================
// Step Result (state + transition events)
// =============================================================================
typedef struct {
  Companions_GameState state;

  // Events that occurred during this step
  Companions_Event events[Companions_MAX_EVENTS];
  int32_t event_count;
  // Events of this step not reported (past Companions_MAX_EVENTS, see
  // "Event System" at the top); 0 when they all fit
  int32_t events_dropped;
} Companions_StepResult;

// =============================================================================
// Environment Configuration
// =============================================================================
typedef struct {
  int32_t rows;
  int32_t cols;
  int32_t num_companions;
  int32_t num_synchro;
  int32_t map_complexity;
  int32_t horizon;
  int32_t d4_transform;
  uint32_t seed;
} Companions_EnvConfig;

// Configuration for AggroEnv (enemy + patrol)
typedef struct {
  int32_t rows;
  int32_t cols;
  int32_t num_companions;
  int32_t patrol_square_size;  // 0 = no patrol, 3 = 3x3 patrol square
  int32_t horizon;
  int32_t d4_transform;
  uint32_t seed;
  Companions_EnemyType enemy_type;
  int32_t map_complexity;
} Companions_AggroEnvConfig;

// =============================================================================
// Opaque Handle
// =============================================================================
typedef struct Companions_Env Companions_Env;

// =============================================================================
// Lifecycle Functions
// =============================================================================

// Create a new SynchroEnv instance
COMPANIONS_API Companions_Env* companions_create(
    const Companions_EnvConfig* config);

// Create a new AggroEnv instance (with FSM enemy)
COMPANIONS_API Companions_Env* companions_create_aggro(
    const Companions_AggroEnvConfig* config);

// =============================================================================
// Task Lens API (runtime task switching)
// =============================================================================

// Task lens types
typedef enum {
    Companions_Lens_Synchro = 0,
    Companions_Lens_Aggro = 1,
    Companions_Lens_Dodge = 2,
    Companions_Lens_TagApply = 3,
    Companions_Lens_Unknown = 0x7FFFFFFF  // Sentinel — lens kind not recognized.
} Companions_LensType;

// Set task lens on environment (swaps interpretation layer)
COMPANIONS_API bool companions_set_task_lens(Companions_Env* env, Companions_LensType lens);

// Set task lens with positional parameters. The lens will materialize objective
// cells at the given positions (e.g. SynchroLens stamps Synchro cells at plate
// positions for a pressure-plate puzzle). Positions are an array of length
// `num_positions`. Returns false if lens is incompatible after activation.
COMPANIONS_API bool companions_set_task_lens_with_params(
    Companions_Env* env,
    Companions_LensType lens,
    const Companions_Position* positions,
    int num_positions);

// Get current lens type
COMPANIONS_API Companions_LensType companions_get_task_lens(Companions_Env* env);

// Destroy environment and free resources
COMPANIONS_API void companions_destroy(Companions_Env* env);

// =============================================================================
// Environment Control
// =============================================================================

// Reset environment with new seed
COMPANIONS_API void companions_reset(Companions_Env* env,
                                           uint32_t seed);

// Step environment with actions, returns full result with events
// actions array must have env->agent_count elements
// A movement outside Stay..Right or an interact outside None..Skill2 refuses
// the whole step (error set, nothing stepped, out_result untouched). An
// interact for a skill slot the action space does not enable yet (today:
// Companions_Interact_Skill2) is treated as None: the movement applies and no
// skill is used.
COMPANIONS_API void companions_step(Companions_Env* env,
                                          const Companions_Action* actions,
                                          int32_t action_count,
                                          Companions_StepResult* out_result);

// =============================================================================
// State Queries
// =============================================================================

// Get current game state snapshot (no events)
COMPANIONS_API void companions_get_state(const Companions_Env* env,
                                               Companions_GameState* out_state);

// Get specific agent state by ID
COMPANIONS_API bool companions_get_agent(const Companions_Env* env,
                                               Companions_ObjectId id,
                                               Companions_AgentState* out_agent);

// Get specific agent state by index
COMPANIONS_API bool companions_get_agent_by_index(
    const Companions_Env* env, int32_t index, Companions_AgentState* out_agent);

// Get cell kind at position
COMPANIONS_API Companions_CellKind companions_get_cell(
    const Companions_Env* env, int32_t row, int32_t col);

// Get full grid as flat array (row-major order)
// out_grid must have rows * cols elements
COMPANIONS_API void companions_get_grid(const Companions_Env* env,
                                              Companions_CellKind* out_grid);

// =============================================================================
// Host-driven changes (between steps)
// =============================================================================
//
// A host that layers its own rules on top of the env (e.g. a game that knows
// a cell is a gate, or that a combo is lethal) pushes the consequences back
// through these generic primitives. The env does not know why.

// Change the kind of a cell (e.g. Wall -> Floor to open a passage).
// Occupancy is not checked: turning an occupied cell into a wall is the
// caller's responsibility. Returns false on error (invalid env, out of bounds).
COMPANIONS_API bool companions_set_cell(Companions_Env* env, int32_t row,
                                        int32_t col, Companions_CellKind kind);

// Spawn a registered effect at a cell, as the env would. Built-in effects
// always available: "kill" (kills an agent; a companion goes down instead,
// see Companions_AgentState.downed, reported by the next step's
// Companions_Event_AgentDowned), "hit" (1 damage), "stun" (stunned, 3 ticks),
// plus enemy attacks. Instant effects (no telegraph) apply
// immediately; telegraphed ones resolve over the next steps.
// source_id: agent immune to the effect (-1 for none).
// Returns false on error (unknown effect, out of bounds).
COMPANIONS_API bool companions_spawn_effect(Companions_Env* env,
                                            const char* effect_name,
                                            int32_t row, int32_t col,
                                            Companions_Direction direction,
                                            Companions_ObjectId source_id);

// =============================================================================
// Skills, tags and zones
// =============================================================================
//
// Tags: opaque names interned per env. Ids stay valid for this env's lifetime
// (resets and snapshot loads included), and so does the string
// companions_get_tag_name returns. Persist names, not ids.
COMPANIONS_API const char* companions_get_tag_name(const Companions_Env* env, int32_t tag_id);  // NULL if unknown
COMPANIONS_API int32_t companions_find_tag(const Companions_Env* env, const char* name);        // -1 if unknown
// Land `tag` on an agent for `duration` steps (-1 = permanent). False for an
// unknown, downed or dead agent (nothing lands on them, the tag is not
// interned), an agent immune to the tag (nothing lands), a NULL / "" tag or
// one over 31 bytes, or a duration of 0 or below -1. It is a landing like a
// skill's or a zone's: the level's tag statuses, the agent's weaknesses and
// the reactions apply (the env's rules, set by the level). Durations,
// statuses' and cooldowns included, tick at the end of each step: applied
// between two steps with duration d, the tag is there for the d next steps
// (d = 1: gone after the next step). Landed during a step with duration d,
// it reads d after that step (the same step timer).
COMPANIONS_API bool companions_apply_tag(Companions_Env* env, Companions_ObjectId agent, const char* tag, int32_t duration);
// Remove `tag` from an agent (true even if it did not carry it). False for an
// unknown agent or a NULL tag.
COMPANIONS_API bool companions_remove_tag(Companions_Env* env, Companions_ObjectId agent, const char* tag);
// Zones: one tag per cell ("" or NULL clears), landed on whoever stands there
// after each step's movement, and on whoever a skill moves there, with
// `duration` (-1 = permanent); each landing is a Companions_Event_TagApplied.
// False out of bounds, for a tag over 31 bytes, or for a duration of 0 or
// below -1 with a tag.
COMPANIONS_API bool companions_set_cell_tag(Companions_Env* env, int32_t row, int32_t col, const char* tag, int32_t duration);
// The cell's tag id, or -1 when it has none. Out of bounds: -1 with the error
// set ("Position out of bounds").
COMPANIONS_API int32_t companions_get_cell_tag(const Companions_Env* env, int32_t row, int32_t col);
// Skill slots (0-based). "" or NULL puts the default "attack" back; false for
// an unknown skill/slot/agent.
// Puts `skill` (a builtin: "attack", "fireball", "lightningStep", "teleport",
// "vortex", "revive", or one a JSON snapshot defines) in a companion's slot
// (its equipped skill: Companions_AgentState.equipped_skills; a context rule
// may still make another the effective one) and makes it ready (cooldown 0).
// Only slot 0 is usable today (Companions_Interact_Skill1); slot 1 can be
// filled but Skill2 is ignored.
COMPANIONS_API bool companions_set_agent_skill(Companions_Env* env, Companions_ObjectId agent, int32_t slot, const char* skill);

// The skill book (since 1.4): every skill the env knows, as data. The
// builtins ("fireball", "lightningStep", "teleport", "vortex", "revive" and
// the fixed default "attack"), then the level's own skills in the order its
// snapshot lists them; a level that retunes a builtin changes it in place.
// The book changes when a snapshot loads (companions_load_snapshot*: the
// builtins plus that snapshot's skills) and on companions_reset, which
// generates a level (the builtins only: a loaded level's skills are dropped).
// Indices and contents are stable in between. What a skill's tags mean is
// the host's business.
typedef enum {
  Companions_SkillTargeting_Self = 0,        // Centre = the caster (after its own motion)
  Companions_SkillTargeting_Ground = 1,      // Centre = `range` cells along the aim; a wall stops it on the cell before
  Companions_SkillTargeting_Projectile = 2,  // Centre = the first agent it affects within `range` (a wall stops it), else the last cell reached
} Companions_SkillTargeting;

typedef enum {
  Companions_SkillArea_Single = 0,  // The centre cell
  Companions_SkillArea_Cross = 1,   // The centre and its 4 orthogonal neighbours
} Companions_SkillArea;

typedef enum {
  Companions_SkillMotion_None = 0,
  Companions_SkillMotion_Dash = 1,      // Caster: up to motion_distance, crossing holes and agents
  Companions_SkillMotion_Teleport = 2,  // Caster: exactly motion_distance, else closer
  Companions_SkillMotion_PushOut = 3,   // The area's ring: motion_distance away from the centre
  Companions_SkillMotion_PullIn = 4,    // One agent of the ring into the centre (needs a Cross area)
} Companions_SkillMotion;

// Who a skill may affect, by faction (see also friendly_fire)
typedef enum {
  Companions_TargetFilter_All = 0,
  Companions_TargetFilter_Companion = 1,
  Companions_TargetFilter_Enemy = 2,
  Companions_TargetFilter_Neutral = 3,
} Companions_TargetFilter;

typedef struct {
  char tag[Companions_SKILL_NAME_LEN];  // Opaque name (see companions_get_tag_name)
  int32_t duration;                     // Steps, -1 = permanent
} Companions_SkillTag;

// A skill, field for field (PufferLib's SkillConfig).
typedef struct {
  char name[Companions_SKILL_NAME_LEN];
  Companions_SkillTargeting targeting;
  int32_t range;
  Companions_TargetFilter filter;
  Companions_SkillArea area;
  Companions_SkillMotion motion;
  int32_t motion_distance;
  bool tag_path;  // Dash: agents crossed on the way are affected too
  // Landed on every affected agent, in order: tag_count of them (never
  // truncated: a skill has at most Companions_MAX_SKILL_TAGS)
  Companions_SkillTag tags[Companions_MAX_SKILL_TAGS];
  int32_t tag_count;
  int32_t damage;      // Health every affected agent loses
  int32_t root_steps;  // Affected agents are rooted for this many next steps
  int32_t cooldown;    // Steps blocked after a use (0: none)
  // Off: only agents not of the caster's faction are affected (a projectile
  // flies past allies). On: allies too, and the caster in its own area,
  // unless a self_* flag below spares it that effect.
  bool friendly_fire;
  bool self_tags;
  bool self_motion;
  bool self_root;
  bool self_damage;
  // Off: the skill affects only the standing (alive, not downed). On: only
  // the downed, which it can only revive (a projectile flies past the rest).
  bool affects_downed;
  // A downed agent it affects gets up with this percent of its max HP,
  // rounded up (0: no revive).
  int32_t revive_percent;
} Companions_SkillInfo;

// Number of skills in the book; 0 for a null env ("Invalid environment").
COMPANIONS_API int32_t companions_get_skill_count(const Companions_Env* env);
// The skill at `index` (0-based, below companions_get_skill_count). False
// for a null env or `out` ("Invalid arguments") or an index out of range
// ("Skill index out of range"), `out` untouched.
COMPANIONS_API bool companions_get_skill(const Companions_Env* env, int32_t index,
                                         Companions_SkillInfo* out);
// The skill named `name`. False for a null env, `name` or `out` ("Invalid
// arguments") or a name the book does not hold ("Unknown skill: <name>"),
// `out` untouched.
COMPANIONS_API bool companions_find_skill(const Companions_Env* env, const char* name,
                                          Companions_SkillInfo* out);

// What a skill use does to one agent it affects (since 1.4): bit flags in
// Companions_SkillPreview.affected_effects / Companions_SkillUseInfo.
// affected_effects. The skill's tags land on it, its damage hits it, it is
// rooted, moved by the area motion (Motion: it really changes cell; a push
// against a wall moves nothing), or revived. The caster, affected under
// friendly fire, gets only what its self_* flags allow; 0 = affected, but
// nothing applies to it.
// A preview's flags are what the use would do now, predicted before its
// damage; a skill use's are what it DID: an agent its own damage downed or
// killed is neither rooted nor moved (no Root, no Motion), and a pull that
// then took the next thing of its ring reports that one with Motion. Tags
// and Damage are the same in both.
typedef enum {
  Companions_SkillEffect_Tags = 1 << 0,
  Companions_SkillEffect_Damage = 1 << 1,
  Companions_SkillEffect_Root = 1 << 2,
  Companions_SkillEffect_Motion = 1 << 3,
  Companions_SkillEffect_Revive = 1 << 4,
} Companions_SkillEffect;

// A skill use previewed (since 1.4): what a companion using the skill in a
// slot, aimed one way, would do NOW, before the next step. The env answers
// with the same code the step uses (its targeting), so a host previews
// without copying the env's rules. Nothing changes: no motion, tag, damage
// or revive.
typedef struct {
  // The step would use it (Companions_Interact_Skill1 for slot 0): an
  // enabled slot, a companion that can act (not downed, dead or stunned), a
  // skill not cooling down, and a rooted companion only for a skill that
  // does not move it
  bool usable;
  // The slot's effective skill (Companions_AgentState.skills: a context
  // rule's, e.g. "revive" next to a downed ally, else the equipped one)
  char skill[Companions_SKILL_NAME_LEN];
  Companions_Position centre;          // The SkillUsed event's position
  Companions_Position caster_landing;  // Where a dash / teleport puts the caster, else its cell
  // The agents it would affect, in the order the step processes them: its
  // area (the centre, then up, right, down, left), then a tag_path dash's
  // path; each with what the use does to it (Companions_SkillEffect flags,
  // in affected_effects at the same index). The first Companions_MAX_AGENTS
  // of them: affected_count; affected_total counts them all (more than
  // affected_count: the list was cut).
  Companions_ObjectId affected[Companions_MAX_AGENTS];
  uint32_t affected_effects[Companions_MAX_AGENTS];
  int32_t affected_count;
  int32_t affected_total;
} Companions_SkillPreview;

// Preview companion `agent`'s slot `slot` (0-based, below
// Companions_MAX_SKILL_SLOTS) aimed `aim`, as the next step would resolve it
// were it the step's only change. The step may differ: it moves everyone
// first (the enemies too), then resolves the skills one caster at a time in
// agent order, so an earlier caster's push, pull, damage or revive changes
// what a later one reaches; a use whose movement is Stay keeps the
// companion's facing (preview it with Companions_AgentState.facing). The
// preview is filled whatever `usable` says (what the skill would do if it
// could). False, `out` untouched, for a null env or `out` ("Invalid
// arguments"), an unknown agent ("Agent not found"), an agent that is not a
// companion ("Not a companion"), a slot out of range ("Skill slot out of
// range") or an aim outside Up..Right ("Invalid direction").
COMPANIONS_API bool companions_preview_skill(const Companions_Env* env, Companions_ObjectId agent,
                                             int32_t slot, Companions_Direction aim,
                                             Companions_SkillPreview* out);

// The last step's skill uses (since 1.4), in resolution order (the order of
// its SkillUsed events), with whom each affected: what the step did, as the
// preview says it before the step. Kept until the next step; a reset or a
// snapshot load empties it.
typedef struct {
  Companions_ObjectId caster;
  char skill[Companions_SKILL_NAME_LEN];
  int32_t slot;                  // 0-based
  Companions_Position centre;    // The SkillUsed event's position
  // The agents it affected, in the order it processed them, with what it
  // did to each: as Companions_SkillPreview's affected, affected_effects,
  // affected_count and affected_total
  Companions_ObjectId affected[Companions_MAX_AGENTS];
  uint32_t affected_effects[Companions_MAX_AGENTS];
  int32_t affected_count;
  int32_t affected_total;
} Companions_SkillUseInfo;

// Number of skill uses of the last step; 0 for a null env ("Invalid environment").
COMPANIONS_API int32_t companions_get_last_skill_use_count(const Companions_Env* env);
// The last step's skill use at `index` (0-based). False, `out` untouched, for
// a null env or `out` ("Invalid arguments") or an index out of range
// ("Skill use index out of range").
COMPANIONS_API bool companions_get_last_skill_use(const Companions_Env* env, int32_t index,
                                                  Companions_SkillUseInfo* out);

// =============================================================================
// Configuration Queries
// =============================================================================

COMPANIONS_API int32_t companions_get_rows(const Companions_Env* env);
COMPANIONS_API int32_t companions_get_cols(const Companions_Env* env);
COMPANIONS_API int32_t
companions_get_agent_count(const Companions_Env* env);
COMPANIONS_API int32_t companions_get_tick(const Companions_Env* env);
COMPANIONS_API bool companions_is_done(const Companions_Env* env);
COMPANIONS_API bool companions_is_success(const Companions_Env* env);
// Why the episode is done (Companions_End_None while companions_is_done is
// false, and for a null env). Fixed on the step (or lens change) where done
// becomes true: steps played on afterwards keep it. Reset and snapshot loads
// start a new episode with the env's own: None, or Horizon for a snapshot
// loaded at the horizon (done at once; the next step reports EpisodeEnd).
// A host that keeps playing on a task failure (a game layer) can tell
// Companions_End_TaskFailed from a time out. Since 1.2.1. Since 1.3, any env
// is also done as Companions_End_TeamDown when the team is down (max_downs
// downs, or every companion down at once): the level is lost.
COMPANIONS_API Companions_EndReason companions_get_end_reason(const Companions_Env* env);

// =============================================================================
// Semantic Annotations (task-specific tags on cells and agents)
// =============================================================================
//
// The physical CellKind enum is now restricted to terrain (Floor / Wall /
// Hazard / HealArea). Task-specific roles such as "synchro goal" and
// "aggro target" live in the annotation layer and flow through the DLL via
// the accessors below. See SemanticTag in annotations.h for the complete
// list of tag integer values (kept in sync with htn_bridge.py's
// SEMANTIC_TAG_NAMES).

#define COMPANIONS_MAX_ANNOTATION_PARAMS 160  // bytes, including null terminator

typedef struct {
  int32_t target_kind;          // 0 = Cell, 1 = Agent
  Companions_Position pos;      // Used when target_kind == 0
  Companions_ObjectId agent_id; // Used when target_kind == 1
  int32_t tag;                  // SemanticTag value (see annotations.h)
  int32_t owner_lens_id;        // -1 = persistent (external); otherwise lens id
  char params_json[COMPANIONS_MAX_ANNOTATION_PARAMS];  // Compact JSON "{\"k\":\"v\",...}"
} Companions_Annotation;

// Number of annotations attached to the env's state.
COMPANIONS_API int32_t companions_get_annotation_count(const Companions_Env* env);

// Fill `out` with up to `count` annotations. Caller should allocate at least
// `companions_get_annotation_count()` entries. Returns the number written.
COMPANIONS_API int32_t companions_get_annotations(
    const Companions_Env* env,
    Companions_Annotation* out,
    int32_t count);

// Test whether a given cell carries `tag` (a SemanticTag int value).
COMPANIONS_API bool companions_has_tag_at(
    const Companions_Env* env, int32_t row, int32_t col, int32_t tag);

// Test whether a given agent carries `tag`.
COMPANIONS_API bool companions_agent_has_tag(
    const Companions_Env* env, Companions_ObjectId agent_id, int32_t tag);

// =============================================================================
// Rendering
// =============================================================================

// Render current state as ASCII string
// Returns required buffer size (including null terminator)
// If out_buffer is NULL, just returns required size
// Uses ANSI color codes for terminal display
COMPANIONS_API int32_t companions_render_ascii(
    const Companions_Env* env,
    char* out_buffer,
    int32_t buffer_size);

// =============================================================================
// Utility
// =============================================================================

// Get library version string (see "Versioning" at the top)
COMPANIONS_API const char* companions_version(void);

// Get last error message (thread-local)
COMPANIONS_API const char* companions_get_error(void);

// =============================================================================
// Snapshot Save/Load
// =============================================================================

// Get the size of a serialized snapshot (call before save_snapshot)
// Returns 0 on error
COMPANIONS_API int32_t companions_get_snapshot_size(
    const Companions_Env* env);

// Save current state to a binary buffer
// Call get_snapshot_size first to determine buffer size
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_save_snapshot(
    const Companions_Env* env,
    uint8_t* out_buffer,
    int32_t buffer_size);

// Load state from a binary buffer
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_load_snapshot(
    Companions_Env* env,
    const uint8_t* data,
    int32_t data_size);

// =============================================================================
// Level Generation
// =============================================================================

// Configuration for level generation
// Set fields to 0/false to disable features
typedef struct {
  // Base map configuration
  int32_t rows;            // Grid rows (e.g., 12)
  int32_t cols;            // Grid cols (e.g., 12)
  int32_t map_complexity;  // 0=empty, 1=obstacles, 2+=rooms
  uint32_t seed;           // RNG seed for generation

  // SynchroEnv features
  int32_t synchro_cell_count;  // Number of synchro cells (0 = none)

  // AggroEnv features
  int32_t patrol_square_size;  // Patrol square size (0 = none, 3 = 3x3)
  bool has_target_cell;        // Place a target cell

  // Agents
  int32_t num_companions;  // Number of companion agents
  int32_t num_enemies;     // Number of FSM enemies (usually 0 or 1)

  // Episode
  int32_t horizon;      // Max steps (e.g., 100)
  int32_t d4_transform;  // D4 symmetry (0-7)
} Companions_LevelConfig;

// Generate a level from config (caches result, returns size)
// Returns 0 on error
COMPANIONS_API int32_t companions_generate_level(
    const Companions_LevelConfig* config);

// Get the generated level snapshot
// Call generate_level first to get size
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_get_generated_level(
    uint8_t* out_buffer,
    int32_t buffer_size);

// =============================================================================
// JSON Snapshot Save/Load
// =============================================================================

// Get current state as JSON string (human-readable, pretty-printed)
// Returns allocated string that must be freed with companions_free_string()
// Returns NULL on error (check companions_get_error)
COMPANIONS_API const char* companions_snapshot_to_json(
    const Companions_Env* env);

// Free a string allocated by companions_snapshot_to_json
COMPANIONS_API void companions_free_string(const char* str);

// Load state from JSON string
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_load_snapshot_json(
    Companions_Env* env,
    const char* json_str);

// Save current state to JSON file
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_save_snapshot_json(
    const Companions_Env* env,
    const char* filepath);

// Load state from JSON file
// Returns false on error (check companions_get_error)
COMPANIONS_API bool companions_load_snapshot_json_file(
    Companions_Env* env,
    const char* filepath);

#ifdef __cplusplus
}
#endif

#endif  // COMPANIONS_API_H_
