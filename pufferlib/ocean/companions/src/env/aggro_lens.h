// Copyright 2024
// AggroLens - TaskLens implementation for AggroEnv

#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

// Lure the enemy (an FSM agent) onto the AggroTarget cell.
//
// Termination: success when a living FSM agent stands on the target; failure
// at the horizon, or as soon as no living FSM agent remains (IsFailed: the
// enemy was killed, the lure can no longer succeed).
//
// Rewards: kWinReward on success, kTimePenalty per other step. The failure is
// terminal: the step the enemy dies (before BaseEnv latches IsTaskFailed) is
// rewarded FailurePenalty, the time cost of the rest of the episode (that
// step included) plus kEnemyDeadPenalty, and every later step 0 (the rest
// was already paid). A killed-enemy episode thus always returns
// horizon * kTimePenalty + kEnemyDeadPenalty, one kEnemyDeadPenalty below
// timing out, whatever the step of the kill: killing is never a shortcut.
//
// "Done" is a verdict for RL episodes, not a stop: the env never refuses to
// step. A host that keeps playing after a kill (a game layer) just ignores
// done for that reason (BaseEnv::GetEndReason() == EndReason::TaskFailed).
class AggroLens : public TaskLens {
 public:
  static constexpr double kWinReward = 1.0;
  static constexpr double kTimePenalty = -0.01;
  static constexpr double kEnemyDeadPenalty = -1.0;

  // The reward of the step that kills the enemy, `tick` being the tick after
  // that step (its 1-based index): the steps left until the horizon, this one
  // included, at kTimePenalty, plus kEnemyDeadPenalty.
  static double FailurePenalty(int horizon, int tick) {
    const int steps_left = horizon - tick + 1;
    return kTimePenalty * (steps_left > 0 ? steps_left : 0) + kEnemyDeadPenalty;
  }

  Kind GetKind() const override { return kAggro; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  bool IsFailed(const BaseEnv& env) const override { return !HasLivingEnemy(env); }
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;
  bool IsGoalCell(const BaseEnv& env, Position pos) const override;
  std::vector<Position> GetGoalCells(const BaseEnv& env) const override;

  void AppendVectorObs(const BaseEnv& env, int agent_id,
                       std::vector<float>& obs) const override;
  int AdditionalVectorObsSize() const override { return 8; }

  // Any living FSM agent, whatever its faction (not filtered, like IsSuccess:
  // any living FSM agent on the target wins). AggroEnv ends the Aggro task
  // without one.
  static bool HasLivingEnemy(const BaseEnv& env);

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
