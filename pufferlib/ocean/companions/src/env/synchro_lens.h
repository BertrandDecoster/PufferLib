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

  // Stamp Synchro cells at params.positions (overlaying base terrain).
  // These cells ARE the synchro goals until Deactivate restores the base.
  void Activate(BaseEnv& env, const LensParams& params) override;
  void Deactivate(BaseEnv& env) override;

 private:
  int CountAgentsOnSynchroCells(const BaseEnv& env) const;
  int CountSynchroCells(const BaseEnv& env) const;

  // Positions this lens stamped on Activate, remembered so Deactivate can
  // un-stamp exactly those cells.
  std::vector<Position> stamped_positions_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_SYNCHRO_LENS_H_
