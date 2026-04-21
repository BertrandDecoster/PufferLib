// Copyright 2024
// C API for external integration (game engines, FFI bindings)
// This is a SEPARATE API from the Python wrapper (synchro.h)
// All types are POD-only for safe DLL boundary crossing
//
// =============================================================================
// Thread Safety
// =============================================================================
// - Each Companions_Env instance is NOT thread-safe. Do not call methods
//   on the same environment from multiple threads simultaneously.
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
// Currently implemented events:
// - Companions_Event_AgentMoved: Agent successfully moved to new position
// - Companions_Event_AgentBlocked: Agent tried to move but was blocked
// - Companions_Event_EpisodeEnd: Episode completed (success or timeout)
//
// Not yet implemented (will be added as needed):
// - Companions_Event_AgentDamaged, Companions_Event_AgentHealed, Companions_Event_AgentDied
// - Companions_Event_FSMTransition, Companions_Event_EffectSpawned, etc.

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

typedef enum {
  Companions_Interact_None = 0,
  Companions_Interact_Attack = 1,
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
  Companions_CellKind_Synchro = 3,
  Companions_CellKind_HealArea = 4,
  Companions_CellKind_Target = 5,
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
  Companions_Status_Slowed = 2,
  Companions_Status_Marked = 3,
} Companions_StatusType;

typedef enum {
  Companions_Enemy_Zombie = 0,
  Companions_Enemy_Archer = 1,
  Companions_Enemy_Mage = 2,
} Companions_EnemyType;

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

// Agent state snapshot
typedef struct {
  Companions_ObjectId id;
  Companions_ObjectType type;
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

  // For Effect events:
  int32_t effect_id;
  char effect_name[Companions_EFFECT_NAME_LEN];

  // For Status events:
  Companions_StatusType status_type;
  int32_t status_duration;

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
    Companions_Lens_TagApply = 3
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

// Get library version string
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
