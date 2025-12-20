// Copyright 2024
// TaskLens - Abstract interface for task-specific interpretation of world state

#ifndef COMPANIONS_ENV_TASK_LENS_H_
#define COMPANIONS_ENV_TASK_LENS_H_

#include <vector>
#include "../core/cell.h"

namespace companions {

class BaseEnv;  // Forward declaration

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
  virtual float ComputeReward(const BaseEnv& env, int agent_id) const = 0;

  // ===========================================================================
  // Observation masking - hide irrelevant cell types from RL agent
  // ===========================================================================
  // Called during observation generation to filter out cells not relevant
  // to the current task. For example:
  //   - SynchroLens: hides Target cells (shows as Floor)
  //   - AggroLens: hides Synchro cells (shows as Floor)
  // This focuses the RL agent's attention on task-relevant features.
  virtual CellKind MaskCell(CellKind kind) const = 0;

  // ===========================================================================
  // Optional: lens-specific vector observation features
  // ===========================================================================
  // Override to add features beyond BaseEnv's default vector observation.
  // These are appended after the base features.
  //
  // Example: AggroLens might add:
  //   - Relative position to enemy
  //   - Enemy FSM state (patrol/aggro/return)
  //   - Distance to target cell
  //
  // Default implementation adds no features.
  virtual void AppendVectorObs(const BaseEnv& env, int agent_id,
                               std::vector<float>& obs) const {
    (void)env;
    (void)agent_id;
    (void)obs;
  }

  // Return the number of additional features appended by AppendVectorObs.
  // Must match the actual number of floats added.
  virtual int AdditionalVectorObsSize() const { return 0; }
};

}  // namespace companions

#endif  // COMPANIONS_ENV_TASK_LENS_H_
