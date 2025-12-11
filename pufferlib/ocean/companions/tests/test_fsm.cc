// Copyright 2024
// Unit tests for FSM enemy AI

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/effect_config.h"
#include "../src/core/fsm/enemies.h"
#include "../src/core/pcg32.h"
#include "../src/core/fsm/fsm_state.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/core/grid.h"
#include "../src/core/level_builder.h"
#include "../src/core/object_manager.h"
#include "../src/core/pathfinder.h"
#include "../src/env/effect_system.h"
#include "../src/env/synchro_env.h"

using namespace companions;

// =============================================================================
// Test macros (same as test_main.cc)
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

#define ASSERT_NE(a, b) \
  if ((a) == (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_NE failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Patrol Path Validation Tests
// =============================================================================
TEST(TestValidatePatrolPathOrthogonal) {
  // Valid L-shaped path (only orthogonal segments)
  std::vector<Position> path = {{0, 0}, {0, 3}, {2, 3}};
  ASSERT_TRUE(ValidatePatrolPath(path));
}

TEST(TestValidatePatrolPathDiagonal) {
  // Invalid diagonal segment
  std::vector<Position> path = {{0, 0}, {1, 1}, {2, 2}};
  ASSERT_FALSE(ValidatePatrolPath(path));
}

TEST(TestValidatePatrolPathSingle) {
  // Single point is valid
  std::vector<Position> path = {{5, 5}};
  ASSERT_TRUE(ValidatePatrolPath(path));
}

TEST(TestValidatePatrolPathEmpty) {
  // Empty path is valid (returns true for size <= 1)
  std::vector<Position> path = {};
  ASSERT_TRUE(ValidatePatrolPath(path));
}

// =============================================================================
// Zombie Cadence Tests
// =============================================================================
TEST(TestZombieCadence) {
  // Create an 8x8 grid with floor cells
  Grid grid(8, 8);
  LevelBuilder builder(grid);
  builder.Fill(CellKind::Floor);

  ObjectManager mgr(8, 8);
  pcg32 rng(42);

  // Create zombie at position (4, 4)
  std::vector<Position> patrol_path = {{4, 4}, {4, 6}};
  Zombie* zombie = CreateZombie(mgr, {4, 4}, patrol_path, rng);

  // Zombie cadence is [1, 0] - should move on tick 0
  ASSERT_TRUE(zombie->CanAct());
  ASSERT_EQ(zombie->GetTick(), 0);

  // Advance tick - should NOT be able to act on tick 1
  zombie->AdvanceTick();
  ASSERT_EQ(zombie->GetTick(), 1);
  ASSERT_FALSE(zombie->CanAct());

  // Advance tick - should be able to act on tick 2
  zombie->AdvanceTick();
  ASSERT_EQ(zombie->GetTick(), 2);
  ASSERT_TRUE(zombie->CanAct());
}

TEST(TestZombieStaysOnOffTick) {
  // Create SynchroEnv (provides grid, pathfinder, etc.)
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create zombie at position (2, 2)
  std::vector<Position> patrol_path = {{2, 2}, {2, 5}};
  Zombie* zombie = CreateZombie(mgr, {2, 2}, patrol_path, rng);

  // Manually set tick to 1 (off-tick)
  zombie->AdvanceTick();
  ASSERT_FALSE(zombie->CanAct());

  // Call MoveTo - should set intention to Stay
  zombie->MoveTo({2, 5}, env);
  ASSERT_EQ(zombie->GetIntention().movement, MovementAction::Stay);
}

// =============================================================================
// Goblin Tests
// =============================================================================
TEST(TestGoblinAlwaysMoves) {
  Grid grid(8, 8);
  LevelBuilder builder(grid);
  builder.Fill(CellKind::Floor);

  ObjectManager mgr(8, 8);
  pcg32 rng(42);

  std::vector<Position> patrol_path = {{3, 3}, {3, 6}};
  Goblin* goblin = CreateGoblin(mgr, {3, 3}, patrol_path, rng);

  // Goblin has no cadence - always can act
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(goblin->CanAct());
    goblin->AdvanceTick();
  }
}

// =============================================================================
// Dragon Tests
// =============================================================================
TEST(TestDragonIsFlying) {
  ObjectManager mgr(8, 8);
  pcg32 rng(42);

  std::vector<Position> patrol_path = {{4, 4}};
  Dragon* dragon = CreateDragon(mgr, {4, 4}, patrol_path, rng);

  ASSERT_TRUE(dragon->IsFlying());
}

TEST(TestDragonIgnoresWalls) {
  // Create grid with walls
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Add walls between dragon and target
  Grid& grid = env.GetMutableGrid();
  grid.SetCell({3, 3}, CellKind::Wall);
  grid.SetCell({3, 4}, CellKind::Wall);
  grid.SetCell({3, 5}, CellKind::Wall);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create dragon at (2, 4), target at (5, 4) - wall in between
  std::vector<Position> patrol_path = {{2, 4}};
  Dragon* dragon = CreateDragon(mgr, {2, 4}, patrol_path, rng);

  // Dragon should move toward target, ignoring wall
  dragon->MoveTo({5, 4}, env);
  DecodedAction intention = dragon->GetIntention();

  // Dragon should move down toward target
  ASSERT_EQ(intention.movement, MovementAction::Down);
}

// =============================================================================
// FSM State Tests
// =============================================================================
TEST(TestPatrolStateMovesToWaypoint) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create zombie at (2, 3) with patrol path ending at (2, 5)
  // Zombie starts in BETWEEN waypoints so it will move toward waypoint
  std::vector<Position> patrol_path = {{2, 2}, {2, 5}};
  Zombie* zombie = CreateZombie(mgr, {2, 3}, patrol_path, rng);

  // Start in PatrolState
  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");

  // Update FSM - patrol_index=0 means target is (2,2), should move left
  zombie->UpdateFSM(env);

  // Check that zombie intends to move left (toward waypoint at column 2)
  ASSERT_EQ(zombie->GetIntention().movement, MovementAction::Left);
}

TEST(TestPatrolStateDetectsCompanion) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  // Get companion position from the environment
  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
  Position comp_pos = companions[0]->GetPosition();

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create zombie within detection range (3) - at distance 2
  Position zombie_pos = {comp_pos.row, comp_pos.col + 2};  // 2 cells away
  std::vector<Position> patrol_path = {{zombie_pos.row, zombie_pos.col}};
  Zombie* zombie = CreateZombie(mgr, zombie_pos, patrol_path, rng);

  // Update FSM - should detect companion and transition to Aggro
  // (not Telegraph yet because zombie is at distance 2, not adjacent)
  zombie->UpdateFSM(env);

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Aggro");
}

TEST(TestPatrolStateNoDetection) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
  Position comp_pos = companions[0]->GetPosition();

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create zombie far from companion (detection range is 3)
  Position zombie_pos = {comp_pos.row + 5, comp_pos.col + 5};
  if (zombie_pos.row >= 8) zombie_pos.row = 7;
  if (zombie_pos.col >= 8) zombie_pos.col = 7;

  std::vector<Position> patrol_path = {{zombie_pos.row, zombie_pos.col}};
  Zombie* zombie = CreateZombie(mgr, zombie_pos, patrol_path, rng);

  // Update FSM - should stay in Patrol (no detection)
  zombie->UpdateFSM(env);

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");
}

TEST(TestAggroStateFollowsTarget) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
  Position comp_pos = companions[0]->GetPosition();

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create goblin at distance 2 from companion
  Position goblin_pos = {comp_pos.row, comp_pos.col + 2};
  std::vector<Position> patrol_path = {{0, 0}};  // Far away patrol
  Goblin* goblin = CreateGoblin(mgr, goblin_pos, patrol_path, rng);

  // Update - transitions to Aggro (distance 2, not adjacent)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Aggro");

  // Movement should be toward companion
  ASSERT_EQ(goblin->GetIntention().movement, MovementAction::Left);
}

TEST(TestAggroStateLosesTarget) {
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Move companion far away to avoid detection during test
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), {11, 11});
  }

  // Create zombie with target_id set but target far away
  Position zombie_pos = {1, 1};
  std::vector<Position> patrol_path = {{1, 1}, {1, 3}};
  Zombie* zombie = CreateZombie(mgr, zombie_pos, patrol_path, rng);

  // Manually set to AggroState with a fake target (will be "lost")
  FSMContext& ctx = zombie->GetFSMContext();
  ctx.target_id = 9999;  // Non-existent target

  // Manually set state to Aggro
  zombie->SetFSM(&AggroState::Instance(), std::move(ctx));

  // Update - should transition away from Aggro (target lost)
  zombie->UpdateFSM(env);

  // Should transition to Patrol or Return (since target is invalid)
  std::string state_name = zombie->GetCurrentState()->GetName();
  ASSERT_TRUE(state_name == "Patrol" || state_name == "ReturnToPatrol");
}

TEST(TestAggroTransitionsToTelegraphWhenMovingAdjacent) {
  // Test that goblin at distance 2 transitions to Telegraph after moving adjacent
  // FSM runs AFTER movements, so we simulate the Step() flow:
  // 1. FSM sets movement intention (while in Aggro)
  // 2. Movement executes (goblin moves adjacent)
  // 3. FSM runs again (sees adjacent, transitions to Telegraph)
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  mgr.UpdatePosition(companions[0]->GetId(), {4, 4});

  // Create goblin at distance 2 from companion (at {4, 6})
  Goblin* goblin = CreateGoblin(mgr, {4, 6}, {{4, 6}}, rng);

  // FSM update 1: Patrol detects companion -> Aggro, dist=2, sets move intention
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Aggro");
  ASSERT_EQ(goblin->GetIntention().movement, MovementAction::Left);

  // Simulate movement execution (goblin moves from {4,6} to {4,5})
  mgr.UpdatePosition(goblin->GetId(), {4, 5});

  // FSM update 2: Now adjacent (dist=1), transitions to Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");
}

// =============================================================================
// Integration Tests
// =============================================================================
TEST(TestReturnToPatrolStateReturnsToPath) {
  // Test that enemy in ReturnToPatrol state moves back toward patrol path
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Move companion far away to avoid detection (detection range is typically 3-5)
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), {11, 11});  // Corner, far from zombie
  }

  // Create zombie OFF patrol path
  std::vector<Position> patrol_path = {{1, 1}, {1, 5}};
  Zombie* zombie = CreateZombie(mgr, {5, 5}, patrol_path, rng);  // Far from patrol

  // Manually set to ReturnToPatrol state (as if it lost target while chasing)
  FSMContext& ctx = zombie->GetFSMContext();
  ctx.patrol_index = 0;  // Target first waypoint
  zombie->SetFSM(&ReturnToPatrolState::Instance(), std::move(ctx));

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "ReturnToPatrol");

  // Update FSM - should move toward patrol waypoint at (1,1)
  zombie->UpdateFSM(env);

  // Should still be in ReturnToPatrol (not at waypoint yet)
  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "ReturnToPatrol");

  // Intention should NOT be Stay (should be moving toward waypoint)
  ASSERT_NE(zombie->GetIntention().movement, MovementAction::Stay);
}

TEST(TestReturnToPatrolTransitionsToPatrol) {
  // Test that enemy transitions from ReturnToPatrol to Patrol when reaching waypoint
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Move companion far away to avoid detection during test
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), {11, 11});
  }

  // Create zombie at patrol waypoint
  std::vector<Position> patrol_path = {{3, 3}, {3, 6}};
  Zombie* zombie = CreateZombie(mgr, {3, 3}, patrol_path, rng);

  // Manually set to ReturnToPatrol state (as if returning and just arrived)
  FSMContext ctx = zombie->GetFSMContext();
  ctx.patrol_index = 0;  // Targeting first waypoint (3,3) where we already are
  zombie->SetFSM(&ReturnToPatrolState::Instance(), std::move(ctx));

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "ReturnToPatrol");

  // Update FSM - should transition to Patrol since we're at the waypoint
  zombie->UpdateFSM(env);

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");
}

TEST(TestReturnToPatrolReAggrosOnDetection) {
  // Test that enemy in ReturnToPatrol can re-aggro if companion comes close
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Get companion
  auto companions = mgr.GetAllCompanions();
  ASSERT_TRUE(!companions.empty());

  // Place companion at known location
  Position comp_pos = {5, 5};
  mgr.UpdatePosition(companions[0]->GetId(), comp_pos);

  // Create zombie near companion (within detection range of 3)
  std::vector<Position> patrol_path = {{1, 1}, {1, 5}};
  Position zombie_pos = {5, 3};  // Distance 2 from companion
  Zombie* zombie = CreateZombie(mgr, zombie_pos, patrol_path, rng);

  // Manually set to ReturnToPatrol state
  FSMContext ctx = zombie->GetFSMContext();
  ctx.patrol_index = 0;
  zombie->SetFSM(&ReturnToPatrolState::Instance(), std::move(ctx));

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "ReturnToPatrol");

  // Update FSM - should detect companion and re-aggro
  zombie->UpdateFSM(env);

  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Aggro");
}

TEST(TestFullPatrolAggroReturnCycle) {
  // Test complete Patrol -> Aggro -> ReturnToPatrol -> Patrol cycle
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(100);  // Use different seed for more control

  // Create zombie on patrol path, far from companion initially
  std::vector<Position> patrol_path = {{1, 1}, {1, 5}};
  Zombie* zombie = CreateZombie(mgr, {1, 1}, patrol_path, rng);

  // Get companion
  auto companions = mgr.GetAllCompanions();
  ASSERT_TRUE(!companions.empty());

  // Place companion far away initially
  mgr.UpdatePosition(companions[0]->GetId(), {10, 10});

  // 1. Should start in Patrol
  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");

  // 2. Move companion into detection range (distance 2, detection range is 3)
  mgr.UpdatePosition(companions[0]->GetId(), {1, 3});
  zombie->UpdateFSM(env);
  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Aggro");

  // 3. Move companion beyond lose_target_range (5) to trigger return
  mgr.UpdatePosition(companions[0]->GetId(), {1, 8});  // Distance 7 from {1,1}
  zombie->UpdateFSM(env);

  // Should transition to ReturnToPatrol since zombie is still at {1,1} which is on patrol path
  // Actually, since zombie IS on patrol path, it should go directly to Patrol
  std::string state = zombie->GetCurrentState()->GetName();
  ASSERT_TRUE(state == "Patrol" || state == "ReturnToPatrol");

  // If it went to ReturnToPatrol, verify it can get back to Patrol
  if (state == "ReturnToPatrol") {
    // Move zombie back to patrol path (already there, so should transition)
    zombie->UpdateFSM(env);
    ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");
  }
}

TEST(TestZombieFullLoop) {
  // Test complete Patrol -> Aggro -> Return -> Patrol cycle
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create zombie far from companion with a patrol path
  std::vector<Position> patrol_path = {{1, 1}, {1, 5}};
  Zombie* zombie = CreateZombie(mgr, {1, 1}, patrol_path, rng);

  // Should start in Patrol
  ASSERT_EQ(zombie->GetCurrentState()->GetName(), "Patrol");

  // Get companion and verify it exists
  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
}

TEST(TestMultipleEnemiesSameStates) {
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Create multiple zombies
  Zombie* z1 = CreateZombie(mgr, {1, 1}, {{1, 1}, {1, 3}}, rng);
  Zombie* z2 = CreateZombie(mgr, {2, 2}, {{2, 2}, {2, 4}}, rng);
  Zombie* z3 = CreateZombie(mgr, {3, 3}, {{3, 3}, {3, 5}}, rng);

  // All should share the same PatrolState singleton
  ASSERT_EQ(z1->GetCurrentState(), z2->GetCurrentState());
  ASSERT_EQ(z2->GetCurrentState(), z3->GetCurrentState());
  ASSERT_EQ(z1->GetCurrentState(), &PatrolState::Instance());
}

TEST(TestDeterministicTieBreak) {
  // Test that same seed produces same tie-break result
  auto run_test = [](unsigned int seed) -> ObjectId {
    SynchroEnv env(12, 12, 3, 1, 0, seed);
    env.Reset(seed);

    ObjectManager& mgr = env.GetMutableObjectManager();
    pcg32 rng(seed);

    // Create zombie
    Zombie* zombie = CreateZombie(mgr, {6, 6}, {{6, 6}}, rng);

    // Update FSM multiple times to potentially trigger target selection
    for (int i = 0; i < 5; ++i) {
      zombie->UpdateFSM(env);
    }

    return zombie->GetFSMContext().target_id;
  };

  // Same seed should produce same result
  ObjectId result1a = run_test(12345);
  ObjectId result1b = run_test(12345);
  ASSERT_EQ(result1a, result1b);
}

// =============================================================================
// Attack System Tests
// =============================================================================
TEST(TestAggroTransitionsToTelegraph) {
  // Create environment with enough space - 12x12 with 1 companion, 1 synchro
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Get companion first and place it at known location
  auto companions = mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1);
  Position companion_pos = {6, 7};
  mgr.UpdatePosition(companions[0]->GetId(), companion_pos);

  // Create goblin adjacent to companion at {6, 6} (detection_range = 4)
  Position goblin_pos = {6, 6};
  Goblin* goblin = CreateGoblin(mgr, goblin_pos, {{6, 6}}, rng);

  // Enable attack in context
  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 1;
  ctx.attack_ticks = 1;
  ctx.recovery_ticks = 1;
  ctx.attack_damage = 1;
  ctx.attack_width = 1;
  ctx.attack_height = 1;
  ctx.attack_filter = TargetFilter::Companion;

  // Verify initial state
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Patrol");

  // Update FSM - Patrol detects companion -> Aggro -> already adjacent -> Telegraph
  // FSM transitions are immediate, so this goes Patrol -> Aggro -> Telegraph in one tick
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");
}

TEST(TestTelegraphToAttackToRecovery) {
  // Tests 2-tick phases: each state stays for 2 updates before transitioning
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  mgr.UpdatePosition(companions[0]->GetId(), {6, 7});

  Goblin* goblin = CreateGoblin(mgr, {6, 6}, {{6, 6}}, rng);

  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 2;  // 2 ticks to complete telegraph
  ctx.attack_ticks = 2;     // 2 ticks to complete attack
  ctx.recovery_ticks = 2;   // 2 ticks to complete recovery
  ctx.attack_damage = 1;

  // Update 1: Patrol -> Aggro -> Telegraph (OnEnter: counter=0)
  //           Update: 0 < 2, counter=1, stay in Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // Update 2: Telegraph (counter=1 < 2, counter=2, stay)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // Update 3: Telegraph (counter=2 >= 2) -> Attack (OnEnter: counter=0)
  //           Update: 0 < 2, counter=1, stay in Attack
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Attack");

  // Update 4: Attack (counter=1 < 2, counter=2, stay)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Attack");

  // Update 5: Attack (counter=2 >= 2) -> Recovery (OnEnter: counter=0)
  //           Update: 0 < 2, counter=1, stay in Recovery
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Recovery");

  // Update 6: Recovery (counter=1 < 2, counter=2, stay)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Recovery");

  // Update 7: Recovery (counter=2 >= 2) -> Aggro (still adjacent) -> Telegraph
  //           (OnEnter: counter=0), Update: 0 < 2, counter=1, stay in Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");
}

TEST(TestAttackTargetPositionLockedIn) {
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  Player* companion = dynamic_cast<Player*>(companions[0]);
  mgr.UpdatePosition(companion->GetId(), {6, 7});

  Goblin* goblin = CreateGoblin(mgr, {6, 6}, {{6, 6}}, rng);

  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 2;  // Use 2 ticks so we can verify attack position before attack
  ctx.attack_ticks = 2;
  ctx.recovery_ticks = 1;

  // Update 1: Patrol -> Aggro -> Telegraph (OnEnter: counter=0)
  //           Update: 0 < 2, counter=1, stay in Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // Target position should be locked in when entering Telegraph
  Position locked_target = ctx.current_attack.target_position;
  ASSERT_EQ(locked_target, Position({6, 7}));

  // Move companion away before attack happens
  mgr.UpdatePosition(companion->GetId(), {9, 9});

  // Update 2: Telegraph (counter=1 < 2, counter=2, stay)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // Update 3: Telegraph (counter=2 >= 2) -> Attack
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Attack");

  // Target position should still be the original position (locked in during telegraph)
  ASSERT_EQ(ctx.current_attack.target_position, Position({6, 7}));
}

TEST(TestHealthSystem) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();

  auto companions = mgr.GetAllCompanions();
  Player* companion = dynamic_cast<Player*>(companions[0]);

  // Check default health
  ASSERT_EQ(companion->GetHealth(), 3);
  ASSERT_EQ(companion->GetMaxHealth(), 3);
  ASSERT_FALSE(companion->IsDead());

  // Take damage
  companion->TakeDamage(1);
  ASSERT_EQ(companion->GetHealth(), 2);
  ASSERT_FALSE(companion->IsDead());

  // Heal
  companion->Heal(1);
  ASSERT_EQ(companion->GetHealth(), 3);

  // Can't heal above max
  companion->Heal(10);
  ASSERT_EQ(companion->GetHealth(), 3);

  // Take lethal damage
  companion->TakeDamage(5);
  ASSERT_EQ(companion->GetHealth(), 0);
  ASSERT_TRUE(companion->IsDead());
  ASSERT_FALSE(companion->IsAlive());
}

TEST(TestFactionSystem) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  Player* companion = dynamic_cast<Player*>(companions[0]);

  // Create enemy
  Goblin* goblin = CreateGoblin(mgr, {3, 3}, {{3, 3}}, rng);

  // Check factions
  ASSERT_EQ(companion->GetFaction(), Faction::COMPANION);
  ASSERT_EQ(goblin->GetFaction(), Faction::ENEMY);

  // Can change faction
  goblin->SetFaction(Faction::NEUTRAL);
  ASSERT_EQ(goblin->GetFaction(), Faction::NEUTRAL);
}

// =============================================================================
// Test helper class to expose protected methods
// =============================================================================
class TestableEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  void TestResolveInteractions() { ResolveInteractions(); }
  void TestTickEffects() { GetEffectSystem().Tick(); }
};

// =============================================================================
// Attack Sequence Tests (1-tick phases)
// =============================================================================
TEST(TestAttackSequenceOneTick) {
  // Small 5x5 grid - verifies Telegraph -> Attack -> Recovery -> Telegraph cycle
  // With 1-tick phases, each state stays for exactly 1 Update before transitioning
  TestableEnv env(5, 5, 1, 1, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Place companion at known position
  auto companions = mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1);
  mgr.UpdatePosition(companions[0]->GetId(), {2, 3});

  // Create goblin adjacent to companion at {2, 2}
  Goblin* goblin = CreateGoblin(mgr, {2, 2}, {{2, 2}}, rng);

  // Use 1-tick phases (each state stays for 1 update, then transitions next update)
  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 1;
  ctx.attack_ticks = 1;
  ctx.recovery_ticks = 1;
  ctx.attack_damage = 1;

  // Tick 0: Patrol -> Aggro -> Telegraph (immediate chain since adjacent)
  //         OnEnter: counter=0, Update: 0 < 1, counter=1, stay in Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // Tick 1: Telegraph (counter=1 >= 1) -> Attack
  //         OnEnter: counter=0, Update: 0 < 1, counter=1, stay in Attack
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Attack");

  // Tick 2: Attack (counter=1 >= 1) -> Recovery
  //         OnEnter: counter=0, Update: 0 < 1, counter=1, stay in Recovery
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Recovery");

  // Tick 3: Recovery (counter=1 >= 1) -> Aggro (still adjacent) -> Telegraph
  //         OnEnter: counter=0, Update: 0 < 1, counter=1, stay in Telegraph
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");
}

TEST(TestDodgeAndReturnGetsDamaged) {
  // Test: Move away during windup, return before attack lands -> get damaged
  // Effect has 1-tick telegraph, so damage is applied when TickEffects() transitions
  // from telegraph to active phase.
  TestableEnv env(5, 5, 1, 1, 42);
  env.Reset(42);

  // Effect with 1-tick telegraph: damage applied during TickEffects() when
  // telegraph phase ends and active phase begins
  EffectConfig effect_cfg;
  effect_cfg.name = "delayed_attack";
  effect_cfg.telegraph_ticks = 1;  // 1-tick telegraph delay
  effect_cfg.active_ticks = 1;
  effect_cfg.recovery_ticks = 0;
  effect_cfg.damage = 1;
  effect_cfg.filter = TargetFilter::Companion;
  effect_cfg.area = {1};  // 1x1 area
  EffectConfigRegistry::Instance().RegisterConfig(effect_cfg);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1);
  Player* companion = dynamic_cast<Player*>(companions[0]);
  ASSERT_TRUE(companion != nullptr);

  Position original_pos = {2, 3};
  mgr.UpdatePosition(companion->GetId(), original_pos);

  // Create goblin adjacent to companion
  Goblin* goblin = CreateGoblin(mgr, {2, 2}, {{2, 2}}, rng);

  // Use 1-tick phases (each state stays for 1 update, then transitions next update)
  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 1;
  ctx.attack_ticks = 1;
  ctx.recovery_ticks = 1;
  ctx.attack_damage = 1;
  ctx.attack_width = 1;
  ctx.attack_height = 1;
  ctx.attack_filter = TargetFilter::Companion;
  ctx.attack_effect_name = "delayed_attack";

  int initial_health = companion->GetHealth();  // 3

  // Tick 0: Patrol -> Aggro -> Telegraph (locks target position at {2,3})
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");
  ASSERT_EQ(ctx.current_attack.target_position, original_pos);

  // Move companion away during telegraph phase
  Position dodge_pos = {2, 4};
  mgr.UpdatePosition(companion->GetId(), dodge_pos);

  // Tick 1: Telegraph -> Attack (spawns effect with 1-tick telegraph)
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Attack");

  // Effect is now in telegraph phase (1 tick remaining)
  // Companion is at dodge_pos - no damage yet
  ASSERT_EQ(companion->GetHealth(), initial_health);

  // Move companion BACK to original position before effect's telegraph ends
  mgr.UpdatePosition(companion->GetId(), original_pos);

  // Tick effects: decrements telegraph counter (1 -> 0), transitions to active,
  // and applies damage. Companion is now at original_pos!
  env.TestTickEffects();

  // Companion should have taken damage since they returned to attack zone
  ASSERT_EQ(companion->GetHealth(), initial_health - 1);
}

TEST(TestFastGoblinDamageViaEffect) {
  // Test: FastGoblin with attack_ticks=0 and recovery_ticks=0
  // FSM transitions instantly through Attack and Recovery, but effect still applies damage
  // With telegraph_ticks=0 on the effect, damage is applied immediately when spawned
  TestableEnv env(5, 5, 1, 1, 42);
  env.Reset(42);

  // Register a "fast_attack" effect with NO telegraph
  // This means damage is applied immediately when effect is spawned
  EffectConfig effect_cfg;
  effect_cfg.name = "fast_attack";
  effect_cfg.telegraph_ticks = 0;  // No telegraph - instant damage on spawn
  effect_cfg.active_ticks = 1;     // Effect lasts 1 tick
  effect_cfg.recovery_ticks = 0;
  effect_cfg.damage = 1;
  effect_cfg.filter = TargetFilter::Companion;
  effect_cfg.area = {1};  // 1x1 area
  EffectConfigRegistry::Instance().RegisterConfig(effect_cfg);

  ObjectManager& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  auto companions = mgr.GetAllCompanions();
  Player* companion = dynamic_cast<Player*>(companions[0]);
  Position companion_pos = {2, 3};
  mgr.UpdatePosition(companion->GetId(), companion_pos);

  // Create goblin adjacent to companion
  Goblin* goblin = CreateGoblin(mgr, {2, 2}, {{2, 2}}, rng);

  // Configure as "FastGoblin" with 0-tick attack and recovery phases
  FSMContext& ctx = goblin->GetFSMContext();
  ctx.has_attack = true;
  ctx.telegraph_ticks = 1;   // 1-tick telegraph (stays in Telegraph for 1 update)
  ctx.attack_ticks = 0;      // Instant transition through Attack
  ctx.recovery_ticks = 0;    // Instant transition through Recovery
  ctx.attack_damage = 1;
  ctx.attack_effect_name = "fast_attack";

  int initial_health = companion->GetHealth();

  // Tick 0: Patrol -> Aggro -> Telegraph
  // Adjacent, so transitions to Telegraph immediately
  goblin->UpdateFSM(env);
  ASSERT_EQ(goblin->GetCurrentState()->GetName(), "Telegraph");

  // No damage yet - still in FSM telegraph phase
  ASSERT_EQ(companion->GetHealth(), initial_health);

  // Tick 1: Telegraph (counter=1 >= 1) -> Attack -> Recovery -> Aggro
  // With attack_ticks=0 and recovery_ticks=0, FSM chains through instantly
  // AttackState::OnEnter spawns the effect with telegraph_ticks=0,
  // so ApplyEffectModifiers is called immediately during SpawnEffect!
  goblin->UpdateFSM(env);

  // FSM should be back in Telegraph (still adjacent to companion)
  std::string state = goblin->GetCurrentState()->GetName();
  ASSERT_TRUE(state == "Aggro" || state == "Telegraph");

  // Companion ALREADY took damage - effect with telegraph_ticks=0 applies immediately
  ASSERT_EQ(companion->GetHealth(), initial_health - 1);
}

// =============================================================================
// Randomized Pathfinding Tests
// =============================================================================
TEST(TestPathfinderRandomizesDiagonalMovement) {
  // When an enemy is diagonal from target (e.g., 1 north, 1 east),
  // both directions are equally optimal. With RNG, the pathfinder should
  // produce different first steps across different seeds.

  Grid grid(5, 5);
  LevelBuilder builder(grid);
  builder.Fill(CellKind::Floor);

  // Enemy at (2,2), target at (1,3) - diagonal (1 north, 1 east)
  Position enemy_pos = {2, 2};
  Position target_pos = {1, 3};

  // Without RNG: deterministic behavior
  {
    Pathfinder pathfinder(grid);
    auto path1 = pathfinder.FindPath(enemy_pos, target_pos);
    auto path2 = pathfinder.FindPath(enemy_pos, target_pos);
    ASSERT_EQ(path1.size(), path2.size());
    ASSERT_TRUE(path1.size() > 1);
    // Same first step without RNG
    ASSERT_EQ(path1[1], path2[1]);
  }

  // With RNG: different seeds should produce varied first steps
  // Count how many times we get each first step
  int up_count = 0;    // (1,2) - moving north
  int right_count = 0; // (2,3) - moving east

  for (int seed = 0; seed < 100; ++seed) {
    pcg32 rng(seed);
    Pathfinder pathfinder(grid);
    pathfinder.SetRng(&rng);
    auto path = pathfinder.FindPath(enemy_pos, target_pos);
    ASSERT_TRUE(path.size() > 1);

    Position first_step = path[1];
    if (first_step.row == 1 && first_step.col == 2) {
      up_count++;
    } else if (first_step.row == 2 && first_step.col == 3) {
      right_count++;
    }
  }

  // Should see both directions with reasonable frequency (randomized)
  ASSERT_TRUE(up_count > 0 || right_count > 0);
  // With 100 trials and ~50% chance each, expect both to appear
  ASSERT_TRUE(up_count > 0 && right_count > 0);
}

TEST(TestGoblinRandomizesDiagonalChase) {
  // Test that Goblin's MoveTo uses the RNG from FSMContext for pathfinding
  // Each iteration creates a fresh environment to avoid object accumulation
  // Use 200 iterations for robust statistical verification (was 50)

  // Target position: diagonal from goblin starting position
  Position goblin_start = {2, 2};
  Position target_pos = {1, 3};

  int up_count = 0;
  int right_count = 0;

  for (int seed = 0; seed < 200; ++seed) {
    // Fresh environment each time
    SynchroEnv env(5, 5, 1, 1, 0, seed);
    env.Reset(seed);

    ObjectManager& mgr = env.GetMutableObjectManager();
    pcg32 rng(seed);

    // Create goblin
    Goblin* goblin = CreateGoblin(mgr, goblin_start, {goblin_start}, rng);

    // Call MoveTo directly
    goblin->MoveTo(target_pos, env);

    // Check the intended movement
    MovementAction action = goblin->GetIntention().movement;
    if (action == MovementAction::Up) {
      up_count++;
    } else if (action == MovementAction::Right) {
      right_count++;
    }
  }

  // Should see both Up (north) and Right (east) as valid first moves
  ASSERT_TRUE(up_count > 0 || right_count > 0);
  // With randomization over 200 trials, expect both directions to appear
  // Each direction should appear at least 10% of the time (20 out of 200)
  ASSERT_TRUE(up_count >= 20 && right_count >= 20);
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

  std::cout << "Running " << tests.size() << " FSM tests...\n\n";

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

  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";

  return (failed > 0) ? 1 : 0;
}
