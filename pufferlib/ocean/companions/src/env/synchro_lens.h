// Copyright 2024
// SynchroLens - TaskLens implementation for SynchroEnv

#ifndef COMPANIONS_ENV_SYNCHRO_LENS_H_
#define COMPANIONS_ENV_SYNCHRO_LENS_H_

#include "task_lens.h"

namespace companions {

class SynchroLens : public TaskLens {
 public:
  static constexpr double kWinReward = 1.0;
  static constexpr double kProgressReward = 0.01;

  // Stable owner id used in the AnnotationStore for SynchroGoal tags placed
  // by this lens (and by SynchroEnv::Reset on initial spawn). Swapping in a
  // new SynchroLens via SetTaskLens removes tags from the previous owner.
  static constexpr int32_t kOwnerId = 1;

  Kind GetKind() const override { return kSynchro; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;
  bool IsGoalCell(const BaseEnv& env, Position pos) const override;
  const std::vector<Position>& GetGoalCells(const BaseEnv& env) const override;

  // Stamp Synchro cells at params.positions (overlaying base terrain).
  // These cells ARE the synchro goals until Deactivate restores the base.
  void Activate(BaseEnv& env, const LensParams& params) override;
  void Deactivate(BaseEnv& env) override;

 private:
  int CountAgentsOnSynchroCells(const BaseEnv& env) const;
  int CountSynchroCells(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_SYNCHRO_LENS_H_
