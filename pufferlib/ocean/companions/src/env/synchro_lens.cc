// Copyright 2024
// SynchroLens implementation

#include "synchro_lens.h"

#include "base_env.h"
#include "../core/grid.h"
#include "../core/object_manager.h"

namespace companions {

bool SynchroLens::CanOperateOn(const BaseEnv& env) const {
  return CountSynchroCells(env) > 0;
}

bool SynchroLens::IsDone(const BaseEnv& env) const {
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool SynchroLens::IsSuccess(const BaseEnv& env) const {
  return CountAgentsOnSynchroCells(env) >= CountSynchroCells(env);
}

float SynchroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task

  int num_agents = env.NumAgents();
  int on_synchro = CountAgentsOnSynchroCells(env);

  // Time penalty: -num_agents * kProgressReward
  // This ensures max progress per step is 0 when all agents are on synchro
  // but not yet winning
  float time_penalty = -static_cast<float>(num_agents) * kProgressReward;
  float reward = kProgressReward * static_cast<float>(on_synchro) + time_penalty;

  // Win reward if all synchro cells are covered
  if (IsSuccess(env)) {
    reward += kWinReward;
  }

  return reward;
}

CellKind SynchroLens::MaskCell(CellKind kind) const {
  // Hide Target cells (used by AggroEnv) - show as Floor
  if (kind == CellKind::Target) {
    return CellKind::Floor;
  }
  // Keep Synchro cells visible - they are the goals for this task
  return kind;
}

int SynchroLens::CountAgentsOnSynchroCells(const BaseEnv& env) const {
  int count = 0;
  const Grid& grid = env.GetGrid();
  const ObjectManager& om = env.GetObjectManager();

  for (const Agent* agent : om.GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    Position pos = agent->GetPosition();
    if (grid.GetCellKind(pos) == CellKind::Synchro) {
      count++;
    }
  }
  return count;
}

int SynchroLens::CountSynchroCells(const BaseEnv& env) const {
  const Grid& grid = env.GetGrid();
  int rows = grid.GetRows();
  int cols = grid.GetCols();
  int count = 0;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (grid.GetCellKind(r, c) == CellKind::Synchro) {
        count++;
      }
    }
  }
  return count;
}

}  // namespace companions
