// Copyright 2024
// C API for external integration (game engines, FFI bindings)
// This is a SEPARATE API from the Python wrapper (synchro.h)
// All types are POD-only for safe DLL boundary crossing
//
// =============================================================================
// Versioning
// =============================================================================
// companions_version() is "1.1.0". 1.1 changed struct layouts
// (Companions_AgentState, Companions_Event, Companions_StepResult): consumers
// must be rebuilt against this header, never mixed with a 1.0 DLL or header.
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
// Currently implemented events, in this order within a step:
// - Companions_Event_AgentMoved: Agent moved to a new position (by walking,
//   or by a skill: a teleport, dash, push or pull)
// - Companions_Event_AgentBlocked: Agent tried to move but was blocked
// - Companions_Event_EffectSpawned: a companion cast (only with companion
//   casts on, see companions_set_companion_cast): subject_id = caster,
//   position = the faced cell, effect_name = "companion_cast".
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
// - Companions_Event_EpisodeEnd: Episode completed (success or timeout)
// A step reports at most Companions_MAX_EVENTS events, in the order above.
// When there are more, the ones past the cap are dropped, except EpisodeEnd:
// a step that ends the episode always reports it, as the last event (the
// others are then cut to Companions_MAX_EVENTS - 1). events_dropped counts
// the events not reported. The state itself (agents' tags, skills, statuses)
// is always complete.
//
// Not yet implemented (will be added as needed):
// - Companions_Event_AgentDamaged, Companions_Event_AgentHealed, Companions_Event_AgentDied
// - Companions_Event_FSMTransition, other EffectSpawned sources, etc.

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

// Skill1 / Skill2 use the skill in slot 0 / 1 (see companions_set_agent_skill):
// the companion stays put and its movement only aims. A skill that cannot be
// used (empty slot, unknown skill, cooldown, rooted for a self-moving skill)
// is dropped and the movement applies as with None. Any other value refuses
// the step (see companions_step).
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

  // Skill slots (companions only; "" = empty, and all "" / 0 for other
  // agents). Names always fit: the env refuses longer ones.
  char skills[Companions_MAX_SKILL_SLOTS][Companions_SKILL_NAME_LEN];
  int32_t skill_cooldowns[Companions_MAX_SKILL_SLOTS];  // Steps until usable, 0 = ready

  // Actions - intent (before collision resolution) vs actual (after)
  Companions_Action action_intent;   // What the agent wanted to do
  Companions_Action action_actual;   // What actually happened after validation
  bool action_succeeded;     // Did movement succeed? (intent == actual)
} Companions_AgentState;

// Cell state snapshot
typedef struct {
  Companions_Position position;
  Companions_CellKind kind;
  Companions_ObjectId occupant_id;  // Companions_INVALID_ID if empty
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
} Companions_EventType;

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

  // For AgentDamaged/AgentHealed:
  int32_t health_amount;
  int32_t health_new;
  Companions_ObjectId health_source_id;

  // For FSMTransition:
  Companions_FSMStateType fsm_from;
  Companions_FSMStateType fsm_to;

  // For Effect events (and SkillUsed: slot / skill name; TagApplied: tag id /
  // tag name, see "Event System" at the top):
  int32_t effect_id;
  char effect_name[Companions_EFFECT_NAME_LEN];

  // For Status events (and TagApplied: the tag's duration, -1 = permanent):
  Companions_StatusType status_type;
  int32_t status_duration;

  // For TagApplied (health_source_id = caster, -1 for a zone):
  bool tag_fresh;  // The agent did not carry the tag just before this landing

  // For EpisodeEnd:
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
// always available: "kill", "hit" (1 damage), "stun" (stunned, 3 ticks), plus
// enemy attacks and "companion_cast". Instant effects (no telegraph) apply
// immediately; telegraphed ones resolve over the next steps.
// source_id: agent immune to the effect (-1 for none).
// Returns false on error (unknown effect, out of bounds).
COMPANIONS_API bool companions_spawn_effect(Companions_Env* env,
                                            const char* effect_name,
                                            int32_t row, int32_t col,
                                            Companions_Direction direction,
                                            Companions_ObjectId source_id);

// Companion casts (default: off). The flag only concerns companions whose
// slot 0 is EMPTY: when on, such a companion whose action is
// Companions_Interact_Attack (= Skill1) stays put (the movement only aims)
// and casts "companion_cast" on the cell it faces; each cast yields a
// Companions_Event_EffectSpawned in its step result. When off, its Skill1
// acts as None (the movement applies). A companion with a skill in slot 0 uses that skill on Skill1
// whatever the flag says (see companions_set_agent_skill); RL envs are
// unaffected only because their slots start empty.
// The setting survives companions_reset and snapshot loads.
COMPANIONS_API void companions_set_companion_cast(Companions_Env* env, bool enabled);
COMPANIONS_API bool companions_get_companion_cast(const Companions_Env* env);

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
// unknown agent, a NULL / "" tag or one over 31 bytes, or a duration of 0 or
// below -1. Durations tick at the start of each step: applied between two
// steps with duration d, the tag is there now and after the next d - 1 steps
// (d = 1: gone after the next step).
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
// Skill slots (0-based). "" empties the slot; false for an unknown skill/slot/agent.
// Puts `skill` (a builtin: "fireball", "lightningStep", "teleport", "vortex",
// or one a JSON snapshot defines) in a companion's slot and makes it ready
// (cooldown 0). Only slot 0 is usable today (Companions_Interact_Skill1);
// slot 1 can be filled but Skill2 is ignored.
COMPANIONS_API bool companions_set_agent_skill(Companions_Env* env, Companions_ObjectId agent, int32_t slot, const char* skill);

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
