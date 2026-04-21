// Copyright 2024
// AggroLens implementation

#include "aggro_lens.h"

#include <cstdlib>
#include <sstream>

#include "base_env.h"
#include "../core/annotations.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"

namespace companions {

bool AggroLens::CanOperateOn(const BaseEnv& env) const {
  // AggroLens requires both a Target cell and at least one FSM agent with a patrol path
  Position target = FindTargetCell(env);
  if (target.row < 0 || target.col < 0) {
    return false;
  }
  return HasPatrolPath(env);
}

bool AggroLens::IsDone(const BaseEnv& env) const {
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool AggroLens::IsSuccess(const BaseEnv& env) const {
  // Success when any AgentFSM (enemy) is standing on the target cell
  // (the goal is to lure the enemy to the target)
  Position target = FindTargetCell(env);
  if (target.row < 0 || target.col < 0) {
    return false;
  }

  const ObjectManager& om = env.GetObjectManager();
  for (const Actor* actor : om.GetAllActors()) {
    const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(actor);
    if (fsm_agent && fsm_agent->IsAlive()) {
      if (fsm_agent->GetPosition() == target) {
        return true;
      }
    }
  }
  return false;
}

float AggroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task

  if (IsSuccess(env)) {
    return kWinReward;
  }
  return kTimePenalty;
}

std::string AggroLens::GetObjectiveString(const BaseEnv& env) const {
  std::ostringstream ss;
  ss << "Aggro: lure enemy to target";

  Position target = FindTargetCell(env);
  if (target.row < 0 || target.col < 0) {
    ss << " (no target cell)";
    return ss.str();
  }

  const ObjectManager& om = env.GetObjectManager();
  const AgentFSM* enemy = nullptr;
  for (const Actor* actor : om.GetAllActors()) {
    const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(actor);
    if (fsm_agent && fsm_agent->IsAlive() && fsm_agent->HasFSM()) {
      enemy = fsm_agent;
      break;
    }
  }

  if (!enemy) {
    ss << " (no enemy)";
    return ss.str();
  }

  if (enemy->GetPosition() == target) {
    ss << " [SUCCESS - enemy on target]";
    return ss.str();
  }

  Position epos = enemy->GetPosition();
  int dist = std::abs(epos.row - target.row) + std::abs(epos.col - target.col);
  std::string state_name = "?";
  const FSMState* state = enemy->GetCurrentState();
  if (state) state_name = state->GetName();
  ss << " (enemy " << state_name << ", " << dist << " cells from target)";
  return ss.str();
}

bool AggroLens::IsGoalCell(const BaseEnv& env, Position pos) const {
  return env.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
      SemanticTag::AggroTarget);
}

Position AggroLens::FindTargetCell(const BaseEnv& env) const {
  auto targets = env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  if (!targets.empty()) {
    return targets.front();
  }
  return Position{-1, -1};  // Not found
}

bool AggroLens::HasPatrolPath(const BaseEnv& env) const {
  const ObjectManager& om = env.GetObjectManager();

  // Check all actors for FSM agents with non-empty patrol paths
  for (const Actor* actor : om.GetAllActors()) {
    // Check if it's an AgentFSM
    const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(actor);
    if (fsm_agent && fsm_agent->HasFSM()) {
      const FSMContext& ctx = fsm_agent->GetFSMContext();
      if (!ctx.patrol_path.empty()) {
        return true;
      }
    }
  }
  return false;
}

void AggroLens::AppendVectorObs(const BaseEnv& env, int agent_id,
                                std::vector<float>& obs) const {
  // Get the companion for this agent_id
  const ObjectManager& om = env.GetObjectManager();
  std::vector<const Companion*> companions = om.GetAllCompanions();

  if (agent_id < 0 || agent_id >= static_cast<int>(companions.size())) {
    // Invalid agent_id, append zeros
    for (int i = 0; i < 8; ++i) {
      obs.push_back(0.0f);
    }
    return;
  }

  const Companion* companion = companions[agent_id];
  Position companion_pos = companion->GetPosition();
  float rows = static_cast<float>(env.GetRows());
  float cols = static_cast<float>(env.GetCols());

  // Find the first FSM agent (enemy)
  const AgentFSM* enemy = nullptr;
  for (const Actor* actor : om.GetAllActors()) {
    const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(actor);
    if (fsm_agent && fsm_agent->HasFSM()) {
      enemy = fsm_agent;
      break;
    }
  }

  // Features 0-1: Relative position to enemy (normalized)
  if (enemy && enemy->IsAlive()) {
    Position enemy_pos = enemy->GetPosition();
    obs.push_back(static_cast<float>(enemy_pos.row - companion_pos.row) / rows);
    obs.push_back(static_cast<float>(enemy_pos.col - companion_pos.col) / cols);
  } else {
    obs.push_back(0.0f);
    obs.push_back(0.0f);
  }

  // Feature 2: Distance to enemy (Manhattan, normalized)
  if (enemy && enemy->IsAlive()) {
    Position enemy_pos = enemy->GetPosition();
    int manhattan = std::abs(enemy_pos.row - companion_pos.row) +
                    std::abs(enemy_pos.col - companion_pos.col);
    float max_dist = rows + cols - 2.0f;
    obs.push_back(static_cast<float>(manhattan) / max_dist);
  } else {
    obs.push_back(1.0f);  // Max distance if no enemy
  }

  // Features 3-5: FSM state one-hot (patrol, aggro, returning)
  // Note: We have 3 main behavioral states: Patrol, Aggro, ReturnToPatrol
  // Other states (Telegraph, Attack, Recovery) are treated as sub-states
  if (enemy && enemy->HasFSM()) {
    const FSMState* state = enemy->GetCurrentState();
    FSMStateType state_type = state ? state->GetType() : FSMStateType::None;

    // Patrol state
    obs.push_back(state_type == FSMStateType::Patrol ? 1.0f : 0.0f);
    // Aggro state (includes Telegraph, Attack, Recovery as "aggressive")
    bool is_aggressive = (state_type == FSMStateType::Aggro ||
                          state_type == FSMStateType::Telegraph ||
                          state_type == FSMStateType::Attack ||
                          state_type == FSMStateType::Recovery);
    obs.push_back(is_aggressive ? 1.0f : 0.0f);
    // Return to patrol state
    obs.push_back(state_type == FSMStateType::ReturnToPatrol ? 1.0f : 0.0f);
  } else {
    obs.push_back(0.0f);
    obs.push_back(0.0f);
    obs.push_back(0.0f);
  }

  // Features 6-7: Relative position to target cell (normalized)
  Position target = FindTargetCell(env);
  if (target.row >= 0 && target.col >= 0) {
    obs.push_back(static_cast<float>(target.row - companion_pos.row) / rows);
    obs.push_back(static_cast<float>(target.col - companion_pos.col) / cols);
  } else {
    obs.push_back(0.0f);
    obs.push_back(0.0f);
  }
}

}  // namespace companions
