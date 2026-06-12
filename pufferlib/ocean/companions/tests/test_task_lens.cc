// Copyright 2024
// Test suite for TaskLens interface

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/effect_config.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/env/task_lens.h"
#include "../src/env/base_env.h"
#include "../src/env/synchro_env.h"
#include "../src/env/synchro_lens.h"
#include "../src/env/aggro_env.h"
#include "../src/env/aggro_lens.h"
#include "../src/env/dodge_env.h"
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
    Kind GetKind() const override { return kUnknown; }
    double ComputeReward(const BaseEnv& env, int agent_id) const override {
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
    Kind GetKind() const override { return kUnknown; }
    double ComputeReward(const BaseEnv& env, int agent_id) const override {
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
    Kind GetKind() const override { return kUnknown; }
    double ComputeReward(const BaseEnv& env, int agent_id) const override {
      (void)env; (void)agent_id; return 0.0f;
    }
    std::string GetObjectiveString(const BaseEnv& env) const override {
      (void)env; return "minimal";
    }
    // Note: NOT overriding WriteVectorObs or AdditionalVectorObsSize
  };

  MinimalLens lens;

  // Defaults should be no-op and zero
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 0);

  // WriteVectorObs default is a no-op: with a 0-size tail it is never called
  // by BaseEnv, and calling it must not touch the buffer.
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
    Kind GetKind() const override { return kUnknown; }
    double ComputeReward(const BaseEnv& env, int agent_id) const override {
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

TEST(TestSynchroLensIsSuccessFalseWithZeroGoals) {
  // A world with zero SynchroGoal annotations must NOT report success:
  // "0 agents on 0 goals" is vacuous, not victory. This matters in-game,
  // where GameEnv accepts arbitrary snapshots (e.g. an aggro level) while
  // SynchroLens is active — a vacuous true would latch spurious success.
  // Consistent with CanOperateOn, which requires > 0 goals.
  DodgeEnv env(9, 1, 100, 50, 42);  // No synchro goals anywhere
  SynchroLens lens;
  ASSERT_EQ(static_cast<int>(
                env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal).size()),
            0);
  ASSERT_FALSE(lens.CanOperateOn(env));
  ASSERT_FALSE(lens.IsSuccess(env));
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
  double reward = lens.ComputeReward(env, 0);
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
  double reward = lens.ComputeReward(env, 0);
  ASSERT_EQ(reward, AggroLens::kTimePenalty);
}

TEST(TestAggroLensAdditionalObsSize) {
  AggroLens lens;
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 8);
}

TEST(TestAggroLensWriteVectorObs) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  AggroLens lens;

  // Should declare 8 features
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 8);

  const Companion* companion = env.GetObjectManager().GetAllCompanions()[0];
  std::vector<float> obs(lens.AdditionalVectorObsSize(), 0.0f);
  lens.WriteVectorObs(env, *companion, obs.data());

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
  // All companions alive and not yet at horizon = per-tick survival bonus only
  double reward = lens.ComputeReward(env, 0);
  ASSERT_TRUE(reward > 0);
  ASSERT_EQ(reward, DodgeLens::kSurvivalBonus);
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
// Universal observation layout (7-plane tensor + base-9 + lens tail vector)
//
// These tests pin the obs contract established by the lens-contract
// remediation: the model input is f(world state, active lens). The tensor is
// 7 planes for every task; the vector is BaseEnv's 9 features followed by the
// active lens's WriteVectorObs tail, both describing GetAllAgents()[player].
// =============================================================================

namespace {

// Register deterministic hazard configs for observation tests.
void RegisterObsTestEffects() {
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  if (!registry.GetConfig("obs_test_active")) {
    EffectConfig active;
    active.name = "obs_test_active";
    active.telegraph_ticks = 0;  // Spawns directly in the active phase
    active.active_ticks = 5;
    active.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // 3x3 full
    active.filter = TargetFilter::Companion;
    active.damage = 0;  // Observation-only: don't kill the test subject
    active.telegraph_visible = true;
    registry.RegisterConfig(active);
  }

  if (!registry.GetConfig("obs_test_telegraph")) {
    EffectConfig telegraph;
    telegraph.name = "obs_test_telegraph";
    telegraph.telegraph_ticks = 5;  // Stays in telegraph for 5 ticks
    telegraph.active_ticks = 1;
    telegraph.area = {1};  // Single cell
    telegraph.filter = TargetFilter::Companion;
    telegraph.damage = 0;
    telegraph.telegraph_visible = true;
    registry.RegisterConfig(telegraph);
  }
}

// Pin the (single) dodge agent to a deterministic interior cell so hazard
// placement relative to it stays in bounds regardless of spawn seed.
Position PinFirstAgentAt(BaseEnv& env, Position pos) {
  Agent* agent = env.GetMutableObjectManager().GetAllAgents()[0];
  env.GetMutableObjectManager().UpdatePosition(agent->GetId(), pos);
  return pos;
}

}  // namespace

TEST(TestUniversalTensorShapeSevenPlanes) {
  // Every env exposes the same 7-plane tensor regardless of task.
  SynchroEnv synchro(42);
  auto shape = synchro.ObservationShape();
  ASSERT_EQ(shape.size(), 3u);
  ASSERT_EQ(shape[0], 7);

  AggroEnv aggro(10, 1, EnemyType::Goblin, 42);
  ASSERT_EQ(aggro.ObservationShape()[0], 7);

  DodgeEnv dodge(7, 1, 3, 50, 42);
  ASSERT_EQ(dodge.ObservationShape()[0], 7);
}

TEST(TestSynchroTensorHazardPlanesEmptyWithoutEffects) {
  // Tasks without hazards still carry planes 5/6 - all zeros.
  SynchroEnv env(42);
  std::vector<float> obs;
  env.ObservationTensor(obs, 0);

  int rows = env.GetRows();
  int cols = env.GetCols();
  ASSERT_EQ(static_cast<int>(obs.size()), 7 * rows * cols);
  for (int i = 5 * rows * cols; i < 7 * rows * cols; ++i) {
    ASSERT_EQ(obs[i], 0.0f);
  }
}

TEST(TestDodgeTensorEnvPathMatchesBaseWriter) {
  // Transitional parity check for the verbatim move of DodgeEnv's hazard
  // plane logic into BaseEnv::WriteObservationTensor: the polymorphic env
  // path and the direct base writer must produce identical tensors for the
  // same world state.
  RegisterObsTestEffects();
  DodgeEnv env(9, 1, 100, 50, 42);  // hazard_interval=100: no random spawns

  Position player_pos = PinFirstAgentAt(env, {4, 4});
  // One active hazard and one telegraphed hazard near the player.
  env.SpawnEffect("obs_test_active",
                  EffectTarget::AtCell({player_pos.row - 2, player_pos.col}));
  env.SpawnEffect("obs_test_telegraph",
                  EffectTarget::AtCell({player_pos.row, player_pos.col + 2}));

  std::vector<float> env_path;
  env.ObservationTensor(env_path, 0);

  int total = 7 * env.GetRows() * env.GetCols();
  ASSERT_EQ(static_cast<int>(env_path.size()), total);

  std::vector<float> base_path(total, -1.0f);
  env.WriteObservationTensor(base_path.data(), 0);

  for (int i = 0; i < total; ++i) {
    ASSERT_EQ(env_path[i], base_path[i]);
  }
}

TEST(TestDodgeTensorHazardPlaneSemantics) {
  // Plane 5 marks telegraphed hazard cells, plane 6 marks active hazard
  // cells (same plane order the pre-refactor DodgeEnv override used).
  RegisterObsTestEffects();
  DodgeEnv env(9, 1, 100, 50, 42);

  int rows = env.GetRows();
  int cols = env.GetCols();
  Position player_pos = PinFirstAgentAt(env, {4, 4});

  Position active_center{player_pos.row - 2, player_pos.col};
  Position telegraph_cell{player_pos.row, player_pos.col + 2};
  env.SpawnEffect("obs_test_active", EffectTarget::AtCell(active_center));
  env.SpawnEffect("obs_test_telegraph", EffectTarget::AtCell(telegraph_cell));

  std::vector<float> obs;
  env.ObservationTensor(obs, 0);

  auto plane_at = [&](int plane, Position p) {
    return obs[plane * rows * cols + p.row * cols + p.col];
  };

  // Telegraphed single-cell hazard shows up in plane 5 only.
  ASSERT_EQ(plane_at(5, telegraph_cell), 1.0f);
  ASSERT_EQ(plane_at(6, telegraph_cell), 0.0f);

  // Active 3x3 hazard shows up in plane 6 (check center + a corner).
  ASSERT_EQ(plane_at(6, active_center), 1.0f);
  ASSERT_EQ(plane_at(
                6, Position{active_center.row - 1, active_center.col - 1}),
            1.0f);
  ASSERT_EQ(plane_at(5, active_center), 0.0f);

  // Plane 5/6 totals: 1 telegraph cell, 9 active cells.
  int plane5_ones = 0, plane6_ones = 0;
  for (int i = 0; i < rows * cols; ++i) {
    if (obs[5 * rows * cols + i] > 0.5f) plane5_ones++;
    if (obs[6 * rows * cols + i] > 0.5f) plane6_ones++;
  }
  ASSERT_EQ(plane5_ones, 1);
  ASSERT_EQ(plane6_ones, 9);
}

TEST(TestDodgeVectorObsLensTailMatchesEnvPath) {
  // Transitional parity check for the verbatim move of DodgeEnv's 10 extra
  // vector features into DodgeLens::WriteVectorObs: the env's vector
  // observation tail must equal the lens output exactly. (In DodgeEnv all
  // agents are companions, so agent slot 0 is the same agent under both the
  // old companion-indexed and the current agent-passing convention.)
  RegisterObsTestEffects();
  DodgeEnv env(9, 1, 100, 50, 42);

  Position player_pos = PinFirstAgentAt(env, {4, 4});
  env.SpawnEffect("obs_test_active",
                  EffectTarget::AtCell({player_pos.row - 2, player_pos.col}));
  env.SpawnEffect("obs_test_telegraph",
                  EffectTarget::AtCell({player_pos.row, player_pos.col + 2}));

  DodgeLens lens;
  ASSERT_EQ(lens.AdditionalVectorObsSize(), 10);
  ASSERT_EQ(env.VectorObservationSize(), 9 + 10);

  std::vector<float> env_path;
  env.VectorObservation(env_path, 0);
  ASSERT_EQ(static_cast<int>(env_path.size()), 19);

  const Agent* agent = env.GetObjectManager().GetAllAgents()[0];
  std::vector<float> lens_tail(lens.AdditionalVectorObsSize(), 0.0f);
  lens.WriteVectorObs(env, *agent, lens_tail.data());
  ASSERT_EQ(static_cast<int>(lens_tail.size()), 10);

  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(env_path[9 + i], lens_tail[i]);
  }

  // Sanity on the tail semantics: an active hazard above the player and a
  // telegraphed hazard to its right must register in the danger features.
  ASSERT_TRUE(lens_tail[2] > 0.0f);  // Active danger up
  ASSERT_TRUE(lens_tail[9] > 0.0f);  // Telegraph danger right
}

TEST(TestAggroVectorObsLensTailIsCanonical) {
  // AggroLens::WriteVectorObs is the single vector-obs tail implementation.
  // Its semantics INTENTIONALLY differ from the deleted
  // AggroEnv::VectorObservation override:
  //   1. Relative positions normalize per-axis (dr/rows, dc/cols) instead of
  //      both by max(rows, cols).
  //   2. Distance normalizes by the true max Manhattan distance
  //      (rows + cols - 2) instead of max(rows, cols) * 2.
  //   3. With no enemy present, the distance feature defaults to 1.0
  //      ("maximally far") instead of being skipped as 0.
  //   4. The FSM one-hot uses FSMStateType via GetType(), folding
  //      Telegraph/Attack/Recovery into the "aggressive" bucket; the env
  //      version pointer-compared only the Patrol/Aggro/Return singletons and
  //      emitted an all-zero one-hot during attack phases (a bug).
  // NOTE: the transitional version of the lens indexed the companion list
  // instead of the agent list. That was a BUG, not canon: it made the tail
  // describe a different agent than the base features (the deleted env
  // override correctly used GetAllAgents()[player]). The lens now receives
  // the resolved Agent directly, so the tail describes the same agent as the
  // base features by construction. See TestAggroVectorObsActionAgentCoherence.
  AggroEnv env(10, 1, EnemyType::Goblin, 42);
  AggroLens lens;

  ASSERT_EQ(env.VectorObservationSize(), 9 + 8);

  // The companion is agent slot 1 in AggroEnv (the enemy spawns first).
  auto agents = env.GetObjectManager().GetAllAgents();
  const Companion* companion =
      env.GetObjectManager().GetAllCompanions()[0];
  ASSERT_EQ(agents[1], static_cast<const Agent*>(companion));

  std::vector<float> env_path;
  env.VectorObservation(env_path, 1);  // Player 1 = the companion
  ASSERT_EQ(static_cast<int>(env_path.size()), 17);

  std::vector<float> lens_tail(lens.AdditionalVectorObsSize(), 0.0f);
  lens.WriteVectorObs(env, *companion, lens_tail.data());

  // The env's vector observation tail must be exactly the lens output.
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(env_path[9 + i], lens_tail[i]);
  }

  // Hand-check the lens semantics against world state.
  const AgentFSM* enemy = env.GetObjectManager().GetAllAgentFSMs()[0];
  Position cpos = companion->GetPosition();
  Position epos = enemy->GetPosition();
  float rows = static_cast<float>(env.GetRows());
  float cols = static_cast<float>(env.GetCols());

  // (1) Per-axis normalization.
  ASSERT_EQ(lens_tail[0], static_cast<float>(epos.row - cpos.row) / rows);
  ASSERT_EQ(lens_tail[1], static_cast<float>(epos.col - cpos.col) / cols);

  // (2) Distance normalized by rows + cols - 2.
  float manhattan = static_cast<float>(std::abs(epos.row - cpos.row) +
                                       std::abs(epos.col - cpos.col));
  ASSERT_EQ(lens_tail[2], manhattan / (rows + cols - 2.0f));

  // Enemy starts on patrol: one-hot = (1, 0, 0).
  ASSERT_EQ(lens_tail[3], 1.0f);
  ASSERT_EQ(lens_tail[4], 0.0f);
  ASSERT_EQ(lens_tail[5], 0.0f);

  // Relative position to the target cell, also per-axis.
  Position target = env.GetTargetPosition();
  ASSERT_EQ(lens_tail[6], static_cast<float>(target.row - cpos.row) / rows);
  ASSERT_EQ(lens_tail[7], static_cast<float>(target.col - cpos.col) / cols);
}

TEST(TestAggroLensFoldsAttackPhasesIntoAggressiveBucket) {
  // Intended difference (4) above: forcing the enemy into Telegraph must
  // light the "aggressive" one-hot slot. The deleted env implementation
  // emitted (0, 0, 0) here because it pointer-compared only the
  // Patrol/Aggro/Return singletons.
  AggroEnv env(10, 1, EnemyType::Goblin, 42);
  AggroLens lens;

  AgentFSM* enemy = env.GetMutableObjectManager().GetAllAgentFSMs()[0];
  enemy->SetCurrentState(GetFSMStateByType(FSMStateType::Telegraph));

  const Companion* companion = env.GetObjectManager().GetAllCompanions()[0];
  std::vector<float> tail(lens.AdditionalVectorObsSize(), 0.0f);
  lens.WriteVectorObs(env, *companion, tail.data());
  ASSERT_EQ(tail[3], 0.0f);  // Not patrol
  ASSERT_EQ(tail[4], 1.0f);  // Aggressive (Telegraph folded in)
  ASSERT_EQ(tail[5], 0.0f);  // Not returning
}

TEST(TestAggroLensNoEnemyDefaultsDistanceToMax) {
  // Intended difference (3) above: on an env with no FSM enemy, the lens
  // reports distance-to-enemy = 1.0 (max) instead of the env version's 0.
  SynchroEnv env(6, 6, 1, 1, 0, 42);
  AggroLens lens;

  const Agent* agent = env.GetObjectManager().GetAllAgents()[0];
  std::vector<float> tail(lens.AdditionalVectorObsSize(), 0.0f);
  lens.WriteVectorObs(env, *agent, tail.data());
  ASSERT_EQ(tail[0], 0.0f);  // No relative position
  ASSERT_EQ(tail[1], 0.0f);
  ASSERT_EQ(tail[2], 1.0f);  // Max distance, not skipped-as-zero
  // No target cell annotation on a SynchroEnv either.
  ASSERT_EQ(tail[6], 0.0f);
  ASSERT_EQ(tail[7], 0.0f);
}

TEST(TestDodgeLensZeroHorizonSurvivalProgressIsZero) {
  // A horizonless world (horizon <= 0) has no survival deadline: the
  // survival-progress feature must be exactly 0, not a division by zero.
  SynchroEnv env(6, 6, 1, 1, 0, 42, 0, /*horizon=*/0);
  DodgeLens lens;

  const Agent* agent = env.GetObjectManager().GetAllAgents()[0];
  std::vector<float> tail(lens.AdditionalVectorObsSize(), -1.0f);
  lens.WriteVectorObs(env, *agent, tail.data());
  ASSERT_EQ(tail[0], 0.0f);
}

TEST(TestAggroVectorObsActionAgentCoherence) {
  // Actions, base vector features, and the lens tail must all describe the
  // SAME agent: GetAllAgents()[player] (the action-slot convention used by
  // GatherIntentions). In AggroEnv the enemy is spawned before the
  // companions, so it occupies agent slot 0 — the lens used to index the
  // companion list instead, shifting every tail by one agent. This test pins
  // action/obs/tail coherence permanently.
  AggroEnv env(10, 2, EnemyType::Goblin, 42);  // enemy at slot 0 + 2 companions

  auto agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(static_cast<int>(agents.size()), 3);
  // Sanity: the FSM enemy really is agent slot 0 (spawned first).
  ASSERT_TRUE(dynamic_cast<const AgentFSM*>(agents[0]) != nullptr);

  const AgentFSM* enemy = env.GetObjectManager().GetAllAgentFSMs()[0];
  Position epos = enemy->GetPosition();
  Position target = env.GetTargetPosition();
  float max_dim = static_cast<float>(std::max(env.GetRows(), env.GetCols()));
  float rows = static_cast<float>(env.GetRows());
  float cols = static_cast<float>(env.GetCols());

  for (int player = 0; player < env.NumAgents(); ++player) {
    std::vector<float> obs;
    env.VectorObservation(obs, player);
    ASSERT_EQ(static_cast<int>(obs.size()), 9 + 8);
    Position apos = agents[player]->GetPosition();

    // Base features 0-1: THIS agent's own normalized position.
    ASSERT_EQ(obs[0], static_cast<float>(apos.row) / max_dim);
    ASSERT_EQ(obs[1], static_cast<float>(apos.col) / max_dim);

    // Tail features 0-2: enemy-relative features computed from the SAME
    // agent's position the base features describe.
    ASSERT_EQ(obs[9], static_cast<float>(epos.row - apos.row) / rows);
    ASSERT_EQ(obs[10], static_cast<float>(epos.col - apos.col) / cols);
    float manhattan = static_cast<float>(std::abs(epos.row - apos.row) +
                                         std::abs(epos.col - apos.col));
    ASSERT_EQ(obs[11], manhattan / (rows + cols - 2.0f));

    // Tail features 6-7: target cell relative to the same agent.
    ASSERT_EQ(obs[15], static_cast<float>(target.row - apos.row) / rows);
    ASSERT_EQ(obs[16], static_cast<float>(target.col - apos.col) / cols);
  }

  // The enemy slot itself: relative-to-self is exactly zero.
  std::vector<float> obs0;
  env.VectorObservation(obs0, 0);
  ASSERT_EQ(obs0[9], 0.0f);
  ASSERT_EQ(obs0[10], 0.0f);
  ASSERT_EQ(obs0[11], 0.0f);
}

TEST(TestVectorObsSizeIsLensAware) {
  // VectorObservationSize = base 9 + active lens tail. Swapping the lens on
  // the same world changes the observation contract accordingly.
  DodgeEnv env(9, 1, 100, 50, 42);
  ASSERT_EQ(env.VectorObservationSize(), 19);  // DodgeLens tail = 10

  // SynchroLens has no tail; DodgeEnv has no synchro cells so swap to a
  // fresh DodgeLens-free state via the base setter with a null lens.
  ASSERT_TRUE(env.SetTaskLens(nullptr));
  ASSERT_EQ(env.VectorObservationSize(), 9);  // No lens: base features only

  ASSERT_TRUE(env.SetTaskLens(std::make_unique<DodgeLens>()));
  ASSERT_EQ(env.VectorObservationSize(), 19);
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
