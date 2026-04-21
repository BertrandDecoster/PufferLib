// Copyright 2024
// Test suite for Snapshot system

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
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
  ASSERT_EQ(snap.CountCells(CellKind::HealArea), 0);

  // Add some walls
  snap.cells[0].kind = CellKind::Wall;
  snap.cells[1].kind = CellKind::Wall;
  ASSERT_EQ(snap.CountCells(CellKind::Floor), 7);
  ASSERT_EQ(snap.CountCells(CellKind::Wall), 2);

  // Add heal-area cells (task-semantic roles like Synchro live on the
  // annotation layer now, not on CellKind).
  snap.cells[4].kind = CellKind::HealArea;
  snap.cells[5].kind = CellKind::HealArea;
  snap.cells[6].kind = CellKind::HealArea;
  ASSERT_EQ(snap.CountCells(CellKind::HealArea), 3);
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

  // HasSynchroCells reads the annotation layer (semantic), not the physical
  // CellKind. Adding a SynchroGoal annotation is the way to mark a goal cell.
  AnnotationSnapshot a;
  a.target_type = 0;
  a.pos = Position{1, 1};
  a.tag = SemanticTag::SynchroGoal;
  snap.annotations.push_back(a);
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

  AnnotationSnapshot a;
  a.target_type = 0;
  a.pos = Position{1, 1};
  a.tag = SemanticTag::AggroTarget;
  snap.annotations.push_back(a);
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
  original.cells[5].kind = CellKind::HealArea;  // Position (1,1)

  std::vector<uint8_t> data = original.Serialize();
  Snapshot restored = Snapshot::Deserialize(data);

  ASSERT_EQ(restored.rows, 4);
  ASSERT_EQ(restored.cols, 4);
  ASSERT_EQ(restored.cells.size(), 16u);

  // Verify cell pattern
  ASSERT_EQ(restored.cells[0].kind, CellKind::Wall);
  ASSERT_EQ(restored.cells[5].kind, CellKind::HealArea);
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
  // SynchroEnv expects at least 3 SynchroGoal annotations. Provide only 1
  // so ValidateSnapshot throws.
  AnnotationSnapshot a;
  a.target_type = 0;
  a.pos = Position{1, 1};
  a.tag = SemanticTag::SynchroGoal;
  snap.annotations.push_back(a);

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
  // Has target (via annotation) but no patrol path.
  AnnotationSnapshot a;
  a.target_type = 0;
  a.pos = Position{5, 5};
  a.tag = SemanticTag::AggroTarget;
  snap.annotations.push_back(a);

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
// v1 → v2 Snapshot Migration
// =============================================================================
//
// Old CellKind enum (v1):  Floor=0 Wall=1 Hazard=2 Synchro=3 HealArea=4 Target=5
// New CellKind enum (v2):  Floor=0 Wall=1 Hazard=2 HealArea=3
//
// Loading a v1 snapshot must remap:
//   - int 3 (old Synchro) → Floor cell + SynchroGoal annotation at that pos
//   - int 4 (old HealArea) → HealArea cell (shifted down to new value 3)
//   - int 5 (old Target)   → Floor cell + AggroTarget annotation at that pos
// All migrated annotations are persistent (owner_lens_id == -1).

namespace {

template <typename T>
void AppendBytes(std::vector<uint8_t>& buf, T value) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
  buf.insert(buf.end(), p, p + sizeof(T));
}

// Build a minimal v1 snapshot buffer (no agents, no effects, no patrol path)
// with `rows*cols` cells taken from `kinds_v1` using the *old* CellKind
// numbering. origin is written as 0 (Default) for every cell.
std::vector<uint8_t> BuildV1Snapshot(int rows, int cols,
                                     const std::vector<int>& kinds_v1) {
  std::vector<uint8_t> buf;
  AppendBytes<uint32_t>(buf, 0x534E4150);     // magic "SNAP"
  AppendBytes<uint32_t>(buf, 1);              // version 1
  AppendBytes<int>(buf, rows);
  AppendBytes<int>(buf, cols);
  AppendBytes<uint32_t>(buf, static_cast<uint32_t>(kinds_v1.size()));
  for (int kind : kinds_v1) {
    AppendBytes<int>(buf, kind);
    AppendBytes<int>(buf, 0);                 // origin = Default
  }
  AppendBytes<uint32_t>(buf, 0);              // num_agents
  AppendBytes<uint32_t>(buf, 0);              // num_effects
  AppendBytes<int>(buf, 0);                   // tick
  AppendBytes<int>(buf, 100);                 // horizon
  AppendBytes<uint64_t>(buf, 0);              // rng_state
  AppendBytes<uint64_t>(buf, 0);              // rng_inc
  AppendBytes<int>(buf, 0);                   // d4_transform
  AppendBytes<uint32_t>(buf, 0);              // patrol_path size
  // v1: no annotations block.
  return buf;
}

}  // namespace

TEST(TestSnapshotV1MigrationRemapsCellKinds) {
  // 3x3 grid exercising every remappable kind:
  //   (0,0)=0 Floor    (0,1)=1 Wall    (0,2)=3 Synchro
  //   (1,0)=4 HealArea (1,1)=2 Hazard  (1,2)=5 Target
  //   (2,0)=5 Target   (2,1)=0 Floor   (2,2)=3 Synchro
  std::vector<int> kinds_v1 = {0, 1, 3, 4, 2, 5, 5, 0, 3};
  std::vector<uint8_t> buf = BuildV1Snapshot(3, 3, kinds_v1);

  Snapshot snap = Snapshot::Deserialize(buf);

  ASSERT_EQ(snap.rows, 3);
  ASSERT_EQ(snap.cols, 3);
  ASSERT_EQ(snap.cells.size(), 9u);
  // Unchanged kinds.
  ASSERT_EQ(snap.cells[0].kind, CellKind::Floor);
  ASSERT_EQ(snap.cells[1].kind, CellKind::Wall);
  ASSERT_EQ(snap.cells[4].kind, CellKind::Hazard);
  ASSERT_EQ(snap.cells[7].kind, CellKind::Floor);
  // Old Synchro → Floor (annotation emitted separately).
  ASSERT_EQ(snap.cells[2].kind, CellKind::Floor);
  ASSERT_EQ(snap.cells[8].kind, CellKind::Floor);
  // Old HealArea (int 4) → new HealArea (int 3).
  ASSERT_EQ(snap.cells[3].kind, CellKind::HealArea);
  // Old Target → Floor.
  ASSERT_EQ(snap.cells[5].kind, CellKind::Floor);
  ASSERT_EQ(snap.cells[6].kind, CellKind::Floor);
}

TEST(TestSnapshotV1MigrationEmitsPersistentAnnotations) {
  // Same layout as above; verify emitted annotations.
  std::vector<int> kinds_v1 = {0, 1, 3, 4, 2, 5, 5, 0, 3};
  std::vector<uint8_t> buf = BuildV1Snapshot(3, 3, kinds_v1);

  Snapshot snap = Snapshot::Deserialize(buf);

  ASSERT_EQ(snap.annotations.size(), 4u);  // 2 Synchro + 2 Target

  int synchro_count = 0, aggro_count = 0;
  bool saw_synchro_0_2 = false, saw_synchro_2_2 = false;
  bool saw_target_1_2 = false, saw_target_2_0 = false;
  for (const AnnotationSnapshot& a : snap.annotations) {
    ASSERT_EQ(a.target_type, (uint8_t)0);       // Cell
    ASSERT_EQ(a.owner_lens_id, -1);             // Persistent
    ASSERT_EQ(a.agent_id, kInvalidObjectId);
    if (a.tag == SemanticTag::SynchroGoal) {
      synchro_count++;
      if (a.pos == Position{0, 2}) saw_synchro_0_2 = true;
      if (a.pos == Position{2, 2}) saw_synchro_2_2 = true;
    } else if (a.tag == SemanticTag::AggroTarget) {
      aggro_count++;
      if (a.pos == Position{1, 2}) saw_target_1_2 = true;
      if (a.pos == Position{2, 0}) saw_target_2_0 = true;
    }
  }
  ASSERT_EQ(synchro_count, 2);
  ASSERT_EQ(aggro_count, 2);
  ASSERT_TRUE(saw_synchro_0_2);
  ASSERT_TRUE(saw_synchro_2_2);
  ASSERT_TRUE(saw_target_1_2);
  ASSERT_TRUE(saw_target_2_0);
}

TEST(TestSnapshotV1MigrationRoundTripsAsV2) {
  // A migrated v1 snapshot should re-serialize as v2 (with the annotation
  // block) and round-trip byte-for-byte identically from that point on.
  std::vector<int> kinds_v1 = {0, 3, 5, 2};  // 2x2 grid, minimal case
  std::vector<uint8_t> buf_v1 = BuildV1Snapshot(2, 2, kinds_v1);
  Snapshot migrated = Snapshot::Deserialize(buf_v1);

  std::vector<uint8_t> buf_v2 = migrated.Serialize();
  // Serialize always writes version 2 now.
  uint32_t magic = 0, version = 0;
  std::memcpy(&magic, buf_v2.data(), sizeof(magic));
  std::memcpy(&version, buf_v2.data() + sizeof(magic), sizeof(version));
  ASSERT_EQ(magic, (uint32_t)0x534E4150);
  ASSERT_EQ(version, (uint32_t)2);

  Snapshot round = Snapshot::Deserialize(buf_v2);
  ASSERT_EQ(round.cells.size(), migrated.cells.size());
  ASSERT_EQ(round.annotations.size(), migrated.annotations.size());
  for (std::size_t i = 0; i < round.cells.size(); ++i) {
    ASSERT_EQ(round.cells[i].kind, migrated.cells[i].kind);
  }
  // One SynchroGoal (from old 3 at index 1 → pos (0,1)) and one AggroTarget
  // (from old 5 at index 2 → pos (1,0)).
  ASSERT_EQ(round.annotations.size(), 2u);
  bool synchro_ok = false, aggro_ok = false;
  for (const AnnotationSnapshot& a : round.annotations) {
    if (a.tag == SemanticTag::SynchroGoal && a.pos == Position{0, 1}) synchro_ok = true;
    if (a.tag == SemanticTag::AggroTarget && a.pos == Position{1, 0}) aggro_ok = true;
  }
  ASSERT_TRUE(synchro_ok);
  ASSERT_TRUE(aggro_ok);
}

TEST(TestSnapshotV1MigrationRejectsUnknownCellKind) {
  // An int outside 0..5 was never a legal v1 CellKind; migration should throw.
  std::vector<int> kinds_v1 = {0, 99, 0, 0};
  std::vector<uint8_t> buf = BuildV1Snapshot(2, 2, kinds_v1);
  ASSERT_THROW(Snapshot::Deserialize(buf), std::runtime_error);
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
