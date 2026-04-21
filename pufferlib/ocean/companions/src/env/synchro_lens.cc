// Copyright 2024
// SynchroLens implementation

#include "synchro_lens.h"

#include "base_env.h"
#include "../core/annotations.h"
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
  const AnnotationStore& annotations = env.GetAnnotations();
  const ObjectManager& om = env.GetObjectManager();

  for (const Agent* agent : om.GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    Position pos = agent->GetPosition();
    AnnotationKey key{AnnotationTarget::Cell, pos, kInvalidObjectId};
    if (annotations.HasTag(key, SemanticTag::SynchroGoal)) {
      count++;
    }
  }
  return count;
}

int SynchroLens::CountSynchroCells(const BaseEnv& env) const {
  return static_cast<int>(
      env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal).size());
}

void SynchroLens::Activate(BaseEnv& env, const LensParams& params) {
  Grid& grid = env.GetMutableGrid();
  AnnotationStore& annotations = env.GetMutableAnnotations();
  // Drop any stale SynchroGoal tags from a prior lens instance before adding
  // our own, so a back-to-back Activate does not double-count goals.
  annotations.RemoveByOwner(kOwnerId);
  stamped_positions_.clear();
  stamped_positions_.reserve(params.positions.size());
  for (const Position& pos : params.positions) {
    if (pos.row < 0 || pos.row >= grid.GetRows() ||
        pos.col < 0 || pos.col >= grid.GetCols()) {
      continue;
    }
    Cell& cell = grid.GetMutableCell(pos);
    cell.StampOverlay(CellKind::Synchro, /*is_stamp_overlay=*/true);
    stamped_positions_.push_back(pos);

    AnnotationKey key{AnnotationTarget::Cell, pos, kInvalidObjectId};
    annotations.Add(key, Annotation{SemanticTag::SynchroGoal, {}, kOwnerId});
  }
}

void SynchroLens::Deactivate(BaseEnv& env) {
  Grid& grid = env.GetMutableGrid();
  for (const Position& pos : stamped_positions_) {
    if (pos.row < 0 || pos.row >= grid.GetRows() ||
        pos.col < 0 || pos.col >= grid.GetCols()) {
      continue;
    }
    grid.GetMutableCell(pos).RemoveOverlay();
  }
  stamped_positions_.clear();
  env.GetMutableAnnotations().RemoveByOwner(kOwnerId);
}

}  // namespace companions
