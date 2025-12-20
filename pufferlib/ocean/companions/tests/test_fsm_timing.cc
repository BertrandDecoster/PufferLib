// Copyright 2024
// Unit tests for FSM movement timing (1-tick delay bug)
//
// These tests verify that FSM agents move on tick 1, not tick 2.
// Before the fix, FSM intentions are set AFTER movements execute,
// causing a 1-tick delay.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/fsm/enemies.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/env/aggro_env.h"

using namespace companions;

// =============================================================================
// Test macros
// =============================================================================
#define TEST(name)                                             \
  void name();                                                 \
  struct name##_registrar {                                    \
    name##_registrar() { tests.push_back({#name, name}); }     \
  } name##_instance;                                           \
  void name()

#define ASSERT_TRUE(cond)                                                    \
  if (!(cond)) {                                                             \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":"      \
        << __LINE__;                                                         \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_NE(a, b)                                                       \
  if ((a) == (b)) {                                                           \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_NE failed: " << #a << " == " << #b << " at "              \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_EQ(a, b)                                                       \
  if ((a) != (b)) {                                                           \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at "              \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Helper: Find FSM agent in environment
// =============================================================================
AgentFSM* FindFSMAgent(AggroEnv& env) {
  for (Agent* agent : env.GetMutableObjectManager().GetAllAgents()) {
    if (auto* fsm = dynamic_cast<AgentFSM*>(agent)) {
      return fsm;
    }
  }
  return nullptr;
}

// =============================================================================
// Test: Goblin moves on FIRST tick (no 1-tick delay)
// =============================================================================
TEST(TestGoblinMovesOnFirstTick) {
  // Use large grid and specific seed to ensure companion is far from enemy
  // so enemy stays in Patrol state (not Aggro)
  AggroEnv env(16, 1, EnemyType::Goblin, 999);

  AgentFSM* goblin = FindFSMAgent(env);
  ASSERT_TRUE(goblin != nullptr);
  ASSERT_TRUE(goblin->HasFSM());

  Position start_pos = goblin->GetPosition();

  // Goblin has no cadence, so it should move every tick
  // On tick 1, it should move to next patrol waypoint
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  env.Step(actions);

  Position after_tick1 = goblin->GetPosition();

  // BUG: With 1-tick delay, goblin stays on tick 1 (intention not set yet)
  // FIX: Goblin should move on tick 1 (intention set in PreStep)
  ASSERT_NE(start_pos, after_tick1);
}

// =============================================================================
// Test: Zombie moves on first ACTIVE tick (respects cadence)
// =============================================================================
TEST(TestZombieMovesOnFirstActiveTick) {
  // Zombie has cadence [1,0] = move on tick 0, skip on tick 1, etc.
  AggroEnv env(16, 1, EnemyType::Zombie, 999);

  AgentFSM* zombie = FindFSMAgent(env);
  ASSERT_TRUE(zombie != nullptr);
  ASSERT_TRUE(zombie->HasFSM());

  // Verify cadence is [1,0]
  const auto& cadence = zombie->GetCadence();
  ASSERT_EQ(cadence.size(), 2u);
  ASSERT_EQ(cadence[0], 1);  // Move on even ticks
  ASSERT_EQ(cadence[1], 0);  // Skip on odd ticks

  Position start_pos = zombie->GetPosition();

  // Tick 1 (internal tick 0): cadence[0]=1 → should move
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  env.Step(actions);

  Position after_tick1 = zombie->GetPosition();

  // BUG: With 1-tick delay, zombie stays on tick 1
  // FIX: Zombie should move on tick 1 (cadence allows it)
  ASSERT_NE(start_pos, after_tick1);
}

// =============================================================================
// Test: Verify intention is set BEFORE movement execution
// =============================================================================
TEST(TestFSMIntentionSetBeforeMovement) {
  // This test verifies the fix by checking that after Step(),
  // the FSM agent's position reflects immediate movement, not delayed.
  //
  // If intentions are set after movements (bug), position won't change on tick 1.
  // If intentions are set before movements (fix), position changes on tick 1.

  AggroEnv env(16, 1, EnemyType::Goblin, 12345);

  AgentFSM* goblin = FindFSMAgent(env);
  ASSERT_TRUE(goblin != nullptr);

  Position start = goblin->GetPosition();

  // Get patrol path to know expected movement
  const auto& patrol = env.GetPatrolPath();
  ASSERT_TRUE(patrol.size() >= 2);

  // Find goblin's position in patrol path
  int patrol_idx = -1;
  for (size_t i = 0; i < patrol.size(); ++i) {
    if (patrol[i] == start) {
      patrol_idx = static_cast<int>(i);
      break;
    }
  }

  // Goblin should be on patrol path
  ASSERT_TRUE(patrol_idx >= 0);

  // Step once
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  env.Step(actions);

  Position after = goblin->GetPosition();

  // Goblin should have moved to next patrol waypoint
  // (either forward or backward depending on patrol direction)
  bool moved_to_adjacent = false;
  if (patrol_idx > 0 && after == patrol[patrol_idx - 1]) {
    moved_to_adjacent = true;
  }
  if (patrol_idx < static_cast<int>(patrol.size()) - 1 &&
      after == patrol[patrol_idx + 1]) {
    moved_to_adjacent = true;
  }

  // Movement should happen on tick 1
  ASSERT_TRUE(moved_to_adjacent);
}

// =============================================================================
// Main
// =============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " FSM timing tests...\n\n";

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "Running " << test.name << "... ";
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "FAILED\n  " << e.what() << "\n";
      failed++;
    }
  }

  std::cout << "\n=== Results: " << passed << " passed, " << failed
            << " failed ===\n";

  return (failed > 0) ? 1 : 0;
}
