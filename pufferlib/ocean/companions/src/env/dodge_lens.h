// Copyright 2024
// DodgeLens - TaskLens implementation for survival-based tasks

#ifndef COMPANIONS_ENV_DODGE_LENS_H_
#define COMPANIONS_ENV_DODGE_LENS_H_

#include "task_lens.h"

namespace companions {

// Survive until the horizon: success when every companion is up at the
// horizon step. A companion down is no failure (only a team down or the
// horizon fails a task): an ally may revive it before the horizon.
//
// Rewards: kSurvivalBonus per step while nobody is down (0 while someone is),
// plus kWinReward on success.
class DodgeLens : public TaskLens {
 public:
  static constexpr double kSurvivalBonus = 0.1;
  static constexpr double kWinReward = 10.0;

  std::unique_ptr<TaskLens> Clone() const override { return std::make_unique<DodgeLens>(*this); }
  Kind GetKind() const override { return kDodge; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;

 private:
  // A companion down (or dead): not affectable
  bool AnyCompanionIncapacitated(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_DODGE_LENS_H_
