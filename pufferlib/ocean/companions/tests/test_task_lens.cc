// Copyright 2024
// Test suite for TaskLens interface

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/env/task_lens.h"
#include "../src/env/base_env.h"
#include "../src/env/synchro_env.h"
#include "../src/env/synchro_lens.h"
#include "../src/env/aggro_env.h"
#include "../src/env/aggro_lens.h"
#include "../src/env/dodge_lens.h"

using namespace companions;

// =============================================================================
// Test macros (same pattern as other test files)
// =============================================================================
#define TEST(name) \
  void name(); \
  struct name##_registrar { \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance; \
  void name()

#define ASSERT_TRUE(cond) \
  if (!(cond)) { \
    std::ostringstream oss; \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_FALSE(cond) \
  if (cond) { \
    std::ostringstream oss; \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_EQ(a, b) \
  if ((a) != (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// TaskLens Interface Tests
// =============================================================================

TEST(TestTaskLensInterface) {
  // TaskLens should be abstract - cannot instantiate directly
  // This test just verifies the header compiles and interface exists

  // Create a mock lens for testing
  class MockLens : public TaskLens {
   public:
    bool CanOperateOn(const BaseEnv& env) const override {
      (void)env;  // Suppress unused parameter warning
      return true;
    }
    bool IsDone(const BaseEnv& env) const override {
      (void)env;
      return false;
    }
    bool IsSuccess(const BaseEnv& env) const override {
      (void)env;
      return false;
    }
    float ComputeReward(const BaseEnv& env, int agent_id) const override {
      (void)env;
      (void)agent_id;
      return 0.0f;
    }
    std::string GetObjectiveString(const BaseEnv& env) const override {
      (void)env;
      return "mock";
    }
  };

  MockLens lens;

  // Verify optional methods have defaults
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 0);

  ASSERT_TRUE(true);  // Compiles = passes
}

TEST(TestTaskLensVirtualDestructor) {
  // Verify virtual destructor works (important for polymorphic use)
  class MockLens : public TaskLens {
   public:
    bool* destroyed_flag;
    explicit MockLens(bool* flag) : destroyed_flag(flag) {}
    ~MockLens() override { *destroyed_flag = true; }

    bool CanOperateOn(const BaseEnv& env) const override { (void)env; return true; }
    bool IsDone(const BaseEnv& env) const override { (void)env; return false; }
    bool IsSuccess(const BaseEnv& env) const override { (void)env; return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override {
      (void)env; (void)agent_id; return 0.0f;
    }
    std::string GetObjectiveString(const BaseEnv& env) const override {
      (void)env; return "mock";
    }
  };

  bool destroyed = false;
  {
    TaskLens* lens = new MockLens(&destroyed);
    delete lens;  // Should call MockLens destructor via virtual
  }
  ASSERT_TRUE(destroyed);
}

TEST(TestTaskLensOptionalMethods) {
  // Test that optional methods have sensible defaults
  class MinimalLens : public TaskLens {
   public:
    bool CanOperateOn(const BaseEnv& env) const override { (void)env; return true; }
    bool IsDone(const BaseEnv& env) const override { (void)env; return false; }
    bool IsSuccess(const BaseEnv& env) const override { (void)env; return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override {
      (void)env; (void)agent_id; return 0.0f;
    }
    std::string GetObjectiveString(const BaseEnv& env) const override {
      (void)env; return "minimal";
    }
    // Note: NOT overriding AppendVectorObs or AdditionalVectorObsSize
  };

  MinimalLens lens;

  // Defaults should be no-op and zero
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 0);

  // AppendVectorObs should not modify the vector (no-op default)
  std::vector<float> obs = {1.0f, 2.0f, 3.0f};
  // We can't call AppendVectorObs without a valid env, but the default is empty
}

TEST(TestBaseEnvSetTaskLens) {
  // Create a minimal concrete env for testing
  SynchroEnv env(6, 6, 1, 1, 0, 42);

  class MockLens : public TaskLens {
   public:
    bool can_operate = true;
    bool CanOperateOn(const BaseEnv& env) const override { (void)env; return can_operate; }
    bool IsDone(const BaseEnv& env) const override { (void)env; return false; }
    bool IsSuccess(const BaseEnv& env) const override { (void)env; return false; }
    float ComputeReward(const BaseEnv& env, int agent_id) const override {
      (void)env; (void)agent_id; return 0.5f;
    }
    std::string GetObjectiveString(const BaseEnv& env) const override {
      (void)env; return "mock";
    }
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

// =============================================================================
// SynchroLens Tests
// =============================================================================

TEST(TestSynchroLensCanOperateOn) {
  SynchroEnv env(6, 6, 1, 1, 0, 42);
  SynchroLens lens;
  ASSERT_TRUE(lens.CanOperateOn(env));
}

TEST(TestSynchroLensIsDoneTimeout) {
  // Create env with horizon=10
  SynchroEnv env(6, 6, 1, 1, 0, 42, 0, 10);
  SynchroLens lens;

  // Initially not done
  ASSERT_FALSE(lens.IsDone(env));

  // Step until horizon (use Stay action = 0)
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay, InteractAction::None)};
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  // Should be done due to timeout
  ASSERT_TRUE(lens.IsDone(env));
}

TEST(TestSynchroLensRewardStructure) {
  // Create env with 2 agents and 2 synchro cells
  SynchroEnv env(6, 6, 2, 2, 0, 42);
  SynchroLens lens;

  // Initial reward should be negative (time penalty, no agents on synchro)
  float reward = lens.ComputeReward(env, 0);
  // Time penalty = -num_agents * kProgressReward = -2 * 0.01 = -0.02
  // Progress = 0 (no agents on synchro initially, most likely)
  ASSERT_TRUE(reward <= 0.0f);
}

// =============================================================================
// AggroLens Tests
// =============================================================================

TEST(TestAggroLensCanOperateOn) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  AggroLens lens;
  ASSERT_TRUE(lens.CanOperateOn(env));

  // SynchroEnv has no target cell - should fail
  SynchroEnv synchro_env(6, 6, 1, 1, 0, 42);
  ASSERT_FALSE(lens.CanOperateOn(synchro_env));
}

TEST(TestAggroLensIsDoneTimeout) {
  // Create env with horizon=10
  AggroEnv env(10, 1, EnemyType::Zombie, 42, 0, 10);
  AggroLens lens;

  // Initially not done
  ASSERT_FALSE(lens.IsDone(env));

  // Step until horizon (use Stay action for each agent: 1 companion + 1 zombie = 2 agents)
  int num_agents = env.NumAgents();
  std::vector<Action> actions(num_agents, EncodeAction(MovementAction::Stay, InteractAction::None));
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  // Should be done due to timeout
  ASSERT_TRUE(lens.IsDone(env));
}

TEST(TestAggroLensRewardStructure) {
  // Create AggroEnv
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  AggroLens lens;

  // Initial reward should be time penalty (not on target yet)
  float reward = lens.ComputeReward(env, 0);
  ASSERT_EQ(reward, AggroLens::kTimePenalty);
}

TEST(TestAggroLensAdditionalObsSize) {
  AggroLens lens;
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 8);
}

TEST(TestAggroLensAppendVectorObs) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  AggroLens lens;

  std::vector<float> obs;
  lens.AppendVectorObs(env, 0, obs);

  // Should have 8 features
  ASSERT_EQ(static_cast<int>(obs.size()), 8);

  // All values should be in reasonable range [-1, 1] for normalized positions
  for (size_t i = 0; i < obs.size(); ++i) {
    ASSERT_TRUE(obs[i] >= -1.0f && obs[i] <= 1.0f);
  }
}

// =============================================================================
// DodgeLens Tests
// =============================================================================

TEST(TestDodgeLensCanOperateOn) {
  SynchroEnv env(6, 6, 1, 1, 0, 42);
  DodgeLens lens;
  ASSERT_TRUE(lens.CanOperateOn(env));  // Always works
}

TEST(TestDodgeLensReward) {
  SynchroEnv env(6, 6, 1, 1, 0, 42);
  DodgeLens lens;
  // All companions alive = survival reward
  float reward = lens.ComputeReward(env, 0);
  ASSERT_TRUE(reward > 0);
  ASSERT_EQ(reward, DodgeLens::kSurvivalReward);
}

TEST(TestDodgeLensIsDoneTimeout) {
  // Create env with horizon=10
  SynchroEnv env(6, 6, 1, 1, 0, 42, 0, 10);
  DodgeLens lens;

  // Initially not done
  ASSERT_FALSE(lens.IsDone(env));

  // Step until horizon (use Stay action = 0)
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay, InteractAction::None)};
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  // Should be done due to timeout
  ASSERT_TRUE(lens.IsDone(env));
  // And since no one died, it should be success
  ASSERT_TRUE(lens.IsSuccess(env));
}

TEST(TestDodgeLensIsSuccessRequiresSurvival) {
  SynchroEnv env(6, 6, 1, 1, 0, 42, 0, 10);
  DodgeLens lens;

  // Initially not done and not success (need to reach horizon)
  ASSERT_FALSE(lens.IsDone(env));
  ASSERT_FALSE(lens.IsSuccess(env));

  // Step to horizon
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay, InteractAction::None)};
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  // Success only if survived to horizon
  ASSERT_TRUE(lens.IsSuccess(env));
}

// =============================================================================
// Runtime Task Switching Tests
// =============================================================================

TEST(TestRuntimeTaskSwitching) {
  // Create env with synchro cells
  SynchroEnv env(8, 8, 2, 2, 0, 42);

  // Set SynchroLens
  auto synchro_lens = std::make_unique<SynchroLens>();
  ASSERT_TRUE(env.SetTaskLens(std::move(synchro_lens)));

  // Verify SynchroLens is active
  ASSERT_TRUE(env.GetTaskLens() != nullptr);

  // Take a step with SynchroLens
  std::vector<Action> stay_actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };
  env.Step(stay_actions);

  // Capture world state before lens swap
  int tick_before = env.GetTick();
  auto agents = env.GetObjectManager().GetAllAgents();
  Position agent_pos_before = agents[0]->GetPosition();

  // Swap to DodgeLens (accepts any env)
  auto dodge_lens = std::make_unique<DodgeLens>();
  ASSERT_TRUE(env.SetTaskLens(std::move(dodge_lens)));

  // Verify world state is UNCHANGED after lens swap
  ASSERT_EQ(env.GetTick(), tick_before);
  agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents[0]->GetPosition().row, agent_pos_before.row);
  ASSERT_EQ(agents[0]->GetPosition().col, agent_pos_before.col);

  // Verify new lens is active (DodgeLens is stateless - just confirm it's set)
  ASSERT_TRUE(dynamic_cast<DodgeLens*>(env.GetTaskLens()) != nullptr);

  // Can still step with new lens
  std::vector<Action> up_actions = {
    EncodeAction(MovementAction::Up),
    EncodeAction(MovementAction::Up)
  };
  auto result = env.Step(up_actions);
  ASSERT_EQ(env.GetTick(), tick_before + 1);
  ASSERT_EQ(result.rewards.size(), 2);
}

TEST(TestSetTaskLensFailsForIncompatibleLens) {
  // A level generated with no aggro config should NOT be compatible with AggroLens
  SynchroEnv env(6, 6, 1, 1, 0, 42);  // Basic synchro level - no Target cells, no patrol path

  // Set initial SynchroLens
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));
  TaskLens* original_lens = env.GetTaskLens();

  // Try to switch to AggroLens - should fail because:
  // 1. No Target cell in grid
  // 2. No patrol path defined
  auto aggro_lens = std::make_unique<AggroLens>();
  ASSERT_FALSE(env.SetTaskLens(std::move(aggro_lens)));

  // Lens should remain unchanged after failed switch
  ASSERT_EQ(env.GetTaskLens(), original_lens);
}

// =============================================================================
// Full Task Switching Workflow Integration Test
// =============================================================================

TEST(TestFullTaskSwitchingWorkflow) {
  // Simulate game workflow: load level, switch tasks as plan progresses

  // Create an env with synchro cells
  SynchroEnv env(10, 10, 2, 2, 0, 42);

  // Manually add an AggroTarget annotation to simulate a rich game level.
  env.GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Cell, Position{5, 5}, kInvalidObjectId},
      Annotation{SemanticTag::AggroTarget, {}, -1});

  // Start with SynchroLens
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));

  // Run a few steps under SynchroLens
  std::vector<Action> step_actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Left)
  };
  for (int i = 0; i < 5; ++i) {
    env.Step(step_actions);
  }

  // Attempt to switch to AggroLens (will fail - no patrol path)
  auto aggro_lens = std::make_unique<AggroLens>();
  bool can_switch = env.SetTaskLens(std::move(aggro_lens));
  // AggroLens correctly rejects - no patrol path
  ASSERT_FALSE(can_switch);

  // Switch to DodgeLens (accepts any env)
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<DodgeLens>()));

  // Verify world state preserved after multiple lens swaps
  ASSERT_EQ(env.GetTick(), 5);

  // Switch back to SynchroLens
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));

  // Continue stepping - no crash, world state preserved
  std::vector<Action> up_actions = {
    EncodeAction(MovementAction::Up),
    EncodeAction(MovementAction::Up)
  };
  auto result = env.Step(up_actions);

  ASSERT_EQ(result.rewards.size(), 2);
  ASSERT_EQ(env.GetTick(), 6);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    try {
      test.func();
      std::cout << "[PASS] " << test.name << std::endl;
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << test.name << ": " << e.what() << std::endl;
      failed++;
    }
  }

  std::cout << "\n" << passed << " passed, " << failed << " failed" << std::endl;
  return failed > 0 ? 1 : 0;
}
