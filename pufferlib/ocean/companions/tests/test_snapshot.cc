// Copyright 2024
// Test suite for Snapshot system

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/snapshot.h"
#include "../src/core/types.h"
#include "../src/core/cell.h"
#include "../src/env/synchro_env.h"
#include "../src/env/aggro_env.h"
#include "../src/env/dodge_env.h"

using namespace companions;

// =============================================================================
// Test macros
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

#define ASSERT_THROW(expr, exc_type) \
  do { \
    bool caught = false; \
    try { expr; } \
    catch (const exc_type&) { caught = true; } \
    if (!caught) { \
      std::ostringstream oss; \
      oss << "ASSERT_THROW failed: expected " #exc_type " at " << __FILE__ << ":" << __LINE__; \
      throw std::runtime_error(oss.str()); \
    } \
  } while (0)

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Snapshot Validation Helper Tests
// =============================================================================

TEST(TestSnapshotCountCells) {
  Snapshot snap;
  snap.rows = 3;
  snap.cols = 3;
  snap.cells.resize(9);

  // All floor by default
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }

  ASSERT_EQ(snap.CountCells(CellKind::Floor), 9);
  ASSERT_EQ(snap.CountCells(CellKind::Wall), 0);
  ASSERT_EQ(snap.CountCells(CellKind::Synchro), 0);

  // Add some walls
  snap.cells[0].kind = CellKind::Wall;
  snap.cells[1].kind = CellKind::Wall;
  ASSERT_EQ(snap.CountCells(CellKind::Floor), 7);
  ASSERT_EQ(snap.CountCells(CellKind::Wall), 2);

  // Add synchro cells
  snap.cells[4].kind = CellKind::Synchro;
  snap.cells[5].kind = CellKind::Synchro;
  snap.cells[6].kind = CellKind::Synchro;
  ASSERT_EQ(snap.CountCells(CellKind::Synchro), 3);
  ASSERT_EQ(snap.CountCells(CellKind::Floor), 4);
}

TEST(TestSnapshotHasSynchroCells) {
  Snapshot snap;
  snap.rows = 3;
  snap.cols = 3;
  snap.cells.resize(9);

  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }

  ASSERT_FALSE(snap.HasSynchroCells());

  snap.cells[4].kind = CellKind::Synchro;
  ASSERT_TRUE(snap.HasSynchroCells());
}

TEST(TestSnapshotHasTargetCell) {
  Snapshot snap;
  snap.rows = 3;
  snap.cols = 3;
  snap.cells.resize(9);

  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }

  ASSERT_FALSE(snap.HasTargetCell());

  snap.cells[4].kind = CellKind::Target;
  ASSERT_TRUE(snap.HasTargetCell());
}

TEST(TestSnapshotHasPatrolPath) {
  Snapshot snap;
  snap.rows = 3;
  snap.cols = 3;
  snap.cells.resize(9);

  // No patrol path
  ASSERT_FALSE(snap.HasPatrolPath());

  // Add patrol path directly
  snap.patrol_path = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
  ASSERT_TRUE(snap.HasPatrolPath());

  // Clear direct patrol path, add via FSM agent
  snap.patrol_path.clear();
  ASSERT_FALSE(snap.HasPatrolPath());

  AgentSnapshot agent;
  agent.has_fsm = true;
  agent.fsm.patrol_path = {{2, 2}, {2, 3}};
  snap.agents.push_back(agent);
  ASSERT_TRUE(snap.HasPatrolPath());
}

// =============================================================================
// Serialization Round-Trip Tests
// =============================================================================

TEST(TestSnapshotSerializeDeserializeEmpty) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;
  original.tick = 10;
  original.horizon = 100;
  original.d4_transform = 3;
  original.rng_state = 12345;
  original.rng_inc = 67890;

  // Empty cells, no agents, no effects

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.rows, original.rows);
  ASSERT_EQ(restored.cols, original.cols);
  ASSERT_EQ(restored.tick, original.tick);
  ASSERT_EQ(restored.horizon, original.horizon);
  ASSERT_EQ(restored.d4_transform, original.d4_transform);
  ASSERT_EQ(restored.rng_state, original.rng_state);
  ASSERT_EQ(restored.rng_inc, original.rng_inc);
  ASSERT_EQ(restored.cells.size(), original.cells.size());
  ASSERT_EQ(restored.agents.size(), original.agents.size());
  ASSERT_EQ(restored.effects.size(), original.effects.size());
}

TEST(TestSnapshotSerializeDeserializeWithCells) {
  Snapshot original;
  original.rows = 4;
  original.cols = 4;
  original.cells.resize(16);

  // Create a pattern: walls on border, floor inside, one synchro
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      int idx = r * 4 + c;
      if (r == 0 || r == 3 || c == 0 || c == 3) {
        original.cells[idx].kind = CellKind::Wall;
      } else {
        original.cells[idx].kind = CellKind::Floor;
      }
    }
  }
  original.cells[5].kind = CellKind::Synchro;  // Position (1,1)

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.rows, 4);
  ASSERT_EQ(restored.cols, 4);
  ASSERT_EQ(restored.cells.size(), 16u);

  // Verify cell pattern
  ASSERT_EQ(restored.cells[0].kind, CellKind::Wall);
  ASSERT_EQ(restored.cells[5].kind, CellKind::Synchro);
  ASSERT_EQ(restored.cells[6].kind, CellKind::Floor);  // Position (1,2)
  ASSERT_EQ(restored.cells[15].kind, CellKind::Wall);  // Position (3,3)
}

TEST(TestSnapshotSerializeDeserializeWithAgents) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;

  // Add a player agent
  AgentSnapshot player;
  player.id = 1;
  player.type = static_cast<int>(ObjectType::Player);
  player.position = {2, 2};
  player.prev_position = {2, 1};
  player.health = 3;
  player.max_health = 3;
  player.alive = true;
  player.agent_index = 0;
  player.has_fsm = false;
  original.agents.push_back(player);

  // Add an FSM agent
  AgentSnapshot fsm_agent;
  fsm_agent.id = 2;
  fsm_agent.type = static_cast<int>(ObjectType::AgentFSM);
  fsm_agent.position = {3, 3};
  fsm_agent.health = 5;
  fsm_agent.max_health = 5;
  fsm_agent.alive = true;
  fsm_agent.has_fsm = true;
  fsm_agent.fsm.state_type = FSMStateType::Patrol;
  fsm_agent.fsm.patrol_path = {{1, 1}, {1, 2}, {2, 2}, {2, 1}};
  fsm_agent.fsm.patrol_index = 2;
  fsm_agent.fsm.patrol_forward = true;
  fsm_agent.fsm.detection_range = 4;
  fsm_agent.fsm.lose_target_range = 6;
  fsm_agent.cadence = {1, 0};
  original.agents.push_back(fsm_agent);

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.agents.size(), 2u);

  // Check player
  ASSERT_EQ(restored.agents[0].id, 1);
  ASSERT_EQ(restored.agents[0].position.row, 2);
  ASSERT_EQ(restored.agents[0].position.col, 2);
  ASSERT_EQ(restored.agents[0].health, 3);
  ASSERT_FALSE(restored.agents[0].has_fsm);

  // Check FSM agent
  ASSERT_EQ(restored.agents[1].id, 2);
  ASSERT_EQ(restored.agents[1].position.row, 3);
  ASSERT_EQ(restored.agents[1].position.col, 3);
  ASSERT_TRUE(restored.agents[1].has_fsm);
  ASSERT_EQ(static_cast<int>(restored.agents[1].fsm.state_type),
            static_cast<int>(FSMStateType::Patrol));
  ASSERT_EQ(restored.agents[1].fsm.patrol_path.size(), 4u);
  ASSERT_EQ(restored.agents[1].fsm.patrol_index, 2);
  ASSERT_EQ(restored.agents[1].fsm.detection_range, 4);
  ASSERT_EQ(restored.agents[1].cadence.size(), 2u);
  ASSERT_EQ(restored.agents[1].cadence[0], 1);
  ASSERT_EQ(restored.agents[1].cadence[1], 0);
}

TEST(TestSnapshotSerializeDeserializeWithEffects) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;

  EffectSnapshot effect;
  effect.effect_name = "fire_burst";
  effect.target_type = 0;  // AtCell
  effect.target_cell = {2, 3};
  effect.direction = static_cast<int>(Direction::Down);
  effect.ticks_remaining = 2;
  effect.in_telegraph = true;
  effect.loops_remaining = 0;
  original.effects.push_back(effect);

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.effects.size(), 1u);
  ASSERT_EQ(restored.effects[0].effect_name, "fire_burst");
  ASSERT_EQ(restored.effects[0].target_cell.row, 2);
  ASSERT_EQ(restored.effects[0].target_cell.col, 3);
  ASSERT_EQ(restored.effects[0].ticks_remaining, 2);
  ASSERT_TRUE(restored.effects[0].in_telegraph);
}

TEST(TestSnapshotSerializeDeserializeWithPatrolPath) {
  Snapshot original;
  original.rows = 8;
  original.cols = 8;
  original.patrol_path = {{1, 1}, {1, 2}, {1, 3}, {2, 3}, {3, 3}, {3, 2}, {3, 1}, {2, 1}};

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.patrol_path.size(), 8u);
  ASSERT_EQ(restored.patrol_path[0].row, 1);
  ASSERT_EQ(restored.patrol_path[0].col, 1);
  ASSERT_EQ(restored.patrol_path[4].row, 3);
  ASSERT_EQ(restored.patrol_path[4].col, 3);
}

// =============================================================================
// SaveSnapshot/LoadSnapshot Integration Tests
// =============================================================================

TEST(TestSynchroEnvSaveLoadRoundTrip) {
  // Create env and run a few steps
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);

  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Right));
  env.Step(actions);
  env.Step(actions);

  // Save snapshot
  Snapshot snap = env.SaveSnapshot();

  // Create new env and load snapshot
  SynchroEnv env2(8, 8, 2, 2, 0, 999, 0, 100);  // Different seed
  env2.LoadSnapshot(snap);

  // Verify state matches
  ASSERT_EQ(env2.GetTick(), env.GetTick());
  ASSERT_EQ(env2.GetGrid().GetRows(), env.GetGrid().GetRows());
  ASSERT_EQ(env2.GetGrid().GetCols(), env.GetGrid().GetCols());

  auto agents1 = env.GetObjectManager().GetAllAgents();
  auto agents2 = env2.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents1.size(), agents2.size());

  for (size_t i = 0; i < agents1.size(); ++i) {
    ASSERT_EQ(agents1[i]->GetPosition(), agents2[i]->GetPosition());
    ASSERT_EQ(agents1[i]->GetHealth(), agents2[i]->GetHealth());
  }
}

TEST(TestAggroEnvSaveLoadRoundTrip) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42, 0, 100);

  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));
  env.Step(actions);
  env.Step(actions);

  Snapshot snap = env.SaveSnapshot();

  AggroEnv env2(10, 1, EnemyType::Zombie, 999, 0, 100);
  env2.LoadSnapshot(snap);

  ASSERT_EQ(env2.GetTick(), env.GetTick());

  auto agents1 = env.GetObjectManager().GetAllAgents();
  auto agents2 = env2.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents1.size(), agents2.size());

  for (size_t i = 0; i < agents1.size(); ++i) {
    ASSERT_EQ(agents1[i]->GetPosition(), agents2[i]->GetPosition());
  }
}

TEST(TestDodgeEnvSaveLoadRoundTrip) {
  DodgeEnv env(7, 2, 5, 50, 42, 0);

  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Down));
  env.Step(actions);

  Snapshot snap = env.SaveSnapshot();

  DodgeEnv env2(7, 2, 5, 50, 999, 0);
  env2.LoadSnapshot(snap);

  ASSERT_EQ(env2.GetTick(), env.GetTick());

  auto agents1 = env.GetObjectManager().GetAllAgents();
  auto agents2 = env2.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents1.size(), agents2.size());
}

TEST(TestSaveLoadPreservesAgentPositions) {
  SynchroEnv env(10, 10, 3, 3, 0, 12345, 0, 100);

  // Move agents around
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Down),
    EncodeAction(MovementAction::Left)
  };
  env.Step(actions);
  env.Step(actions);

  // Record positions before save
  auto agents_before = env.GetObjectManager().GetAllAgents();
  std::vector<Position> positions_before;
  for (const auto* agent : agents_before) {
    positions_before.push_back(agent->GetPosition());
  }

  // Save and load
  Snapshot snap = env.SaveSnapshot();
  SynchroEnv env2(10, 10, 3, 3, 0, 99999, 0, 100);
  env2.LoadSnapshot(snap);

  // Verify positions match
  auto agents_after = env2.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents_after.size(), positions_before.size());
  for (size_t i = 0; i < agents_after.size(); ++i) {
    ASSERT_EQ(agents_after[i]->GetPosition(), positions_before[i]);
  }
}

TEST(TestSaveLoadPreservesAgentHealth) {
  DodgeEnv env(7, 1, 3, 100, 42, 0);

  // Get agent and damage it
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_TRUE(agents.size() > 0);
  agents[0]->TakeDamage(1);
  int health_before = agents[0]->GetHealth();

  Snapshot snap = env.SaveSnapshot();
  DodgeEnv env2(7, 1, 3, 100, 999, 0);
  env2.LoadSnapshot(snap);

  auto agents2 = env2.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents2[0]->GetHealth(), health_before);
}

TEST(TestSaveLoadPreservesTick) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);

  // Run several steps
  std::vector<Action> actions(2, EncodeAction(MovementAction::Stay));
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  int tick_before = env.GetTick();
  ASSERT_EQ(tick_before, 10);

  Snapshot snap = env.SaveSnapshot();
  SynchroEnv env2(8, 8, 2, 2, 0, 999, 0, 100);
  env2.LoadSnapshot(snap);

  ASSERT_EQ(env2.GetTick(), tick_before);
}

// =============================================================================
// ValidateSnapshot Exception Tests
// =============================================================================

TEST(TestSynchroEnvValidateSnapshotMissingSynchro) {
  SynchroEnv env(8, 8, 3, 3, 0, 42, 0, 100);  // 3 companions, requires 3 synchro cells

  // Create snapshot with no synchro cells
  Snapshot snap;
  snap.rows = 8;
  snap.cols = 8;
  snap.cells.resize(64);
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }
  // Add walls on border
  for (int i = 0; i < 8; ++i) {
    snap.cells[i].kind = CellKind::Wall;           // Top row
    snap.cells[56 + i].kind = CellKind::Wall;      // Bottom row
    snap.cells[i * 8].kind = CellKind::Wall;       // Left col
    snap.cells[i * 8 + 7].kind = CellKind::Wall;   // Right col
  }
  // Only 1 synchro cell (need 3)
  snap.cells[9].kind = CellKind::Synchro;

  // Add agents to make snapshot loadable
  AgentSnapshot agent1;
  agent1.id = 1;
  agent1.type = static_cast<int>(ObjectType::Player);
  agent1.position = {2, 2};
  agent1.health = 3;
  agent1.max_health = 3;
  agent1.alive = true;
  snap.agents.push_back(agent1);

  AgentSnapshot agent2;
  agent2.id = 2;
  agent2.type = static_cast<int>(ObjectType::Companion);
  agent2.position = {3, 3};
  agent2.health = 3;
  agent2.max_health = 3;
  agent2.alive = true;
  snap.agents.push_back(agent2);

  AgentSnapshot agent3;
  agent3.id = 3;
  agent3.type = static_cast<int>(ObjectType::Companion);
  agent3.position = {4, 4};
  agent3.health = 3;
  agent3.max_health = 3;
  agent3.alive = true;
  snap.agents.push_back(agent3);

  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
}

TEST(TestAggroEnvValidateSnapshotMissingTarget) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42, 0, 100);

  // Create snapshot without target cell
  Snapshot snap;
  snap.rows = 10;
  snap.cols = 10;
  snap.cells.resize(100);
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }
  // Add walls on border
  for (int i = 0; i < 10; ++i) {
    snap.cells[i].kind = CellKind::Wall;
    snap.cells[90 + i].kind = CellKind::Wall;
    snap.cells[i * 10].kind = CellKind::Wall;
    snap.cells[i * 10 + 9].kind = CellKind::Wall;
  }
  // Has patrol path but no target
  snap.patrol_path = {{2, 2}, {2, 3}, {3, 3}, {3, 2}};

  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
}

TEST(TestAggroEnvValidateSnapshotMissingPatrol) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42, 0, 100);

  // Create snapshot without patrol path
  Snapshot snap;
  snap.rows = 10;
  snap.cols = 10;
  snap.cells.resize(100);
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }
  for (int i = 0; i < 10; ++i) {
    snap.cells[i].kind = CellKind::Wall;
    snap.cells[90 + i].kind = CellKind::Wall;
    snap.cells[i * 10].kind = CellKind::Wall;
    snap.cells[i * 10 + 9].kind = CellKind::Wall;
  }
  // Has target but no patrol path
  snap.cells[55].kind = CellKind::Target;

  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
}

TEST(TestDodgeEnvValidateSnapshotInsufficientFloor) {
  DodgeEnv env(7, 3, 5, 50, 42, 0);  // Requires 3 companions

  // Create snapshot with only 2 floor cells
  Snapshot snap;
  snap.rows = 7;
  snap.cols = 7;
  snap.cells.resize(49);
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Wall;  // All walls
  }
  // Only 2 floor cells
  snap.cells[8].kind = CellKind::Floor;
  snap.cells[9].kind = CellKind::Floor;

  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
}

TEST(TestLoadSnapshotDimensionMismatch) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);

  // Create snapshot with different dimensions
  Snapshot snap;
  snap.rows = 10;  // Mismatch: env is 8x8
  snap.cols = 10;
  snap.cells.resize(100);
  for (auto& cell : snap.cells) {
    cell.kind = CellKind::Floor;
  }

  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
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

  std::cout << "Running " << tests.size() << " tests...\n\n";

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "Running " << test.name << "... ";
    std::cout.flush();
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "FAILED: " << e.what() << "\n";
      failed++;
    } catch (...) {
      std::cout << "FAILED (unknown exception)\n";
      failed++;
    }
  }

  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";

  return (failed > 0) ? 1 : 0;
}
