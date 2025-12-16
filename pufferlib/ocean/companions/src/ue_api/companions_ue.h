// Copyright 2024
// C API for Unreal Engine 5 integration
// This is a SEPARATE API from the Python wrapper (synchro.h)
// All types are POD-only for safe DLL boundary crossing
//
// =============================================================================
// Thread Safety
// =============================================================================
// - Each UE_CompanionsEnv instance is NOT thread-safe. Do not call methods
//   on the same environment from multiple threads simultaneously.
// - Different UE_CompanionsEnv instances can be used concurrently from
//   different threads.
// - ue_companions_get_error() uses thread-local storage and is thread-safe.
// - ue_companions_reset() and ue_companions_step() must not be called
//   concurrently on the same environment.
//
// =============================================================================
// Error Handling
// =============================================================================
// On error, functions return false/zero/default values and set an error
// string retrievable via ue_companions_get_error().
//
// =============================================================================
// Event System (Partial Implementation)
// =============================================================================
// Currently implemented events:
// - UE_Event_AgentMoved: Agent successfully moved to new position
// - UE_Event_AgentBlocked: Agent tried to move but was blocked
// - UE_Event_EpisodeEnd: Episode completed (success or timeout)
//
// Not yet implemented (will be added as needed):
// - UE_Event_AgentDamaged, UE_Event_AgentHealed, UE_Event_AgentDied
// - UE_Event_FSMTransition, UE_Event_EffectSpawned, etc.

#ifndef COMPANIONS_UE_API_H_
#define COMPANIONS_UE_API_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// DLL Export Macros
// =============================================================================
#ifdef _WIN32
#ifdef COMPANIONS_UE_EXPORTS
#define COMPANIONS_UE_API __declspec(dllexport)
#else
#define COMPANIONS_UE_API __declspec(dllimport)
#endif
#else
#define COMPANIONS_UE_API __attribute__((visibility("default")))
#endif

// =============================================================================
// Constants
// =============================================================================
#define UE_INVALID_ID (-1)
#define UE_MAX_AGENTS 8
#define UE_MAX_EFFECTS 32
#define UE_MAX_EVENTS 64
#define UE_MAX_STATUSES 4
#define UE_MAX_SPECIAL_CELLS 64
#define UE_MAX_EFFECT_CELLS 16
#define UE_EFFECT_NAME_LEN 32

// =============================================================================
// Basic Types
// =============================================================================
typedef int32_t UE_ObjectId;

typedef struct {
  int32_t row;
  int32_t col;
} UE_Position;

// =============================================================================
// Enums (matching companions::* enums)
// =============================================================================
typedef enum {
  UE_Direction_Up = 0,
  UE_Direction_Down = 1,
  UE_Direction_Left = 2,
  UE_Direction_Right = 3,
} UE_Direction;

typedef enum {
  UE_Movement_Stay = 0,
  UE_Movement_Up = 1,
  UE_Movement_Down = 2,
  UE_Movement_Left = 3,
  UE_Movement_Right = 4,
} UE_MovementAction;

typedef enum {
  UE_Interact_None = 0,
  UE_Interact_Attack = 1,
} UE_InteractAction;

typedef enum {
  UE_Faction_Companion = 0,
  UE_Faction_Enemy = 1,
  UE_Faction_Neutral = 2,
} UE_Faction;

typedef enum {
  UE_ObjectType_Object = 0,
  UE_ObjectType_Actor = 1,
  UE_ObjectType_Agent = 2,
  UE_ObjectType_AgentFSM = 3,
  UE_ObjectType_Companion = 4,
  UE_ObjectType_Player = 5,
  UE_ObjectType_NPCCompanion = 6,
} UE_ObjectType;

typedef enum {
  UE_CellKind_Floor = 0,
  UE_CellKind_Wall = 1,
  UE_CellKind_Hazard = 2,
  UE_CellKind_Synchro = 3,
  UE_CellKind_HealArea = 4,
  UE_CellKind_Target = 5,
} UE_CellKind;

typedef enum {
  UE_FSMState_None = 0,
  UE_FSMState_Patrol = 1,
  UE_FSMState_Aggro = 2,
  UE_FSMState_ReturnToPatrol = 3,
  UE_FSMState_Telegraph = 4,
  UE_FSMState_Attack = 5,
  UE_FSMState_Recovery = 6,
} UE_FSMStateType;

typedef enum {
  UE_Status_None = 0,
  UE_Status_Stunned = 1,
  UE_Status_Slowed = 2,
  UE_Status_Marked = 3,
} UE_StatusType;

// =============================================================================
// Composite Structs
// =============================================================================

// Action input from UE5
typedef struct {
  UE_MovementAction movement;
  UE_InteractAction interact;
} UE_Action;

// Status effect on an agent
typedef struct {
  UE_StatusType type;
  int32_t duration;  // ticks remaining
} UE_StatusEffect;

// Agent state snapshot
typedef struct {
  UE_ObjectId id;
  UE_ObjectType type;
  UE_Position position;
  UE_Position prev_position;  // Position before this step (for animation)
  UE_Direction facing;
  UE_Faction faction;
  int32_t health;
  int32_t max_health;
  bool alive;
  int32_t agent_index;  // Index in action array

  // FSM state (for AgentFSM types)
  UE_FSMStateType fsm_state;
  UE_ObjectId fsm_target_id;  // Who is being chased (if aggro)

  // Status effects
  UE_StatusEffect statuses[UE_MAX_STATUSES];
  int32_t status_count;

  // Actions - intent (before collision resolution) vs actual (after)
  UE_Action action_intent;   // What the agent wanted to do
  UE_Action action_actual;   // What actually happened after validation
  bool action_succeeded;     // Did movement succeed? (intent == actual)
} UE_AgentState;

// Cell state snapshot
typedef struct {
  UE_Position position;
  UE_CellKind kind;
  UE_ObjectId occupant_id;  // UE_INVALID_ID if empty
} UE_CellState;

// Active effect instance
typedef struct {
  int32_t effect_id;               // Runtime instance ID
  char effect_name[UE_EFFECT_NAME_LEN];
  UE_Position center;
  UE_Direction direction;
  int32_t ticks_remaining;
  bool in_telegraph;  // true = warning, false = active damage
  UE_ObjectId source_id;

  // Affected area (relative positions from center)
  UE_Position affected_cells[UE_MAX_EFFECT_CELLS];
  int32_t affected_count;
} UE_ActiveEffect;

// =============================================================================
// Event Types (for transition/animation events)
// =============================================================================
typedef enum {
  UE_Event_None = 0,
  UE_Event_AgentMoved = 1,
  UE_Event_AgentBlocked = 2,  // Movement was blocked
  UE_Event_AgentDamaged = 3,
  UE_Event_AgentHealed = 4,
  UE_Event_AgentDied = 5,
  UE_Event_AgentSpawned = 6,
  UE_Event_FSMTransition = 7,
  UE_Event_EffectSpawned = 8,
  UE_Event_EffectActivated = 9,
  UE_Event_EffectEnded = 10,
  UE_Event_StatusApplied = 11,
  UE_Event_StatusRemoved = 12,
  UE_Event_GoalReached = 13,
  UE_Event_EpisodeEnd = 14,
} UE_EventType;

// Transition event (delta information for animations)
typedef struct {
  UE_EventType type;
  int32_t tick;  // When it happened

  // Subject of the event
  UE_ObjectId subject_id;
  UE_Position position;

  // Event-specific data (union-like via fields, C99 compatible)
  // For AgentMoved/AgentBlocked:
  UE_Position from_pos;
  UE_Position to_pos;
  UE_MovementAction move_action;

  // For AgentDamaged/AgentHealed:
  int32_t health_amount;
  int32_t health_new;
  UE_ObjectId health_source_id;

  // For FSMTransition:
  UE_FSMStateType fsm_from;
  UE_FSMStateType fsm_to;

  // For Effect events:
  int32_t effect_id;
  char effect_name[UE_EFFECT_NAME_LEN];

  // For Status events:
  UE_StatusType status_type;
  int32_t status_duration;

  // For EpisodeEnd:
  bool episode_success;
  float episode_reward;
  int32_t episode_steps;
} UE_Event;

// =============================================================================
// Game State Snapshot
// =============================================================================
typedef struct {
  // Grid dimensions
  int32_t rows;
  int32_t cols;
  int32_t tick;

  // Agents
  UE_AgentState agents[UE_MAX_AGENTS];
  int32_t agent_count;

  // Special cells (non-Floor cells for efficient iteration)
  UE_CellState special_cells[UE_MAX_SPECIAL_CELLS];
  int32_t special_cell_count;

  // Active effects
  UE_ActiveEffect effects[UE_MAX_EFFECTS];
  int32_t effect_count;

  // Episode status
  bool done;
  bool success;
  float rewards[UE_MAX_AGENTS];
} UE_GameState;

// =============================================================================
// Step Result (state + transition events)
// =============================================================================
typedef struct {
  UE_GameState state;

  // Events that occurred during this step
  UE_Event events[UE_MAX_EVENTS];
  int32_t event_count;
} UE_StepResult;

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
} UE_EnvConfig;

// =============================================================================
// Opaque Handle
// =============================================================================
typedef struct UE_CompanionsEnv UE_CompanionsEnv;

// =============================================================================
// Lifecycle Functions
// =============================================================================

// Create a new SynchroEnv instance
COMPANIONS_UE_API UE_CompanionsEnv* ue_companions_create(
    const UE_EnvConfig* config);

// Destroy environment and free resources
COMPANIONS_UE_API void ue_companions_destroy(UE_CompanionsEnv* env);

// =============================================================================
// Environment Control
// =============================================================================

// Reset environment with new seed
COMPANIONS_UE_API void ue_companions_reset(UE_CompanionsEnv* env,
                                           uint32_t seed);

// Step environment with actions, returns full result with events
// actions array must have env->agent_count elements
COMPANIONS_UE_API void ue_companions_step(UE_CompanionsEnv* env,
                                          const UE_Action* actions,
                                          int32_t action_count,
                                          UE_StepResult* out_result);

// =============================================================================
// State Queries
// =============================================================================

// Get current game state snapshot (no events)
COMPANIONS_UE_API void ue_companions_get_state(const UE_CompanionsEnv* env,
                                               UE_GameState* out_state);

// Get specific agent state by ID
COMPANIONS_UE_API bool ue_companions_get_agent(const UE_CompanionsEnv* env,
                                               UE_ObjectId id,
                                               UE_AgentState* out_agent);

// Get specific agent state by index
COMPANIONS_UE_API bool ue_companions_get_agent_by_index(
    const UE_CompanionsEnv* env, int32_t index, UE_AgentState* out_agent);

// Get cell kind at position
COMPANIONS_UE_API UE_CellKind ue_companions_get_cell(
    const UE_CompanionsEnv* env, int32_t row, int32_t col);

// Get full grid as flat array (row-major order)
// out_grid must have rows * cols elements
COMPANIONS_UE_API void ue_companions_get_grid(const UE_CompanionsEnv* env,
                                              UE_CellKind* out_grid);

// =============================================================================
// Configuration Queries
// =============================================================================

COMPANIONS_UE_API int32_t ue_companions_get_rows(const UE_CompanionsEnv* env);
COMPANIONS_UE_API int32_t ue_companions_get_cols(const UE_CompanionsEnv* env);
COMPANIONS_UE_API int32_t
ue_companions_get_agent_count(const UE_CompanionsEnv* env);
COMPANIONS_UE_API int32_t ue_companions_get_tick(const UE_CompanionsEnv* env);
COMPANIONS_UE_API bool ue_companions_is_done(const UE_CompanionsEnv* env);
COMPANIONS_UE_API bool ue_companions_is_success(const UE_CompanionsEnv* env);

// =============================================================================
// Utility
// =============================================================================

// Get library version string
COMPANIONS_UE_API const char* ue_companions_version(void);

// Get last error message (thread-local)
COMPANIONS_UE_API const char* ue_companions_get_error(void);

#ifdef __cplusplus
}
#endif

#endif  // COMPANIONS_UE_API_H_
