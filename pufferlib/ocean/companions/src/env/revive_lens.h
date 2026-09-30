// Copyright 2024
// ReviveLens - TaskLens: get the downed allies up

#ifndef COMPANIONS_ENV_REVIVE_LENS_H_
#define COMPANIONS_ENV_REVIVE_LENS_H_

#include <vector>

#include "task_lens.h"

namespace companions {

// Get the downed allies up (C API: Companions_Lens_Revive, since 1.6).
//
// Params (SetTaskLensWithParams): the cells of the downed allies to revive,
// the targets, kept as agent ids. Every cell must hold a downed companion,
// else the lens refuses (CanOperateOn) and the previous lens stays. No cells
// (or plain SetTaskLens): every downed ally.
//
// Goal bodies: the targets still down, plus any companion down that went down
// since the activation (its times_downed grew); if none while someone is
// down, every downed ally (the policy always has a target). Without targets:
// every downed ally.
//
// Goal cells (IsGoalCell / GetGoalCells: the goal-distance feature, tensor
// plane 2): the orthogonal neighbours of the goal bodies that are walkable
// and hold no downed body. A cell a standing agent holds still counts (the
// terrain is walkable; the agent may move off).
//
// Success: no companion down. It never fails on its own (only the team down
// or the horizon do), and a down never interrupts it (IsInterruptible false):
// the down pays BaseEnv's down cost and joins the goal. CanOperateOn: someone
// is down (and the params were valid).
//
// Rewards: kTimePenalty per step, plus kSuccessBonus on success (while it
// counts: BaseEnv::SuccessCounts).
class ReviveLens : public TaskLens {
 public:
  static constexpr double kTimePenalty = -0.01;
  static constexpr double kSuccessBonus = 1.0;

  std::unique_ptr<TaskLens> Clone() const override { return std::make_unique<ReviveLens>(*this); }
  Kind GetKind() const override { return kRevive; }
  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  bool IsInterruptible() const override { return false; }
  double ComputeReward(const BaseEnv& env, int agent_id) const override;
  std::string GetObjectiveString(const BaseEnv& env) const override;
  bool IsGoalCell(const BaseEnv& env, Position pos) const override;
  std::vector<Position> GetGoalCells(const BaseEnv& env) const override;

  // Stores the targets (the downed companions on params.positions) and each
  // companion's times_downed. Stamps nothing (no Deactivate needed).
  void Activate(BaseEnv& env, const LensParams& params) override;

  // The targets (agent ids; empty: every downed ally)
  const std::vector<ObjectId>& GetTargets() const { return targets_; }
  // The goal bodies now, in agent order (see above)
  std::vector<ObjectId> GetGoalBodies(const BaseEnv& env) const;

 private:
  struct DownsAtActivation {
    ObjectId id;
    int times_downed;
  };

  // Whether the companion went down since the activation
  bool WentDownSince(ObjectId id, int times_downed) const;

  std::vector<ObjectId> targets_;
  std::vector<DownsAtActivation> downs_at_activation_;
  // False once a param cell held no downed companion: CanOperateOn refuses
  bool params_valid_ = true;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_REVIVE_LENS_H_
