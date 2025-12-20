// Copyright 2024
// Test suite for JSON Snapshot serialization

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/core/types.h"
#include "../src/core/cell.h"
#include "../src/env/synchro_env.h"
#include "../src/env/aggro_env.h"

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
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__; \
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
// Helper: Compare snapshots
// =============================================================================
void AssertSnapshotsEqual(const Snapshot& a, const Snapshot& b) {
  ASSERT_EQ(a.rows, b.rows);
  ASSERT_EQ(a.cols, b.cols);
  ASSERT_EQ(a.tick, b.tick);
  ASSERT_EQ(a.horizon, b.horizon);
  ASSERT_EQ(a.d4_transform, b.d4_transform);
  ASSERT_EQ(a.rng_state, b.rng_state);
  ASSERT_EQ(a.rng_inc, b.rng_inc);

  // Compare cells
  ASSERT_EQ(a.cells.size(), b.cells.size());
  for (size_t i = 0; i < a.cells.size(); ++i) {
    ASSERT_EQ(static_cast<int>(a.cells[i].kind), static_cast<int>(b.cells[i].kind));
    ASSERT_EQ(static_cast<int>(a.cells[i].origin), static_cast<int>(b.cells[i].origin));
  }

  // Compare agents
  ASSERT_EQ(a.agents.size(), b.agents.size());
  for (size_t i = 0; i < a.agents.size(); ++i) {
    ASSERT_EQ(a.agents[i].id, b.agents[i].id);
    ASSERT_EQ(a.agents[i].type, b.agents[i].type);
    ASSERT_EQ(a.agents[i].position.row, b.agents[i].position.row);
    ASSERT_EQ(a.agents[i].position.col, b.agents[i].position.col);
    ASSERT_EQ(a.agents[i].health, b.agents[i].health);
    ASSERT_EQ(a.agents[i].max_health, b.agents[i].max_health);
    ASSERT_EQ(a.agents[i].faction, b.agents[i].faction);
    ASSERT_EQ(a.agents[i].direction, b.agents[i].direction);
    ASSERT_EQ(a.agents[i].color, b.agents[i].color);
    ASSERT_EQ(a.agents[i].alive, b.agents[i].alive);
    ASSERT_EQ(a.agents[i].has_fsm, b.agents[i].has_fsm);

    // Compare statuses
    ASSERT_EQ(a.agents[i].statuses.size(), b.agents[i].statuses.size());

    // Compare FSM if present
    if (a.agents[i].has_fsm) {
      ASSERT_EQ(static_cast<int>(a.agents[i].fsm.state_type),
                static_cast<int>(b.agents[i].fsm.state_type));
      ASSERT_EQ(a.agents[i].fsm.target_id, b.agents[i].fsm.target_id);
      ASSERT_EQ(a.agents[i].fsm.detection_range, b.agents[i].fsm.detection_range);
    }
  }

  // Compare effects
  ASSERT_EQ(a.effects.size(), b.effects.size());

  // Compare patrol path
  ASSERT_EQ(a.patrol_path.size(), b.patrol_path.size());
  for (size_t i = 0; i < a.patrol_path.size(); ++i) {
    ASSERT_EQ(a.patrol_path[i].row, b.patrol_path[i].row);
    ASSERT_EQ(a.patrol_path[i].col, b.patrol_path[i].col);
  }
}

// =============================================================================
// Basic Serialization Tests
// =============================================================================

TEST(TestEmptySnapshot) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;
  original.cells.resize(25);
  for (auto& cell : original.cells) {
    cell.kind = CellKind::Floor;
    cell.origin = CellOrigin::Default;
  }
  original.tick = 0;
  original.horizon = 100;

  std::string json = SnapshotToJson(original);
  ASSERT_TRUE(json.find("\"rows\": 5") != std::string::npos);
  ASSERT_TRUE(json.find("\"cols\": 5") != std::string::npos);

  Snapshot restored = SnapshotFromJson(json);
  AssertSnapshotsEqual(original, restored);
}

TEST(TestCellKindSerialization) {
  Snapshot original;
  original.rows = 2;
  original.cols = 3;
  original.cells.resize(6);

  // Test all cell kinds
  original.cells[0].kind = CellKind::Floor;
  original.cells[1].kind = CellKind::Wall;
  original.cells[2].kind = CellKind::Hazard;
  original.cells[3].kind = CellKind::Synchro;
  original.cells[4].kind = CellKind::HealArea;
  original.cells[5].kind = CellKind::Target;

  std::string json = SnapshotToJson(original);

  // Verify enum strings in JSON
  ASSERT_TRUE(json.find("\"cell_kind\": \"Floor\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cell_kind\": \"Wall\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cell_kind\": \"Synchro\"") != std::string::npos);

  Snapshot restored = SnapshotFromJson(json);
  AssertSnapshotsEqual(original, restored);
}

TEST(TestAgentSerialization) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;
  original.cells.resize(25);

  AgentSnapshot agent;
  agent.id = 42;
  agent.type = static_cast<int>(ObjectType::Companion);
  agent.position = {2, 3};
  agent.prev_position = {2, 2};
  agent.health = 80;
  agent.max_health = 100;
  agent.faction = static_cast<int>(Faction::COMPANION);
  agent.direction = static_cast<int>(Direction::Right);
  agent.color = 2;  // Blue
  agent.alive = true;
  agent.has_fsm = false;

  original.agents.push_back(agent);

  std::string json = SnapshotToJson(original);

  // Verify agent data in JSON
  ASSERT_TRUE(json.find("\"id\": 42") != std::string::npos);
  ASSERT_TRUE(json.find("\"agent_type\": \"Companion\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"health\": 80") != std::string::npos);
  ASSERT_TRUE(json.find("\"direction\": \"Right\"") != std::string::npos);

  Snapshot restored = SnapshotFromJson(json);
  AssertSnapshotsEqual(original, restored);
}

TEST(TestFSMAgentSerialization) {
  Snapshot original;
  original.rows = 5;
  original.cols = 5;
  original.cells.resize(25);

  AgentSnapshot agent;
  agent.id = 99;
  agent.type = static_cast<int>(ObjectType::AgentFSM);
  agent.position = {1, 1};
  agent.health = 50;
  agent.max_health = 50;
  agent.faction = static_cast<int>(Faction::ENEMY);
  agent.alive = true;
  agent.has_fsm = true;
  agent.fsm.state_type = FSMStateType::Patrol;
  agent.fsm.target_id = -1;
  agent.fsm.detection_range = 4;
  agent.fsm.lose_target_range = 6;
  agent.fsm.patrol_path.push_back({0, 0});
  agent.fsm.patrol_path.push_back({0, 3});
  agent.fsm.patrol_path.push_back({3, 3});

  original.agents.push_back(agent);

  std::string json = SnapshotToJson(original);

  // Verify FSM data in JSON
  ASSERT_TRUE(json.find("\"state_type\": \"Patrol\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"detection_range\": 4") != std::string::npos);

  Snapshot restored = SnapshotFromJson(json);
  AssertSnapshotsEqual(original, restored);
}

TEST(TestRNGStateSerialization) {
  Snapshot original;
  original.rows = 3;
  original.cols = 3;
  original.cells.resize(9);
  original.rng_state = 12345678901234ULL;
  original.rng_inc = 98765432109876ULL;

  std::string json = SnapshotToJson(original);
  Snapshot restored = SnapshotFromJson(json);

  ASSERT_EQ(original.rng_state, restored.rng_state);
  ASSERT_EQ(original.rng_inc, restored.rng_inc);
}

// =============================================================================
// Environment Round-Trip Tests
// =============================================================================

TEST(TestSynchroEnvRoundTrip) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset(42);

  // Take a few steps
  std::vector<Action> actions = {EncodeAction(MovementAction::Up),
                                  EncodeAction(MovementAction::Right)};
  env.Step(actions);
  env.Step(actions);

  Snapshot original = env.SaveSnapshot();
  std::string json = SnapshotToJson(original);
  Snapshot restored = SnapshotFromJson(json);

  AssertSnapshotsEqual(original, restored);
}

TEST(TestAggroEnvRoundTrip) {
  AggroEnv env(10, 2, EnemyType::Zombie, 42, 0, 100);
  env.Reset(42);

  // Take a few steps
  std::vector<Action> actions = {EncodeAction(MovementAction::Up),
                                  EncodeAction(MovementAction::Down)};
  env.Step(actions);

  Snapshot original = env.SaveSnapshot();
  std::string json = SnapshotToJson(original);
  Snapshot restored = SnapshotFromJson(json);

  AssertSnapshotsEqual(original, restored);
}

// =============================================================================
// File I/O Tests
// =============================================================================

TEST(TestFileIO) {
  Snapshot original;
  original.rows = 4;
  original.cols = 4;
  original.cells.resize(16);
  original.cells[0].kind = CellKind::Wall;
  original.cells[5].kind = CellKind::Synchro;
  original.tick = 42;
  original.horizon = 200;

  const char* filepath = "test_snapshot.json";

  // Save to file
  bool saved = SaveSnapshotToJsonFile(original, filepath);
  ASSERT_TRUE(saved);

  // Load from file
  Snapshot restored = LoadSnapshotFromJsonFile(filepath);
  AssertSnapshotsEqual(original, restored);

  // Cleanup
  std::remove(filepath);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST(TestInvalidJson) {
  ASSERT_THROW(SnapshotFromJson("not valid json"), std::exception);
  ASSERT_THROW(SnapshotFromJson("{\"invalid\": true}"), std::exception);
}

TEST(TestMissingFile) {
  ASSERT_THROW(LoadSnapshotFromJsonFile("nonexistent_file.json"), std::runtime_error);
}

// =============================================================================
// JSON Structure Tests
// =============================================================================

TEST(TestJsonStructure) {
  Snapshot snap;
  snap.rows = 3;
  snap.cols = 3;
  snap.cells.resize(9);
  snap.tick = 10;
  snap.horizon = 100;
  snap.d4_transform = 2;

  std::string json = SnapshotToJson(snap);

  // Check required top-level keys
  ASSERT_TRUE(json.find("\"grid\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"agents\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"effects\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"tick\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"horizon\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"rng_state\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"d4_value\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"patrol_path\"") != std::string::npos);

  // Check grid structure
  ASSERT_TRUE(json.find("\"rows\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cols\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cells\"") != std::string::npos);
}

TEST(TestPrettyPrint) {
  Snapshot snap;
  snap.rows = 2;
  snap.cols = 2;
  snap.cells.resize(4);

  std::string json = SnapshotToJson(snap);

  // Pretty-print should have newlines and indentation
  ASSERT_TRUE(json.find('\n') != std::string::npos);
  ASSERT_TRUE(json.find("  ") != std::string::npos);  // 2-space indent
}

// =============================================================================
// Main
// =============================================================================

int main() {
  int passed = 0;
  int failed = 0;

  std::cout << "Running " << tests.size() << " JSON snapshot tests...\n\n";

  for (const auto& test : tests) {
    std::cout << "  " << test.name << "... ";
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "FAILED\n    " << e.what() << "\n";
      failed++;
    }
  }

  std::cout << "\n";
  std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

  return failed > 0 ? 1 : 0;
}
