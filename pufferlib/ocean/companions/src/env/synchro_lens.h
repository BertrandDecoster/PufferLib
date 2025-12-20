// Copyright 2024
// SynchroLens - TaskLens implementation for SynchroEnv

#ifndef COMPANIONS_ENV_SYNCHRO_LENS_H_
#define COMPANIONS_ENV_SYNCHRO_LENS_H_

#include "task_lens.h"

namespace companions {

class SynchroLens : public TaskLens {
 public:
  static constexpr float kWinReward = 1.0f;
  static constexpr float kProgressReward = 0.01f;

  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  float ComputeReward(const BaseEnv& env, int agent_id) const override;
  CellKind MaskCell(CellKind kind) const override;

 private:
  int CountAgentsOnSynchroCells(const BaseEnv& env) const;
  int CountSynchroCells(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_SYNCHRO_LENS_H_
