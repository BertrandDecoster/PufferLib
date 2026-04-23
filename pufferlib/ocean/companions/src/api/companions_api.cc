// Copyright 2024
// C API implementation for external integration

#include "companions_api.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../core/annotations.h"
#include "../core/cell.h"
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"
#include "../core/grid.h"
#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "../core/fsm/enemies.h"
#include "../core/object.h"
#include "../core/object_manager.h"
#include "../core/snapshot.h"
#include "../core/snapshot_json.h"
#include "../core/types.h"
#include "../env/effect_system.h"
#include "../env/aggro_env.h"
#include "../env/synchro_env.h"
#include "../env/task_lens.h"
#include "../env/synchro_lens.h"
#include "../env/aggro_lens.h"
#include "../env/dodge_lens.h"
#include "../viz/renderer.h"

// =============================================================================
// Version
// =============================================================================
#define COMPANIONS_VERSION "1.0.0"

// =============================================================================
// Thread-local error message
// =============================================================================
static thread_local char g_error_buffer[256] = {0};

static void SetError(const char* msg) {
  std::strncpy(g_error_buffer, msg, sizeof(g_error_buffer) - 1);
  g_error_buffer[sizeof(g_error_buffer) - 1] = '\0';
}

// =============================================================================
// Thread-local generated level cache
// =============================================================================
static thread_local std::vector<uint8_t> g_generated_level;

// =============================================================================
// Internal Environment Wrapper
// =============================================================================
struct Companions_Env {
  std::unique_ptr<companions::BaseEnv> env;
  Companions_EnvConfig config;
  bool done = false;
  bool success = false;
  std::vector<double> last_rewards;

  // Previous positions for tracking movement (for animation)
  std::vector<companions::Position> prev_positions;

  // Event buffer for current step
  std::vector<Companions_Event> events;

  // Cached snapshot for the two-call save pattern. `mutable` because
  // companions_get_snapshot_size is declared `const Companions_Env*` on the
  // C API boundary, yet must populate the cache to return its size.
  // Audit F13.
  mutable std::vector<uint8_t> cached_snapshot;
};

// =============================================================================
// Conversion Helpers
// =============================================================================

static Companions_Position ToAPIPosition(companions::Position pos) {
  return {pos.row, pos.col};
}

static Companions_Direction ToAPIDirection(companions::Direction dir) {
  switch (dir) {
    case companions::Direction::Up:
      return Companions_Direction_Up;
    case companions::Direction::Down:
      return Companions_Direction_Down;
    case companions::Direction::Left:
      return Companions_Direction_Left;
    case companions::Direction::Right:
      return Companions_Direction_Right;
  }
  return Companions_Direction_Down;
}

static Companions_Faction ToAPIFaction(companions::Faction faction) {
  switch (faction) {
    case companions::Faction::COMPANION:
      return Companions_Faction_Companion;
    case companions::Faction::ENEMY:
      return Companions_Faction_Enemy;
    case companions::Faction::NEUTRAL:
      return Companions_Faction_Neutral;
  }
  return Companions_Faction_Neutral;
}

static Companions_ObjectType ToAPIObjectType(companions::ObjectType type) {
  switch (type) {
    case companions::ObjectType::Object:
      return Companions_ObjectType_Object;
    case companions::ObjectType::Actor:
      return Companions_ObjectType_Actor;
    case companions::ObjectType::Agent:
      return Companions_ObjectType_Agent;
    case companions::ObjectType::AgentFSM:
      return Companions_ObjectType_AgentFSM;
    case companions::ObjectType::Companion:
      return Companions_ObjectType_Companion;
    case companions::ObjectType::Player:
      return Companions_ObjectType_Player;
    case companions::ObjectType::NPCCompanion:
      return Companions_ObjectType_NPCCompanion;
  }
  return Companions_ObjectType_Object;
}

static Companions_CellKind ToAPICellKind(companions::CellKind kind) {
  switch (kind) {
    case companions::CellKind::Floor:
      return Companions_CellKind_Floor;
    case companions::CellKind::Wall:
      return Companions_CellKind_Wall;
    case companions::CellKind::Hazard:
      return Companions_CellKind_Hazard;
    case companions::CellKind::HealArea:
      return Companions_CellKind_HealArea;
  }
  return Companions_CellKind_Floor;
}

static Companions_StatusType ToAPIStatusType(companions::StatusType type) {
  switch (type) {
    case companions::StatusType::None:
      return Companions_Status_None;
    case companions::StatusType::Stunned:
      return Companions_Status_Stunned;
    case companions::StatusType::Slowed:
      return Companions_Status_Slowed;
    case companions::StatusType::Marked:
      return Companions_Status_Marked;
  }
  return Companions_Status_None;
}

// companions::AgentKind is numerically 1:1 with Companions_AgentKind by
// construction — these asserts fire at compile time if someone adds a value
// to one enum and forgets the other.
static_assert(static_cast<int32_t>(companions::AgentKind::Unknown) ==
                  Companions_AgentKind_Unknown,
              "AgentKind::Unknown out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::Companion) ==
                  Companions_AgentKind_Companion,
              "AgentKind::Companion out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::NeutralNpc) ==
                  Companions_AgentKind_NeutralNpc,
              "AgentKind::NeutralNpc out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyZombie) ==
                  Companions_AgentKind_EnemyZombie,
              "AgentKind::EnemyZombie out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyGoblin) ==
                  Companions_AgentKind_EnemyGoblin,
              "AgentKind::EnemyGoblin out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyDragon) ==
                  Companions_AgentKind_EnemyDragon,
              "AgentKind::EnemyDragon out of sync with C API");

static Companions_AgentKind ToAPIAgentKind(const companions::Agent* agent) {
  return static_cast<Companions_AgentKind>(agent->GetAgentKind());
}

static Companions_FSMStateType ToAPIFSMState(const companions::FSMState* state) {
  if (!state) return Companions_FSMState_None;

  std::string name = state->GetName();
  if (name == "Patrol") return Companions_FSMState_Patrol;
  if (name == "Aggro") return Companions_FSMState_Aggro;
  if (name == "ReturnToPatrol") return Companions_FSMState_ReturnToPatrol;
  if (name == "Telegraph") return Companions_FSMState_Telegraph;
  if (name == "Attack") return Companions_FSMState_Attack;
  if (name == "Recovery") return Companions_FSMState_Recovery;
  return Companions_FSMState_None;
}

// Extract agent state from C++ Agent
static void ExtractAgentState(const companions::Agent* agent,
                              Companions_AgentState* out,
                              companions::Position prev_pos) {
  out->id = agent->GetId();
  out->type = ToAPIObjectType(agent->GetType());
  out->kind = ToAPIAgentKind(agent);
  out->position = ToAPIPosition(agent->GetPosition());
  out->prev_position = ToAPIPosition(prev_pos);
  out->faction = ToAPIFaction(agent->GetFaction());
  out->health = agent->GetHealth();
  out->max_health = agent->GetMaxHealth();
  out->alive = agent->IsAlive();
  out->agent_index = agent->GetAgentIndex();

  // Default values for non-companion types
  out->facing = Companions_Direction_Down;

  // Check if it's a Companion (has direction)
  if (auto* companion =
          dynamic_cast<const companions::Companion*>(agent)) {
    out->facing = ToAPIDirection(companion->GetDirection());
  }

  // FSM state (for AgentFSM types)
  out->fsm_state = Companions_FSMState_None;
  out->fsm_target_id = Companions_INVALID_ID;
  if (auto* fsm_agent =
          dynamic_cast<const companions::AgentFSM*>(agent)) {
    out->fsm_state = ToAPIFSMState(fsm_agent->GetCurrentState());
    if (fsm_agent->HasFSM()) {
      out->fsm_target_id = fsm_agent->GetFSMContext().target_id;
    }
  }

  // Status effects
  const auto& statuses = agent->GetStatuses();
  out->status_count = std::min(static_cast<int>(statuses.size()),
                               Companions_MAX_STATUSES);
  for (int i = 0; i < out->status_count; i++) {
    out->statuses[i].type = ToAPIStatusType(statuses[i].type);
    out->statuses[i].duration = statuses[i].duration;
  }

  // Actions - intent (before collision resolution) vs actual (after)
  auto intent = agent->GetOriginalIntention();
  out->action_intent.movement =
      static_cast<Companions_MovementAction>(intent.movement);
  out->action_intent.interact =
      static_cast<Companions_InteractAction>(intent.interact);

  auto actual = agent->GetExecutedAction();
  out->action_actual.movement =
      static_cast<Companions_MovementAction>(actual.movement);
  out->action_actual.interact =
      static_cast<Companions_InteractAction>(actual.interact);

  // Action succeeded if intent matches actual (movement wasn't blocked)
  out->action_succeeded = (intent.movement == actual.movement);
}

// Extract full game state
static void ExtractGameState(const Companions_Env* wrapper,
                             Companions_GameState* out) {
  auto* env = wrapper->env.get();

  out->rows = env->GetRows();
  out->cols = env->GetCols();
  out->tick = env->GetTick();

  // Extract agents
  const auto& obj_mgr = env->GetObjectManager();
  auto agents = obj_mgr.GetAllAgents();
  out->agent_count = std::min(static_cast<int>(agents.size()),
                              static_cast<int>(Companions_MAX_AGENTS));

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
  for (int r = 0; r < out->rows && out->special_cell_count < Companions_MAX_SPECIAL_CELLS; r++) {
    for (int c = 0; c < out->cols && out->special_cell_count < Companions_MAX_SPECIAL_CELLS; c++) {
      const auto& cell = grid.GetCell(r, c);
      if (cell.GetKind() != companions::CellKind::Floor) {
        auto& sc = out->special_cells[out->special_cell_count++];
        sc.position = {r, c};
        sc.kind = ToAPICellKind(cell.GetKind());
        // Check for occupant
        auto* actor = obj_mgr.GetActorAt({r, c});
        sc.occupant_id = actor ? actor->GetId() : Companions_INVALID_ID;
      }
    }
  }

  // Extract active effects
  const auto& effects = env->GetActiveEffects();
  out->effect_count = std::min(static_cast<int>(effects.size()),
                               static_cast<int>(Companions_MAX_EFFECTS));
  for (int i = 0; i < out->effect_count; i++) {
    auto& ue_effect = out->effects[i];
    const auto& effect = effects[i];

    // Generate a simple ID from index
    ue_effect.effect_id = i;
    // Get name from config
    if (effect.config) {
      std::strncpy(ue_effect.effect_name, effect.config->name.c_str(),
                   Companions_EFFECT_NAME_LEN - 1);
    } else {
      ue_effect.effect_name[0] = '\0';
    }
    ue_effect.effect_name[Companions_EFFECT_NAME_LEN - 1] = '\0';
    ue_effect.center = ToAPIPosition(effect.GetCenter(obj_mgr));
    ue_effect.direction = ToAPIDirection(effect.direction);
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

static void AddMovementEvents(Companions_Env* wrapper) {
  auto* env = wrapper->env.get();
  auto agents = env->GetObjectManager().GetAllAgents();

  for (size_t i = 0; i < agents.size() && i < wrapper->prev_positions.size(); i++) {
    auto prev = wrapper->prev_positions[i];
    auto curr = agents[i]->GetPosition();

    if (prev != curr) {
      // Agent moved
      Companions_Event evt = {};
      evt.type = Companions_Event_AgentMoved;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToAPIPosition(curr);
      evt.from_pos = ToAPIPosition(prev);
      evt.to_pos = ToAPIPosition(curr);
      evt.move_action = static_cast<Companions_MovementAction>(
          agents[i]->GetExecutedAction().movement);
      wrapper->events.push_back(evt);
    } else if (agents[i]->GetOriginalIntention().movement !=
               companions::MovementAction::Stay) {
      // Tried to move but was blocked. Use original_intention_ (pre-collision)
      // because collision resolution overwrites intention_ with Stay, so
      // GetExecutedAction() would report Stay for every blocked agent.
      Companions_Event evt = {};
      evt.type = Companions_Event_AgentBlocked;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToAPIPosition(curr);
      evt.from_pos = ToAPIPosition(prev);
      auto intended = companions::ApplyMovement(
          prev, agents[i]->GetOriginalIntention().movement);
      evt.to_pos = ToAPIPosition(intended);
      evt.move_action = static_cast<Companions_MovementAction>(
          agents[i]->GetOriginalIntention().movement);
      wrapper->events.push_back(evt);
    }
  }
}

// =============================================================================
// Public API Implementation
// =============================================================================

extern "C" {

COMPANIONS_API Companions_Env* companions_create(
    const Companions_EnvConfig* config) {
  if (!config) {
    SetError("Config is null");
    return nullptr;
  }

  auto* wrapper = new Companions_Env();
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

COMPANIONS_API void companions_destroy(Companions_Env* env) {
  delete env;
}

COMPANIONS_API Companions_Env* companions_create_aggro(
    const Companions_AggroEnvConfig* config) {
  if (!config) {
    SetError("Config is null");
    return nullptr;
  }

  auto* wrapper = new Companions_Env();

  try {
    // Convert API enemy type to internal enum
    companions::EnemyType enemy_type;
    switch (config->enemy_type) {
      case Companions_Enemy_Zombie:
        enemy_type = companions::EnemyType::Zombie;
        break;
      case Companions_Enemy_Archer:
      case Companions_Enemy_Mage:
      default:
        enemy_type = companions::EnemyType::Zombie;  // Default for now
        break;
    }

    // AggroEnv uses grid_size (square grid)
    int grid_size = std::max(config->rows, config->cols);

    wrapper->env = std::make_unique<companions::AggroEnv>(
        grid_size,
        config->num_companions,
        enemy_type,
        config->seed,
        config->d4_transform,
        config->horizon);

    // AggroEnv has companions + 1 enemy
    int total_agents = config->num_companions + 1;
    wrapper->last_rewards.resize(total_agents, 0.0);
    wrapper->prev_positions.resize(total_agents);

  } catch (const std::exception& e) {
    SetError(e.what());
    delete wrapper;
    return nullptr;
  }

  return wrapper;
}

// =============================================================================
// Task Lens API
// =============================================================================

COMPANIONS_API bool companions_set_task_lens(Companions_Env* env, Companions_LensType lens) {
  if (!env || !env->env) {
    SetError("companions_set_task_lens: null env");
    return false;
  }
  try {
    std::unique_ptr<companions::TaskLens> new_lens;
    switch (lens) {
      case Companions_Lens_Synchro:
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      case Companions_Lens_Aggro:
        new_lens = std::make_unique<companions::AggroLens>();
        break;
      case Companions_Lens_Dodge:
        new_lens = std::make_unique<companions::DodgeLens>();
        break;
      default:
        SetError("companions_set_task_lens: invalid lens type");
        return false;
    }
    if (!env->env->SetTaskLens(std::move(new_lens))) {
      SetError("Lens incompatible with current environment state");
      return false;
    }
    // Refresh cached done/success with new lens's evaluation
    // (Player may already be on goal cell for the new lens)
    env->done = env->env->IsDone();
    env->success = env->env->IsSuccess();
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

COMPANIONS_API bool companions_set_task_lens_with_params(
    Companions_Env* env,
    Companions_LensType lens,
    const Companions_Position* positions,
    int num_positions) {
  if (!env || !env->env) {
    SetError("companions_set_task_lens_with_params: null env");
    return false;
  }
  try {
    std::unique_ptr<companions::TaskLens> new_lens;
    switch (lens) {
      case Companions_Lens_Synchro:
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      case Companions_Lens_Aggro:
        new_lens = std::make_unique<companions::AggroLens>();
        break;
      case Companions_Lens_Dodge:
        new_lens = std::make_unique<companions::DodgeLens>();
        break;
      case Companions_Lens_TagApply:
        // TagApplyLens not yet implemented — fall back to SynchroLens so the
        // plumbing path exercises. Remove this fallback once the lens lands.
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      default:
        SetError("companions_set_task_lens_with_params: invalid lens type");
        return false;
    }

    companions::LensParams params;
    if (positions && num_positions > 0) {
      params.positions.reserve(num_positions);
      for (int i = 0; i < num_positions; ++i) {
        params.positions.push_back(companions::Position{
            positions[i].row, positions[i].col});
      }
    }

    if (!env->env->SetTaskLensWithParams(std::move(new_lens), params)) {
      SetError("Lens incompatible with current environment state");
      return false;
    }
    env->done = env->env->IsDone();
    env->success = env->env->IsSuccess();
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

COMPANIONS_API Companions_LensType companions_get_task_lens(Companions_Env* env) {
  if (!env || !env->env) {
    return Companions_Lens_Synchro;  // Default
  }
  auto* lens = env->env->GetTaskLens();
  if (!lens) return Companions_Lens_Synchro;

  // Single virtual dispatch instead of a dynamic_cast chain; unknown kinds
  // map to Companions_Lens_Unknown rather than silently returning Synchro.
  // Audit F9.
  switch (lens->GetKind()) {
    case companions::TaskLens::kSynchro:  return Companions_Lens_Synchro;
    case companions::TaskLens::kAggro:    return Companions_Lens_Aggro;
    case companions::TaskLens::kDodge:    return Companions_Lens_Dodge;
    case companions::TaskLens::kTagApply: return Companions_Lens_TagApply;
    case companions::TaskLens::kUnknown:  return Companions_Lens_Unknown;
  }
  return Companions_Lens_Unknown;
}

COMPANIONS_API void companions_reset(Companions_Env* env,
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

COMPANIONS_API void companions_step(Companions_Env* env,
                                          const Companions_Action* actions,
                                          int32_t action_count,
                                          Companions_StepResult* out_result) {
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
    if (actions[i].movement < Companions_Movement_Stay ||
        actions[i].movement > Companions_Movement_Right) {
      SetError("Invalid movement action");
      return;
    }
    // Validate interact action range
    if (actions[i].interact < Companions_Interact_None ||
        actions[i].interact > Companions_Interact_Attack) {
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
    Companions_Event evt = {};
    evt.type = Companions_Event_EpisodeEnd;
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
                                     Companions_MAX_EVENTS);
  for (int i = 0; i < out_result->event_count; i++) {
    out_result->events[i] = env->events[i];
  }
}

COMPANIONS_API void companions_get_state(const Companions_Env* env,
                                               Companions_GameState* out_state) {
  if (!env || !env->env || !out_state) {
    SetError("Invalid arguments");
    return;
  }

  ExtractGameState(env, out_state);
}

COMPANIONS_API bool companions_get_agent(const Companions_Env* env,
                                               Companions_ObjectId id,
                                               Companions_AgentState* out_agent) {
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

COMPANIONS_API bool companions_get_agent_by_index(
    const Companions_Env* env,
    int32_t index,
    Companions_AgentState* out_agent) {
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

COMPANIONS_API Companions_CellKind companions_get_cell(
    const Companions_Env* env,
    int32_t row,
    int32_t col) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return Companions_CellKind_Floor;
  }

  if (row < 0 || row >= env->env->GetRows() ||
      col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return Companions_CellKind_Wall;
  }

  return ToAPICellKind(env->env->GetGrid().GetCell(row, col).GetKind());
}

COMPANIONS_API void companions_get_grid(const Companions_Env* env,
                                              Companions_CellKind* out_grid) {
  if (!env || !env->env || !out_grid) {
    SetError("Invalid arguments");
    return;
  }

  const auto& grid = env->env->GetGrid();
  int rows = env->env->GetRows();
  int cols = env->env->GetCols();

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      out_grid[r * cols + c] = ToAPICellKind(grid.GetCell(r, c).GetKind());
    }
  }
}

COMPANIONS_API int32_t companions_get_rows(const Companions_Env* env) {
  return env && env->env ? env->env->GetRows() : 0;
}

COMPANIONS_API int32_t companions_get_cols(const Companions_Env* env) {
  return env && env->env ? env->env->GetCols() : 0;
}

COMPANIONS_API int32_t companions_get_agent_count(
    const Companions_Env* env) {
  return env && env->env ? env->env->NumAgents() : 0;
}

COMPANIONS_API int32_t companions_get_tick(const Companions_Env* env) {
  return env && env->env ? env->env->GetTick() : 0;
}

COMPANIONS_API bool companions_is_done(const Companions_Env* env) {
  return env ? env->done : true;
}

COMPANIONS_API bool companions_is_success(const Companions_Env* env) {
  return env ? env->success : false;
}

COMPANIONS_API int32_t companions_render_ascii(
    const Companions_Env* env,
    char* out_buffer,
    int32_t buffer_size) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }

  // Use the renderer to generate ASCII output
  companions::Renderer renderer;
  std::string ascii = renderer.RenderAscii(*env->env);

  int32_t required_size = static_cast<int32_t>(ascii.size() + 1);  // +1 for null terminator

  // If out_buffer is NULL, just return required size
  if (!out_buffer) {
    return required_size;
  }

  // Check buffer size
  if (buffer_size < required_size) {
    SetError("Buffer too small");
    return required_size;
  }

  // Copy to output buffer
  std::memcpy(out_buffer, ascii.c_str(), ascii.size());
  out_buffer[ascii.size()] = '\0';

  return required_size;
}

// =============================================================================
// Semantic Annotations (task-role tags on cells / agents)
// =============================================================================

namespace {

// Render an annotation's params map as compact JSON into `out`. Writes a
// "{...}" string fitting in `out_size` bytes. If any key/value can't fit,
// output is truncated and null-terminated.
void ParamsToCompactJson(const std::unordered_map<std::string, std::string>& params,
                         char* out, std::size_t out_size) {
  if (out_size == 0) return;
  std::string buf;
  buf.reserve(out_size);
  buf.push_back('{');
  bool first = true;
  for (const auto& kv : params) {
    if (!first) buf.push_back(',');
    first = false;
    buf.push_back('"');
    buf.append(kv.first);
    buf.append("\":\"");
    buf.append(kv.second);
    buf.push_back('"');
    if (buf.size() + 1 >= out_size) break;  // leave room for closing brace
  }
  buf.push_back('}');
  std::size_t copy_len = std::min(buf.size(), out_size - 1);
  std::memcpy(out, buf.data(), copy_len);
  out[copy_len] = '\0';
}

}  // namespace

COMPANIONS_API int32_t
companions_get_annotation_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetAnnotations().Size());
}

COMPANIONS_API int32_t companions_get_annotations(
    const Companions_Env* env, Companions_Annotation* out, int32_t count) {
  if (!env || !env->env || !out) {
    SetError("Invalid environment or output buffer");
    return 0;
  }
  const auto& store = env->env->GetAnnotations();
  auto serialized = store.Serialize();
  int32_t n = std::min(count, static_cast<int32_t>(serialized.size()));
  for (int32_t i = 0; i < n; ++i) {
    const auto& a = serialized[i];
    out[i].target_kind = a.target_type;
    out[i].pos.row = a.pos.row;
    out[i].pos.col = a.pos.col;
    out[i].agent_id = a.agent_id;
    out[i].tag = static_cast<int32_t>(a.tag);
    out[i].owner_lens_id = a.owner_lens_id;
    std::unordered_map<std::string, std::string> params_map;
    for (const auto& kv : a.params) params_map.emplace(kv.first, kv.second);
    ParamsToCompactJson(params_map, out[i].params_json,
                        COMPANIONS_MAX_ANNOTATION_PARAMS);
  }
  return n;
}

COMPANIONS_API bool companions_has_tag_at(
    const Companions_Env* env, int32_t row, int32_t col, int32_t tag) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  return env->env->GetAnnotations().HasTag(
      companions::AnnotationKey{companions::AnnotationTarget::Cell,
                                companions::Position{row, col},
                                companions::kInvalidObjectId},
      static_cast<companions::SemanticTag>(tag));
}

COMPANIONS_API bool companions_agent_has_tag(
    const Companions_Env* env, Companions_ObjectId agent_id, int32_t tag) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  return env->env->GetAnnotations().HasTag(
      companions::AnnotationKey{companions::AnnotationTarget::Agent,
                                companions::Position{-1, -1}, agent_id},
      static_cast<companions::SemanticTag>(tag));
}

COMPANIONS_API const char* companions_version(void) {
  return COMPANIONS_VERSION;
}

COMPANIONS_API const char* companions_get_error(void) {
  return g_error_buffer;
}

// =============================================================================
// Snapshot Save/Load
// =============================================================================

COMPANIONS_API int32_t
companions_get_snapshot_size(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }

  try {
    // Save and cache snapshot
    companions::Snapshot snap = env->env->SaveSnapshot();
    env->cached_snapshot = snap.Serialize();
    return static_cast<int32_t>(env->cached_snapshot.size());
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  }
}

COMPANIONS_API bool companions_save_snapshot(const Companions_Env* env,
                                                   uint8_t* out_buffer,
                                                   int32_t buffer_size) {
  if (!env || !out_buffer) {
    SetError("Invalid arguments");
    return false;
  }

  if (env->cached_snapshot.empty()) {
    SetError("No cached snapshot - call get_snapshot_size first");
    return false;
  }

  if (buffer_size < static_cast<int32_t>(env->cached_snapshot.size())) {
    SetError("Buffer too small");
    return false;
  }

  std::memcpy(out_buffer, env->cached_snapshot.data(),
              env->cached_snapshot.size());
  return true;
}

COMPANIONS_API bool companions_load_snapshot(Companions_Env* env,
                                                   const uint8_t* data,
                                                   int32_t data_size) {
  if (!env || !env->env || !data || data_size <= 0) {
    SetError("Invalid arguments");
    return false;
  }

  // Minimum buffer size validation to avoid exceptions crossing DLL boundary
  constexpr int32_t kMinSnapshotSize = 60;
  if (data_size < kMinSnapshotSize) {
    SetError("Snapshot buffer too small");
    return false;
  }

  // Validate magic number before attempting full deserialization
  uint32_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  if (magic != 0x534E4150) {  // "SNAP"
    SetError("Invalid snapshot magic number");
    return false;
  }

  try {
    std::vector<uint8_t> buffer(data, data + data_size);
    companions::Snapshot snap = companions::Snapshot::Deserialize(buffer);
    env->env->LoadSnapshot(snap);

    // Update wrapper state
    env->done = false;
    env->success = false;

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

// =============================================================================
// Level Generation
// =============================================================================

COMPANIONS_API int32_t companions_generate_level(
    const Companions_LevelConfig* config) {
  if (!config) {
    SetError("Config is null");
    return 0;
  }

  if (config->rows <= 0 || config->cols <= 0) {
    SetError("Invalid grid dimensions");
    return 0;
  }

  try {
    // Convert Companions_LevelConfig to companions::LevelConfig
    companions::LevelConfig level_config;

    // Base map
    level_config.map = companions::MapGenerator::DefaultConfig(
        config->rows, config->cols, config->map_complexity, config->seed);

    // Level features
    level_config.synchro_cell_count = config->synchro_cell_count;
    level_config.patrol_square_size = config->patrol_square_size;
    level_config.has_target_cell = config->has_target_cell;

    // Agents
    level_config.num_companions = config->num_companions;
    level_config.num_enemies = config->num_enemies;

    // Episode
    level_config.horizon = config->horizon > 0 ? config->horizon : 100;
    level_config.d4_transform = config->d4_transform;

    // Generate level
    companions::Snapshot snapshot = companions::LevelGenerator::Generate(level_config);

    // Serialize and cache
    g_generated_level = snapshot.Serialize();

    return static_cast<int32_t>(g_generated_level.size());
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  }
}

COMPANIONS_API bool companions_get_generated_level(uint8_t* out_buffer,
                                                          int32_t buffer_size) {
  if (!out_buffer) {
    SetError("Output buffer is null");
    return false;
  }

  if (g_generated_level.empty()) {
    SetError("No generated level - call generate_level first");
    return false;
  }

  if (buffer_size < static_cast<int32_t>(g_generated_level.size())) {
    SetError("Buffer too small");
    return false;
  }

  std::memcpy(out_buffer, g_generated_level.data(), g_generated_level.size());
  return true;
}

// =============================================================================
// JSON Snapshot Save/Load
// =============================================================================

COMPANIONS_API const char* companions_snapshot_to_json(
    const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return nullptr;
  }

  try {
    companions::Snapshot snap = env->env->SaveSnapshot();
    std::string json = companions::SnapshotToJson(snap);

    // Allocate new string (caller must free with companions_free_string)
    char* result = new char[json.size() + 1];
    std::memcpy(result, json.c_str(), json.size() + 1);
    return result;
  } catch (const std::exception& e) {
    SetError(e.what());
    return nullptr;
  }
}

COMPANIONS_API void companions_free_string(const char* str) {
  delete[] str;
}

COMPANIONS_API bool companions_load_snapshot_json(
    Companions_Env* env,
    const char* json_str) {
  if (!env || !env->env || !json_str) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = companions::SnapshotFromJson(json_str);
    env->env->LoadSnapshot(snap);

    // Update wrapper state
    env->done = false;
    env->success = false;

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

COMPANIONS_API bool companions_save_snapshot_json(
    const Companions_Env* env,
    const char* filepath) {
  if (!env || !env->env || !filepath) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = env->env->SaveSnapshot();
    return companions::SaveSnapshotToJsonFile(snap, filepath);
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

COMPANIONS_API bool companions_load_snapshot_json_file(
    Companions_Env* env,
    const char* filepath) {
  if (!env || !env->env || !filepath) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = companions::LoadSnapshotFromJsonFile(filepath);
    env->env->LoadSnapshot(snap);

    // Update wrapper state
    env->done = false;
    env->success = false;

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  }
}

}  // extern "C"
