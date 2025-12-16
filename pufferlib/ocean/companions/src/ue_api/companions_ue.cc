// Copyright 2024
// C API implementation for Unreal Engine 5 integration

#include "companions_ue.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../core/cell.h"
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"
#include "../core/grid.h"
#include "../core/object.h"
#include "../core/object_manager.h"
#include "../core/types.h"
#include "../env/effect_system.h"
#include "../env/synchro_env.h"

// =============================================================================
// Version
// =============================================================================
#define COMPANIONS_UE_VERSION "1.0.0"

// =============================================================================
// Thread-local error message
// =============================================================================
static thread_local char g_error_buffer[256] = {0};

static void SetError(const char* msg) {
  std::strncpy(g_error_buffer, msg, sizeof(g_error_buffer) - 1);
  g_error_buffer[sizeof(g_error_buffer) - 1] = '\0';
}

// =============================================================================
// Internal Environment Wrapper
// =============================================================================
struct UE_CompanionsEnv {
  std::unique_ptr<companions::SynchroEnv> env;
  UE_EnvConfig config;
  bool done = false;
  bool success = false;
  std::vector<double> last_rewards;

  // Previous positions for tracking movement (for animation)
  std::vector<companions::Position> prev_positions;

  // Event buffer for current step
  std::vector<UE_Event> events;
};

// =============================================================================
// Conversion Helpers
// =============================================================================

static UE_Position ToUEPosition(companions::Position pos) {
  return {pos.row, pos.col};
}

static UE_Direction ToUEDirection(companions::Direction dir) {
  switch (dir) {
    case companions::Direction::Up:
      return UE_Direction_Up;
    case companions::Direction::Down:
      return UE_Direction_Down;
    case companions::Direction::Left:
      return UE_Direction_Left;
    case companions::Direction::Right:
      return UE_Direction_Right;
  }
  return UE_Direction_Down;
}

static UE_Faction ToUEFaction(companions::Faction faction) {
  switch (faction) {
    case companions::Faction::COMPANION:
      return UE_Faction_Companion;
    case companions::Faction::ENEMY:
      return UE_Faction_Enemy;
    case companions::Faction::NEUTRAL:
      return UE_Faction_Neutral;
  }
  return UE_Faction_Neutral;
}

static UE_ObjectType ToUEObjectType(companions::ObjectType type) {
  switch (type) {
    case companions::ObjectType::Object:
      return UE_ObjectType_Object;
    case companions::ObjectType::Actor:
      return UE_ObjectType_Actor;
    case companions::ObjectType::Agent:
      return UE_ObjectType_Agent;
    case companions::ObjectType::AgentFSM:
      return UE_ObjectType_AgentFSM;
    case companions::ObjectType::Companion:
      return UE_ObjectType_Companion;
    case companions::ObjectType::Player:
      return UE_ObjectType_Player;
    case companions::ObjectType::NPCCompanion:
      return UE_ObjectType_NPCCompanion;
  }
  return UE_ObjectType_Object;
}

static UE_CellKind ToUECellKind(companions::CellKind kind) {
  switch (kind) {
    case companions::CellKind::Floor:
      return UE_CellKind_Floor;
    case companions::CellKind::Wall:
      return UE_CellKind_Wall;
    case companions::CellKind::Hazard:
      return UE_CellKind_Hazard;
    case companions::CellKind::Synchro:
      return UE_CellKind_Synchro;
    case companions::CellKind::HealArea:
      return UE_CellKind_HealArea;
    case companions::CellKind::Target:
      return UE_CellKind_Target;
  }
  return UE_CellKind_Floor;
}

static UE_StatusType ToUEStatusType(companions::StatusType type) {
  switch (type) {
    case companions::StatusType::None:
      return UE_Status_None;
    case companions::StatusType::Stunned:
      return UE_Status_Stunned;
    case companions::StatusType::Slowed:
      return UE_Status_Slowed;
    case companions::StatusType::Marked:
      return UE_Status_Marked;
  }
  return UE_Status_None;
}

static UE_FSMStateType ToUEFSMState(const companions::FSMState* state) {
  if (!state) return UE_FSMState_None;

  std::string name = state->GetName();
  if (name == "Patrol") return UE_FSMState_Patrol;
  if (name == "Aggro") return UE_FSMState_Aggro;
  if (name == "ReturnToPatrol") return UE_FSMState_ReturnToPatrol;
  if (name == "Telegraph") return UE_FSMState_Telegraph;
  if (name == "Attack") return UE_FSMState_Attack;
  if (name == "Recovery") return UE_FSMState_Recovery;
  return UE_FSMState_None;
}

// Extract agent state from C++ Agent
static void ExtractAgentState(const companions::Agent* agent,
                              UE_AgentState* out,
                              companions::Position prev_pos) {
  out->id = agent->GetId();
  out->type = ToUEObjectType(agent->GetType());
  out->position = ToUEPosition(agent->GetPosition());
  out->prev_position = ToUEPosition(prev_pos);
  out->faction = ToUEFaction(agent->GetFaction());
  out->health = agent->GetHealth();
  out->max_health = agent->GetMaxHealth();
  out->alive = agent->IsAlive();
  out->agent_index = agent->GetAgentIndex();

  // Default values for non-companion types
  out->facing = UE_Direction_Down;

  // Check if it's a Companion (has direction)
  if (auto* companion =
          dynamic_cast<const companions::Companion*>(agent)) {
    out->facing = ToUEDirection(companion->GetDirection());
  }

  // FSM state (for AgentFSM types)
  out->fsm_state = UE_FSMState_None;
  out->fsm_target_id = UE_INVALID_ID;
  if (auto* fsm_agent =
          dynamic_cast<const companions::AgentFSM*>(agent)) {
    out->fsm_state = ToUEFSMState(fsm_agent->GetCurrentState());
    if (fsm_agent->HasFSM()) {
      out->fsm_target_id = fsm_agent->GetFSMContext().target_id;
    }
  }

  // Status effects
  const auto& statuses = agent->GetStatuses();
  out->status_count = std::min(static_cast<int>(statuses.size()),
                               UE_MAX_STATUSES);
  for (int i = 0; i < out->status_count; i++) {
    out->statuses[i].type = ToUEStatusType(statuses[i].type);
    out->statuses[i].duration = statuses[i].duration;
  }

  // Actions - intent (before collision resolution) vs actual (after)
  auto intent = agent->GetOriginalIntention();
  out->action_intent.movement =
      static_cast<UE_MovementAction>(intent.movement);
  out->action_intent.interact =
      static_cast<UE_InteractAction>(intent.interact);

  auto actual = agent->GetIntention();
  out->action_actual.movement =
      static_cast<UE_MovementAction>(actual.movement);
  out->action_actual.interact =
      static_cast<UE_InteractAction>(actual.interact);

  // Action succeeded if intent matches actual (movement wasn't blocked)
  out->action_succeeded = (intent.movement == actual.movement);
}

// Extract full game state
static void ExtractGameState(const UE_CompanionsEnv* wrapper,
                             UE_GameState* out) {
  auto* env = wrapper->env.get();

  out->rows = env->GetRows();
  out->cols = env->GetCols();
  out->tick = env->GetTick();

  // Extract agents
  const auto& obj_mgr = env->GetObjectManager();
  auto agents = obj_mgr.GetAllAgents();
  out->agent_count = std::min(static_cast<int>(agents.size()),
                              static_cast<int>(UE_MAX_AGENTS));

  for (int i = 0; i < out->agent_count; i++) {
    companions::Position prev_pos = agents[i]->GetPosition();
    if (i < static_cast<int>(wrapper->prev_positions.size())) {
      prev_pos = wrapper->prev_positions[i];
    }
    ExtractAgentState(agents[i], &out->agents[i], prev_pos);
  }

  // Extract special cells (non-Floor)
  const auto& grid = env->GetGrid();
  out->special_cell_count = 0;
  for (int r = 0; r < out->rows && out->special_cell_count < UE_MAX_SPECIAL_CELLS; r++) {
    for (int c = 0; c < out->cols && out->special_cell_count < UE_MAX_SPECIAL_CELLS; c++) {
      const auto& cell = grid.GetCell(r, c);
      if (cell.GetKind() != companions::CellKind::Floor) {
        auto& sc = out->special_cells[out->special_cell_count++];
        sc.position = {r, c};
        sc.kind = ToUECellKind(cell.GetKind());
        // Check for occupant
        auto* actor = obj_mgr.GetActorAt({r, c});
        sc.occupant_id = actor ? actor->GetId() : UE_INVALID_ID;
      }
    }
  }

  // Extract active effects
  const auto& effects = env->GetActiveEffects();
  out->effect_count = std::min(static_cast<int>(effects.size()),
                               static_cast<int>(UE_MAX_EFFECTS));
  for (int i = 0; i < out->effect_count; i++) {
    auto& ue_effect = out->effects[i];
    const auto& effect = effects[i];

    // Generate a simple ID from index
    ue_effect.effect_id = i;
    // Get name from config
    if (effect.config) {
      std::strncpy(ue_effect.effect_name, effect.config->name.c_str(),
                   UE_EFFECT_NAME_LEN - 1);
    } else {
      ue_effect.effect_name[0] = '\0';
    }
    ue_effect.effect_name[UE_EFFECT_NAME_LEN - 1] = '\0';
    ue_effect.center = ToUEPosition(effect.GetCenter(obj_mgr));
    ue_effect.direction = ToUEDirection(effect.direction);
    ue_effect.ticks_remaining = effect.ticks_remaining;
    ue_effect.in_telegraph = effect.in_telegraph;
    ue_effect.source_id = effect.source_id;

    // For now, leave affected_count as 0 (would need to compute from config)
    ue_effect.affected_count = 0;
  }

  // Episode status
  out->done = wrapper->done;
  out->success = wrapper->success;
  for (int i = 0; i < out->agent_count; i++) {
    out->rewards[i] = static_cast<float>(
        i < static_cast<int>(wrapper->last_rewards.size())
            ? wrapper->last_rewards[i]
            : 0.0);
  }
}

// =============================================================================
// Event Generation
// =============================================================================

static void AddMovementEvents(UE_CompanionsEnv* wrapper) {
  auto* env = wrapper->env.get();
  auto agents = env->GetObjectManager().GetAllAgents();

  for (size_t i = 0; i < agents.size() && i < wrapper->prev_positions.size(); i++) {
    auto prev = wrapper->prev_positions[i];
    auto curr = agents[i]->GetPosition();

    if (prev != curr) {
      // Agent moved
      UE_Event evt = {};
      evt.type = UE_Event_AgentMoved;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToUEPosition(curr);
      evt.from_pos = ToUEPosition(prev);
      evt.to_pos = ToUEPosition(curr);
      evt.move_action = static_cast<UE_MovementAction>(
          agents[i]->GetIntention().movement);
      wrapper->events.push_back(evt);
    } else if (agents[i]->GetIntention().movement !=
               companions::MovementAction::Stay) {
      // Tried to move but was blocked
      UE_Event evt = {};
      evt.type = UE_Event_AgentBlocked;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToUEPosition(curr);
      evt.from_pos = ToUEPosition(prev);
      auto intended = companions::ApplyMovement(
          prev, agents[i]->GetIntention().movement);
      evt.to_pos = ToUEPosition(intended);
      evt.move_action = static_cast<UE_MovementAction>(
          agents[i]->GetIntention().movement);
      wrapper->events.push_back(evt);
    }
  }
}

// =============================================================================
// Public API Implementation
// =============================================================================

extern "C" {

COMPANIONS_UE_API UE_CompanionsEnv* ue_companions_create(
    const UE_EnvConfig* config) {
  if (!config) {
    SetError("Config is null");
    return nullptr;
  }

  auto* wrapper = new UE_CompanionsEnv();
  wrapper->config = *config;

  try {
    wrapper->env = std::make_unique<companions::SynchroEnv>(
        config->rows,
        config->cols,
        config->num_companions,
        config->num_synchro,
        config->map_complexity,
        config->seed,
        config->d4_transform,
        config->horizon);

    wrapper->last_rewards.resize(config->num_companions, 0.0);
    wrapper->prev_positions.resize(config->num_companions);

  } catch (const std::exception& e) {
    SetError(e.what());
    delete wrapper;
    return nullptr;
  }

  return wrapper;
}

COMPANIONS_UE_API void ue_companions_destroy(UE_CompanionsEnv* env) {
  delete env;
}

COMPANIONS_UE_API void ue_companions_reset(UE_CompanionsEnv* env,
                                           uint32_t seed) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return;
  }

  env->env->Reset(seed);
  env->done = false;
  env->success = false;
  std::fill(env->last_rewards.begin(), env->last_rewards.end(), 0.0);
  env->events.clear();

  // Store initial positions
  auto agents = env->env->GetObjectManager().GetAllAgents();
  env->prev_positions.resize(agents.size());
  for (size_t i = 0; i < agents.size(); i++) {
    env->prev_positions[i] = agents[i]->GetPosition();
  }
}

COMPANIONS_UE_API void ue_companions_step(UE_CompanionsEnv* env,
                                          const UE_Action* actions,
                                          int32_t action_count,
                                          UE_StepResult* out_result) {
  if (!env || !env->env || !actions || !out_result) {
    SetError("Invalid arguments");
    return;
  }

  // Store previous positions for event generation
  auto agents = env->env->GetObjectManager().GetAllAgents();
  env->prev_positions.resize(agents.size());
  for (size_t i = 0; i < agents.size(); i++) {
    env->prev_positions[i] = agents[i]->GetPosition();
  }

  // Validate and convert actions to C++ format
  std::vector<companions::Action> cpp_actions(action_count);
  for (int i = 0; i < action_count; i++) {
    // Validate movement action range
    if (actions[i].movement < UE_Movement_Stay ||
        actions[i].movement > UE_Movement_Right) {
      SetError("Invalid movement action");
      return;
    }
    // Validate interact action range
    if (actions[i].interact < UE_Interact_None ||
        actions[i].interact > UE_Interact_Attack) {
      SetError("Invalid interact action");
      return;
    }
    cpp_actions[i] = companions::EncodeAction(
        static_cast<companions::MovementAction>(actions[i].movement),
        static_cast<companions::InteractAction>(actions[i].interact));
  }

  // Clear events from previous step
  env->events.clear();

  // Step the environment
  auto result = env->env->Step(cpp_actions);

  // Update wrapper state
  env->done = result.done;
  env->success = env->env->IsSuccess();
  env->last_rewards = result.rewards;

  // Generate movement events
  AddMovementEvents(env);

  // Add episode end event if done
  if (env->done) {
    UE_Event evt = {};
    evt.type = UE_Event_EpisodeEnd;
    evt.tick = env->env->GetTick();
    evt.episode_success = env->success;
    evt.episode_reward = 0.0f;
    for (double r : env->last_rewards) {
      evt.episode_reward += static_cast<float>(r);
    }
    evt.episode_steps = env->env->GetTick();
    env->events.push_back(evt);
  }

  // Extract state
  ExtractGameState(env, &out_result->state);

  // Copy events
  out_result->event_count = std::min(static_cast<int>(env->events.size()),
                                     UE_MAX_EVENTS);
  for (int i = 0; i < out_result->event_count; i++) {
    out_result->events[i] = env->events[i];
  }
}

COMPANIONS_UE_API void ue_companions_get_state(const UE_CompanionsEnv* env,
                                               UE_GameState* out_state) {
  if (!env || !env->env || !out_state) {
    SetError("Invalid arguments");
    return;
  }

  ExtractGameState(env, out_state);
}

COMPANIONS_UE_API bool ue_companions_get_agent(const UE_CompanionsEnv* env,
                                               UE_ObjectId id,
                                               UE_AgentState* out_agent) {
  if (!env || !env->env || !out_agent) {
    SetError("Invalid arguments");
    return false;
  }

  auto agents = env->env->GetObjectManager().GetAllAgents();
  for (size_t i = 0; i < agents.size(); i++) {
    if (agents[i]->GetId() == id) {
      companions::Position prev = agents[i]->GetPosition();
      if (i < env->prev_positions.size()) {
        prev = env->prev_positions[i];
      }
      ExtractAgentState(agents[i], out_agent, prev);
      return true;
    }
  }

  SetError("Agent not found");
  return false;
}

COMPANIONS_UE_API bool ue_companions_get_agent_by_index(
    const UE_CompanionsEnv* env,
    int32_t index,
    UE_AgentState* out_agent) {
  if (!env || !env->env || !out_agent) {
    SetError("Invalid arguments");
    return false;
  }

  auto agents = env->env->GetObjectManager().GetAllAgents();
  if (index < 0 || index >= static_cast<int>(agents.size())) {
    SetError("Index out of range");
    return false;
  }

  companions::Position prev = agents[index]->GetPosition();
  if (static_cast<size_t>(index) < env->prev_positions.size()) {
    prev = env->prev_positions[index];
  }
  ExtractAgentState(agents[index], out_agent, prev);
  return true;
}

COMPANIONS_UE_API UE_CellKind ue_companions_get_cell(
    const UE_CompanionsEnv* env,
    int32_t row,
    int32_t col) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return UE_CellKind_Floor;
  }

  if (row < 0 || row >= env->env->GetRows() ||
      col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return UE_CellKind_Wall;
  }

  return ToUECellKind(env->env->GetGrid().GetCell(row, col).GetKind());
}

COMPANIONS_UE_API void ue_companions_get_grid(const UE_CompanionsEnv* env,
                                              UE_CellKind* out_grid) {
  if (!env || !env->env || !out_grid) {
    SetError("Invalid arguments");
    return;
  }

  const auto& grid = env->env->GetGrid();
  int rows = env->env->GetRows();
  int cols = env->env->GetCols();

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      out_grid[r * cols + c] = ToUECellKind(grid.GetCell(r, c).GetKind());
    }
  }
}

COMPANIONS_UE_API int32_t ue_companions_get_rows(const UE_CompanionsEnv* env) {
  return env && env->env ? env->env->GetRows() : 0;
}

COMPANIONS_UE_API int32_t ue_companions_get_cols(const UE_CompanionsEnv* env) {
  return env && env->env ? env->env->GetCols() : 0;
}

COMPANIONS_UE_API int32_t ue_companions_get_agent_count(
    const UE_CompanionsEnv* env) {
  return env && env->env ? env->env->NumAgents() : 0;
}

COMPANIONS_UE_API int32_t ue_companions_get_tick(const UE_CompanionsEnv* env) {
  return env && env->env ? env->env->GetTick() : 0;
}

COMPANIONS_UE_API bool ue_companions_is_done(const UE_CompanionsEnv* env) {
  return env ? env->done : true;
}

COMPANIONS_UE_API bool ue_companions_is_success(const UE_CompanionsEnv* env) {
  return env ? env->success : false;
}

COMPANIONS_UE_API const char* ue_companions_version(void) {
  return COMPANIONS_UE_VERSION;
}

COMPANIONS_UE_API const char* ue_companions_get_error(void) {
  return g_error_buffer;
}

}  // extern "C"
