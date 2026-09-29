// Copyright 2024
// Test suite for Snapshot system

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/d4_transform.h"
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
  // Serialize always writes the current version (7: v6 + zone table and
  // cell fields, reactions, tag statuses, weaknesses and immunities).
  uint32_t magic = 0, version = 0;
  std::memcpy(&magic, buf_v2.data(), sizeof(magic));
  std::memcpy(&version, buf_v2.data() + sizeof(magic), sizeof(version));
  ASSERT_EQ(magic, (uint32_t)0x534E4150);
  ASSERT_EQ(version, (uint32_t)7);

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
// Snapshot v4: skills, agent tags / slots / cooldowns, zone tags
// =============================================================================

namespace {

Snapshot BinaryRoundTrip(const Snapshot& s) { return Snapshot::Deserialize(s.Serialize()); }

Agent* FirstAgent(BaseEnv& env) { return env.GetMutableObjectManager().GetAllAgents()[0]; }

int CountZones(const BaseEnv& env) {
  int n = 0;
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (env.GetCellTag({r, c}).tag != kInvalidTag) ++n;
    }
  }
  return n;
}

void AssertSkillEq(const SkillConfig& a, const SkillConfig& b) {
  ASSERT_EQ(a.name, b.name);
  ASSERT_TRUE(a.targeting == b.targeting);
  ASSERT_EQ(a.range, b.range);
  ASSERT_TRUE(a.filter == b.filter);
  ASSERT_TRUE(a.area == b.area);
  ASSERT_TRUE(a.motion == b.motion);
  ASSERT_EQ(a.motion_distance, b.motion_distance);
  ASSERT_EQ(a.tag_path, b.tag_path);
  ASSERT_EQ(a.tags.size(), b.tags.size());
  for (size_t i = 0; i < a.tags.size(); ++i) {
    ASSERT_EQ(a.tags[i].tag, b.tags[i].tag);
    ASSERT_EQ(a.tags[i].duration, b.tags[i].duration);
  }
  ASSERT_EQ(a.root_steps, b.root_steps);
  ASSERT_EQ(a.cooldown, b.cooldown);
  ASSERT_EQ(a.friendly_fire, b.friendly_fire);
  ASSERT_EQ(a.self_tags, b.self_tags);
  ASSERT_EQ(a.self_motion, b.self_motion);
  ASSERT_EQ(a.self_root, b.self_root);
  ASSERT_EQ(a.damage, b.damage);
  ASSERT_EQ(a.self_damage, b.self_damage);
  ASSERT_EQ(a.affects_downed, b.affects_downed);
  ASSERT_EQ(a.revive_percent, b.revive_percent);
}

// One skill per value of every enum, and every scalar off its default.
std::vector<SkillConfig> EverySkillShape() {
  std::vector<SkillConfig> out;
  const SkillTargeting targetings[] = {SkillTargeting::Self, SkillTargeting::Ground,
                                       SkillTargeting::Projectile};
  const TargetFilter filters[] = {TargetFilter::All, TargetFilter::Companion,
                                  TargetFilter::Enemy, TargetFilter::Neutral};
  const SkillArea areas[] = {SkillArea::Single, SkillArea::Cross};
  const SkillMotion motions[] = {SkillMotion::None, SkillMotion::Dash, SkillMotion::Teleport,
                                 SkillMotion::PushOut, SkillMotion::PullIn};
  int i = 0;
  for (SkillMotion m : motions) {
    for (SkillArea ar : areas) {
      SkillConfig s;
      s.name = "shape" + std::to_string(i);
      s.targeting = targetings[i % 3];
      s.filter = filters[i % 4];
      s.area = ar;
      s.motion = m;
      s.range = 2 + i;
      s.motion_distance = 1 + i;
      s.tag_path = (i % 2) == 0;
      s.tags = {{"t" + std::to_string(i), 1 + i}, {"perm", kPermanentTag}};
      s.root_steps = i;
      s.cooldown = 3 + i;
      // Each flag both ways across the shapes, in different combinations.
      s.friendly_fire = (i % 2) == 1;
      s.self_tags = (i % 3) != 0;
      s.self_motion = (i % 4) != 1;
      s.self_root = (i % 5) != 2;
      s.damage = i;
      s.self_damage = (i % 6) != 4;
      out.push_back(s);
      ++i;
    }
  }
  // Skills that affect the downed (they can only revive: no tags, damage,
  // root or motion), reviving or not.
  SkillConfig mend;
  mend.name = "mend";
  mend.range = 2;
  mend.filter = TargetFilter::Companion;
  mend.affects_downed = true;
  mend.revive_percent = 75;
  out.push_back(mend);
  mend.name = "touch";
  mend.revive_percent = 0;
  out.push_back(mend);
  return out;
}

// The v7 block of a snapshot without v7 level data: three empty counts (zone
// table, reactions, tag statuses), last in the buffer.
constexpr size_t kV7Tail = 4 + 4 + 4;

// 2x2 snapshot, one companion, nothing v4-specific in it.
Snapshot MinimalSnapshot() {
  Snapshot s;
  s.rows = 2;
  s.cols = 2;
  s.cells.resize(4);
  AgentSnapshot a;
  a.id = 0;
  a.type = static_cast<int>(ObjectType::Companion);
  a.position = {1, 1};
  s.agents.push_back(a);
  return s;
}

}  // namespace

TEST(TestBinaryRoundTripSkillsTagsZones) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  SkillConfig frost;
  frost.name = "frost";
  frost.targeting = SkillTargeting::Projectile;
  frost.range = 2;
  frost.filter = TargetFilter::Enemy;
  frost.tags = {{"chilled", kPermanentTag}};
  env.GetMutableSkillBook().Define(frost);
  SkillConfig far_fireball = *env.GetSkillBook().Find("fireball");
  far_fireball.range = 5;  // a level retunes a builtin
  env.GetMutableSkillBook().Define(far_fireball);
  Agent* a = FirstAgent(env);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "frost"));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 1, "vortex"));
  dynamic_cast<Companion*>(a)->SetCooldown(1, 2);
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "burning", 3));
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "blessed", kPermanentTag));
  a->ApplyStatus(StatusType::Rooted, 2);
  ASSERT_TRUE(env.SetCellTag({2, 2}, "wet", kPermanentTag));
  ASSERT_TRUE(env.SetCellTag({5, 6}, "oil", 2));

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.Reset();
  other.GetMutableTagTable().Intern("unrelated");  // Ids differ between envs: names travel
  other.LoadSnapshot(BinaryRoundTrip(env.SaveSnapshot()));

  const SkillConfig* f = other.GetSkillBook().Find("frost");
  ASSERT_TRUE(f != nullptr);
  AssertSkillEq(*f, frost);
  ASSERT_EQ(other.GetSkillBook().Find("fireball")->range, 5);
  ASSERT_EQ(other.GetSkillBook().All().size(), env.GetSkillBook().All().size());
  auto* c = dynamic_cast<Companion*>(FirstAgent(other));
  ASSERT_EQ(c->GetSkill(0), std::string("frost"));
  ASSERT_EQ(c->GetSkill(1), std::string("vortex"));
  ASSERT_EQ(c->GetCooldown(0), 0);
  ASSERT_EQ(c->GetCooldown(1), 2);
  ASSERT_EQ(c->GetTags().size(), 2u);
  ASSERT_EQ(c->GetTags()[0].id, other.GetTagTable().Find("burning"));
  ASSERT_EQ(c->GetTags()[0].duration, 3);
  ASSERT_EQ(c->GetTags()[1].id, other.GetTagTable().Find("blessed"));
  ASSERT_EQ(c->GetTags()[1].duration, kPermanentTag);
  ASSERT_TRUE(c->IsRooted());
  ASSERT_EQ(other.GetCellTag({2, 2}).tag, other.GetTagTable().Find("wet"));
  ASSERT_EQ(other.GetCellTag({2, 2}).duration, kPermanentTag);
  ASSERT_EQ(other.GetCellTag({5, 6}).tag, other.GetTagTable().Find("oil"));
  ASSERT_EQ(other.GetCellTag({5, 6}).duration, 2);
  ASSERT_EQ(CountZones(other), 2);
}

TEST(TestBinaryEverySkillConfigFieldRoundTrips) {
  Snapshot s = MinimalSnapshot();
  s.skills = EverySkillShape();
  s.agents[0].tags = {{"burning", 3}, {"blessed", kPermanentTag}};
  s.agents[0].skills = {"shape1", ""};
  s.agents[0].cooldowns = {4, 0};
  s.cell_tags = {{Position{0, 1}, "wet", 5}};
  Snapshot back = BinaryRoundTrip(s);
  ASSERT_EQ(back.skills.size(), s.skills.size());
  for (size_t i = 0; i < s.skills.size(); ++i) AssertSkillEq(s.skills[i], back.skills[i]);
  const AgentSnapshot& a = back.agents[0];
  ASSERT_EQ(a.tags.size(), 2u);
  ASSERT_EQ(a.tags[0].tag, std::string("burning"));
  ASSERT_EQ(a.tags[0].duration, 3);
  ASSERT_EQ(a.tags[1].duration, kPermanentTag);
  ASSERT_TRUE(a.skills == s.agents[0].skills);
  ASSERT_TRUE(a.cooldowns == s.agents[0].cooldowns);
  ASSERT_EQ(back.cell_tags.size(), 1u);
  ASSERT_TRUE(back.cell_tags[0].cell == (Position{0, 1}));
  ASSERT_EQ(back.cell_tags[0].tag, std::string("wet"));
  ASSERT_EQ(back.cell_tags[0].duration, 5);
}

// The skill record: name (length + bytes), targeting, range, filter, area,
// motion, motion_distance (ints), tag_path (bool), tag count, the tags,
// root_steps, cooldown, damage (ints), friendly_fire, self_tags, self_motion,
// self_root, self_damage (bools); v6 appends affects_downed (a byte) and
// revive_percent (int). Without tags: 55 bytes plus the name.
TEST(TestBinarySkillRecordLayout) {
  Snapshot s = MinimalSnapshot();
  const size_t without = s.Serialize().size();
  SkillConfig frost;
  frost.name = "frost";
  frost.damage = 7;
  frost.friendly_fire = false;
  frost.self_root = false;
  frost.self_damage = false;
  s.skills.push_back(frost);
  std::vector<uint8_t> bytes = s.Serialize();
  ASSERT_EQ(bytes.size(), without + 55 + frost.name.size());
  // affects_downed and revive_percent close the record, just before the zone
  // count, max_downs (v5), the context skills flag (v6, absent here) and the
  // empty v7 block; the five flags and the damage come right before them.
  const size_t revive_at = bytes.size() - kV7Tail - 1 - 4 - 4 - 4;
  const size_t affects_at = revive_at - 1;
  const size_t flags_at = affects_at - 5;
  int damage = 0;
  std::memcpy(&damage, bytes.data() + flags_at - sizeof(int), sizeof(int));
  ASSERT_EQ(damage, 7);
  ASSERT_EQ(bytes[flags_at + 0], 0);  // friendly_fire
  ASSERT_EQ(bytes[flags_at + 1], 1);  // self_tags
  ASSERT_EQ(bytes[flags_at + 2], 1);  // self_motion
  ASSERT_EQ(bytes[flags_at + 3], 0);  // self_root
  ASSERT_EQ(bytes[flags_at + 4], 0);  // self_damage
  ASSERT_EQ(bytes[affects_at], 0);    // affects_downed
  AssertSkillEq(Snapshot::Deserialize(bytes).skills[0], frost);

  SkillConfig mend;  // The v6 fields set; a name as long as "frost"
  mend.name = "mends";
  mend.affects_downed = true;
  mend.revive_percent = 75;
  s.skills = {mend};
  bytes = s.Serialize();
  ASSERT_EQ(bytes.size(), without + 55 + mend.name.size());
  int percent = 0;
  std::memcpy(&percent, bytes.data() + revive_at, sizeof(int));
  ASSERT_EQ(percent, 75);
  ASSERT_EQ(bytes[affects_at], 1);
  AssertSkillEq(Snapshot::Deserialize(bytes).skills[0], mend);
}

namespace {

// After MinimalSnapshot's agent: effects count, tick, horizon, rng x2, d4,
// patrol count, annotations count, skills count, cell tags count, max_downs.
constexpr size_t kV5Tail = 4 + 4 + 4 + 8 + 8 + 4 + 4 + 4 + 4 + 4 + 4;

// The v7 bytes of `s`, without zones nor any v7 data, as version 6: without
// each agent's two empty v7 counts (weak_to, immune), closing its record, nor
// the trailing v7 block.
std::vector<uint8_t> AsV6(const Snapshot& s) {
  if (!s.cell_tags.empty() || !s.zones.empty() || !s.reactions.empty() ||
      !s.tag_statuses.empty()) {
    throw std::runtime_error("AsV6: a snapshot with zones or v7 data");
  }
  const std::vector<uint8_t> v7 = s.Serialize();
  // magic, version, rows, cols, cell count, the cells, agent count
  const size_t header = 4 + 4 + 4 + 4 + 4 + 8 * s.cells.size() + 4;
  Snapshot prefix = s;
  prefix.agents.clear();
  const size_t tail = prefix.Serialize().size() - header;  // After the agents
  std::vector<uint8_t> v6(v7.begin(), v7.begin() + static_cast<std::ptrdiff_t>(header));
  size_t start = header;
  for (size_t k = 0; k < s.agents.size(); ++k) {
    if (!s.agents[k].weak_to.empty() || !s.agents[k].immune.empty()) {
      throw std::runtime_error("AsV6: an agent with v7 data");
    }
    prefix.agents.push_back(s.agents[k]);
    const size_t end = prefix.Serialize().size() - tail;  // Of agent k's record
    v6.insert(v6.end(), v7.begin() + static_cast<std::ptrdiff_t>(start),
              v7.begin() + static_cast<std::ptrdiff_t>(end - 8));
    start = end;
  }
  v6.insert(v6.end(), v7.begin() + static_cast<std::ptrdiff_t>(start),
            v7.end() - static_cast<std::ptrdiff_t>(kV7Tail));
  uint32_t six = 6;
  std::memcpy(v6.data() + 4, &six, sizeof(six));
  return v6;
}

// A v6 buffer of a snapshot without skills nor context skills (nullopt) as
// version 5: without the trailing context skills flag.
std::vector<uint8_t> AsV5(const std::vector<uint8_t>& v6) {
  std::vector<uint8_t> v5(v6.begin(), v6.end() - 1);
  uint32_t five = 5;
  std::memcpy(v5.data() + 4, &five, sizeof(five));
  return v5;
}

// A v5 buffer of a one-agent snapshot as version 4: without the agent's
// downed (bool) and times_downed (int), closing its record, nor the trailing
// max_downs.
std::vector<uint8_t> AsV4(const std::vector<uint8_t>& v5) {
  std::vector<uint8_t> v4(v5.begin(), v5.end() - kV5Tail - 5);
  v4.insert(v4.end(), v5.end() - kV5Tail, v5.end() - 4);
  uint32_t four = 4;
  std::memcpy(v4.data() + 4, &four, sizeof(four));
  return v4;
}

}  // namespace

TEST(TestBinaryDownsRoundTrip) {
  Snapshot s = MinimalSnapshot();
  s.max_downs = 7;
  s.agents[0].health = 0;
  s.agents[0].downed = true;
  s.agents[0].times_downed = 2;
  Snapshot back = BinaryRoundTrip(s);
  ASSERT_EQ(back.max_downs, 7);
  ASSERT_TRUE(back.agents[0].downed);
  ASSERT_EQ(back.agents[0].times_downed, 2);
  // The v5 record: downed (1 byte) and times_downed close the agent's record
  // (but the v7 fields), max_downs comes last but the v6 context skills flag
  // and the v7 block.
  std::vector<uint8_t> v5 = AsV5(AsV6(s));
  ASSERT_EQ(v5.size(), AsV4(v5).size() + 1 + 4 + 4);
  std::vector<uint8_t> bytes = s.Serialize();
  int max_downs = 0;
  std::memcpy(&max_downs, bytes.data() + bytes.size() - kV7Tail - 1 - 4, sizeof(int));
  ASSERT_EQ(max_downs, 7);
  // Truncated before max_downs: an underflow, not a default
  bytes.resize(bytes.size() - kV7Tail - 1 - 4);
  ASSERT_THROW(Snapshot::Deserialize(bytes), std::runtime_error);
}

TEST(TestBinaryV4SnapshotLoadsWithoutDowns) {
  Snapshot s = MinimalSnapshot();
  s.max_downs = 7;  // Dropped with the v5 fields: defaults on load
  s.agents[0].health = 0;
  s.agents[0].downed = true;
  s.agents[0].times_downed = 2;
  Snapshot back = Snapshot::Deserialize(AsV4(AsV5(AsV6(s))));
  ASSERT_EQ(back.max_downs, 3);
  ASSERT_FALSE(back.agents[0].downed);
  ASSERT_EQ(back.agents[0].times_downed, 0);
  ASSERT_EQ(back.agents[0].health, 0);
  ASSERT_EQ(back.horizon, 100);
}

TEST(TestBinaryV3SnapshotStillLoads) {
  // Everything v4 adds is empty here, so a v3 buffer is the v4 one minus the
  // agent's three empty counts (tags, skills, cooldowns) and the two trailing
  // empty counts (skills, cell tags), with version 3.
  std::vector<uint8_t> v4 = AsV4(AsV5(AsV6(MinimalSnapshot())));
  // After the agent: effects count, tick, horizon, rng x2, d4, patrol count,
  // annotations count, skills count, cell tags count.
  const size_t tail = 4 + 4 + 4 + 8 + 8 + 4 + 4 + 4 + 4 + 4;
  std::vector<uint8_t> v3(v4.begin(), v4.end() - tail - 12);
  v3.insert(v3.end(), v4.end() - tail, v4.end() - 8);
  uint32_t three = 3;
  std::memcpy(v3.data() + 4, &three, sizeof(three));

  Snapshot s = Snapshot::Deserialize(v3);
  ASSERT_EQ(s.agents.size(), 1u);
  ASSERT_TRUE(s.agents[0].position == (Position{1, 1}));
  ASSERT_TRUE(s.agents[0].tags.empty());
  ASSERT_TRUE(s.agents[0].skills.empty());
  ASSERT_TRUE(s.agents[0].cooldowns.empty());
  ASSERT_TRUE(s.skills.empty());
  ASSERT_TRUE(s.cell_tags.empty());
  ASSERT_EQ(s.horizon, 100);
  // And the v4 buffer itself is exactly that plus the empty v4 counts.
  ASSERT_EQ(v4.size(), v3.size() + 20);
}

TEST(TestLoadSnapshotRejectsInvalidSkillsTagsZones) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  const Snapshot good = env.SaveSnapshot();
  env.LoadSnapshot(good);  // Sanity

  std::vector<Snapshot> bad;
  for (int d : {0, -2}) {
    Snapshot s = good;
    s.agents[0].tags = {{"burning", d}};
    bad.push_back(s);
    s = good;
    s.cell_tags = {{Position{2, 2}, "wet", d}};
    bad.push_back(s);
    s = good;
    s.skills[0].tags = {{"burning", d}};
    bad.push_back(s);
  }
  Snapshot s = good;
  s.skills[0].name = "";
  bad.push_back(s);
  s = good;
  s.skills[0].tags = {{"", 2}};
  bad.push_back(s);
  s = good;
  s.agents[0].tags = {{"", 2}};
  bad.push_back(s);
  s = good;
  s.cell_tags = {{Position{2, 2}, "", 2}};
  bad.push_back(s);
  s = good;
  s.cell_tags = {{Position{8, 0}, "wet", 2}};  // Out of the grid
  bad.push_back(s);
  s = good;
  s.agents[0].skills = {"", "", ""};  // More than kMaxSkillSlots
  bad.push_back(s);
  s = good;
  s.agents[0].cooldowns = {-1, 0};
  bad.push_back(s);
  s = good;
  s.skills[0].targeting = static_cast<SkillTargeting>(9);
  bad.push_back(s);
  s = good;
  s.skills[0].filter = static_cast<TargetFilter>(9);
  bad.push_back(s);

  for (const Snapshot& b : bad) {
    ASSERT_THROW(env.LoadSnapshot(b), std::runtime_error);
    ASSERT_THROW(Snapshot::Deserialize(b.Serialize()), std::runtime_error);
  }
}

namespace {

// `fn` must throw a std::runtime_error whose message contains `needle`.
template <typename F>
void AssertThrowsMentioning(F fn, const std::string& needle) {
  std::string what;
  bool caught = false;
  try {
    fn();
  } catch (const std::runtime_error& e) {
    caught = true;
    what = e.what();
  }
  if (!caught) throw std::runtime_error("expected a std::runtime_error mentioning " + needle);
  if (what.find(needle) == std::string::npos) {
    throw std::runtime_error("error \"" + what + "\" does not mention \"" + needle + "\"");
  }
}

// LoadSnapshot and a binary round trip must both reject `s` with `needle`.
void AssertSnapshotRejected(const Snapshot& s, const std::string& needle) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  AssertThrowsMentioning([&] { env.LoadSnapshot(s); }, needle);
  std::vector<uint8_t> bytes = s.Serialize();
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bytes); }, needle);
}

}  // namespace

TEST(TestSnapshotRejectsNegativeSkillNumbers) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  SkillConfig frost;
  frost.name = "frost";
  for (int field = 0; field < 5; ++field) {
    Snapshot s = good;
    SkillConfig bad = frost;
    const char* name = "";
    switch (field) {
      case 0: bad.cooldown = -1; name = "cooldown"; break;
      case 1: bad.range = -1; name = "range"; break;
      case 2: bad.motion_distance = -1; name = "motion_distance"; break;
      case 3: bad.root_steps = -1; name = "root_steps"; break;
      case 4: bad.damage = -1; name = "damage"; break;
    }
    s.skills.push_back(bad);
    AssertSnapshotRejected(s, "skill 'frost': " + std::string(name) + " must be >= 0 (got -1)");
  }
}

// The default attack is fixed: a snapshot cannot carry (so cannot redefine) it.
TEST(TestSnapshotRejectsTheAttackSkill) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  Snapshot s = good;
  SkillConfig attack = *SkillBook().Find(kDefaultSkill);
  s.skills.push_back(attack);
  AssertSnapshotRejected(s, "skills[" + std::to_string(good.skills.size()) + "]");
  AssertSnapshotRejected(s, "'attack' is the fixed default skill");
}

// SaveSnapshot writes every skill of the book but the fixed attack; slots
// holding it keep it.
TEST(TestSaveSnapshotLeavesTheAttackOut) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  Agent* a = FirstAgent(env);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 1, "vortex"));
  Snapshot s = env.SaveSnapshot();
  ASSERT_EQ(s.skills.size(), env.GetSkillBook().All().size() - 1);
  for (const SkillConfig& skill : s.skills) ASSERT_TRUE(skill.name != kDefaultSkill);
  ASSERT_TRUE(s.agents[0].skills == (std::vector<std::string>{kDefaultSkill, "vortex"}));

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.LoadSnapshot(BinaryRoundTrip(s));
  auto* c = dynamic_cast<Companion*>(FirstAgent(other));
  ASSERT_EQ(c->GetSkill(0), std::string(kDefaultSkill));
  ASSERT_EQ(c->GetSkill(1), std::string("vortex"));
  ASSERT_TRUE(other.GetSkillBook().Find(kDefaultSkill) != nullptr);
}

// Older files wrote "" for an empty slot: it loads as the attack, and so do
// missing slots.
TEST(TestEmptyOrMissingSlotsLoadAsTheAttack) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  Snapshot s = env.SaveSnapshot();
  s.agents[0].skills = {"", ""};
  env.LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
  auto* c = dynamic_cast<Companion*>(FirstAgent(env));
  ASSERT_EQ(c->GetSkill(0), std::string(kDefaultSkill));
  ASSERT_EQ(c->GetSkill(1), std::string(kDefaultSkill));
  s.agents[0].skills = {"teleport"};
  env.LoadSnapshot(s);
  c = dynamic_cast<Companion*>(FirstAgent(env));
  ASSERT_EQ(c->GetSkill(0), std::string("teleport"));
  ASSERT_EQ(c->GetSkill(1), std::string(kDefaultSkill));
}

TEST(TestValidateSkillConfig) {
  SkillConfig s;
  s.name = "frost";
  ValidateSkillConfig(s);  // Defaults are valid
  for (const SkillConfig& b : EverySkillShape()) ValidateSkillConfig(b);

  SkillConfig bad = s;
  bad.name = "";
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "name");
  bad = s;
  bad.tags = {{"", 2}};
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "skill 'frost': tags[0]");
  bad = s;
  bad.tags = {{"chilled", 3}, {"burning", 0}};
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "skill 'frost': tags[1] ('burning')");
  bad = s;
  bad.area = static_cast<SkillArea>(7);
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "skill 'frost': area");
  bad = s;
  bad.motion = static_cast<SkillMotion>(-1);
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "skill 'frost': motion");

  // Names (the skill's and its tags') fit in kMaxNameLength bytes.
  const std::string max_name(kMaxNameLength, 'm');
  const std::string too_long(kMaxNameLength + 1, 'x');
  bad = s;
  bad.name = max_name;
  bad.tags = {{max_name, 2}};
  ValidateSkillConfig(bad);
  bad = s;
  bad.name = too_long;
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "name is 32 bytes, at most 31");
  bad = s;
  bad.tags = {{"chilled", 3}, {too_long, 2}};
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); },
                         "skill 'frost': tags[1] ('" + too_long + "'): name is 32 bytes, at most 31");
}

// Tag and slot names longer than kMaxNameLength are refused where they enter
// the env (the C API hands them out in fixed-size buffers).
TEST(TestSnapshotRejectsOverlongNames) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string agent0 = "agent #0 (id " + std::to_string(good.agents[0].id) + ")";
  const std::string max_name(kMaxNameLength, 'm');
  const std::string too_long(kMaxNameLength + 1, 'x');

  Snapshot s = good;
  s.agents[0].tags = {{too_long, 2}};
  AssertSnapshotRejected(s, agent0);
  AssertSnapshotRejected(s, "at most 31");
  s = good;
  s.agents[0].skills = {"", too_long};
  AssertSnapshotRejected(s, agent0);
  AssertSnapshotRejected(s, "skills[1]");
  AssertSnapshotRejected(s, "at most 31");
  s = good;
  s.cell_tags = {{Position{2, 3}, too_long, 2}};
  AssertSnapshotRejected(s, "(2, 3)");
  AssertSnapshotRejected(s, "at most 31");
  s = good;
  SkillConfig frost;
  frost.name = too_long;
  s.skills.push_back(frost);
  AssertSnapshotRejected(s, "skills[" + std::to_string(good.skills.size()) + "]");
  AssertSnapshotRejected(s, "at most 31");

  s = good;  // The longest names load
  s.agents[0].tags = {{max_name, 2}};
  s.agents[0].skills = {max_name, ""};
  s.cell_tags = {{Position{2, 3}, max_name, 2}};
  frost.name = max_name;
  s.skills.push_back(frost);
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
  ASSERT_EQ(env.GetTagTable().Find(max_name), env.GetCellTag({2, 3}).tag);
}

TEST(TestSnapshotValidationErrorsSayWhere) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string agent0 = "agent #0 (id " + std::to_string(good.agents[0].id) + ")";

  Snapshot s = good;
  s.cell_tags = {{Position{8, 3}, "wet", 2}};
  AssertSnapshotRejected(s, "(8, 3)");
  s = good;
  s.cell_tags = {{Position{2, 3}, "wet", 0}};
  AssertSnapshotRejected(s, "(2, 3)");

  s = good;
  s.agents[0].skills = {"", "", ""};
  AssertSnapshotRejected(s, agent0);
  AssertSnapshotRejected(s, "at most " + std::to_string(kMaxSkillSlots));
  s = good;
  s.agents[0].cooldowns = {0, 0, 0};
  AssertSnapshotRejected(s, agent0);
  s = good;
  s.agents[0].cooldowns = {0, -1};
  AssertSnapshotRejected(s, agent0);
  AssertSnapshotRejected(s, "(got -1)");
  s = good;
  s.agents[0].tags = {{"burning", 0}};
  AssertSnapshotRejected(s, agent0);
  s = good;
  s.skills.push_back(SkillConfig{});  // No name
  AssertSnapshotRejected(s, "skills[" + std::to_string(good.skills.size()) + "]");
  AssertSnapshotRejected(s, "without a name");
}

// Downs (v5): max_downs >= 1; a downed agent is a companion at 0 HP that went
// down at least once; times_downed >= 0 and only on a companion.
TEST(TestSnapshotRejectsBadDowns) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string agent0 = "agent #0 (id " + std::to_string(good.agents[0].id) + ")";

  Snapshot s = good;
  s.max_downs = 0;
  AssertSnapshotRejected(s, "Snapshot: max_downs must be >= 1 (got 0)");
  s = good;
  s.agents[0].downed = true;
  s.agents[0].times_downed = 1;
  AssertSnapshotRejected(s, agent0 + ": downed with " + std::to_string(good.agents[0].health) +
                                " HP, a downed companion has 0 HP");
  s = good;
  s.agents[0].times_downed = -1;
  AssertSnapshotRejected(s, agent0 + ": times_downed must be >= 0 (>= 1 when downed)");
  s = good;
  s.agents[0].health = 0;
  s.agents[0].downed = true;
  AssertSnapshotRejected(s, agent0 + ": times_downed must be >= 0 (>= 1 when downed)");
  s = good;  // A downed companion has no statuses (cleared when it went down)
  s.agents[0].health = 0;
  s.agents[0].downed = true;
  s.agents[0].times_downed = 1;
  s.agents[0].statuses = {{static_cast<int>(StatusType::Stunned), 2}};
  AssertSnapshotRejected(s, agent0 + ": downed with statuses, a downed companion has none");
  s.agents[0].statuses.clear();  // Sanity: the same downed companion loads
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(s);

  // Not on a non-companion (a second agent, a plain Agent)
  AgentSnapshot enemy = good.agents[0];
  enemy.id = 99;
  enemy.type = static_cast<int>(ObjectType::Agent);
  enemy.position = {6, 6};
  enemy.skills.clear();
  enemy.cooldowns.clear();
  enemy.health = 0;
  s = good;
  s.agents.push_back(enemy);
  s.agents.back().times_downed = 1;
  AssertSnapshotRejected(s, "agent #1 (id 99): downed / times_downed on a non-companion (only a companion goes down)");
  s.agents.back().downed = true;
  AssertSnapshotRejected(s, "downed / times_downed on a non-companion (only a companion goes down)");
  s.agents.back().downed = false;  // Sanity: the same agent without downs loads
  s.agents.back().times_downed = 0;
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(s);

  // Every Companion type goes down
  for (ObjectType type : {ObjectType::Companion, ObjectType::Player, ObjectType::NPCCompanion}) {
    s = good;
    s.agents[0].type = static_cast<int>(type);
    s.agents[0].health = 0;
    s.agents[0].downed = true;
    s.agents[0].times_downed = 1;
    SynchroEnv env(8, 8, 1, 1, 0, 42);
    env.LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
    ASSERT_TRUE(env.GetObjectManager().GetAllAgents()[0]->IsDowned());
  }
}

// Statuses are StatusType values; Slowed (2) was removed and is reserved.
TEST(TestSnapshotRejectsRemovedOrUnknownStatus) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string agent0 = "agent #0 (id " + std::to_string(good.agents[0].id) + ")";

  Snapshot s = good;
  s.agents[0].statuses = {{1, 2}, {2, 3}};
  AssertSnapshotRejected(s, "status 2 (slowed) was removed");
  AssertSnapshotRejected(s, agent0);
  for (int type : {-1, 5, 99}) {
    s.agents[0].statuses = {{type, 3}};
    AssertSnapshotRejected(s, "unknown status " + std::to_string(type));
  }

  s = good;  // Every remaining status loads
  s.agents[0].statuses = {{0, 0}, {1, 2}, {3, 2}, {4, 2}};
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
  const Agent* a = env.GetObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(a->IsStunned());
  ASSERT_TRUE(a->IsMarked());
  ASSERT_TRUE(a->IsRooted());
}

TEST(TestDeserializeRejectsOversizedV4Counts) {
  // Nothing in the snapshot after the skill book: the buffer ends with the
  // skill count, the zone count, max_downs (v5), the context skills flag
  // (v6; absent), then the empty v7 block.
  Snapshot s = MinimalSnapshot();
  std::vector<uint8_t> bytes = s.Serialize();
  const size_t zones_at = bytes.size() - kV7Tail - 9;
  const size_t skills_at = bytes.size() - kV7Tail - 13;

  std::vector<uint8_t> bad = bytes;
  uint32_t huge = 9000000;  // Under the sanity cap, far over the bytes left
  std::memcpy(bad.data() + zones_at, &huge, sizeof(huge));
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bad); }, "zone count");

  bad = bytes;
  huge = 90000;
  std::memcpy(bad.data() + skills_at, &huge, sizeof(huge));
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bad); }, "skill count");

  // Counts that fit the remaining bytes still get the usual underflow error.
  bad = bytes;
  uint32_t one = 1;
  std::memcpy(bad.data() + zones_at, &one, sizeof(one));
  ASSERT_THROW(Snapshot::Deserialize(bad), std::runtime_error);
}

// =============================================================================
// Snapshot v6: revive fields, context skills
// =============================================================================

// Every form keeps the builtin revive a revive (a save + reload once turned it
// into a no-op), and the env's context rules.
TEST(TestSaveLoadKeepsTheReviveAndTheContextSkills) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  mend.revive_percent = 100;
  env.GetMutableSkillBook().Define(mend);
  const std::vector<ContextSkillRule> rules = {
      {ContextCondition::AdjacentDownedAlly, 1, "mend"},
      {ContextCondition::AdjacentDownedAlly, 0, "revive"}};
  ASSERT_TRUE(env.SetContextSkills(rules));
  const Snapshot saved = env.SaveSnapshot();
  ASSERT_TRUE(saved.context_skills.has_value());
  ASSERT_TRUE(*saved.context_skills == rules);
  for (const Snapshot& s : {saved, BinaryRoundTrip(saved)}) {
    SynchroEnv other(8, 8, 1, 1, 0, 7);
    other.LoadSnapshot(s);
    const SkillConfig* revive = other.GetSkillBook().Find("revive");
    ASSERT_TRUE(revive != nullptr);
    ASSERT_TRUE(revive->affects_downed);
    ASSERT_EQ(revive->revive_percent, 50);
    AssertSkillEq(*other.GetSkillBook().Find("mend"), mend);
    ASSERT_TRUE(other.GetContextSkills() == rules);
  }
}

TEST(TestBinaryContextSkillsRoundTrip) {
  Snapshot s = MinimalSnapshot();
  const size_t absent = s.Serialize().size();
  ASSERT_FALSE(BinaryRoundTrip(s).context_skills.has_value());
  ASSERT_EQ(s.Serialize()[absent - kV7Tail - 1], 0);  // The flag (before the v7 block): none

  s.context_skills = std::vector<ContextSkillRule>{};  // Present, empty: no override
  std::vector<uint8_t> bytes = s.Serialize();
  ASSERT_EQ(bytes.size(), absent + 4);  // The flag set, then a count of 0
  Snapshot back = Snapshot::Deserialize(bytes);
  ASSERT_TRUE(back.context_skills.has_value());
  ASSERT_TRUE(back.context_skills->empty());

  // Each rule: condition, slot (ints), skill (length + bytes)
  s.context_skills = std::vector<ContextSkillRule>{
      {ContextCondition::AdjacentDownedAlly, 1, "revive"},
      {ContextCondition::AdjacentDownedAlly, 0, "attack"}};
  bytes = s.Serialize();
  ASSERT_EQ(bytes.size(), absent + 4 + (4 + 4 + 4 + 6) + (4 + 4 + 4 + 6));
  back = Snapshot::Deserialize(bytes);
  ASSERT_TRUE(back.context_skills.has_value());
  ASSERT_TRUE(*back.context_skills == *s.context_skills);

  // A corrupt rule count fails on the bytes left, not on an allocation
  std::vector<uint8_t> bad = bytes;
  const uint32_t huge = 9000000;
  std::memcpy(bad.data() + absent - kV7Tail, &huge, sizeof(huge));
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bad); }, "context skill count");
  // Truncated: an underflow
  bad = bytes;
  bad.resize(bad.size() - 3);
  ASSERT_THROW(Snapshot::Deserialize(bad), std::runtime_error);
}

// A v5 file has no context skills (the default rules on load) and v5 skill
// records (no revive fields: they affect the standing).
TEST(TestBinaryV5SnapshotLoadsWithTheDefaultRules) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Snapshot saved = env.SaveSnapshot();
  saved.skills.clear();  // Builtins only, as AsV5 needs
  saved.context_skills.reset();
  Snapshot back = Snapshot::Deserialize(AsV5(AsV6(saved)));
  ASSERT_FALSE(back.context_skills.has_value());
  ASSERT_TRUE(env.SetContextSkills({}));
  env.LoadSnapshot(back);
  ASSERT_TRUE(env.GetContextSkills() == DefaultContextSkills());

  Snapshot s = MinimalSnapshot();

  // A v5 skill record: the v6 one without its last 5 bytes (the skill is the
  // last record, before the zone count, max_downs and the v6 flag)
  SkillConfig frost;
  frost.name = "frost";
  frost.range = 4;
  s.skills = {frost};
  std::vector<uint8_t> v6 = AsV6(s);
  std::vector<uint8_t> v5(v6.begin(), v6.end() - 1 - 4 - 4 - 5);
  v5.insert(v5.end(), v6.end() - 1 - 4 - 4, v6.end() - 1);
  uint32_t five = 5;
  std::memcpy(v5.data() + 4, &five, sizeof(five));
  back = Snapshot::Deserialize(v5);
  ASSERT_EQ(back.skills.size(), 1u);
  AssertSkillEq(back.skills[0], frost);
  ASSERT_FALSE(back.context_skills.has_value());
}

// A snapshot without context skills gets the default rules, whatever the env
// had; with them, exactly those.
TEST(TestLoadSnapshotSetsTheContextSkills) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Snapshot s = env.SaveSnapshot();
  s.context_skills.reset();
  SynchroEnv other(8, 8, 1, 1, 0, 7);
  ASSERT_TRUE(other.SetContextSkills({}));
  other.LoadSnapshot(s);
  ASSERT_TRUE(other.GetContextSkills() == DefaultContextSkills());
  s.context_skills = std::vector<ContextSkillRule>{};
  other.LoadSnapshot(s);
  ASSERT_TRUE(other.GetContextSkills().empty());
}

// Rules are checked against the snapshot's own book (builtins + its skills),
// before any change; messages name context_skills[i] and the field.
TEST(TestSnapshotRejectsBadContextSkills) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  using Rules = std::vector<ContextSkillRule>;

  Snapshot s = good;
  s.context_skills = Rules{{static_cast<ContextCondition>(99), 0, "revive"}};
  AssertSnapshotRejected(s, "Snapshot: context_skills[0]");
  AssertSnapshotRejected(s, "condition: unknown value 99");
  s.context_skills = Rules{{adj, 0, "revive"}, {adj, kMaxSkillSlots, "revive"}};
  AssertSnapshotRejected(s, "context_skills[1]");
  AssertSnapshotRejected(s, "slot: out of range");
  s.context_skills = Rules{{adj, -1, "revive"}};
  AssertSnapshotRejected(s, "slot: out of range");
  s.context_skills = Rules{{adj, 0, "meteor"}};
  AssertSnapshotRejected(s, "context_skills[0]");
  AssertSnapshotRejected(s, "unknown skill 'meteor'");
  s.context_skills = Rules{{adj, 0, ""}};
  AssertSnapshotRejected(s, "skill: none named");
  s.context_skills = Rules{{adj, 0, "fireball"}};
  AssertSnapshotRejected(s, "'fireball' has cooldown 3");

  // The level's book: a skill of its own is known; a retuned revive with a
  // cooldown is rejected while a rule (the default ones included) uses it
  SkillConfig mend = *SkillBook().Find("revive");
  mend.name = "mend";
  s = good;
  s.skills.push_back(mend);
  s.context_skills = Rules{{adj, 0, "mend"}};
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
  SkillConfig slow_revive = *SkillBook().Find("revive");
  slow_revive.cooldown = 3;
  s = good;
  s.skills.push_back(slow_revive);
  s.context_skills.reset();  // The default rules
  AssertSnapshotRejected(s, "context_skills[0]");
  AssertSnapshotRejected(s, "'revive' has cooldown 3");
  s.context_skills = Rules{};  // No rule uses it: a level may give it one
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(Snapshot::Deserialize(s.Serialize()));

  // Rejected before any change
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  const Rules custom = {{adj, 1, "revive"}};
  ASSERT_TRUE(env.SetContextSkills(custom));
  s = good;
  s.max_downs = 5;
  s.context_skills = Rules{{adj, 0, "meteor"}};
  ASSERT_THROW(env.LoadSnapshot(s), std::runtime_error);
  ASSERT_TRUE(env.GetContextSkills() == custom);
  ASSERT_EQ(env.GetMaxDowns(), BaseEnv::kDefaultMaxDowns);
}

// Every agent has max_health >= 1 (a revive brings back a percent of it).
TEST(TestSnapshotRejectsMaxHealthBelowOne) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string agent0 = "agent #0 (id " + std::to_string(good.agents[0].id) + ")";
  for (int max_health : {0, -3}) {
    Snapshot s = good;
    s.agents[0].max_health = max_health;
    s.agents[0].health = 0;
    AssertSnapshotRejected(s, agent0 + ": max_health must be >= 1 (got " +
                                  std::to_string(max_health) + ")");
  }
  Snapshot s = good;
  s.agents[0].max_health = 1;
  s.agents[0].health = 1;
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(Snapshot::Deserialize(s.Serialize()));
}

TEST(TestZoneSaveLoadRoundTripAtIdentity) {
  SynchroEnv env(6, 9, 1, 1, 0, 42, 0);
  ASSERT_TRUE(env.SetCellTag({1, 7}, "wet", 3));
  SynchroEnv other(6, 9, 1, 1, 0, 7, 0);
  other.LoadSnapshot(BinaryRoundTrip(env.SaveSnapshot()));
  ASSERT_EQ(CountZones(other), 1);
  ASSERT_EQ(other.GetCellTag({1, 7}).tag, other.GetTagTable().Find("wet"));
  ASSERT_EQ(other.GetCellTag({1, 7}).duration, 3);
}

// Snapshots hold the untransformed world plus the d4 to apply: a zone lands on
// the same physical cell as a cell annotation saved on the same cell.
TEST(TestZoneFollowsD4LikeAnnotations) {
  const int kRows = 6, kCols = 9;
  const Position wall{2, 4}, zone{2, 5}, start{3, 5};  // Wall left of the zone, agent below
  for (int t : {1, 4, 6}) {  // Rot90, FlipH, FlipD (transpose)
    SynchroEnv src(kRows, kCols, 1, 1, 0, 42, 0);
    Snapshot snap = src.SaveSnapshot();
    for (int r = 1; r < kRows - 1; ++r) {
      for (int c = 1; c < kCols - 1; ++c) snap.cells[r * kCols + c].kind = CellKind::Floor;
    }
    snap.cells[wall.row * kCols + wall.col].kind = CellKind::Wall;
    snap.annotations.clear();
    AnnotationSnapshot goal;
    goal.target_type = 0;
    goal.pos = zone;
    goal.agent_id = kInvalidObjectId;
    goal.tag = SemanticTag::SynchroGoal;
    goal.owner_lens_id = -1;
    snap.annotations.push_back(goal);
    snap.agents[0].position = start;
    snap.agents[0].prev_position = start;
    snap.cell_tags = {{zone, "wet", kPermanentTag}};
    snap.d4_transform = t;

    SynchroEnv dst(kRows, kCols, 1, 1, 0, 42, 0);
    dst.LoadSnapshot(BinaryRoundTrip(snap));

    const D4Transform tr = static_cast<D4Transform>(t);
    const Position z = TransformPosition(zone, kRows, kCols, tr);
    const Position w = TransformPosition(wall, kRows, kCols, tr);
    auto goals = dst.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
    ASSERT_EQ(goals.size(), 1u);
    ASSERT_TRUE(goals[0] == z);
    const TagId wet = dst.GetTagTable().Find("wet");
    ASSERT_EQ(CountZones(dst), 1);
    ASSERT_EQ(dst.GetCellTag(z).tag, wet);
    ASSERT_TRUE(dst.GetGrid().GetCell(w).GetKind() == CellKind::Wall);
    ASSERT_EQ(std::abs(w.row - z.row) + std::abs(w.col - z.col), 1);

    Agent* agent = FirstAgent(dst);
    const Position p = agent->GetPosition();
    ASSERT_TRUE(p == TransformPosition(start, kRows, kCols, tr));
    MovementAction mv = z.row < p.row   ? MovementAction::Up
                        : z.row > p.row ? MovementAction::Down
                        : z.col < p.col ? MovementAction::Left
                                        : MovementAction::Right;
    ASSERT_FALSE(agent->HasTag(wet));
    dst.Step({EncodeAction(mv)});
    ASSERT_TRUE(agent->GetPosition() == z);
    ASSERT_TRUE(agent->HasTag(wet));
  }
}

// Zones and tags restored before the next Step land as usual: fresh for an
// agent without the tag, not fresh for one saved carrying it.
TEST(TestZoneRestoredByLoadLandsOnNextStep) {
  const Action kStay = EncodeAction(MovementAction::Stay);
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  ASSERT_TRUE(env.SetCellTag(FirstAgent(env)->GetPosition(), "wet", 2));

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.LoadSnapshot(BinaryRoundTrip(env.SaveSnapshot()));
  ASSERT_TRUE(other.GetLastTagsApplied().empty());
  other.Step({kStay});
  ASSERT_EQ(other.GetLastTagsApplied().size(), 1u);
  const auto& first = other.GetLastTagsApplied()[0];
  ASSERT_EQ(first.tag, other.GetTagTable().Find("wet"));
  ASSERT_EQ(first.duration, 2);
  ASSERT_EQ(first.cause, std::string("zone"));
  ASSERT_TRUE(first.fresh);

  SynchroEnv third(8, 8, 1, 1, 0, 9);
  third.LoadSnapshot(BinaryRoundTrip(other.SaveSnapshot()));
  ASSERT_TRUE(FirstAgent(third)->HasTag(third.GetTagTable().Find("wet")));
  third.Step({kStay});  // The tag ticks 2 -> 1, then the zone re-lands it
  ASSERT_EQ(third.GetLastTagsApplied().size(), 1u);
  ASSERT_FALSE(third.GetLastTagsApplied()[0].fresh);
}

// =============================================================================
// Snapshot v7: the zone table and the cells' zone fields, reactions, tag
// statuses, weaknesses and immunities
// =============================================================================

namespace {

ZoneDef MakeZone(int duration, int steps, const std::string& then, int damage) {
  ZoneDef z;
  z.duration = duration;
  z.steps = steps;
  z.then = then;
  z.damage = damage;
  return z;
}

CellTagSnapshot MakeCell(Position cell, const std::string& tag, std::optional<int> duration,
                         std::optional<int> steps, std::optional<std::string> then,
                         std::optional<int> damage) {
  CellTagSnapshot z;
  z.cell = cell;
  z.tag = tag;
  z.duration = duration;
  z.steps = steps;
  z.then = then;
  z.damage = damage;
  return z;
}

ReactionRule MakeReaction(const std::string& a, const std::string& b, const std::string& result,
                          std::vector<std::string> keep, int damage, bool spread,
                          const std::string& zone_becomes) {
  ReactionRule r;
  r.a = a;
  r.b = b;
  r.result = result;
  r.keep = std::move(keep);
  r.damage = damage;
  r.spread = spread;
  r.zone_becomes = zone_becomes;
  return r;
}

const std::nullopt_t kAbsent = std::nullopt;

// Every v7 field, off its default, and cells with every mix of present and
// absent fields. The zone table has a cycle (burning -> smoke -> burning) and
// a successor it does not define (ash): both legal.
Snapshot V7Snapshot() {
  Snapshot s = MinimalSnapshot();
  s.zones = {{"burning", MakeZone(2, 4, "smoke", 1)},
             {"smoke", MakeZone(kPermanentTag, 2, "burning", 0)},
             {"oil", MakeZone(3, kPermanentTag, "ash", 0)},
             {"wet", ZoneDef{}}};
  s.reactions = {MakeReaction("wet", "electrified", "shocked", {"wet"}, 1, true, "wet"),
                 MakeReaction("oil", "burning", "burning", {}, 0, true, ""),
                 MakeReaction("wet", "chilled", "stunned", {"chilled", "wet"}, 2, false, "")};
  s.tag_statuses = {{"stunned", StatusType::Stunned, 3},
                    {"marked", StatusType::Marked, 1},
                    {"rooted", StatusType::Rooted, 2}};
  s.agents[0].weak_to = {{"wet", "electrified"}, {"oil", "burning"}};
  s.agents[0].immune = {"burning", "stunned"};
  s.cell_tags = {MakeCell({0, 1}, "burning", 3, 2, std::string("smoke"), 1),
                 MakeCell({1, 0}, "wet", kAbsent, kAbsent, kAbsent, kAbsent),
                 MakeCell({1, 1}, "smoke", 5, kAbsent, kAbsent, kAbsent),
                 MakeCell({0, 0}, "oil", kAbsent, 7, std::string(), kAbsent)};
  return s;
}

void AssertCellTagEq(const CellTagSnapshot& a, const CellTagSnapshot& b) {
  ASSERT_TRUE(a.cell == b.cell);
  ASSERT_EQ(a.tag, b.tag);
  ASSERT_TRUE(a.duration == b.duration);
  ASSERT_TRUE(a.steps == b.steps);
  ASSERT_TRUE(a.then == b.then);
  ASSERT_TRUE(a.damage == b.damage);
}

void AssertV7Eq(const Snapshot& a, const Snapshot& b) {
  ASSERT_TRUE(a.zones == b.zones);
  ASSERT_TRUE(a.reactions == b.reactions);
  ASSERT_TRUE(a.tag_statuses == b.tag_statuses);
  ASSERT_EQ(a.agents.size(), b.agents.size());
  for (size_t i = 0; i < a.agents.size(); ++i) {
    ASSERT_TRUE(a.agents[i].weak_to == b.agents[i].weak_to);
    ASSERT_TRUE(a.agents[i].immune == b.agents[i].immune);
  }
  ASSERT_EQ(a.cell_tags.size(), b.cell_tags.size());
  for (size_t i = 0; i < a.cell_tags.size(); ++i) AssertCellTagEq(a.cell_tags[i], b.cell_tags[i]);
}

// A zone cell of `env`, by names
struct ZoneAt {
  std::string tag;
  int duration = 0;
  int steps = 0;
  std::string then;
  int damage = 0;
  bool operator==(const ZoneAt& o) const {
    return tag == o.tag && duration == o.duration && steps == o.steps && then == o.then &&
           damage == o.damage;
  }
};
ZoneAt ZoneOf(const BaseEnv& env, Position p) {
  const BaseEnv::CellTag c = env.GetCellTag(p);
  ZoneAt z;
  if (c.tag == kInvalidTag) return z;
  z.tag = env.GetTagTable().Name(c.tag);
  z.duration = c.duration;
  z.steps = c.steps;
  z.then = c.then == kInvalidTag ? "" : env.GetTagTable().Name(c.then);
  z.damage = c.damage;
  return z;
}
ZoneAt Zone(const std::string& tag, int duration, int steps, const std::string& then, int damage) {
  ZoneAt z;
  z.tag = tag;
  z.duration = duration;
  z.steps = steps;
  z.then = then;
  z.damage = damage;
  return z;
}

}  // namespace

TEST(TestBinaryV7FieldsRoundTrip) {
  const Snapshot s = V7Snapshot();
  const Snapshot back = BinaryRoundTrip(s);
  AssertV7Eq(s, back);
  // Absent stays absent: the cell still follows the table
  ASSERT_FALSE(back.cell_tags[1].duration.has_value());
  ASSERT_TRUE(back.cell_tags[3].then == std::string());  // Present: no successor
  // CellZone: the table's fields, overridden by the cell's own
  ASSERT_TRUE(back.CellZone(back.cell_tags[0]) == MakeZone(3, 2, "smoke", 1));
  ASSERT_TRUE(back.CellZone(back.cell_tags[1]) == ZoneDef{});
  ASSERT_TRUE(back.CellZone(back.cell_tags[2]) == MakeZone(5, 2, "burning", 0));
  ASSERT_TRUE(back.CellZone(back.cell_tags[3]) == MakeZone(3, 7, "", 0));
}

// A zone record (v7): cell, tag, a byte of the fields present (duration 1,
// steps 2, then 4, damage 8), then those fields. Another bit is corrupt.
TEST(TestBinaryV7ZoneRecord) {
  Snapshot s = MinimalSnapshot();
  const size_t without = s.Serialize().size();
  s.cell_tags = {MakeCell({0, 1}, "wet", kAbsent, 6, kAbsent, 2)};
  std::vector<uint8_t> bytes = s.Serialize();
  ASSERT_EQ(bytes.size(), without + 8 + 4 + 3 + 1 + 4 + 4);
  // It closes the v4 block: before max_downs, the v6 flag and the v7 block
  const size_t mask_at = bytes.size() - kV7Tail - 1 - 4 - 4 - 4 - 1;
  ASSERT_EQ(bytes[mask_at], 2 | 8);
  int steps = 0;
  std::memcpy(&steps, bytes.data() + mask_at + 1, sizeof(int));
  ASSERT_EQ(steps, 6);
  bytes[mask_at] |= 16;
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bytes); }, "zone fields");

  // A corrupt count fails on the bytes left, not on an allocation (the v7
  // block starts with the zone table's count)
  std::vector<uint8_t> plain = MinimalSnapshot().Serialize();
  const uint32_t huge = 9000000;
  std::memcpy(plain.data() + plain.size() - kV7Tail, &huge, sizeof(huge));
  AssertThrowsMentioning([&] { Snapshot::Deserialize(plain); }, "zone table count");
  bytes = V7Snapshot().Serialize();
  bytes.resize(bytes.size() - 2);  // Truncated: an underflow
  ASSERT_THROW(Snapshot::Deserialize(bytes), std::runtime_error);
}

// The env saves the whole of it (the table, the reactions, the tag statuses,
// each agent's weaknesses and immunities, each zone's resolved fields) and a
// load replaces what the env had.
TEST(TestSaveLoadKeepsTheV7Data) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  ASSERT_TRUE(env.DefineZone("burning", MakeZone(2, 4, "smoke", 1)));
  ASSERT_TRUE(env.DefineZone("smoke", MakeZone(kPermanentTag, 2, "", 0)));
  ASSERT_TRUE(env.SetReactions({MakeReaction("oil", "burning", "burning", {}, 1, true, "burning"),
                                MakeReaction("wet", "chilled", "stunned", {"wet"}, 0, false, "")}));
  ASSERT_TRUE(env.SetTagStatuses({{"stunned", StatusType::Stunned, 2}}));
  Agent* a = FirstAgent(env);
  ASSERT_TRUE(env.SetWeaknesses(a->GetId(), {{"wet", "electrified"}}));
  ASSERT_TRUE(env.SetImmunities(a->GetId(), {"burning", "chilled"}));
  ASSERT_TRUE(env.SetCellTag({2, 2}, "burning"));                        // By name
  ASSERT_TRUE(env.SetCellTag({2, 3}, "wet", MakeZone(3, 5, "ice", 2)));  // An override
  ASSERT_TRUE(env.SetCellTag({2, 4}, "smoke", 4));                       // Name + duration
  const Snapshot saved = env.SaveSnapshot();
  ASSERT_TRUE(saved.zones == env.GetZoneDefs());
  for (const CellTagSnapshot& z : saved.cell_tags) {  // Always every field, resolved
    ASSERT_TRUE(z.duration && z.steps && z.then && z.damage);
  }

  for (const Snapshot& s : {saved, BinaryRoundTrip(saved)}) {
    SynchroEnv other(8, 8, 1, 1, 0, 7);
    other.GetMutableTagTable().Intern("unrelated");  // Ids differ: names travel
    ASSERT_TRUE(other.DefineZone("burning", MakeZone(9, 9, "", 9)));
    ASSERT_TRUE(other.DefineZone("mud", ZoneDef{}));
    ASSERT_TRUE(other.SetReactions({MakeReaction("a", "b", "c", {}, 0, false, "")}));
    other.LoadSnapshot(s);
    ASSERT_TRUE(other.GetZoneDefs() == env.GetZoneDefs());
    ASSERT_TRUE(other.GetReactions() == env.GetReactions());
    ASSERT_TRUE(other.GetTagStatuses() == env.GetTagStatuses());
    const ObjectId b = FirstAgent(other)->GetId();
    ASSERT_TRUE(other.GetWeaknesses(b) == env.GetWeaknesses(a->GetId()));
    ASSERT_TRUE(other.GetImmunities(b) == env.GetImmunities(a->GetId()));
    ASSERT_TRUE(ZoneOf(other, {2, 2}) == Zone("burning", 2, 4, "smoke", 1));
    ASSERT_TRUE(ZoneOf(other, {2, 3}) == Zone("wet", 3, 5, "ice", 2));
    ASSERT_TRUE(ZoneOf(other, {2, 4}) == Zone("smoke", 4, 2, "", 0));
    ASSERT_EQ(CountZones(other), 3);
  }
}

// A v6 file has none of it: an empty table, no reactions, no tag statuses, no
// weaknesses or immunities, and its zones keep their duration with the
// default fields (permanent, no successor, harmless).
TEST(TestBinaryV6SnapshotLoadsWithNone) {
  SynchroEnv src(8, 8, 1, 1, 0, 42);
  Snapshot s = src.SaveSnapshot();
  s.context_skills.reset();  // The zone count is then 4 + 1 bytes from the end
  std::vector<uint8_t> v6 = AsV6(s);
  // One v6 zone record: cell, tag, duration
  std::vector<uint8_t> zone;
  AppendBytes<int>(zone, 2);
  AppendBytes<int>(zone, 3);
  AppendBytes<uint32_t>(zone, 3);
  zone.push_back('w');
  zone.push_back('e');
  zone.push_back('t');
  AppendBytes<int>(zone, 2);
  const size_t count_at = v6.size() - 1 - 4 - 4;
  const uint32_t one = 1;
  std::memcpy(v6.data() + count_at, &one, sizeof(one));
  v6.insert(v6.begin() + static_cast<std::ptrdiff_t>(count_at + 4), zone.begin(), zone.end());
  const Snapshot back = Snapshot::Deserialize(v6);
  ASSERT_EQ(back.cell_tags.size(), 1u);
  ASSERT_TRUE(back.cell_tags[0].duration == 2);  // Every v4-v6 zone has its duration
  ASSERT_FALSE(back.cell_tags[0].steps.has_value());
  ASSERT_FALSE(back.cell_tags[0].then.has_value());
  ASSERT_FALSE(back.cell_tags[0].damage.has_value());

  SynchroEnv env(8, 8, 1, 1, 0, 7);
  ASSERT_TRUE(env.DefineZone("wet", MakeZone(5, 3, "ice", 2)));
  ASSERT_TRUE(env.SetReactions({MakeReaction("wet", "chilled", "stunned", {}, 0, false, "")}));
  ASSERT_TRUE(env.SetTagStatuses({{"stunned", StatusType::Stunned, 2}}));
  ASSERT_TRUE(env.SetWeaknesses(FirstAgent(env)->GetId(), {{"wet", "electrified"}}));
  ASSERT_TRUE(env.SetImmunities(FirstAgent(env)->GetId(), {"wet"}));
  env.LoadSnapshot(back);
  ASSERT_TRUE(env.GetZoneDefs().empty());
  ASSERT_TRUE(env.GetReactions().empty());
  ASSERT_TRUE(env.GetTagStatuses().empty());
  ASSERT_TRUE(env.GetWeaknesses(FirstAgent(env)->GetId()).empty());
  ASSERT_TRUE(env.GetImmunities(FirstAgent(env)->GetId()).empty());
  ASSERT_TRUE(ZoneOf(env, {2, 3}) == Zone("wet", 2, kPermanentTag, "", 0));
}

// Each v7 rule, before any change; messages say where and what.
TEST(TestSnapshotRejectsBadV7Data) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const std::string overlong(kMaxNameLength + 1, 'x');
  auto with_zone = [&](const std::string& tag, const ZoneDef& def) {
    Snapshot s = good;
    s.zones[tag] = def;
    return s;
  };
  AssertSnapshotRejected(with_zone("", ZoneDef{}), "zones['']: tag: empty");
  AssertSnapshotRejected(with_zone(overlong, ZoneDef{}), "tag: longer than");
  AssertSnapshotRejected(with_zone("burning", MakeZone(0, 2, "", 0)), "zones['burning']: duration");
  AssertSnapshotRejected(with_zone("burning", MakeZone(-2, 2, "", 0)), "zones['burning']: duration");
  AssertSnapshotRejected(with_zone("burning", MakeZone(2, 0, "", 0)), "zones['burning']: steps");
  AssertSnapshotRejected(with_zone("burning", MakeZone(2, -3, "", 0)), "zones['burning']: steps");
  AssertSnapshotRejected(with_zone("burning", MakeZone(2, 2, "", -1)), "zones['burning']: damage");
  AssertSnapshotRejected(with_zone("burning", MakeZone(2, 2, overlong, 0)),
                         "zones['burning']: then");

  auto with_cell = [&](const CellTagSnapshot& z) {
    Snapshot s = good;
    s.cell_tags = {z};
    return s;
  };
  AssertSnapshotRejected(with_cell(MakeCell({1, 2}, "wet", 0, kAbsent, kAbsent, kAbsent)),
                         "zone at (1, 2)");
  AssertSnapshotRejected(with_cell(MakeCell({1, 2}, "wet", kAbsent, 0, kAbsent, kAbsent)),
                         "zone at (1, 2): steps");
  AssertSnapshotRejected(with_cell(MakeCell({1, 2}, "wet", kAbsent, kAbsent, overlong, kAbsent)),
                         "zone at (1, 2): then");
  AssertSnapshotRejected(with_cell(MakeCell({1, 2}, "wet", kAbsent, kAbsent, kAbsent, -1)),
                         "zone at (1, 2): damage");

  auto with_reaction = [&](const ReactionRule& r) {
    Snapshot s = good;
    s.reactions = {MakeReaction("oil", "burning", "burning", {}, 0, false, ""), r};
    return s;
  };
  AssertSnapshotRejected(with_reaction(MakeReaction("wet", "ice", "x", {"fire"}, 0, false, "")),
                         "reactions[1] ('wet' + 'ice'): keep: 'fire' is neither a nor b");
  AssertSnapshotRejected(with_reaction(MakeReaction("wet", "ice", "x", {}, -1, false, "")),
                         "reactions[1] ('wet' + 'ice'): damage: negative");
  AssertSnapshotRejected(with_reaction(MakeReaction("wet", "wet", "x", {}, 0, false, "")),
                         "the same tag");
  AssertSnapshotRejected(with_reaction(MakeReaction("wet", "ice", "", {}, 0, false, "")),
                         "result: empty");
  AssertSnapshotRejected(with_reaction(MakeReaction("wet", "ice", "x", {}, 0, false, "wet")),
                         "zone_becomes: needs spread");
  AssertSnapshotRejected(with_reaction(MakeReaction("burning", "oil", "x", {}, 0, false, "")),
                         "already reacts in reactions[0]");

  auto with_status = [&](int status, int steps) {
    Snapshot s = good;
    s.tag_statuses = {{"stunned", static_cast<StatusType>(status), steps}};
    return s;
  };
  AssertSnapshotRejected(with_status(2, 1), "tag_statuses[0] ('stunned'): status: unknown value 2");
  AssertSnapshotRejected(with_status(0, 1),
                         "tag_statuses[0] ('stunned'): status: none is not a status");
  AssertSnapshotRejected(with_status(9, 1), "status: unknown value 9");
  AssertSnapshotRejected(with_status(1, 0), "tag_statuses[0] ('stunned'): steps");

  const std::string who = "agent #0 (id " + std::to_string(good.agents[0].id) + "): ";
  Snapshot s = good;
  s.agents[0].weak_to = {{"wet", "electrified"}, {"wet", "electrified"}};
  AssertSnapshotRejected(s, who + "weak_to[1] ('wet', 'electrified'): twice");
  s = good;
  s.agents[0].weak_to = {{"", "electrified"}};
  AssertSnapshotRejected(s, who + "weak_to[0] ('', 'electrified'): zone: empty");
  s = good;
  s.agents[0].immune = {"burning", overlong};
  AssertSnapshotRejected(s, who + "immune[1]");
  s = good;
  s.agents[0].immune = {"burning", "burning"};
  AssertSnapshotRejected(s, who + "immune[1] ('burning'): twice");
}

// Names only need to be valid: a cycle of finite zones, a successor or a
// zone_becomes the table does not define all load.
TEST(TestSnapshotAcceptsZoneCyclesAndUndefinedNames) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Snapshot s = env.SaveSnapshot();
  s.zones = {{"tide", MakeZone(kPermanentTag, 1, "ebb", 0)},
             {"ebb", MakeZone(kPermanentTag, 1, "tide", 0)},
             {"flicker", MakeZone(kPermanentTag, 2, "flicker", 0)},
             {"fire", MakeZone(kPermanentTag, 2, "ash", 0)}};
  s.reactions = {MakeReaction("oil", "burning", "burning", {}, 0, true, "lava")};
  s.cell_tags = {MakeCell({2, 2}, "tide", kAbsent, kAbsent, kAbsent, kAbsent),
                 MakeCell({2, 3}, "mist", kAbsent, 1, std::string("fog"), kAbsent)};
  env.LoadSnapshot(BinaryRoundTrip(s));
  ASSERT_TRUE(ZoneOf(env, {2, 2}) == Zone("tide", kPermanentTag, 1, "ebb", 0));
  ASSERT_TRUE(ZoneOf(env, {2, 3}) == Zone("mist", kPermanentTag, 1, "fog", 0));
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_TRUE(ZoneOf(env, {2, 2}) == Zone("ebb", kPermanentTag, 1, "tide", 0));
  ASSERT_TRUE(ZoneOf(env, {2, 3}) == Zone("fog", kPermanentTag, kPermanentTag, "", 0));
}

// The d4 transform moves each zone cell with its fields (they are not
// positional); the table is not positional either.
TEST(TestD4MovesZoneCellsWithTheirFields) {
  const int kRows = 6, kCols = 9;
  const Position a{1, 2}, b{4, 6};
  for (int t = 0; t < 8; ++t) {
    SynchroEnv src(kRows, kCols, 1, 1, 0, 42, 0);
    Snapshot snap = src.SaveSnapshot();
    snap.zones = {{"burning", MakeZone(2, 4, "smoke", 1)}};
    snap.cell_tags = {MakeCell(a, "burning", kAbsent, kAbsent, kAbsent, kAbsent),
                      MakeCell(b, "wet", 3, 5, std::string("ice"), 2)};
    snap.d4_transform = t;
    SynchroEnv dst(kRows, kCols, 1, 1, 0, 42, 0);
    dst.LoadSnapshot(BinaryRoundTrip(snap));
    const D4Transform tr = static_cast<D4Transform>(t);
    ASSERT_EQ(CountZones(dst), 2);
    ASSERT_TRUE(ZoneOf(dst, TransformPosition(a, kRows, kCols, tr)) ==
                Zone("burning", 2, 4, "smoke", 1));
    ASSERT_TRUE(ZoneOf(dst, TransformPosition(b, kRows, kCols, tr)) == Zone("wet", 3, 5, "ice", 2));
    ASSERT_TRUE(dst.GetZoneDefs() == snap.zones);
  }
}

// A v7 file ends with its last block: trailing bytes are corrupt.
TEST(TestBinaryV7RejectsTrailingBytes) {
  std::vector<uint8_t> bytes = V7Snapshot().Serialize();
  Snapshot::Deserialize(bytes);  // Sanity
  bytes.push_back(0);
  AssertThrowsMentioning([&] { Snapshot::Deserialize(bytes); }, "trailing");
}

// Every truncation of a v7 buffer is an error (never a crash, never a load).
TEST(TestBinaryV7EveryTruncationIsRejected) {
  const std::vector<uint8_t> bytes = V7Snapshot().Serialize();
  for (size_t n = 0; n < bytes.size(); ++n) {
    const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
    ASSERT_THROW(Snapshot::Deserialize(cut), std::runtime_error);
  }
}

// Timers have a ceiling (kMaxTimerSteps), so `n + 1` during a step never
// overflows: zone durations and steps, tag status steps, agent and zone tag
// durations, a skill's tag durations, root_steps and cooldown.
TEST(TestTimersAreCapped) {
  const Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 42).SaveSnapshot();
  const int max = kMaxTimerSteps;
  Snapshot s = good;
  s.zones = {{"burning", MakeZone(max, max, "", 0)}};
  s.tag_statuses = {{"stunned", StatusType::Stunned, max}};
  s.cell_tags = {MakeCell({1, 2}, "wet", max, max, kAbsent, kAbsent)};
  s.agents[0].tags = {{"blessed", max}};
  SynchroEnv(8, 8, 1, 1, 0, 42).LoadSnapshot(BinaryRoundTrip(s));  // The ceiling itself is fine

  s = good;
  s.zones = {{"burning", MakeZone(max + 1, 2, "", 0)}};
  AssertSnapshotRejected(s, "zones['burning']: duration");
  s.zones = {{"burning", MakeZone(2, max + 1, "", 0)}};
  AssertSnapshotRejected(s, "zones['burning']: steps");
  s.zones = {{"burning", MakeZone(2, 2147483647, "", 0)}};
  AssertSnapshotRejected(s, "zones['burning']: steps");
  s = good;
  s.cell_tags = {MakeCell({1, 2}, "wet", kAbsent, max + 1, kAbsent, kAbsent)};
  AssertSnapshotRejected(s, "zone at (1, 2): steps");
  s.cell_tags = {MakeCell({1, 2}, "wet", max + 1, kAbsent, kAbsent, kAbsent)};
  AssertSnapshotRejected(s, "zone at (1, 2)");
  s = good;
  s.tag_statuses = {{"stunned", StatusType::Stunned, max + 1}};
  AssertSnapshotRejected(s, "tag_statuses[0] ('stunned'): steps");
  s = good;
  s.agents[0].tags = {{"blessed", max + 1}};
  AssertSnapshotRejected(s, "invalid duration");

  SkillConfig skill;
  skill.name = "frost";
  SkillConfig bad = skill;
  bad.tags = {{"chilled", max}};
  bad.root_steps = max;
  bad.cooldown = max;
  ValidateSkillConfig(bad);
  bad.tags = {{"chilled", max + 1}};
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "skill 'frost': tags[0] ('chilled')");
  bad = skill;
  bad.root_steps = max + 1;
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "root_steps");
  bad = skill;
  bad.cooldown = max + 1;
  AssertThrowsMentioning([&] { ValidateSkillConfig(bad); }, "cooldown");

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  ASSERT_FALSE(env.DefineZone("burning", MakeZone(2, max + 1, "", 0)));
  ASSERT_FALSE(env.SetCellTag({1, 2}, "wet", max + 1));
  ASSERT_FALSE(env.SetTagStatuses({{"stunned", StatusType::Stunned, max + 1}}));
  ASSERT_FALSE(env.ApplyTagTo(FirstAgent(env)->GetId(), "blessed", max + 1));
  ASSERT_TRUE(env.ApplyTagTo(FirstAgent(env)->GetId(), "blessed", max));
}

// A rejected load changes nothing: the env keeps its zone table, reactions,
// tag statuses, weaknesses, immunities and zones.
TEST(TestARejectedV7LoadChangesNothing) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  ASSERT_TRUE(env.DefineZone("burning", MakeZone(2, 4, "smoke", 1)));
  ASSERT_TRUE(
      env.SetReactions({MakeReaction("oil", "burning", "burning", {}, 1, true, "burning")}));
  ASSERT_TRUE(env.SetTagStatuses({{"stunned", StatusType::Stunned, 2}}));
  const ObjectId a = FirstAgent(env)->GetId();
  ASSERT_TRUE(env.SetWeaknesses(a, {{"wet", "electrified"}}));
  ASSERT_TRUE(env.SetImmunities(a, {"burning"}));
  ASSERT_TRUE(env.SetCellTag({2, 2}, "burning"));
  const Snapshot before = env.SaveSnapshot();

  Snapshot good = SynchroEnv(8, 8, 1, 1, 0, 7).SaveSnapshot();
  good.zones = {{"wet", MakeZone(3, 3, "", 0)}};
  good.reactions = {MakeReaction("wet", "chilled", "stunned", {}, 0, false, "")};
  good.tag_statuses = {{"rooted", StatusType::Rooted, 1}};
  good.cell_tags = {MakeCell({3, 3}, "wet", kAbsent, kAbsent, kAbsent, kAbsent)};
  std::vector<Snapshot> bad(5, good);
  bad[0].zones["wet"].steps = 0;
  bad[1].reactions[0].keep = {"fire"};
  bad[2].tag_statuses[0].steps = 0;
  bad[3].agents[0].immune = {"wet", "wet"};
  bad[4].cell_tags[0].damage = -1;
  for (const Snapshot& s : bad) {
    ASSERT_THROW(env.LoadSnapshot(s), std::runtime_error);
    ASSERT_TRUE(env.GetZoneDefs() == before.zones);
    ASSERT_TRUE(env.GetReactions() == before.reactions);
    ASSERT_TRUE(env.GetTagStatuses() == before.tag_statuses);
    ASSERT_TRUE(env.GetWeaknesses(a) == before.agents[0].weak_to);
    ASSERT_TRUE(env.GetImmunities(a) == before.agents[0].immune);
    ASSERT_TRUE(ZoneOf(env, {2, 2}) == Zone("burning", 2, 4, "smoke", 1));
    ASSERT_EQ(CountZones(env), 1);
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
