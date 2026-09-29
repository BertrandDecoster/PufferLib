// Copyright 2024
// AggroLens - TaskLens implementation for AggroEnv

#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

// Lure the enemy (an FSM agent) onto the AggroTarget cell.
//
// Termination: success when a living FSM agent stands on the target; else
// the horizon (or the team down: the env's). Nothing else fails the task: a
// dead enemy just leaves it unwinnable until the horizon (only a team down or
// the horizon fails a task).
//
// Rewards: kWinReward on success (while it counts: BaseEnv::SuccessCounts,
// not past an episode ended otherwise), kTimePenalty per other step (a kill's
// included): a killed-enemy episode returns what timing out does,
// horizon * kTimePenalty, whatever the step of the kill. A kill after a
// latched success pays kTimePenalty, like any step with the enemy off the
// target after a success.
//
// "Done" is a verdict for RL episodes, not a stop: the env never refuses to
// step.
class AggroLens : public TaskLens {
 public:
  static constexpr double kWinReward = 1.0;
  static constexpr double kTimePenalty = -0.01;

  std::unique_ptr<TaskLens> Clone() const override { return std::make_unique<AggroLens>(*this); }
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

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
