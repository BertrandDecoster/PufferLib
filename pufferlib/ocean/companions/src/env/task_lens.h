// Copyright 2024
// TaskLens - Abstract interface for task-specific interpretation of world state

#ifndef COMPANIONS_ENV_TASK_LENS_H_
#define COMPANIONS_ENV_TASK_LENS_H_

#include <string>
#include <vector>
#include "../core/cell.h"
#include "../core/types.h"

namespace companions {

class BaseEnv;  // Forward declaration
class Agent;    // Forward declaration

// Parameters handed to a lens at activation time. Positions tell the lens
// where to stamp objective cells (Synchro plates, TagApply markers, etc.).
// Args are the textual operator arguments from the HTN (e.g. {"wet","gob1"})
// for lenses that need entity identity beyond raw positions.
struct LensParams {
  std::vector<Position> positions;
  std::vector<std::string> args;
};

// =============================================================================
// TaskLens - Abstract interface for task-specific interpretation of world state
// =============================================================================
// Lenses are stateless - all methods compute results on-demand from BaseEnv.
// This enables runtime task switching without snapshot serialization.
//
// Architecture:
//   BaseEnv holds physical world state (grid, agents, effects, tick).
//   TaskLens objects interpret success/rewards/observations for specific tasks.
//   Swapping tasks = swapping lens pointer. World state remains unchanged.
//
// Example usage:
//   // Start with Synchro task
//   env.SetTaskLens(std::make_unique<SynchroLens>());
//   // ... run steps ...
//   // Switch to Aggro task without resetting world
//   env.SetTaskLens(std::make_unique<AggroLens>());
//
// =============================================================================
class TaskLens {
 public:
  virtual ~TaskLens() = default;

  // ===========================================================================
  // Validation - can this lens operate on the given env state?
  // ===========================================================================
  // Returns false if required elements are missing (e.g., no synchro cells
  // for SynchroLens, no target cell for AggroLens).
  // Called by BaseEnv::SetTaskLens() to validate before accepting.
  virtual bool CanOperateOn(const BaseEnv& env) const = 0;

  // ===========================================================================
  // Task completion
  // ===========================================================================
  // IsDone: true if episode should terminate (success OR failure/timeout)
  // IsSuccess: true if task was completed successfully
  virtual bool IsDone(const BaseEnv& env) const = 0;
  virtual bool IsSuccess(const BaseEnv& env) const = 0;

  // ===========================================================================
  // Reward calculation (called per agent per step)
  // ===========================================================================
  // Returns the reward for the given agent based on current env state.
  // Different lenses implement different reward shaping strategies.
  // Returns double to match StepResult::rewards element type — precision
  // matters for tests that do exact reward comparisons.
  virtual double ComputeReward(const BaseEnv& env, int agent_id) const = 0;

  // ===========================================================================
  // Kind identity — stable integer tag for DLL callers.
  // ===========================================================================
  // Keeps the C API free of dynamic_cast chains when identifying the active
  // lens from the outside. Values must match Companions_LensType in the C API
  // header (0=Synchro, 1=Aggro, 2=Dodge, 3=TagApply, 0x7FFFFFFF=Unknown).
  // Audit F9.
  enum Kind : int {
    kSynchro = 0,
    kAggro = 1,
    kDodge = 2,
    kTagApply = 3,
    kUnknown = 0x7FFFFFFF,
  };
  virtual Kind GetKind() const = 0;

  // ===========================================================================
  // Human-readable objective line for the demo HUD
  // ===========================================================================
  // Returns a one-line description of what the task is about plus current
  // progress (e.g. "Synchro: 2/3 companions on goal cells"). The renderer
  // prints this below the grid so each env explains itself.
  virtual std::string GetObjectiveString(const BaseEnv& env) const = 0;

  // ===========================================================================
  // Goal cell predicate - is this cell a "goal" for the current task?
  // ===========================================================================
  // Used by the observation pipeline (plane 2 of the 7-plane tensor) to mark
  // task-relevant target cells. Default: no goals. Lenses override by reading
  // the appropriate SemanticTag from env.GetAnnotations().
  virtual bool IsGoalCell(const BaseEnv& env, Position pos) const {
    (void)env;
    (void)pos;
    return false;
  }

  // ===========================================================================
  // Goal cells for "distance to goal" features (observation feature 3)
  // ===========================================================================
  // Returns the positions the policy should measure distance to. Lenses with
  // no geometric goal (e.g. DodgeLens — survival) return empty, and the
  // distance-to-goal feature becomes explicit zero instead of an accidental
  // 1.0 from an empty SynchroGoal scan. See audit F15.
  //
  // Returns a reference because this runs per agent per step (observation
  // feature 3): implementations forward AnnotationStore::FindCellsWithTag's
  // cache reference, alloc-free. Valid until the next annotation mutation.
  virtual const std::vector<Position>& GetGoalCells(const BaseEnv& env) const {
    (void)env;
    static const std::vector<Position> kEmpty;
    return kEmpty;
  }

  // ===========================================================================
  // Lens-specific vector observation features (the task's "extra information")
  // ===========================================================================
  // Write exactly AdditionalVectorObsSize() floats describing the observation
  // tail for `agent` — the SAME agent whose base features precede the tail.
  // BaseEnv::WriteVectorObservation resolves the agent ONCE from the
  // action/player index (GetAllAgents()[player]) and hands it to the lens, so
  // base features and tail can never describe different agents. The buffer is
  // zero-initialized by the caller; a lens that writes fewer floats leaves
  // well-defined zeros, never garbage.
  //
  // Examples:
  //   - AggroLens: relative enemy position, FSM one-hot, target cell (8)
  //   - DodgeLens: survival progress, hazard danger per direction (10)
  //
  // Default implementation writes no features.
  virtual void WriteVectorObs(const BaseEnv& env, const Agent& agent,
                              float* buffer) const {
    (void)env;
    (void)agent;
    (void)buffer;
  }

  // Return the number of additional features written by WriteVectorObs.
  // Must match the actual number of floats written.
  virtual int AdditionalVectorObsSize() const { return 0; }

  // ===========================================================================
  // Activation / Deactivation - materialize objective cells on the grid
  // ===========================================================================
  // Activate: called when the lens is set on an env. Lens may stamp objective
  // cells (Synchro, Target) at positions specified in params. Default: no-op.
  //
  // Deactivate: called before the lens is replaced. Lens should un-stamp cells
  // it placed so the next lens sees a clean slate. Default: no-op.
  //
  // Subclasses that stamp cells MUST override both and pair them symmetrically.
  virtual void Activate(BaseEnv& env, const LensParams& params) {
    (void)env;
    (void)params;
  }
  virtual void Deactivate(BaseEnv& env) {
    (void)env;
  }
};

}  // namespace companions

#endif  // COMPANIONS_ENV_TASK_LENS_H_
