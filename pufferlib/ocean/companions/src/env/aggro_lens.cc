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
  // (the goal is to lure the enemy to the target).
  Position target = FindTargetCell(env);
  if (target.row < 0 || target.col < 0) {
    return false;
  }
  for (const AgentFSM* fsm_agent : env.GetObjectManager().GetAllAgentFSMs()) {
    if (fsm_agent->IsAlive() && fsm_agent->GetPosition() == target) {
      return true;
    }
  }
  return false;
}

double AggroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
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

  const AgentFSM* enemy = nullptr;
  for (const AgentFSM* fsm_agent : env.GetObjectManager().GetAllAgentFSMs()) {
    if (fsm_agent->IsAlive() && fsm_agent->HasFSM()) {
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

std::vector<Position> AggroLens::GetGoalCells(const BaseEnv& env) const {
  return env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
}

Position AggroLens::FindTargetCell(const BaseEnv& env) const {
  auto targets = env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  if (!targets.empty()) {
    return targets.front();
  }
  return Position{-1, -1};  // Not found
}

bool AggroLens::HasPatrolPath(const BaseEnv& env) const {
  for (const AgentFSM* fsm_agent : env.GetObjectManager().GetAllAgentFSMs()) {
    if (fsm_agent->HasFSM() && !fsm_agent->GetFSMContext().patrol_path.empty()) {
      return true;
    }
  }
  return false;
}

void AggroLens::WriteVectorObs(const BaseEnv& env, const Agent& agent,
                               float* buffer) const {
  // `agent` is the SAME agent whose base features precede this tail (BaseEnv
  // resolves GetAllAgents()[player] once and passes it down), so the combined
  // vector observation is always coherent — including in AggroEnv where the
  // FSM enemy occupies agent slot 0.
  Position agent_pos = agent.GetPosition();
  float rows = static_cast<float>(env.GetRows());
  float cols = static_cast<float>(env.GetCols());
  int idx = 0;

  // Find the first FSM agent (enemy)
  const AgentFSM* enemy = nullptr;
  for (const AgentFSM* fsm_agent : env.GetObjectManager().GetAllAgentFSMs()) {
    if (fsm_agent->HasFSM()) {
      enemy = fsm_agent;
      break;
    }
  }

  // Features 0-1: Relative position to enemy (per-axis normalized)
  if (enemy && enemy->IsAlive()) {
    Position enemy_pos = enemy->GetPosition();
    buffer[idx++] = static_cast<float>(enemy_pos.row - agent_pos.row) / rows;
    buffer[idx++] = static_cast<float>(enemy_pos.col - agent_pos.col) / cols;
  } else {
    buffer[idx++] = 0.0f;
    buffer[idx++] = 0.0f;
  }

  // Feature 2: Distance to enemy (Manhattan, normalized)
  if (enemy && enemy->IsAlive()) {
    Position enemy_pos = enemy->GetPosition();
    int manhattan = std::abs(enemy_pos.row - agent_pos.row) +
                    std::abs(enemy_pos.col - agent_pos.col);
    float max_dist = rows + cols - 2.0f;
    buffer[idx++] = static_cast<float>(manhattan) / max_dist;
  } else {
    buffer[idx++] = 1.0f;  // Max distance if no enemy
  }

  // Features 3-5: FSM state one-hot (patrol, aggro, returning)
  // Note: We have 3 main behavioral states: Patrol, Aggro, ReturnToPatrol
  // Other states (Telegraph, Attack, Recovery) are treated as sub-states
  if (enemy && enemy->HasFSM()) {
    const FSMState* state = enemy->GetCurrentState();
    FSMStateType state_type = state ? state->GetType() : FSMStateType::None;

    // Patrol state
    buffer[idx++] = state_type == FSMStateType::Patrol ? 1.0f : 0.0f;
    // Aggro state (includes Telegraph, Attack, Recovery as "aggressive")
    bool is_aggressive = (state_type == FSMStateType::Aggro ||
                          state_type == FSMStateType::Telegraph ||
                          state_type == FSMStateType::Attack ||
                          state_type == FSMStateType::Recovery);
    buffer[idx++] = is_aggressive ? 1.0f : 0.0f;
    // Return to patrol state
    buffer[idx++] = state_type == FSMStateType::ReturnToPatrol ? 1.0f : 0.0f;
  } else {
    buffer[idx++] = 0.0f;
    buffer[idx++] = 0.0f;
    buffer[idx++] = 0.0f;
  }

  // Features 6-7: Relative position to target cell (per-axis normalized)
  Position target = FindTargetCell(env);
  if (target.row >= 0 && target.col >= 0) {
    buffer[idx++] = static_cast<float>(target.row - agent_pos.row) / rows;
    buffer[idx++] = static_cast<float>(target.col - agent_pos.col) / cols;
  } else {
    buffer[idx++] = 0.0f;
    buffer[idx++] = 0.0f;
  }
}

}  // namespace companions
