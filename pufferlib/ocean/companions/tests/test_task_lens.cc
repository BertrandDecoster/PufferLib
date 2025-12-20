// Copyright 2024
// Test suite for TaskLens interface

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/env/task_lens.h"
#include "../src/env/base_env.h"
#include "../src/env/synchro_env.h"

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
    CellKind MaskCell(CellKind kind) const override {
      return kind;
    }
  };

  MockLens lens;

  // Verify the interface methods are callable
  // We can't actually call CanOperateOn/IsDone/IsSuccess without a real env,
  // but we can test MaskCell which doesn't need env
  ASSERT_EQ(lens.MaskCell(CellKind::Floor), CellKind::Floor);
  ASSERT_EQ(lens.MaskCell(CellKind::Wall), CellKind::Wall);
  ASSERT_EQ(lens.MaskCell(CellKind::Synchro), CellKind::Synchro);
  ASSERT_EQ(lens.MaskCell(CellKind::Target), CellKind::Target);

  // Verify optional methods have defaults
  std::vector<float> obs;
  // AppendVectorObs is a no-op by default (can't call without env, but test signature exists)
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
    CellKind MaskCell(CellKind kind) const override { return kind; }
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
    CellKind MaskCell(CellKind kind) const override { return kind; }
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
