// Copyright 2024
// DodgeLens - TaskLens implementation for survival-based tasks

#ifndef COMPANIONS_ENV_DODGE_LENS_H_
#define COMPANIONS_ENV_DODGE_LENS_H_

#include "task_lens.h"

namespace companions {

class DodgeLens : public TaskLens {
 public:
  static constexpr double kSurvivalBonus = 0.1;
  static constexpr double kWinReward = 10.0;
  static constexpr double kDeathPenalty = -10.0;

  Kind GetKind() const override { return kDodge; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;

  // Dodge-specific vector observation tail (10 features):
  //   [0]   Survival progress (ticks remaining / horizon)
  //   [1]   Number of active effects (normalized, capped at 10)
  //   [2-5] Active hazard danger per direction (up, down, left, right);
  //         inverted distance: 0 = far/safe, 1 = on top of the hazard
  //   [6-9] Telegraphed hazard danger per direction (same encoding)
  void AppendVectorObs(const BaseEnv& env, int agent_id,
                       std::vector<float>& obs) const override;
  int AdditionalVectorObsSize() const override { return 10; }

 private:
  bool AnyCompanionIncapacitated(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_DODGE_LENS_H_
