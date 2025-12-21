# TaskLens Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor environment architecture from inheritance to composition, enabling runtime task switching without snapshot serialization.

**Architecture:** BaseEnv holds physical world state. TaskLens objects interpret success/rewards/observations. Swapping tasks = swapping lens pointer. Existing env classes (SynchroEnv, AggroEnv) become thin wrappers.

**Tech Stack:** C++17, CMake, PufferLib

---

## Task 1: Create TaskLens Interface

**Files:**
- Create: `src/env/task_lens.h`
- Test: `tests/test_task_lens.cc`

**Step 1: Write the failing test**

```cpp
// tests/test_task_lens.cc
#include "../src/env/task_lens.h"
#include "../src/env/base_env.h"

TEST(TestTaskLensInterface) {
  // TaskLens should be abstract - cannot instantiate directly
  // This test just verifies the header compiles and interface exists

  // Create a mock lens for testing
  class MockLens : public TaskLens {
   public:
    bool CanOperateOn(const BaseEnv& env) const override { return true; }
    bool IsDone(const BaseEnv& env) const override { return false; }
    bool IsSuccess(const BaseEnv& env) const override { return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override { return 0.0f; }
    CellKind MaskCell(CellKind kind) const override { return kind; }
  };

  MockLens lens;
  ASSERT_TRUE(true);  // Compiles = passes
}
```

**Step 2: Run test to verify it fails**

```bash
cd pufferlib/ocean/companions/build && cmake .. && cmake --build . && ./companions_task_lens_test
```
Expected: FAIL - task_lens.h not found

**Step 3: Write minimal implementation**

```cpp
// src/env/task_lens.h
#ifndef COMPANIONS_ENV_TASK_LENS_H_
#define COMPANIONS_ENV_TASK_LENS_H_

#include <vector>
#include "../core/types.h"

namespace companions {

class BaseEnv;  // Forward declaration

// =============================================================================
// TaskLens - Abstract interface for task-specific interpretation of world state
// =============================================================================
// Lenses are stateless - all methods compute results on-demand from BaseEnv.
// This enables runtime task switching without snapshot serialization.
// =============================================================================
class TaskLens {
 public:
  virtual ~TaskLens() = default;

  // Validation - can this lens operate on the given env state?
  // Returns false if required elements are missing (e.g., no synchro cells)
  virtual bool CanOperateOn(const BaseEnv& env) const = 0;

  // Task completion
  virtual bool IsDone(const BaseEnv& env) const = 0;
  virtual bool IsSuccess(const BaseEnv& env) const = 0;

  // Reward calculation (called per agent per step)
  virtual float ComputeReward(const BaseEnv& env, int agent_id) const = 0;

  // Observation masking - hide irrelevant cell types from RL agent
  virtual CellKind MaskCell(CellKind kind) const = 0;

  // Optional: lens-specific vector observation features
  // Override to add features beyond BaseEnv's default
  virtual void AppendVectorObs(const BaseEnv& env, int agent_id,
                               std::vector<float>& obs) const {}
  virtual int AdditionalVectorObsSize() const { return 0; }
};

}  // namespace companions

#endif  // COMPANIONS_ENV_TASK_LENS_H_
```

**Step 4: Update CMakeLists.txt to include new test**

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(companions_task_lens_test test_task_lens.cc)
target_link_libraries(companions_task_lens_test companions_lib)
add_test(NAME TaskLensTest COMMAND companions_task_lens_test)
```

**Step 5: Run test to verify it passes**

```bash
cd pufferlib/ocean/companions/build && cmake .. && cmake --build . && ./companions_task_lens_test
```
Expected: PASS

**Step 6: Commit**

```bash
git add src/env/task_lens.h tests/test_task_lens.cc tests/CMakeLists.txt
git commit -m "feat: add TaskLens abstract interface for runtime task switching"
```

---

## Task 2: Add TaskLens to BaseEnv

**Files:**
- Modify: `src/env/base_env.h:52-199` (add task_lens_ member)
- Modify: `src/env/base_env.cc` (add SetTaskLens implementation)
- Test: `tests/test_task_lens.cc` (extend)

**Step 1: Write the failing test**

Add to `tests/test_task_lens.cc`:
```cpp
TEST(TestBaseEnvSetTaskLens) {
  // Create a minimal concrete env for testing
  // (We'll use SynchroEnv as it exists)
  SynchroEnv env(6, 6, 1, 1, 0, 42);

  class MockLens : public TaskLens {
   public:
    bool can_operate = true;
    bool CanOperateOn(const BaseEnv& env) const override { return can_operate; }
    bool IsDone(const BaseEnv& env) const override { return false; }
    bool IsSuccess(const BaseEnv& env) const override { return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override { return 0.5f; }
    CellKind MaskCell(CellKind kind) const override { return kind; }
  };

  auto lens = std::make_unique<MockLens>();
  bool result = env.SetTaskLens(std::move(lens));
  ASSERT_TRUE(result);
  ASSERT_TRUE(env.GetTaskLens() != nullptr);

  // Test rejection when CanOperateOn returns false
  auto bad_lens = std::make_unique<MockLens>();
  bad_lens->can_operate = false;
  result = env.SetTaskLens(std::move(bad_lens));
  ASSERT_FALSE(result);
}
```

**Step 2: Run test to verify it fails**

Expected: FAIL - SetTaskLens and GetTaskLens not defined

**Step 3: Add declarations to base_env.h**

Add to `src/env/base_env.h` after line 67 (after IsSuccess):
```cpp
  // Task lens management
  // Returns false if lens cannot operate on current state
  bool SetTaskLens(std::unique_ptr<TaskLens> lens);
  TaskLens* GetTaskLens() const { return task_lens_.get(); }
  const TaskLens* GetTaskLensConst() const { return task_lens_.get(); }
```

Add to private section (after line 198):
```cpp
  std::unique_ptr<TaskLens> task_lens_;
```

Add include at top:
```cpp
#include "task_lens.h"
```

**Step 4: Implement SetTaskLens in base_env.cc**

Add to `src/env/base_env.cc`:
```cpp
bool BaseEnv::SetTaskLens(std::unique_ptr<TaskLens> lens) {
  if (lens && !lens->CanOperateOn(*this)) {
    return false;
  }
  task_lens_ = std::move(lens);
  return true;
}
```

**Step 5: Run test to verify it passes**

```bash
cd pufferlib/ocean/companions/build && cmake --build . && ./companions_task_lens_test
```
Expected: PASS

**Step 6: Commit**

```bash
git add src/env/base_env.h src/env/base_env.cc tests/test_task_lens.cc
git commit -m "feat: add SetTaskLens/GetTaskLens to BaseEnv"
```

---

## Task 3: Implement SynchroLens

**Files:**
- Create: `src/env/synchro_lens.h`
- Create: `src/env/synchro_lens.cc`
- Test: `tests/test_task_lens.cc` (extend)

**Step 1: Write the failing test**

Add to `tests/test_task_lens.cc`:
```cpp
#include "../src/env/synchro_lens.h"

TEST(TestSynchroLensCanOperateOn) {
  // Env with synchro cells - should work
  SynchroEnv env_with_synchro(6, 6, 1, 1, 0, 42);
  SynchroLens lens;
  ASSERT_TRUE(lens.CanOperateOn(env_with_synchro));
}

TEST(TestSynchroLensIsDone) {
  SynchroEnv env(6, 6, 1, 1, 0, 42);
  SynchroLens lens;

  // Not done initially
  ASSERT_FALSE(lens.IsDone(env));

  // Run until done (move agent to synchro cell)
  // We'll use Step() to move the agent
  for (int i = 0; i < 100 && !lens.IsDone(env); ++i) {
    env.Step({{Action::Up, InteractKind::None}});
  }
  // Either agent reached synchro or horizon reached
  ASSERT_TRUE(lens.IsDone(env));
}

TEST(TestSynchroLensMaskCell) {
  SynchroLens lens;
  // Target cells should be hidden (shown as Floor)
  ASSERT_EQ(lens.MaskCell(CellKind::Target), CellKind::Floor);
  // Synchro cells should remain visible
  ASSERT_EQ(lens.MaskCell(CellKind::Synchro), CellKind::Synchro);
  // Other cells unchanged
  ASSERT_EQ(lens.MaskCell(CellKind::Wall), CellKind::Wall);
  ASSERT_EQ(lens.MaskCell(CellKind::Floor), CellKind::Floor);
}
```

**Step 2: Run test to verify it fails**

Expected: FAIL - synchro_lens.h not found

**Step 3: Write SynchroLens implementation**

```cpp
// src/env/synchro_lens.h
#ifndef COMPANIONS_ENV_SYNCHRO_LENS_H_
#define COMPANIONS_ENV_SYNCHRO_LENS_H_

#include "task_lens.h"

namespace companions {

// =============================================================================
// SynchroLens - Task: All companions must stand on synchro cells simultaneously
// =============================================================================
class SynchroLens : public TaskLens {
 public:
  // Reward constants
  static constexpr float kWinReward = 1.0f;
  static constexpr float kProgressReward = 0.01f;

  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  float ComputeReward(const BaseEnv& env, int agent_id) const override;
  CellKind MaskCell(CellKind kind) const override;

 private:
  // Helper: count agents on synchro cells
  int CountAgentsOnSynchroCells(const BaseEnv& env) const;
  // Helper: count synchro cells in grid
  int CountSynchroCells(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_SYNCHRO_LENS_H_
```

```cpp
// src/env/synchro_lens.cc
#include "synchro_lens.h"
#include "base_env.h"
#include "../core/grid.h"
#include "../core/object_manager.h"

namespace companions {

bool SynchroLens::CanOperateOn(const BaseEnv& env) const {
  return CountSynchroCells(env) > 0;
}

bool SynchroLens::IsDone(const BaseEnv& env) const {
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool SynchroLens::IsSuccess(const BaseEnv& env) const {
  int on_synchro = CountAgentsOnSynchroCells(env);
  int required = CountSynchroCells(env);
  return on_synchro >= required;
}

float SynchroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  int on_synchro = CountAgentsOnSynchroCells(env);
  int num_agents = env.NumAgents();

  // Time penalty ensures max progress per step is 0 when not winning
  float time_penalty = -static_cast<float>(num_agents) * kProgressReward;
  float reward = kProgressReward * on_synchro + time_penalty;

  if (IsSuccess(env)) {
    reward += kWinReward;
  }

  return reward;
}

CellKind SynchroLens::MaskCell(CellKind kind) const {
  // Hide Target cells (used by AggroEnv, not relevant for Synchro)
  if (kind == CellKind::Target) return CellKind::Floor;
  return kind;
}

int SynchroLens::CountAgentsOnSynchroCells(const BaseEnv& env) const {
  int count = 0;
  const auto& grid = env.GetGrid();
  for (const Agent* agent : env.GetObjectManager().GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    Position pos = agent->GetPosition();
    if (grid.GetCellKind(pos) == CellKind::Synchro) {
      count++;
    }
  }
  return count;
}

int SynchroLens::CountSynchroCells(const BaseEnv& env) const {
  int count = 0;
  const auto& grid = env.GetGrid();
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (grid.GetCellKind({r, c}) == CellKind::Synchro) {
        count++;
      }
    }
  }
  return count;
}

}  // namespace companions
```

**Step 4: Update CMakeLists.txt**

Add `src/env/synchro_lens.cc` to companions_lib sources.

**Step 5: Run test to verify it passes**

```bash
cd pufferlib/ocean/companions/build && cmake --build . && ./companions_task_lens_test
```
Expected: PASS

**Step 6: Commit**

```bash
git add src/env/synchro_lens.h src/env/synchro_lens.cc tests/test_task_lens.cc CMakeLists.txt
git commit -m "feat: implement SynchroLens with reward and masking logic"
```

---

## Task 4: Implement AggroLens

**Files:**
- Create: `src/env/aggro_lens.h`
- Create: `src/env/aggro_lens.cc`
- Test: `tests/test_task_lens.cc` (extend)

**Step 1: Write the failing test**

Add to `tests/test_task_lens.cc`:
```cpp
#include "../src/env/aggro_lens.h"
#include "../src/env/aggro_env.h"

TEST(TestAggroLensCanOperateOn) {
  // Env with target cell and patrol path - should work
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  AggroLens lens;
  ASSERT_TRUE(lens.CanOperateOn(env));

  // SynchroEnv has no target cell - should fail
  SynchroEnv synchro_env(6, 6, 1, 1, 0, 42);
  ASSERT_FALSE(lens.CanOperateOn(synchro_env));
}

TEST(TestAggroLensMaskCell) {
  AggroLens lens;
  // Synchro cells should be hidden (shown as Floor)
  ASSERT_EQ(lens.MaskCell(CellKind::Synchro), CellKind::Floor);
  // Target cells should remain visible
  ASSERT_EQ(lens.MaskCell(CellKind::Target), CellKind::Target);
}
```

**Step 2: Run test to verify it fails**

Expected: FAIL - aggro_lens.h not found

**Step 3: Write AggroLens implementation**

```cpp
// src/env/aggro_lens.h
#ifndef COMPANIONS_ENV_AGGRO_LENS_H_
#define COMPANIONS_ENV_AGGRO_LENS_H_

#include "task_lens.h"

namespace companions {

// =============================================================================
// AggroLens - Task: Lure enemy to target cell
// =============================================================================
class AggroLens : public TaskLens {
 public:
  static constexpr float kWinReward = 1.0f;
  static constexpr float kTimePenalty = -0.01f;

  bool CanOperateOn(const BaseEnv& env) const override;
  bool IsDone(const BaseEnv& env) const override;
  bool IsSuccess(const BaseEnv& env) const override;
  float ComputeReward(const BaseEnv& env, int agent_id) const override;
  CellKind MaskCell(CellKind kind) const override;

  // Extended observation features for AggroEnv
  void AppendVectorObs(const BaseEnv& env, int agent_id,
                       std::vector<float>& obs) const override;
  int AdditionalVectorObsSize() const override { return 8; }

 private:
  Position FindTargetCell(const BaseEnv& env) const;
  bool HasPatrolPath(const BaseEnv& env) const;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_LENS_H_
```

```cpp
// src/env/aggro_lens.cc
#include "aggro_lens.h"
#include "base_env.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/fsm/fsm_states.h"

namespace companions {

bool AggroLens::CanOperateOn(const BaseEnv& env) const {
  Position target = FindTargetCell(env);
  return (target.row != -1) && HasPatrolPath(env);
}

bool AggroLens::IsDone(const BaseEnv& env) const {
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool AggroLens::IsSuccess(const BaseEnv& env) const {
  Position target = FindTargetCell(env);
  if (target.row == -1) return false;

  // Check if any FSM agent (enemy) is on target cell
  for (const Agent* agent : env.GetObjectManager().GetAllAgents()) {
    if (auto* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      if (fsm_agent->GetPosition() == target) {
        return true;
      }
    }
  }
  return false;
}

float AggroLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  if (IsSuccess(env)) {
    return kWinReward;
  }
  return kTimePenalty;
}

CellKind AggroLens::MaskCell(CellKind kind) const {
  // Hide Synchro cells (used by SynchroEnv, not relevant for Aggro)
  if (kind == CellKind::Synchro) return CellKind::Floor;
  return kind;
}

Position AggroLens::FindTargetCell(const BaseEnv& env) const {
  const auto& grid = env.GetGrid();
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (grid.GetCellKind({r, c}) == CellKind::Target) {
        return {r, c};
      }
    }
  }
  return {-1, -1};  // Not found
}

bool AggroLens::HasPatrolPath(const BaseEnv& env) const {
  // Check if there's a patrol path in the snapshot
  // BaseEnv needs GetPatrolPath() or we check for FSM agents
  for (const Agent* agent : env.GetObjectManager().GetAllAgents()) {
    if (auto* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      const auto& ctx = fsm_agent->GetFSMContext();
      if (!ctx.patrol_path.empty()) {
        return true;
      }
    }
  }
  return false;
}

void AggroLens::AppendVectorObs(const BaseEnv& env, int agent_id,
                                std::vector<float>& obs) const {
  // Implementation matches AggroEnv::VectorObservation additional features
  // See aggro_env.cc lines 317-378
  auto agents = env.GetObjectManager().GetAllAgents();
  if (agent_id < 0 || agent_id >= static_cast<int>(agents.size())) {
    obs.resize(obs.size() + 8, 0.0f);
    return;
  }

  const Agent* current_agent = agents[agent_id];
  Position my_pos = current_agent->GetPosition();
  float max_dim = static_cast<float>(std::max(env.GetRows(), env.GetCols()));

  // Find enemy
  const AgentFSM* enemy = nullptr;
  for (const Agent* agent : agents) {
    if (auto* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      enemy = fsm_agent;
      break;
    }
  }

  if (enemy) {
    Position enemy_pos = enemy->GetPosition();
    obs.push_back(static_cast<float>(enemy_pos.row - my_pos.row) / max_dim);
    obs.push_back(static_cast<float>(enemy_pos.col - my_pos.col) / max_dim);

    float enemy_dist = static_cast<float>(std::abs(enemy_pos.row - my_pos.row) +
                                          std::abs(enemy_pos.col - my_pos.col));
    obs.push_back(enemy_dist / (max_dim * 2.0f));

    // FSM state one-hot
    const FSMState* state = enemy->GetCurrentState();
    obs.push_back(state == &PatrolState::Instance() ? 1.0f : 0.0f);
    obs.push_back(state == &AggroState::Instance() ? 1.0f : 0.0f);
    obs.push_back(state == &ReturnToPatrolState::Instance() ? 1.0f : 0.0f);
  } else {
    for (int i = 0; i < 6; ++i) obs.push_back(0.0f);
  }

  // Relative position to target
  Position target = FindTargetCell(env);
  obs.push_back(static_cast<float>(target.row - my_pos.row) / max_dim);
  obs.push_back(static_cast<float>(target.col - my_pos.col) / max_dim);
}

}  // namespace companions
```

**Step 4: Update CMakeLists.txt**

Add `src/env/aggro_lens.cc` to companions_lib sources.

**Step 5: Run test to verify it passes**

```bash
cd pufferlib/ocean/companions/build && cmake --build . && ./companions_task_lens_test
```
Expected: PASS

**Step 6: Commit**

```bash
git add src/env/aggro_lens.h src/env/aggro_lens.cc tests/test_task_lens.cc CMakeLists.txt
git commit -m "feat: implement AggroLens with FSM state observations"
```

---

## Task 5: Implement DodgeLens

**Files:**
- Create: `src/env/dodge_lens.h`
- Create: `src/env/dodge_lens.cc`
- Test: `tests/test_task_lens.cc` (extend)

**Step 1-6:** Follow same pattern as Tasks 3-4.

Key differences for DodgeLens:
- `IsSuccess()`: Check if tick >= horizon AND no companions currently incapacitated
- `MaskCell()`: Hide both Synchro and Target cells
- No additional vector obs features

**Commit message:**
```bash
git commit -m "feat: implement DodgeLens for survival-based task"
```

---

## Task 6: Add Task Switching Test

**Files:**
- Test: `tests/test_task_lens.cc` (extend)

**Step 1: Write the test**

```cpp
TEST(TestRuntimeTaskSwitching) {
  // Start with SynchroEnv (has both synchro and no target cells by default)
  // We need an env that can support both lenses

  // Create env with synchro cells
  SynchroEnv env(8, 8, 2, 2, 0, 42);

  // Set SynchroLens
  auto synchro_lens = std::make_unique<SynchroLens>();
  ASSERT_TRUE(env.SetTaskLens(std::move(synchro_lens)));

  // Verify SynchroLens is active
  ASSERT_TRUE(env.GetTaskLens() != nullptr);
  ASSERT_EQ(env.GetTaskLens()->MaskCell(CellKind::Target), CellKind::Floor);

  // Take a step with SynchroLens
  auto result1 = env.Step({{Action::Stay, InteractKind::None},
                           {Action::Stay, InteractKind::None}});

  // Verify world state is preserved after lens swap would happen
  int tick_before = env.GetTick();
  Position agent_pos_before = env.GetObjectManager().GetAllAgents()[0]->GetPosition();

  // Swap to a different lens (mock that accepts any env)
  class AcceptAllLens : public TaskLens {
   public:
    bool CanOperateOn(const BaseEnv& env) const override { return true; }
    bool IsDone(const BaseEnv& env) const override { return false; }
    bool IsSuccess(const BaseEnv& env) const override { return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override { return 0.0f; }
    CellKind MaskCell(CellKind kind) const override { return kind; }
  };

  auto new_lens = std::make_unique<AcceptAllLens>();
  ASSERT_TRUE(env.SetTaskLens(std::move(new_lens)));

  // Verify world state is UNCHANGED
  ASSERT_EQ(env.GetTick(), tick_before);
  ASSERT_EQ(env.GetObjectManager().GetAllAgents()[0]->GetPosition().row,
            agent_pos_before.row);
  ASSERT_EQ(env.GetObjectManager().GetAllAgents()[0]->GetPosition().col,
            agent_pos_before.col);

  // Can still step with new lens
  auto result2 = env.Step({{Action::Up, InteractKind::None},
                           {Action::Up, InteractKind::None}});
  ASSERT_EQ(env.GetTick(), tick_before + 1);
}
```

**Step 2: Run test to verify it passes**

```bash
cd pufferlib/ocean/companions/build && cmake --build . && ./companions_task_lens_test
```
Expected: PASS

**Step 3: Commit**

```bash
git add tests/test_task_lens.cc
git commit -m "test: verify runtime task switching preserves world state"
```

---

## Task 7: Refactor SynchroEnv to Use SynchroLens

**Files:**
- Modify: `src/env/synchro_env.h:17-103`
- Modify: `src/env/synchro_env.cc:1-245`
- Test: Existing tests must still pass

**Step 1: Run existing tests to establish baseline**

```bash
cd pufferlib/ocean/companions/build && ctest -R synchro
```
Expected: All tests PASS (this is our baseline)

**Step 2: Modify SynchroEnv constructor to set lens**

In `synchro_env.cc`, after `Reset()` call in constructor:
```cpp
SynchroEnv::SynchroEnv(int rows, int cols, int num_companions, int num_synchro,
                       int map_complexity, unsigned int seed, int d4_transform,
                       int horizon)
    : BaseEnv(rows, cols, d4_transform),
      num_companions_(num_companions),
      num_synchro_(num_synchro),
      map_complexity_(std::max(0, std::min(5, map_complexity))),
      rng_(seed) {
  horizon_ = horizon;
  ValidateConfig();
  Reset();
  SetTaskLens(std::make_unique<SynchroLens>());  // NEW
}
```

**Step 3: Delegate IsDone/IsSuccess to lens (optional - can keep for backward compat)**

For now, keep existing implementations. The lens is available for runtime switching.

**Step 4: Run tests to verify no regression**

```bash
cd pufferlib/ocean/companions/build && ctest -R synchro
```
Expected: All tests PASS

**Step 5: Commit**

```bash
git add src/env/synchro_env.h src/env/synchro_env.cc
git commit -m "refactor: SynchroEnv sets SynchroLens in constructor"
```

---

## Task 8: Refactor AggroEnv to Use AggroLens

**Files:**
- Modify: `src/env/aggro_env.h:34-123`
- Modify: `src/env/aggro_env.cc:1-416`
- Test: Existing tests must still pass

**Step 1: Run existing tests**

```bash
cd pufferlib/ocean/companions/build && ctest -R aggro
```
Expected: All tests PASS

**Step 2: Modify AggroEnv constructor**

```cpp
AggroEnv::AggroEnv(...)
    : BaseEnv(...) {
  // ... existing code ...
  Reset();
  SetTaskLens(std::make_unique<AggroLens>());  // NEW
}
```

**Step 3: Run tests**

```bash
cd pufferlib/ocean/companions/build && ctest -R aggro
```
Expected: All tests PASS

**Step 4: Commit**

```bash
git add src/env/aggro_env.h src/env/aggro_env.cc
git commit -m "refactor: AggroEnv sets AggroLens in constructor"
```

---

## Task 9: Add GetPatrolPath to BaseEnv

**Files:**
- Modify: `src/env/base_env.h`
- Modify: `src/env/base_env.cc`

**Purpose:** Allow lenses to query patrol path without dynamic_cast.

**Step 1: Add declaration**

```cpp
// In base_env.h, public section
virtual const std::vector<Position>& GetPatrolPath() const;
```

**Step 2: Add default implementation**

```cpp
// In base_env.cc
const std::vector<Position>& BaseEnv::GetPatrolPath() const {
  static const std::vector<Position> empty;
  return empty;
}
```

**Step 3: Override in AggroEnv**

Already has `GetPatrolPath()` - just ensure it's marked `override`.

**Step 4: Run tests**

```bash
cd pufferlib/ocean/companions/build && ctest
```
Expected: All tests PASS

**Step 5: Commit**

```bash
git add src/env/base_env.h src/env/base_env.cc src/env/aggro_env.h
git commit -m "feat: add virtual GetPatrolPath to BaseEnv for lens queries"
```

---

## Task 10: Final Integration Test

**Files:**
- Test: `tests/test_task_lens.cc` (extend)

**Step 1: Write comprehensive integration test**

```cpp
TEST(TestFullTaskSwitchingWorkflow) {
  // Simulate game workflow: load level, switch tasks as plan progresses

  // 1. Create a level with BOTH synchro cells AND target cell
  //    This simulates a game level designed for multiple tasks

  // For this test, we'll manually set up such an env
  // In real usage, MapGenerator would create this

  SynchroEnv env(10, 10, 2, 2, 0, 42);

  // Add a target cell manually for testing
  env.GetMutableGrid().SetCell({5, 5}, CellKind::Target);

  // Start with SynchroLens
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));

  // Run a few steps under SynchroLens
  for (int i = 0; i < 5; ++i) {
    env.Step({{Action::Right, InteractKind::None},
              {Action::Left, InteractKind::None}});
  }

  // Switch to AggroLens (simulating plan progression)
  // Note: AggroLens needs patrol path, so this might fail
  // This tests the validation
  auto aggro_lens = std::make_unique<AggroLens>();
  bool can_switch = env.SetTaskLens(std::move(aggro_lens));

  // If no patrol path, lens correctly rejects
  // This is expected behavior - validates CanOperateOn works

  // Switch back to SynchroLens
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));

  // Continue stepping - no crash, world state preserved
  auto result = env.Step({{Action::Up, InteractKind::None},
                          {Action::Up, InteractKind::None}});

  ASSERT_TRUE(result.rewards.size() == 2);
}
```

**Step 2: Run all tests**

```bash
cd pufferlib/ocean/companions/build && ctest --output-on-failure
```
Expected: All tests PASS

**Step 3: Commit**

```bash
git add tests/test_task_lens.cc
git commit -m "test: add full task-switching workflow integration test"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Create TaskLens interface | `task_lens.h`, test |
| 2 | Add TaskLens to BaseEnv | `base_env.h/.cc` |
| 3 | Implement SynchroLens | `synchro_lens.h/.cc` |
| 4 | Implement AggroLens | `aggro_lens.h/.cc` |
| 5 | Implement DodgeLens | `dodge_lens.h/.cc` |
| 6 | Test runtime task switching | test |
| 7 | Refactor SynchroEnv | `synchro_env.h/.cc` |
| 8 | Refactor AggroEnv | `aggro_env.h/.cc` |
| 9 | Add GetPatrolPath to BaseEnv | `base_env.h/.cc` |
| 10 | Integration test | test |

**Total estimated tasks:** 10 major tasks, ~50 steps

**Run full test suite after each task:**
```bash
cd pufferlib/ocean/companions/build && cmake --build . && ctest --output-on-failure
```
