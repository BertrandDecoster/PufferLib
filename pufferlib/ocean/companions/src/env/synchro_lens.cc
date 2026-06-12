// Copyright 2024
// SynchroLens implementation

#include "synchro_lens.h"

#include <sstream>

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
  // A world with zero synchro goals is not a (vacuously) solved task — it is
  // a world this lens cannot succeed on, consistent with CanOperateOn
  // requiring > 0 goals. This matters in-game: GameEnv accepts arbitrary
  // snapshots, and a goal-less one must not latch spurious success.
  int num_goals = CountSynchroCells(env);
  return num_goals > 0 && CountAgentsOnSynchroCells(env) >= num_goals;
}

double SynchroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task

  int num_agents = env.NumAgents();
  int on_synchro = CountAgentsOnSynchroCells(env);

  // Time penalty: -num_agents * kProgressReward
  // This ensures max progress per step is 0 when all agents are on synchro
  // but not yet winning
  double time_penalty = -static_cast<double>(num_agents) * kProgressReward;
  double reward = kProgressReward * static_cast<double>(on_synchro) + time_penalty;

  // Win reward if all synchro cells are covered
  if (IsSuccess(env)) {
    reward += kWinReward;
  }

  return reward;
}

std::string SynchroLens::GetObjectiveString(const BaseEnv& env) const {
  std::ostringstream ss;
  int on = CountAgentsOnSynchroCells(env);
  int total = CountSynchroCells(env);
  ss << "Synchro: " << on << "/" << total << " companions on goal cells";
  if (IsSuccess(env)) {
    ss << " [SUCCESS]";
  }
  return ss.str();
}

bool SynchroLens::IsGoalCell(const BaseEnv& env, Position pos) const {
  return env.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
      SemanticTag::SynchroGoal);
}

const std::vector<Position>& SynchroLens::GetGoalCells(
    const BaseEnv& env) const {
  return env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
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
  const Grid& grid = env.GetGrid();
  AnnotationStore& annotations = env.GetMutableAnnotations();
  // Drop any stale SynchroGoal tags from a prior instance of this lens before
  // adding our own, so a back-to-back Activate does not double-count goals.
  annotations.RemoveByOwner(kOwnerId);
  for (const Position& pos : params.positions) {
    if (pos.row < 0 || pos.row >= grid.GetRows() ||
        pos.col < 0 || pos.col >= grid.GetCols()) {
      continue;
    }
    annotations.Add(
        AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
        Annotation{SemanticTag::SynchroGoal, {}, kOwnerId});
  }
}

void SynchroLens::Deactivate(BaseEnv& env) {
  env.GetMutableAnnotations().RemoveByOwner(kOwnerId);
}

}  // namespace companions
