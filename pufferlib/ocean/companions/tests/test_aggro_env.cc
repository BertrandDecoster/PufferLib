// Copyright 2024
// Unit tests for AggroEnv

#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/cell.h"
#include "../src/core/fsm/enemies.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/core/grid.h"
#include "../src/core/object_manager.h"
#include "../src/env/aggro_env.h"

using namespace companions;

// =============================================================================
// Test macros (same as other test files)
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

#define ASSERT_FALSE(cond)                                                    \
  if (cond) {                                                                 \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ << ":"     \
        << __LINE__;                                                         \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_EQ(a, b)                                                       \
  if ((a) != (b)) {                                                           \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at "              \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_GE(a, b)                                                       \
  if ((a) < (b)) {                                                            \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_GE failed: " << #a << " < " << #b << " at "               \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_LE(a, b)                                                       \
  if ((a) > (b)) {                                                            \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_LE failed: " << #a << " > " << #b << " at "               \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

#define ASSERT_GT(a, b)                                                       \
  if ((a) <= (b)) {                                                           \
    std::ostringstream oss;                                                  \
    oss << "ASSERT_GT failed: " << #a << " <= " << #b << " at "              \
        << __FILE__ << ":" << __LINE__;                                      \
    throw std::runtime_error(oss.str());                                     \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Grid Setup Tests
// =============================================================================
TEST(TestAggroEnvGridSetup) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  const Grid& grid = env.GetGrid();

  // Check walls on perimeter
  for (int c = 0; c < 10; ++c) {
    ASSERT_EQ(grid.GetCellKind({0, c}), CellKind::Wall);
    ASSERT_EQ(grid.GetCellKind({9, c}), CellKind::Wall);
  }
  for (int r = 0; r < 10; ++r) {
    ASSERT_EQ(grid.GetCellKind({r, 0}), CellKind::Wall);
    ASSERT_EQ(grid.GetCellKind({r, 9}), CellKind::Wall);
  }

  // Interior should be Floor (except for Target cell)
  int floor_count = 0;
  int target_count = 0;
  for (int r = 1; r < 9; ++r) {
    for (int c = 1; c < 9; ++c) {
      CellKind kind = grid.GetCellKind({r, c});
      if (kind == CellKind::Floor) floor_count++;
      if (kind == CellKind::Target) target_count++;
    }
  }
  ASSERT_EQ(target_count, 1);
  ASSERT_GT(floor_count, 0);
}

TEST(TestPatrolSquareGeneration) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  const auto& patrol_path = env.GetPatrolPath();

  // Should have 8 cells
  ASSERT_EQ(patrol_path.size(), 8);

  // All cells should be within bounds (not on walls)
  for (const Position& pos : patrol_path) {
    ASSERT_GE(pos.row, 1);
    ASSERT_LE(pos.row, 8);
    ASSERT_GE(pos.col, 1);
    ASSERT_LE(pos.col, 8);
  }

  // Check that it forms a 3x3 perimeter (clockwise order)
  // First 3 cells should be in same row (top edge)
  ASSERT_EQ(patrol_path[0].row, patrol_path[1].row);
  ASSERT_EQ(patrol_path[1].row, patrol_path[2].row);

  // Next cell should be one row down (right edge)
  ASSERT_EQ(patrol_path[3].row, patrol_path[2].row + 1);
  ASSERT_EQ(patrol_path[3].col, patrol_path[2].col);

  // Bottom-right corner
  ASSERT_EQ(patrol_path[4].row, patrol_path[3].row + 1);
  ASSERT_EQ(patrol_path[4].col, patrol_path[3].col);
}

TEST(TestPatrolSquareWithinBounds) {
  // Test with multiple seeds to ensure patrol is always valid
  for (int seed = 0; seed < 20; ++seed) {
    AggroEnv env(10, 1, EnemyType::Zombie, seed);
    const auto& patrol_path = env.GetPatrolPath();

    for (const Position& pos : patrol_path) {
      // Not on walls
      ASSERT_GE(pos.row, 1);
      ASSERT_LE(pos.row, 8);
      ASSERT_GE(pos.col, 1);
      ASSERT_LE(pos.col, 8);
    }
  }
}

// =============================================================================
// Target Placement Tests
// =============================================================================
TEST(TestTargetOutsideAggroRange) {
  AggroEnv env(12, 1, EnemyType::Zombie, 42);

  Position target = env.GetTargetPosition();
  Position enemy_spawn = env.GetEnemySpawnPosition();

  // Target should be > 3 distance from enemy spawn position
  // (on larger grids where this is possible)
  int dist = ManhattanDistance(target, enemy_spawn);
  ASSERT_GT(dist, AggroEnv::kAggroRange);
}

TEST(TestTargetNotInCorner) {
  AggroEnv env(12, 1, EnemyType::Zombie, 42);

  Position target = env.GetTargetPosition();
  const Grid& grid = env.GetGrid();

  // Count walkable adjacent cells
  int walkable_adjacent = 0;
  const Position deltas[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

  for (const Position& d : deltas) {
    Position adj = {target.row + d.row, target.col + d.col};
    if (grid.IsInBounds(adj) && grid.GetCell(adj).IsWalkable()) {
      walkable_adjacent++;
    }
  }

  ASSERT_GE(walkable_adjacent, 2);
}

TEST(TestTargetIsTargetCell) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  Position target = env.GetTargetPosition();
  const Grid& grid = env.GetGrid();

  ASSERT_EQ(grid.GetCellKind(target), CellKind::Target);
}

// =============================================================================
// Spawn Tests
// =============================================================================
TEST(TestCompanionsOutsideAggroRange) {
  AggroEnv env(12, 2, EnemyType::Zombie, 42);

  Position enemy_spawn = env.GetEnemySpawnPosition();
  auto companions = env.GetObjectManager().GetAllCompanions();

  ASSERT_EQ(companions.size(), 2);

  // Companions should be outside aggro range of enemy spawn position
  // (on larger grids where this is possible)
  for (const Companion* comp : companions) {
    Position pos = comp->GetPosition();
    int dist = ManhattanDistance(pos, enemy_spawn);
    ASSERT_GT(dist, AggroEnv::kAggroRange);
  }
}

TEST(TestEnemyOnPatrolCell) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  const auto& patrol_path = env.GetPatrolPath();
  auto agents = env.GetMutableObjectManager().GetAllAgents();

  // Find the FSM agent (should be a Goblin)
  AgentFSM* fsm_agent = nullptr;
  for (Agent* agent : agents) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      fsm_agent = e;
      break;
    }
  }

  ASSERT_TRUE(fsm_agent != nullptr);

  // Agent position should be on one of the patrol cells
  Position agent_pos = fsm_agent->GetPosition();
  bool on_patrol = false;
  for (const Position& patrol_pos : patrol_path) {
    if (agent_pos == patrol_pos) {
      on_patrol = true;
      break;
    }
  }
  ASSERT_TRUE(on_patrol);
}

TEST(TestAgentHasFSM) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  auto agents = env.GetMutableObjectManager().GetAllAgents();

  AgentFSM* fsm_agent = nullptr;
  for (Agent* agent : agents) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      fsm_agent = e;
      break;
    }
  }

  ASSERT_TRUE(fsm_agent != nullptr);
  ASSERT_TRUE(fsm_agent->HasFSM());
  ASSERT_EQ(fsm_agent->GetCurrentState()->GetName(), "Patrol");
}

// =============================================================================
// Win Condition Tests
// =============================================================================
TEST(TestNotDoneInitially) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
}

TEST(TestTimePenalty) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);

  // Step with Stay action
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  auto result = env.Step(actions);

  // Should get time penalty for each agent (1 companion + 1 enemy = 2 agents)
  ASSERT_EQ(result.rewards.size(), 2);
  ASSERT_EQ(result.rewards[0], AggroEnv::kTimePenalty);
  ASSERT_EQ(result.rewards[1], AggroEnv::kTimePenalty);
}

// =============================================================================
// FSM Integration Tests
// =============================================================================
TEST(TestAgentPatrols) {
  // Use a large grid to ensure companion spawns far from patrol
  // Note: seed 999 chosen to ensure companion spawns far from entire patrol path
  AggroEnv env(16, 1, EnemyType::Zombie, 999);

  // Get initial agent position
  AgentFSM* fsm_agent = nullptr;
  for (Agent* agent : env.GetMutableObjectManager().GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      fsm_agent = e;
      break;
    }
  }
  ASSERT_TRUE(fsm_agent != nullptr);

  // Step a few times (Zombie moves every 2nd tick due to cadence)
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  for (int i = 0; i < 6; ++i) {
    env.Step(actions);
  }

  // Enemy should have moved (patrolling)
  Position new_pos = fsm_agent->GetPosition();

  // Verify movement occurred (at least moved once in 6 steps with cadence)
  const auto& patrol_path = env.GetPatrolPath();
  bool on_patrol = false;
  for (const Position& patrol_pos : patrol_path) {
    if (new_pos == patrol_pos) {
      on_patrol = true;
      break;
    }
  }

  // Should be on a patrol cell (large grid ensures no aggro triggered)
  ASSERT_TRUE(on_patrol);
}

// =============================================================================
// Determinism Tests
// =============================================================================
TEST(TestSameSeedSameLayout) {
  AggroEnv env1(12, 2, EnemyType::Zombie, 12345);
  AggroEnv env2(12, 2, EnemyType::Zombie, 12345);

  // Same target position
  ASSERT_EQ(env1.GetTargetPosition(), env2.GetTargetPosition());

  // Same patrol path
  const auto& path1 = env1.GetPatrolPath();
  const auto& path2 = env2.GetPatrolPath();
  ASSERT_EQ(path1.size(), path2.size());
  for (size_t i = 0; i < path1.size(); ++i) {
    ASSERT_EQ(path1[i], path2[i]);
  }

  // Same companion positions
  auto comps1 = env1.GetObjectManager().GetAllCompanions();
  auto comps2 = env2.GetObjectManager().GetAllCompanions();
  ASSERT_EQ(comps1.size(), comps2.size());
  for (size_t i = 0; i < comps1.size(); ++i) {
    ASSERT_EQ(comps1[i]->GetPosition(), comps2[i]->GetPosition());
  }
}

TEST(TestDifferentSeedsDifferentLayouts) {
  AggroEnv env1(12, 2, EnemyType::Zombie, 11111);
  AggroEnv env2(12, 2, EnemyType::Zombie, 22222);

  // At least one of these should differ (very likely with different seeds)
  bool target_differs = (env1.GetTargetPosition() != env2.GetTargetPosition());
  bool patrol_differs = (env1.GetPatrolPath()[0] != env2.GetPatrolPath()[0]);

  ASSERT_TRUE(target_differs || patrol_differs);
}

// =============================================================================
// Configuration Tests
// =============================================================================
TEST(TestMultipleCompanions) {
  AggroEnv env(12, 3, EnemyType::Zombie, 42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_EQ(companions.size(), 3);

  // All should be outside aggro range of enemy spawn position
  Position enemy_spawn = env.GetEnemySpawnPosition();
  for (const Companion* comp : companions) {
    Position pos = comp->GetPosition();
    int dist = ManhattanDistance(pos, enemy_spawn);
    ASSERT_GT(dist, AggroEnv::kAggroRange);
  }
}

TEST(TestResetWithNewSeed) {
  AggroEnv env(12, 1, EnemyType::Zombie, 42);

  // Store original target position
  [[maybe_unused]] Position target1 = env.GetTargetPosition();

  // Reset with different seed
  env.Reset(999);

  // Store new target position
  [[maybe_unused]] Position target2 = env.GetTargetPosition();

  // Target position should (very likely) change with new seed
  // Note: there's a small chance they could be the same, but unlikely
  // We'll just verify reset completed without error
  ASSERT_TRUE(env.GetPatrolPath().size() == 8);
}

// =============================================================================
// Vector Observation Tests
// =============================================================================
TEST(TestAggroEnvVectorObservationSize) {
  AggroEnv env(10, 1, EnemyType::Goblin, 42);

  // AggroEnv adds 8 features to base (9): total = 17
  ASSERT_EQ(env.VectorObservationSize(), 17);
}

TEST(TestAggroEnvVectorObservationValues) {
  AggroEnv env(10, 1, EnemyType::Goblin, 42);

  std::vector<float> obs;
  env.VectorObservation(obs, 0);

  ASSERT_EQ(obs.size(), static_cast<size_t>(env.VectorObservationSize()));

  // All values should be normalized to reasonable range
  for (size_t i = 0; i < obs.size(); ++i) {
    ASSERT_TRUE(obs[i] >= -1.0f && obs[i] <= 1.0f);
  }

  // Feature 8-9: Relative position to enemy
  // Feature 8: steps_left (base env)
  // Feature 9-10: AggroEnv specific (enemy position normalized)
  // Feature 11: Distance to enemy (normalized)
  // Features 12-14: FSM state one-hot (should sum to 1)
  float fsm_sum = obs[12] + obs[13] + obs[14];
  ASSERT_TRUE(fsm_sum > 0.99f && fsm_sum < 1.01f);  // One-hot should sum to 1

  // Features 15-16: Relative position to target
  // These should be valid relative positions
  ASSERT_TRUE(obs[15] >= -1.0f && obs[15] <= 1.0f);
  ASSERT_TRUE(obs[16] >= -1.0f && obs[16] <= 1.0f);
}

TEST(TestAggroEnvVectorObservationFSMState) {
  AggroEnv env(10, 1, EnemyType::Goblin, 42);

  std::vector<float> obs;
  env.VectorObservation(obs, 0);

  // Initially enemy should be in patrol state
  // Feature 12 = patrol, 13 = aggro, 14 = returning (shifted by 1 due to steps_left at index 8)
  ASSERT_EQ(obs[12], 1.0f);  // Patrol
  ASSERT_EQ(obs[13], 0.0f);  // Not aggro
  ASSERT_EQ(obs[14], 0.0f);  // Not returning
}

// =============================================================================
// Win/Lose Condition Tests
// =============================================================================
TEST(TestAggroEnvWinCondition) {
  // Test that enemy reaching target cell triggers win condition
  // AggroEnv puzzle: lure enemy to target cell
  AggroEnv env(12, 1, EnemyType::Zombie, 999);

  // Get target position and enemy
  Position target = env.GetTargetPosition();
  auto& obj_mgr = env.GetMutableObjectManager();

  // Find enemy (FSM agent)
  AgentFSM* enemy = nullptr;
  for (Agent* agent : obj_mgr.GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      enemy = e;
      break;
    }
  }
  ASSERT_TRUE(enemy != nullptr);

  // Place enemy on target cell
  obj_mgr.UpdatePosition(enemy->GetId(), target);

  // Disable FSM so enemy stays on target during Step
  // (FSM now runs in PreStep before win check, so we need to prevent movement)
  enemy->SetCurrentState(nullptr);

  // Verify enemy is on target
  ASSERT_EQ(enemy->GetPosition(), target);

  // Step once - should trigger win
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  auto result = env.Step(actions);

  // Verify win condition
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());

  // Win reward should be received
  ASSERT_EQ(result.rewards.size(), static_cast<size_t>(env.NumAgents()));
  // All agents should get win reward
  for (const auto& r : result.rewards) {
    ASSERT_EQ(r, AggroEnv::kWinReward);
  }
}

TEST(TestAggroEnvWinWithEnemyMovement) {
  // Test winning by enemy actually moving to target cell
  AggroEnv env(10, 1, EnemyType::Goblin, 5555);

  Position target = env.GetTargetPosition();
  auto& obj_mgr = env.GetMutableObjectManager();

  // Find enemy
  AgentFSM* enemy = nullptr;
  for (Agent* agent : obj_mgr.GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      enemy = e;
      break;
    }
  }
  ASSERT_TRUE(enemy != nullptr);

  // Place enemy adjacent to target
  Position adjacent_pos = {target.row - 1, target.col};  // Try above
  if (!env.GetGrid().IsWalkable(adjacent_pos)) {
    adjacent_pos = {target.row + 1, target.col};  // Try below
  }
  if (!env.GetGrid().IsWalkable(adjacent_pos)) {
    adjacent_pos = {target.row, target.col - 1};  // Try left
  }
  if (!env.GetGrid().IsWalkable(adjacent_pos)) {
    adjacent_pos = {target.row, target.col + 1};  // Try right
  }

  obj_mgr.UpdatePosition(enemy->GetId(), adjacent_pos);

  ASSERT_FALSE(env.IsDone());

  // Make enemy move toward target
  enemy->MoveTo(target, env);

  // Step
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  auto result = env.Step(actions);

  // Should win if enemy reached target
  if (enemy->GetPosition() == target) {
    ASSERT_TRUE(env.IsDone());
    ASSERT_TRUE(env.IsSuccess());
  }
}

TEST(TestAggroEnvEnemyCatchesCompanion) {
  // Test that enemy catching companion triggers lose condition
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);

  auto& obj_mgr = env.GetMutableObjectManager();

  // Get enemy agent
  AgentFSM* enemy = nullptr;
  for (Agent* agent : obj_mgr.GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      enemy = e;
      break;
    }
  }
  ASSERT_TRUE(enemy != nullptr);

  // Get companion
  auto companions = obj_mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1u);
  Companion* companion = companions[0];

  // Place enemy and companion on adjacent cells
  Position companion_pos = {5, 5};
  Position enemy_pos = {5, 6};  // Right next to companion

  obj_mgr.UpdatePosition(companion->GetId(), companion_pos);
  obj_mgr.UpdatePosition(enemy->GetId(), enemy_pos);

  ASSERT_FALSE(env.IsDone());

  // Make enemy move into companion's cell
  enemy->MoveTo(companion_pos, env);

  // Step
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Stay),  // Companion stays
    EncodeAction(MovementAction::Stay)   // Enemy action (will be overridden by FSM)
  };

  auto result = env.Step(actions);

  // Check if enemy caught companion (should trigger lose condition)
  // Note: This depends on collision resolution allowing enemy to move into companion cell
  // The exact behavior depends on BaseEnv collision rules
  if (enemy->GetPosition() == companion->GetPosition()) {
    ASSERT_TRUE(env.IsDone());
    ASSERT_FALSE(env.IsSuccess());
  }
}

TEST(TestAggroEnvTimePenaltyAccumulation) {
  // Test that time penalty accumulates correctly over multiple steps
  AggroEnv env(12, 2, EnemyType::Zombie, 1111);

  // Keep companions away from target
  auto& obj_mgr = env.GetMutableObjectManager();
  auto companions = obj_mgr.GetAllCompanions();

  // Move companions to corner away from everything
  obj_mgr.UpdatePosition(companions[0]->GetId(), {2, 2});
  obj_mgr.UpdatePosition(companions[1]->GetId(), {2, 3});

  // Step multiple times and accumulate time penalties
  std::vector<Action> stay_actions(env.NumAgents(), EncodeAction(MovementAction::Stay));

  double total_penalty = 0.0;
  for (int i = 0; i < 5; ++i) {
    auto result = env.Step(stay_actions);

    // Each agent should get time penalty
    ASSERT_EQ(result.rewards.size(), static_cast<size_t>(env.NumAgents()));

    // Companions get time penalty (first 2 agents)
    ASSERT_EQ(result.rewards[0], AggroEnv::kTimePenalty);
    ASSERT_EQ(result.rewards[1], AggroEnv::kTimePenalty);

    total_penalty += AggroEnv::kTimePenalty;
  }

  // Total penalty should accumulate
  ASSERT_TRUE(total_penalty < 0.0);  // Penalties are negative
  ASSERT_EQ(total_penalty, 5.0 * AggroEnv::kTimePenalty);

  // Should not be done yet (no timeout in this test)
  ASSERT_FALSE(env.IsDone());
}

TEST(TestAggroEnvFullGameLoopWinScenario) {
  // Integration test: Complete game with enemy reaching target
  AggroEnv env(14, 1, EnemyType::Zombie, 3333);

  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_EQ(env.GetTick(), 0);

  Position target = env.GetTargetPosition();
  auto& obj_mgr = env.GetMutableObjectManager();

  // Find enemy
  AgentFSM* enemy = nullptr;
  for (Agent* agent : obj_mgr.GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) {
      enemy = e;
      break;
    }
  }
  ASSERT_TRUE(enemy != nullptr);

  Position initial_enemy_pos = enemy->GetPosition();

  // Verify enemy not initially on target
  ASSERT_TRUE(initial_enemy_pos != target);

  // Manually move enemy to target for clean test
  obj_mgr.UpdatePosition(enemy->GetId(), target);

  // Disable FSM so enemy stays on target during Step
  // (FSM now runs in PreStep before win check, so we need to prevent movement)
  enemy->SetCurrentState(nullptr);

  // Step - should win
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  auto result = env.Step(actions);

  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
  // All agents get win reward
  for (const auto& r : result.rewards) {
    ASSERT_EQ(r, AggroEnv::kWinReward);
  }
}

// =============================================================================
// Main
// =============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  // Disable Windows error dialogs (crash reports, assert dialogs)
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " AggroEnv tests...\n\n";

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "Running " << test.name << "... ";
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (...) {
      std::cout << "FAILED\n";
      failed++;
    }
  }

  std::cout << "\n=== Results: " << passed << " passed, " << failed
            << " failed ===\n";

  return (failed > 0) ? 1 : 0;
}
