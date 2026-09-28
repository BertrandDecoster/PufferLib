// Copyright 2024
// AggroLens - TaskLens implementation for AggroEnv

#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

// Lure the enemy (an FSM agent) onto the AggroTarget cell.
//
// Termination: success when a living FSM agent stands on the target; failure
// at the horizon, or as soon as no living FSM agent remains (the enemy was
// killed: the lure can no longer succeed). The dead-enemy step is rewarded
// kEnemyDeadPenalty (the mirror of kWinReward) instead of the time penalty.
//
// "Done" is a verdict for RL episodes, not a stop: the env never refuses to
// step. A host that keeps playing after a kill (a game layer) just ignores
// done for that reason (IsSuccess false, tick below the horizon, no living
// enemy); every further step is rewarded kEnemyDeadPenalty again.
class AggroLens : public TaskLens {
 public:
  static constexpr double kWinReward = 1.0;
  static constexpr double kTimePenalty = -0.01;
  static constexpr double kEnemyDeadPenalty = -1.0;

  Kind GetKind() const override { return kAggro; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;
  bool IsGoalCell(const BaseEnv& env, Position pos) const override;
  std::vector<Position> GetGoalCells(const BaseEnv& env) const override;

  void AppendVectorObs(const BaseEnv& env, int agent_id,
                       std::vector<float>& obs) const override;
  int AdditionalVectorObsSize() const override { return 8; }

  // Any living FSM agent. AggroEnv ends the Aggro task without one.
  static bool HasLivingEnemy(const BaseEnv& env);

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
