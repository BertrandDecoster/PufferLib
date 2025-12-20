// Copyright 2024
// AggroLens - TaskLens implementation for AggroEnv

#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

class AggroLens : public TaskLens {
 public:
  static constexpr float kWinReward = 1.0f;
  static constexpr float kTimePenalty = -0.01f;

  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  float ComputeReward(const BaseEnv& env, int agent_id) const override;
  CellKind MaskCell(CellKind kind) const override;

  void AppendVectorObs(const BaseEnv& env, int agent_id,
                       std::vector<float>& obs) const override;
  int AdditionalVectorObsSize() const override { return 8; }

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
