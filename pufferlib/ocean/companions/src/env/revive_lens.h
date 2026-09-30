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
// terrain is walkable; the agent may move off). A body boxed in (no such
// neighbour) gives no goal cell: with none at all, the goal-distance feature
// reads 0.0, as on a goal (BaseEnv::WriteVectorObservation).
//
// Success: no companion down, once someone has been down during the lens's
// episode (down at the activation, or anyone's times_downed grown since: a
// host down the next step revives counts). An episode with nobody down never
// succeeds: it runs to the horizon. It never fails on its own (only the team
// down or the horizon do), and a down never interrupts it (IsInterruptible
// false): the down pays BaseEnv's down cost and joins the goal. CanOperateOn:
// someone is down (and the params were valid).
//
// A new episode with the lens kept (Reset, LoadSnapshot: OnNewEpisode) starts
// over: no targets (every downed ally), the downs recorded afresh.
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
  // The goal cells once per tensor (IsGoalCell rebuilds the goal per cell)
  void WriteGoalPlane(const BaseEnv& env, float* plane) const override;

  // Stores the targets (the downed companions on params.positions) and each
  // companion's times_downed. Stamps nothing (no Deactivate needed).
  void Activate(BaseEnv& env, const LensParams& params) override;
  // Forgets the targets and records the downs afresh (see above)
  void OnNewEpisode(BaseEnv& env) override;

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
  // Whether a companion known at the activation went down since
  bool AnyoneWentDownSince(const BaseEnv& env) const;
  // Each companion's times_downed now
  void RecordDowns(const BaseEnv& env);

  std::vector<ObjectId> targets_;
  std::vector<DownsAtActivation> downs_at_activation_;
  // False once a param cell held no downed companion: CanOperateOn refuses
  bool params_valid_ = true;
  // Someone was down as the episode began (the lens is only accepted with
  // someone down: true until an OnNewEpisode finds nobody)
  bool saw_down_ = true;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_REVIVE_LENS_H_
