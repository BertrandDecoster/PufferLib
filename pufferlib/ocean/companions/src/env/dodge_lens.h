// Copyright 2024
// DodgeLens - TaskLens implementation for survival-based tasks

#ifndef COMPANIONS_ENV_DODGE_LENS_H_
#define COMPANIONS_ENV_DODGE_LENS_H_

#include "task_lens.h"

namespace companions {

class DodgeLens : public TaskLens {
 public:
  static constexpr float kSurvivalReward = 0.01f;
  static constexpr float kDeathPenalty = -1.0f;

  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  float ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;

 private:
  bool AnyCompanionIncapacitated(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_DODGE_LENS_H_
