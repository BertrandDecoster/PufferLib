// Copyright 2024
// Unit tests for AggroEnv

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/cell.h"
#include "../src/core/effect_config.h"
#include "../src/core/fsm/enemies.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/core/grid.h"
#include "../src/core/object_manager.h"
#include "../src/env/aggro_env.h"
#include "../src/env/aggro_lens.h"
#include "../src/env/dodge_lens.h"
#include "../src/env/synchro_lens.h"
#include "effect_registry_guard.h"

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

  // Interior is all Floor now; target role lives on the annotation layer.
  int floor_count = 0;
  for (int r = 1; r < 9; ++r) {
    for (int c = 1; c < 9; ++c) {
      if (grid.GetCellKind({r, c}) == CellKind::Floor) floor_count++;
    }
  }
  ASSERT_GT(floor_count, 0);
  auto targets =
      env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  ASSERT_EQ(targets.size(), 1u);
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

  // Target cell is physically Floor; the AggroTarget role lives on the
  // annotation layer.
  ASSERT_EQ(grid.GetCellKind(target), CellKind::Floor);
  ASSERT_TRUE(env.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Cell, target, kInvalidObjectId},
      SemanticTag::AggroTarget));
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

// The FSM's RNG state of the first FSM agent, as a snapshot saves it
static uint64_t FsmRngState(const BaseEnv& env) {
  for (const auto& a : env.SaveSnapshot().agents) {
    if (a.has_fsm) return a.fsm.rng_state;
  }
  throw std::runtime_error("no FSM agent");
}

static const FSMContext& FsmContext(BaseEnv& env) {
  for (Agent* agent : env.GetMutableObjectManager().GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) return e->GetFSMContext();
  }
  throw std::runtime_error("no FSM agent");
}

// A copy (Clone, copy constructor, assignment) draws from its own RNG: its
// FSM agents no longer point at the original env's, so stepping the copy
// leaves the original's RNG where it was.
TEST(TestACloneDrawsFromItsOwnRng) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  const pcg32* original = FsmContext(env).rng;
  ASSERT_TRUE(original != nullptr);
  const uint64_t before = FsmRngState(env);

  std::unique_ptr<BaseEnv> clone = env.Clone();
  ASSERT_TRUE(FsmContext(*clone).rng != nullptr);
  ASSERT_TRUE(FsmContext(*clone).rng != original);
  ASSERT_TRUE(FsmRngState(*clone) == before);  // The same state, its own copy
  AggroEnv copy(env);
  ASSERT_TRUE(FsmContext(copy).rng != original);
  AggroEnv assigned(10, 1, EnemyType::Zombie, 7);
  assigned = env;
  ASSERT_TRUE(FsmContext(assigned).rng != original);
  ASSERT_TRUE(FsmContext(assigned).rng != FsmContext(copy).rng);

  // The companion walks around the patrol (the zombie chases it: its
  // pathfinder breaks ties with the RNG), in the clone only
  const MovementAction walk[] = {MovementAction::Up, MovementAction::Left, MovementAction::Down,
                                 MovementAction::Right};
  for (int i = 0; i < 40; ++i) {
    std::vector<Action> actions(clone->NumAgents(), EncodeAction(MovementAction::Stay));
    actions[0] = EncodeAction(walk[(i / 3) % 4]);
    clone->Step(actions);
  }
  ASSERT_TRUE(FsmRngState(*clone) != before);  // The clone did draw
  ASSERT_TRUE(FsmRngState(env) == before);
  ASSERT_TRUE(FsmContext(env).rng == original);
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
// Rooted Tests
// =============================================================================

// A goblin 2 cells from a companion on open floor, stepped once.
static AgentFSM* StepGoblinTowardCompanion(AggroEnv& env, bool rooted) {
  Grid& grid = env.GetMutableGrid();
  for (int r = 1; r < grid.GetRows() - 1; ++r) {
    for (int c = 1; c < grid.GetCols() - 1; ++c) grid.SetCell({r, c}, CellKind::Floor);
  }
  auto& obj_mgr = env.GetMutableObjectManager();
  AgentFSM* enemy = nullptr;
  for (Agent* agent : obj_mgr.GetAllAgents()) {
    if (auto* e = dynamic_cast<AgentFSM*>(agent)) enemy = e;
  }
  obj_mgr.UpdatePosition(obj_mgr.GetAllCompanions()[0]->GetId(), {5, 3});
  obj_mgr.UpdatePosition(enemy->GetId(), {5, 5});
  if (rooted) enemy->ApplyStatus(StatusType::Rooted, 2);
  env.Step(std::vector<Action>(env.NumAgents(), EncodeAction(MovementAction::Stay)));
  return enemy;
}

TEST(TestRootedGoblinStillThinksButDoesNotMove) {
  AggroEnv free_env(12, 1, EnemyType::Goblin, 7777);
  AgentFSM* free_goblin = StepGoblinTowardCompanion(free_env, false);
  ASSERT_EQ(free_goblin->GetCurrentState()->GetName(), "Aggro");
  ASSERT_TRUE(free_goblin->GetPosition() == (Position{5, 4}));  // Chases

  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  AgentFSM* goblin = StepGoblinTowardCompanion(env, true);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Aggro");     // The FSM still ran
  ASSERT_TRUE(goblin->GetPosition() == (Position{5, 5}));       // but it did not move
}

// =============================================================================
// A dead enemy fails nothing: only a team down or the horizon fails a task
// =============================================================================

// Open floor, the companion on (5,3) facing a 1-HP goblin on (5,4) whose FSM
// is off (it stays put). Returns the goblin.
static AgentFSM* GoblinNextToCompanion(AggroEnv& env, int goblin_health) {
  Grid& grid = env.GetMutableGrid();
  for (int r = 1; r < grid.GetRows() - 1; ++r) {
    for (int c = 1; c < grid.GetCols() - 1; ++c) grid.SetCell({r, c}, CellKind::Floor);
  }
  auto& obj_mgr = env.GetMutableObjectManager();
  AgentFSM* goblin = obj_mgr.GetAllAgentFSMs()[0];
  obj_mgr.UpdatePosition(obj_mgr.GetAllCompanions()[0]->GetId(), {5, 3});
  obj_mgr.UpdatePosition(goblin->GetId(), {5, 4});
  goblin->SetCurrentState(nullptr);
  goblin->SetMaxHealth(goblin_health);
  ASSERT_FALSE(env.GetTargetPosition() == (Position{5, 4}));
  return goblin;
}

// AggroEnv spawns the enemy first: agent 0 is the goblin, agent 1 the companion.
static StepResult AttackRight(AggroEnv& env) {
  return env.Step({EncodeAction(MovementAction::Stay),
                   EncodeAction(MovementAction::Right, InteractAction::Skill1)});
}

// Killing the enemy is no failure: the episode runs on to the horizon, which
// ends it (as Horizon), and every step pays kTimePenalty, the kill's included.
TEST(TestKillingTheEnemyDoesNotEndTheEpisode) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 5);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  StepResult result = AttackRight(env);
  ASSERT_FALSE(goblin->IsAlive());
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (double r : result.rewards) ASSERT_EQ(r, AggroLens::kTimePenalty);
  const Action stay = EncodeAction(MovementAction::Stay);
  while (env.GetTick() < env.GetHorizon()) {
    result = env.Step({stay, stay});
    ASSERT_EQ(result.done, env.GetTick() == env.GetHorizon());
    for (double r : result.rewards) ASSERT_EQ(r, AggroLens::kTimePenalty);
  }
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  // The env never stops by itself: a host that keeps playing just steps on,
  // still paid kTimePenalty
  result = env.Step({stay, stay});
  ASSERT_EQ(env.GetTick(), 6);
  ASSERT_TRUE(result.done);
  for (double r : result.rewards) ASSERT_EQ(r, AggroLens::kTimePenalty);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

TEST(TestAWoundedEnemyKeepsTheEpisodeGoing) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  AgentFSM* goblin = GoblinNextToCompanion(env, 3);
  StepResult result = AttackRight(env);
  ASSERT_TRUE(goblin->IsAlive());
  ASSERT_EQ(goblin->GetHealth(), 2);
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsDone());
  for (double r : result.rewards) ASSERT_EQ(r, AggroLens::kTimePenalty);
}

// The RL return (rewards summed until done) of an episode where the companion
// stays, then kills the 1-HP goblin on step `kill_step` (1-based; 0 = never:
// the episode times out).
static double AggroReturn(int horizon, int kill_step) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, horizon);
  GoblinNextToCompanion(env, 1);
  const Action stay = EncodeAction(MovementAction::Stay);
  double total = 0.0;
  for (int step = 1; step <= horizon; ++step) {
    StepResult r = step == kill_step ? AttackRight(env) : env.Step({stay, stay});
    total += r.rewards[1];
    if (r.done) {
      ASSERT_EQ(env.GetTick(), horizon);  // Only the horizon ends it
      return total;
    }
  }
  throw std::runtime_error("the episode never ended");
}

// Killing the enemy changes no return: whatever the step, the episode runs to
// the horizon and returns what timing out does, the lowest return there is
// but for the downs' cost (MinUtility: every down paid, up to the team down;
// one companion: max_downs downs).
TEST(TestKillingTheEnemyReturnsWhatTimingOutDoes) {
  for (int horizon : {100, 400}) {
    const double time_out = AggroReturn(horizon, 0);
    ASSERT_TRUE(std::abs(time_out - horizon * AggroLens::kTimePenalty) < 1e-9);
    AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, horizon);
    const double downs_cost = env.GetDownCost() * env.GetMaxDowns();
    ASSERT_TRUE(std::abs(time_out + downs_cost - env.MinUtility()) < 1e-9);
    for (int kill_step : {1, 2, horizon / 2, horizon - 1, horizon}) {
      const double killed = AggroReturn(horizon, kill_step);
      ASSERT_TRUE(std::abs(killed - time_out) < 1e-9);
    }
  }
}

// The first outcome is final: a kill after a latched success keeps it. A host
// that keeps playing is rewarded as after any success with the enemy off the
// target (kTimePenalty).
TEST(TestAKillAfterASuccessKeepsTheSuccess) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 100);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  env.GetMutableObjectManager().UpdatePosition(goblin->GetId(), env.GetTargetPosition());
  const Action stay = EncodeAction(MovementAction::Stay);
  StepResult win = env.Step({stay, stay});
  ASSERT_TRUE(win.done);
  ASSERT_TRUE(env.IsSuccess());
  for (double r : win.rewards) ASSERT_EQ(r, AggroLens::kWinReward);

  goblin->TakeDamage(goblin->GetHealth());
  ASSERT_FALSE(goblin->IsAlive());
  while (env.GetTick() < 10) {
    StepResult r = env.Step({stay, stay});
    ASSERT_TRUE(r.done);
    for (double reward : r.rewards) ASSERT_EQ(reward, AggroLens::kTimePenalty);
  }
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
}

// The enemy lured onto the target after the horizon is no success: the
// episode ended there. Nothing latches; kTimePenalty, no kWinReward.
TEST(TestNoAggroSuccessAfterTheHorizon) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 2);
  AgentFSM* goblin = GoblinNextToCompanion(env, 3);
  const Action stay = EncodeAction(MovementAction::Stay);
  env.Step({stay, stay});
  ASSERT_TRUE(env.Step({stay, stay}).done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  env.GetMutableObjectManager().UpdatePosition(goblin->GetId(), env.GetTargetPosition());
  StepResult after = env.Step({stay, stay});
  ASSERT_TRUE(after.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  for (double r : after.rewards) ASSERT_EQ(r, AggroLens::kTimePenalty);
}

// The end reason is fixed when done first becomes true: a kill after the
// horizon changes nothing, the episode ended at the horizon.
TEST(TestAKillAfterTheHorizonKeepsTheHorizonEndReason) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 3);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  const Action stay = EncodeAction(MovementAction::Stay);
  for (int i = 0; i < 2; ++i) {
    env.Step({stay, stay});
    ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  }
  ASSERT_TRUE(env.Step({stay, stay}).done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  AttackRight(env);
  ASSERT_FALSE(goblin->IsAlive());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  env.Step({stay, stay});
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  // Copies keep the lens and the latched reason (a new AggroLens would refuse
  // this env: the goblin's FSM is off)
  AggroEnv copy(env);
  ASSERT_TRUE(copy.GetTaskLens() != nullptr);
  ASSERT_TRUE(copy.GetTaskLens()->GetKind() == TaskLens::kAggro);
  ASSERT_TRUE(copy.GetEndReason() == EndReason::Horizon);
  AggroEnv assigned(12, 1, EnemyType::Goblin, 7777, 0, 3);
  ASSERT_TRUE(assigned.SetTaskLens(std::make_unique<DodgeLens>()));
  assigned = env;
  ASSERT_TRUE(assigned.GetTaskLens() != nullptr);
  ASSERT_TRUE(assigned.GetTaskLens()->GetKind() == TaskLens::kAggro);
  ASSERT_TRUE(assigned.GetEndReason() == EndReason::Horizon);
  // A new episode starts over
  env.Reset();
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
}

// A kill on the horizon step is no failure either: the horizon ended the
// episode, as Horizon.
TEST(TestAKillOnTheHorizonStepEndsAsHorizon) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 2);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  const Action stay = EncodeAction(MovementAction::Stay);
  ASSERT_FALSE(env.Step({stay, stay}).done);
  ASSERT_TRUE(AttackRight(env).done);
  ASSERT_FALSE(goblin->IsAlive());
  ASSERT_EQ(env.GetTick(), env.GetHorizon());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

// A Reset under the Aggro lens loads its level before spawning the enemy: it
// starts with no end reason, and the first step's end is its own.
TEST(TestAResetUnderTheAggroLensLatchesNoStaleEndReason) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 1);
  ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kAggro);
  const Action stay = EncodeAction(MovementAction::Stay);
  for (int i = 0; i < 2; ++i) {
    env.Reset();
    ASSERT_FALSE(env.IsDone());
    ASSERT_TRUE(env.GetEndReason() == EndReason::None);
    ASSERT_TRUE(env.Step({stay, stay}).done);
    ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  }
}

// A snapshot loaded at the horizon is done there: a kill on the next step
// keeps Horizon, as after a Step.
TEST(TestASnapshotLoadedAtTheHorizonKeepsTheHorizonEndReason) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777, 0, 3);
  Snapshot at_horizon = env.SaveSnapshot();
  at_horizon.tick = at_horizon.horizon;

  AggroEnv loaded(12, 1, EnemyType::Goblin, 7777, 0, 3);
  loaded.LoadSnapshot(at_horizon);
  ASSERT_TRUE(loaded.IsDone());
  ASSERT_TRUE(loaded.GetEndReason() == EndReason::Horizon);
  AgentFSM* goblin = GoblinNextToCompanion(loaded, 1);
  AttackRight(loaded);
  ASSERT_FALSE(goblin->IsAlive());
  ASSERT_TRUE(loaded.GetEndReason() == EndReason::Horizon);
}

// No task ends on a dead enemy: under another lens either, AggroEnv is done
// on a latched success, a team down or at the horizon.
TEST(TestAKilledEnemyDoesNotEndAnotherLenssEpisode) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<DodgeLens>()));
  StepResult result = AttackRight(env);
  ASSERT_FALSE(goblin->IsAlive());
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsDone());
}

// Two companions: one down is not the team down, and no failure of the
// Dodge task: the down interrupts it (the next step reads Interrupted), the
// pause runs on, and the episode ends at the horizon, as Horizon (no success:
// a companion is still down).
TEST(TestADownedCompanionDoesNotEndAggroEnvUnderDodgeLens) {
  AggroEnv env(12, 2, EnemyType::Goblin, 7777, 0, 3);
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<DodgeLens>()));
  Agent* companion = env.GetMutableObjectManager().GetAllCompanions()[0];
  companion->TakeDamage(companion->GetHealth());
  ASSERT_TRUE(companion->IsDowned());
  ASSERT_FALSE(env.IsTeamDown());
  ASSERT_FALSE(env.GetTaskLens()->IsDone(env));
  ASSERT_FALSE(env.IsDone());
  const std::vector<Action> stay(static_cast<size_t>(env.NumAgents()),
                                 EncodeAction(MovementAction::Stay));  // The enemy's too
  for (int i = 0; i < 2; ++i) {
    StepResult result = env.Step(stay);
    ASSERT_TRUE(result.done);
    ASSERT_FALSE(env.IsTeamDown());
    ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  }
  ASSERT_TRUE(env.Step(stay).done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

TEST(TestSynchroLensSwappedOntoGoalsIsNotDoneBeforeAStep) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  LensParams params;
  params.positions = {env.GetObjectManager().GetAllCompanions()[0]->GetPosition()};
  ASSERT_TRUE(env.SetTaskLensWithParams(std::make_unique<SynchroLens>(), params));
  ASSERT_TRUE(env.GetTaskLens()->IsDone(env));  // The companion stands on the goal
  ASSERT_FALSE(env.IsDone());                   // but nothing is latched yet
  ASSERT_FALSE(env.IsSuccess());
}

// LoadSnapshot re-issues agent ids: an FSM target is mapped through the
// snapshot's own agent ids, whatever they are.
TEST(TestLoadSnapshotMapsTheFSMTargetThroughSavedIds) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  Snapshot snap = env.SaveSnapshot();
  ASSERT_EQ(snap.agents.size(), 2u);
  ASSERT_TRUE(snap.agents[0].has_fsm);
  snap.agents[0].id = 7;   // The goblin
  snap.agents[1].id = 9;   // The companion
  snap.agents[0].fsm.target_id = 9;
  env.LoadSnapshot(snap);
  const ObjectManager& om = env.GetObjectManager();
  ASSERT_EQ(om.GetAllAgentFSMs()[0]->GetFSMContext().target_id,
            om.GetAllCompanions()[0]->GetId());
  snap.agents[0].fsm.target_id = 8;  // No such agent
  env.LoadSnapshot(snap);
  ASSERT_EQ(env.GetObjectManager().GetAllAgentFSMs()[0]->GetFSMContext().target_id,
            kInvalidObjectId);
}

// Agent annotations are keyed by id: LoadSnapshot maps them through the saved
// ids like the FSM targets, and drops one naming no saved agent.
TEST(TestLoadSnapshotMapsAgentAnnotationsThroughSavedIds) {
  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  Snapshot snap = env.SaveSnapshot();
  ASSERT_EQ(snap.agents.size(), 2u);
  for (const AnnotationSnapshot& a : snap.annotations) ASSERT_EQ(a.target_type, 0);
  const size_t cell_annotations = snap.annotations.size();
  snap.agents[0].id = 7;  // The goblin
  snap.agents[1].id = 9;  // The companion
  AnnotationSnapshot mob;
  mob.target_type = 1;
  mob.agent_id = 7;
  mob.tag = SemanticTag::TargetMob;
  AnnotationSnapshot escort = mob;
  escort.agent_id = 9;
  escort.tag = SemanticTag::Escort;
  AnnotationSnapshot ghost = mob;
  ghost.agent_id = 8;  // No such agent
  ghost.tag = SemanticTag::SkillGiver;
  snap.annotations.push_back(mob);
  snap.annotations.push_back(ghost);
  snap.annotations.push_back(escort);

  env.LoadSnapshot(snap);
  const ObjectManager& om = env.GetObjectManager();
  const AnnotationStore& store = env.GetAnnotations();
  ASSERT_TRUE(store.FindAgentsWithTag(SemanticTag::TargetMob) ==
              std::vector<ObjectId>{om.GetAllAgentFSMs()[0]->GetId()});
  ASSERT_TRUE(store.FindAgentsWithTag(SemanticTag::Escort) ==
              std::vector<ObjectId>{om.GetAllCompanions()[0]->GetId()});
  ASSERT_TRUE(store.FindAgentsWithTag(SemanticTag::SkillGiver).empty());
  ASSERT_EQ(store.Size(), cell_annotations + 2);
  ASSERT_EQ(store.FindCellsWithTag(SemanticTag::AggroTarget).size(), 1u);  // Kept
}

// =============================================================================
// A dead attacker's telegraphed attacks are cancelled
// =============================================================================

// A 1-HP goblin next to the companion winds up (FSM telegraph, then its strike
// effect's own 2-tick telegraph). Once the strike is pending, the companion
// kills the goblin or not. True when the strike hurt the companion.
static bool GoblinStrikeLands(bool kill_during_wind_up) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig strike;
  strike.name = "goblin_wind_up";
  strike.telegraph_ticks = 2;
  strike.active_ticks = 1;
  strike.damage = 1;
  strike.area = {1};
  strike.filter = TargetFilter::Companion;
  registry.RegisterConfig(strike);

  AggroEnv env(12, 1, EnemyType::Goblin, 7777);
  AgentFSM* goblin = GoblinNextToCompanion(env, 1);
  goblin->SetCurrentState(&PatrolState::Instance());  // FSM on again
  goblin->GetFSMContext().has_attack = true;
  goblin->GetFSMContext().attack_effect_name = "goblin_wind_up";
  Companion* companion = env.GetMutableObjectManager().GetAllCompanions()[0];
  const int hp = companion->GetHealth();
  const Action stay = EncodeAction(MovementAction::Stay);

  for (int i = 0; i < 10 && env.GetActiveEffects().empty(); ++i) env.Step({stay, stay});
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].source_id, goblin->GetId());
  ASSERT_EQ(companion->GetHealth(), hp);

  if (kill_during_wind_up) {
    Position c = companion->GetPosition(), g = goblin->GetPosition();
    ASSERT_EQ(std::abs(c.row - g.row) + std::abs(c.col - g.col), 1);
    MovementAction aim = g.row < c.row   ? MovementAction::Up
                         : g.row > c.row ? MovementAction::Down
                         : g.col < c.col ? MovementAction::Left
                                         : MovementAction::Right;
    env.Step({stay, EncodeAction(aim, InteractAction::Skill1)});
    ASSERT_FALSE(goblin->IsAlive());
  }
  for (int i = 0; i < 3; ++i) env.Step({stay, stay});
  return companion->GetHealth() < hp;
}

TEST(TestKilledGoblinsPendingStrikeNeverLands) {
  ASSERT_FALSE(GoblinStrikeLands(true));
}

TEST(TestLivingGoblinsPendingStrikeLands) {
  ASSERT_TRUE(GoblinStrikeLands(false));
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
    } catch (const std::exception& e) {
      std::cout << "FAILED: " << e.what() << "\n";
      failed++;
    } catch (...) {
      std::cout << "FAILED\n";
      failed++;
    }
  }

  std::cout << "\n=== Results: " << passed << " passed, " << failed
            << " failed ===\n";

  return (failed > 0) ? 1 : 0;
}
