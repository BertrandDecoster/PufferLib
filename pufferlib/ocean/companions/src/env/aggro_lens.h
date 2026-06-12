// Copyright 2024
// AggroLens - TaskLens implementation for AggroEnv

#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

class AggroLens : public TaskLens {
 public:
  static constexpr double kWinReward = 1.0;
  static constexpr double kTimePenalty = -0.01;

  Kind GetKind() const override { return kAggro; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;
  bool IsGoalCell(const BaseEnv& env, Position pos) const override;
  std::vector<Position> GetGoalCells(const BaseEnv& env) const override;

  // Aggro-specific vector observation tail (8 features), computed relative
  // to the passed agent (the same agent the base features describe):
  //   [0-1] Relative position to enemy (per-axis normalized)
  //   [2]   Manhattan distance to enemy / (rows + cols - 2); 1.0 if no enemy
  //   [3-5] Enemy FSM one-hot (patrol, aggressive, returning)
  //   [6-7] Relative position to target cell (per-axis normalized)
  void WriteVectorObs(const BaseEnv& env, const Agent& agent,
                      float* buffer) const override;
  int AdditionalVectorObsSize() const override { return 8; }

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
