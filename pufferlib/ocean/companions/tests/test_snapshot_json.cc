// Copyright 2024
// Test suite for JSON Snapshot serialization

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/core/types.h"
#include "../src/core/cell.h"
#include "../src/env/synchro_env.h"
#include "../src/env/aggro_env.h"
#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

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
  original.cells[3].kind = CellKind::HealArea;
  original.cells[4].kind = CellKind::HealArea;
  original.cells[5].kind = CellKind::Hazard;

  std::string json = SnapshotToJson(original);

  // Verify enum strings in JSON
  ASSERT_TRUE(json.find("\"cell_kind\": \"Floor\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cell_kind\": \"Wall\"") != std::string::npos);
  ASSERT_TRUE(json.find("\"cell_kind\": \"HealArea\"") != std::string::npos);

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
  original.cells[5].kind = CellKind::HealArea;
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
// Cross-format round-trip tests (audit F4/F8)
// =============================================================================

// Binary -> JSON -> binary must preserve the full Snapshot struct. Guards
// against schema drift between the two parallel serializers.
TEST(TestCrossFormatRoundTripSynchroEnv) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  Snapshot original = env.SaveSnapshot();

  // Binary -> JSON -> binary
  std::vector<uint8_t> bin = original.Serialize();
  Snapshot from_bin = Snapshot::Deserialize(bin);
  std::string json_str = SnapshotToJson(from_bin);
  Snapshot from_json = SnapshotFromJson(json_str);
  std::vector<uint8_t> bin2 = from_json.Serialize();

  // Binary payloads must be byte-identical — proves full-field fidelity.
  ASSERT_EQ(bin.size(), bin2.size());
  for (size_t i = 0; i < bin.size(); ++i) {
    if (bin[i] != bin2[i]) {
      std::ostringstream oss;
      oss << "bytes differ at offset " << i;
      throw std::runtime_error(oss.str());
    }
  }
}

// Version/magic on JSON must be validated — older payloads without the
// fields still load (backwards compatibility); wrong values must reject.
TEST(TestJsonSnapshotVersionRejection) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  std::string json_str = SnapshotToJson(env.SaveSnapshot());

  // Tamper the version field.
  std::string tampered = json_str;
  size_t pos = tampered.find("\"version\": 4");
  ASSERT_TRUE(pos != std::string::npos);
  tampered.replace(pos, 12, "\"version\": 5");  // One above current

  bool threw = false;
  try {
    SnapshotFromJson(tampered);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

// Statuses are saved lowercase ("stunned") and must load back, whatever case.
TEST(TestJsonRoundTripStatuses) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  a->ApplyStatus(StatusType::Stunned, 2);
  a->ApplyStatus(StatusType::Marked, 3);
  a->ApplyStatus(StatusType::Rooted, 4);

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.Reset();
  other.LoadSnapshot(SnapshotFromJson(SnapshotToJson(env.SaveSnapshot())));

  const Agent* b = other.GetObjectManager().GetAllAgents()[0];
  const StatusType types[] = {StatusType::Stunned, StatusType::Marked, StatusType::Rooted};
  for (int i = 0; i < 3; ++i) {
    bool found = false;
    for (const auto& st : b->GetStatuses()) {
      if (st.type == types[i] && st.IsActive()) {
        ASSERT_EQ(st.duration, i + 2);
        found = true;
      }
    }
    ASSERT_TRUE(found);
  }
}

// =============================================================================
// Snapshot v4: skills, agent tags / slots / cooldowns, zone tags
// =============================================================================

namespace {

Snapshot JsonRoundTrip(const Snapshot& s) { return SnapshotFromJson(SnapshotToJson(s)); }

Companion* FirstCompanion(BaseEnv& env) {
  return dynamic_cast<Companion*>(env.GetMutableObjectManager().GetAllAgents()[0]);
}

bool HasAnyZone(const BaseEnv& env) {
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (env.GetCellTag({r, c}).tag != kInvalidTag) return true;
    }
  }
  return false;
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
  return out;
}

}  // namespace

TEST(TestJsonRoundTripSkillsTagsZones) {
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
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "frost"));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 1, "vortex"));
  dynamic_cast<Companion*>(a)->SetCooldown(1, 2);
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "burning", 3));
  a->ApplyStatus(StatusType::Rooted, 2);
  ASSERT_TRUE(env.SetCellTag({2, 2}, "wet", kPermanentTag));

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.Reset();
  other.LoadSnapshot(JsonRoundTrip(env.SaveSnapshot()));

  const SkillConfig* f = other.GetSkillBook().Find("frost");
  ASSERT_TRUE(f && f->range == 2 && f->filter == TargetFilter::Enemy);
  ASSERT_TRUE(f->targeting == SkillTargeting::Projectile);
  ASSERT_EQ(f->tags[0].tag, std::string("chilled"));
  ASSERT_EQ(other.GetSkillBook().Find("fireball")->range, 5);
  const SkillConfig* v = other.GetSkillBook().Find("vortex");
  ASSERT_TRUE(v->motion == SkillMotion::PullIn && v->area == SkillArea::Cross && v->root_steps == 1);
  auto* c = dynamic_cast<Companion*>(other.GetMutableObjectManager().GetAllAgents()[0]);
  ASSERT_EQ(c->GetSkill(0), std::string("frost"));
  ASSERT_EQ(c->GetSkill(1), std::string("vortex"));
  ASSERT_EQ(c->GetCooldown(1), 2);
  ASSERT_TRUE(c->HasTag(other.GetTagTable().Find("burning")));
  ASSERT_EQ(c->GetTags()[0].duration, 3);
  ASSERT_TRUE(c->IsRooted());
  ASSERT_EQ(other.GetCellTag({2, 2}).tag, other.GetTagTable().Find("wet"));
  ASSERT_EQ(other.GetCellTag({2, 2}).duration, kPermanentTag);
}

TEST(TestJsonSnapshotKeys) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "teleport"));
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "burning", 3));
  ASSERT_TRUE(env.SetCellTag({2, 3}, "wet", 4));
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  ASSERT_EQ(j.at("version").get<int>(), 4);
  const json& agent = j.at("agents").at(0);
  ASSERT_EQ(agent.at("skills"), json({"teleport", "attack"}));
  ASSERT_EQ(agent.at("cooldowns"), json({0, 0}));
  ASSERT_EQ(agent.at("tags").at(0).at("tag").get<std::string>(), std::string("burning"));
  ASSERT_EQ(agent.at("tags").at(0).at("duration").get<int>(), 3);
  ASSERT_EQ(j.at("cell_tags").size(), 1u);
  const json& zone = j.at("cell_tags").at(0);
  ASSERT_EQ(zone.at("row").get<int>(), 2);
  ASSERT_EQ(zone.at("col").get<int>(), 3);
  ASSERT_EQ(zone.at("tag").get<std::string>(), std::string("wet"));
  ASSERT_EQ(zone.at("duration").get<int>(), 4);
  // Every skill of the book, builtins included (a level may retune them),
  // but the fixed default attack.
  ASSERT_EQ(j.at("skills").size(), env.GetSkillBook().All().size() - 1);
  for (const json& skill : j.at("skills")) {
    ASSERT_TRUE(skill.at("name").get<std::string>() != kDefaultSkill);
  }
  const json& fireball = j.at("skills").at(0);
  ASSERT_EQ(fireball.at("name").get<std::string>(), std::string("fireball"));
  ASSERT_EQ(fireball.at("targeting").get<std::string>(), std::string("ground"));
  ASSERT_EQ(fireball.at("filter").get<std::string>(), std::string("all"));
  ASSERT_EQ(fireball.at("area").get<std::string>(), std::string("cross"));
  ASSERT_EQ(fireball.at("motion").get<std::string>(), std::string("push_out"));
  ASSERT_EQ(fireball.at("distance").get<int>(), 1);
  ASSERT_EQ(fireball.at("tag_path").get<bool>(), false);
  ASSERT_EQ(fireball.at("root_steps").get<int>(), 0);
  ASSERT_EQ(fireball.at("cooldown").get<int>(), 3);
  ASSERT_EQ(fireball.at("friendly_fire").get<bool>(), true);
  ASSERT_EQ(fireball.at("self_tags").get<bool>(), true);
  ASSERT_EQ(fireball.at("self_motion").get<bool>(), true);
  ASSERT_EQ(fireball.at("self_root").get<bool>(), true);
  ASSERT_EQ(fireball.at("damage").get<int>(), 0);
  ASSERT_EQ(fireball.at("self_damage").get<bool>(), true);
  const json& step = j.at("skills").at(1);
  ASSERT_EQ(step.at("name").get<std::string>(), std::string("lightningStep"));
  ASSERT_EQ(step.at("self_tags").get<bool>(), false);
  const json& vortex = j.at("skills").at(3);
  ASSERT_EQ(vortex.at("name").get<std::string>(), std::string("vortex"));
  ASSERT_EQ(vortex.at("self_root").get<bool>(), false);
}

TEST(TestJsonEverySkillConfigFieldRoundTrips) {
  Snapshot s;
  s.rows = 3;
  s.cols = 3;
  s.cells.resize(9);
  s.skills = EverySkillShape();
  Snapshot back = JsonRoundTrip(s);
  ASSERT_EQ(back.skills.size(), s.skills.size());
  for (size_t i = 0; i < s.skills.size(); ++i) AssertSkillEq(s.skills[i], back.skills[i]);
}

TEST(TestJsonSkillFieldsDefaultWhenAbsent) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  j["skills"] = json::array({json{{"name", "bare"}}});
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.skills.size(), 1u);
  SkillConfig expected;
  expected.name = "bare";  // filter defaults to "all" like SkillConfig
  AssertSkillEq(s.skills[0], expected);
}

TEST(TestJsonV3SnapshotLoadsWithoutSkillsTagsZones) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "vortex"));
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "burning", 3));
  ASSERT_TRUE(env.SetCellTag({2, 2}, "wet", kPermanentTag));
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  j["version"] = 3;
  j.erase("skills");
  j.erase("cell_tags");
  for (json& agent : j.at("agents")) {
    agent.erase("tags");
    agent.erase("skills");
    agent.erase("cooldowns");
  }

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  SkillConfig frost;
  frost.name = "frost";
  other.GetMutableSkillBook().Define(frost);
  other.LoadSnapshot(SnapshotFromJson(j.dump()));
  Companion* c = FirstCompanion(other);
  ASSERT_EQ(c->GetSkill(0), std::string(kDefaultSkill));  // No slots: the attack
  ASSERT_EQ(c->GetSkill(1), std::string(kDefaultSkill));
  ASSERT_EQ(c->GetCooldown(0), 0);
  ASSERT_TRUE(c->GetTags().empty());
  ASSERT_EQ(other.GetSkillBook().All().size(), SkillBook().All().size());
  ASSERT_TRUE(other.GetSkillBook().Find("frost") == nullptr);
  ASSERT_FALSE(HasAnyZone(other));
}

TEST(TestJsonV2AndUnversionedSnapshotsStillLoad) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  j["version"] = 2;
  SnapshotFromJson(j.dump());
  j.erase("version");
  SnapshotFromJson(j.dump());
  j["version"] = 1;
  ASSERT_THROW(SnapshotFromJson(j.dump()), std::runtime_error);
}

TEST(TestJsonMissingSkillsResetsBookToBuiltins) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  j.erase("skills");

  SynchroEnv other(8, 8, 1, 1, 0, 7);
  SkillConfig frost;
  frost.name = "frost";
  other.GetMutableSkillBook().Define(frost);
  SkillConfig far_fireball = *other.GetSkillBook().Find("fireball");
  far_fireball.range = 9;
  other.GetMutableSkillBook().Define(far_fireball);
  other.LoadSnapshot(SnapshotFromJson(j.dump()));
  // A level's skills never leak into the next level.
  ASSERT_TRUE(other.GetSkillBook().Find("frost") == nullptr);
  ASSERT_EQ(other.GetSkillBook().Find("fireball")->range, 3);
}

TEST(TestJsonUndefinedSkillNameStillLoads) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  json j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  j.at("agents").at(0)["skills"] = json({"meteor", ""});
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  Companion* c = FirstCompanion(env);
  ASSERT_EQ(c->GetSkill(0), std::string("meteor"));
  // Not usable: the step drops the skill and the movement applies.
  Position before = c->GetPosition();
  env.Step({EncodeAction(MovementAction::Stay, InteractAction::Skill1)});
  ASSERT_TRUE(env.GetLastSkillUses().empty());
  ASSERT_TRUE(c->GetPosition() == before);
}

TEST(TestJsonRejectsInvalidSkillsTagsZones) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "burning", 3));
  ASSERT_TRUE(env.SetCellTag({2, 2}, "wet", kPermanentTag));
  const json good = json::parse(SnapshotToJson(env.SaveSnapshot()));
  SnapshotFromJson(good.dump());  // Sanity: the untouched one loads

  std::vector<json> bad;
  for (int d : {0, -2}) {
    json j = good;
    j.at("agents").at(0).at("tags").at(0)["duration"] = d;
    bad.push_back(j);
    j = good;
    j.at("cell_tags").at(0)["duration"] = d;
    bad.push_back(j);
    j = good;
    j.at("skills").at(0).at("tags").at(0)["duration"] = d;  // fireball's "burning"
    bad.push_back(j);
  }
  const char* enum_keys[][2] = {{"targeting", "sideways"}, {"area", "blob"},
                                {"motion", "fly"}, {"filter", "friends"}};
  for (const auto& kv : enum_keys) {
    json j = good;
    j.at("skills").at(0)[kv[0]] = kv[1];
    bad.push_back(j);
  }
  {
    json j = good;
    j.at("skills").at(0).erase("name");
    bad.push_back(j);
    j = good;
    j.at("skills").at(0)["name"] = "";
    bad.push_back(j);
    j = good;
    j.at("cell_tags").at(0)["tag"] = "";
    bad.push_back(j);
    j = good;
    j.at("cell_tags").at(0)["row"] = 8;  // Out of the 8x8 grid
    bad.push_back(j);
    j = good;
    j.at("agents").at(0)["skills"] = json({"", "", ""});  // More than kMaxSkillSlots
    bad.push_back(j);
  }
  for (const json& j : bad) {
    ASSERT_THROW(SnapshotFromJson(j.dump()), std::runtime_error);
  }
}

// =============================================================================
// Level JSON errors: a hand-authored level gets a message that says where
// =============================================================================

namespace {

// A valid 8x8 snapshot as JSON with an extra skill "frost" (last in "skills"),
// a zone and an agent tag, for the tests below to break.
json LevelJson() {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  SkillConfig frost;
  frost.name = "frost";
  frost.range = 2;
  frost.tags = {{"chilled", 2}};
  env.GetMutableSkillBook().Define(frost);
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[0];
  env.ApplyTagTo(a->GetId(), "burning", 3);
  env.SetCellTag({2, 2}, "wet", kPermanentTag);
  return json::parse(SnapshotToJson(env.SaveSnapshot()));
}

json& Frost(json& j) { return j.at("skills").at(j.at("skills").size() - 1); }

std::string FrostSection(const json& j) {
  return "skills[" + std::to_string(j.at("skills").size() - 1) + "] ('frost')";
}

// SnapshotFromJson(j) must throw a std::runtime_error (not a raw nlohmann
// exception) whose message contains every one of `needles`.
void AssertJsonErrorMentions(const json& j, const std::vector<std::string>& needles) {
  std::string what;
  bool caught = false;
  try {
    SnapshotFromJson(j.dump());
  } catch (const std::runtime_error& e) {
    caught = true;
    what = e.what();
  }
  if (!caught) throw std::runtime_error("expected a std::runtime_error mentioning " + needles[0]);
  for (const std::string& needle : needles) {
    if (what.find(needle) == std::string::npos) {
      throw std::runtime_error("error \"" + what + "\" does not mention \"" + needle + "\"");
    }
  }
}

}  // namespace

TEST(TestJsonLevelHelperLoads) { SnapshotFromJson(LevelJson().dump()); }

TEST(TestJsonRejectsNegativeSkillNumbers) {
  const char* fields[][2] = {{"cooldown", "cooldown"}, {"range", "range"},
                             {"distance", "motion_distance"}, {"root_steps", "root_steps"},
                             {"damage", "damage"}};
  for (const auto& f : fields) {
    json j = LevelJson();
    Frost(j)[f[0]] = -1;
    AssertJsonErrorMentions(j, {"skill 'frost': " + std::string(f[1]) + " must be >= 0 (got -1)"});
  }
}

TEST(TestJsonFriendlyFireAndSelfFlags) {
  json j = LevelJson();
  Frost(j)["friendly_fire"] = false;
  Frost(j)["self_motion"] = false;
  Snapshot s = SnapshotFromJson(j.dump());
  const SkillConfig& frost = s.skills.back();
  ASSERT_EQ(frost.name, std::string("frost"));
  ASSERT_FALSE(frost.friendly_fire);
  ASSERT_TRUE(frost.self_tags);  // Absent: true
  ASSERT_FALSE(frost.self_motion);
  ASSERT_TRUE(frost.self_root);

  for (const char* key : {"friendly_fire", "self_tags", "self_motion", "self_root", "self_damage"}) {
    j = LevelJson();
    Frost(j)[key] = "no";
    AssertJsonErrorMentions(j, {FrostSection(j) + ": " + key + ": type must be boolean"});
  }
}

TEST(TestJsonDamageKeys) {
  json j = LevelJson();
  Frost(j)["damage"] = 2;
  Frost(j)["self_damage"] = false;
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.skills.back().damage, 2);
  ASSERT_FALSE(s.skills.back().self_damage);
  Frost(j).erase("damage");
  Frost(j).erase("self_damage");
  s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.skills.back().damage, 0);  // Absent: 0 / true
  ASSERT_TRUE(s.skills.back().self_damage);
}

// The default attack is fixed: a level cannot define a skill named "attack".
TEST(TestJsonRejectsTheAttackSkill) {
  json j = LevelJson();
  j.at("skills").push_back(json{{"name", "attack"}, {"damage", 3}});
  AssertJsonErrorMentions(j, {"skills[" + std::to_string(j.at("skills").size() - 1) + "]",
                              "'attack' is the fixed default skill"});
}

// v3 / v4 files wrote "" for an empty slot: it loads as the attack.
TEST(TestJsonEmptySlotsLoadAsTheAttack) {
  json j = LevelJson();
  j.at("agents").at(0)["skills"] = json({"", ""});
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  Companion* c = FirstCompanion(env);
  ASSERT_EQ(c->GetSkill(0), std::string(kDefaultSkill));
  ASSERT_EQ(c->GetSkill(1), std::string(kDefaultSkill));
  // And a save writes the attack back by name, never "".
  json saved = json::parse(SnapshotToJson(env.SaveSnapshot()));
  ASSERT_EQ(saved.at("agents").at(0).at("skills"), json({"attack", "attack"}));
}

TEST(TestJsonErrorsNameTheSection) {
  json j = LevelJson();
  j.at("cell_tags").at(0).erase("tag");
  AssertJsonErrorMentions(j, {"cell_tags[0]: key 'tag' not found"});

  j = LevelJson();
  Frost(j)["tags"] = "burning";
  AssertJsonErrorMentions(j, {FrostSection(j) + ": tags: type must be array, but is string"});

  j = LevelJson();
  Frost(j)["range"] = "3";
  AssertJsonErrorMentions(j, {FrostSection(j) + ": range: type must be number, but is string"});

  j = LevelJson();
  j["skills"] = json::array({nullptr});
  AssertJsonErrorMentions(j, {"skills[0]", "null"});

  j = LevelJson();
  j.at("skills").at(1)["name"] = 7;
  AssertJsonErrorMentions(j, {"skills[1]: name: type must be string, but is number"});

  j = LevelJson();
  Frost(j).at("tags").at(0).erase("tag");
  AssertJsonErrorMentions(j, {FrostSection(j) + ": tags[0]: key 'tag' not found"});

  j = LevelJson();
  j.at("agents").at(0).at("tags").at(0)["duration"] = "3";
  AssertJsonErrorMentions(j, {"agents[0].tags[0]: duration: type must be number, but is string"});

  j = LevelJson();
  j.at("agents").at(0).erase("health");
  AssertJsonErrorMentions(j, {"agents[0]", "key 'health' not found"});

  j = LevelJson();
  j.at("grid")["rows"] = "8";
  AssertJsonErrorMentions(j, {"grid", "type must be number, but is string"});

  AssertJsonErrorMentions(json("not an object"), {"type must be object"});
  ASSERT_THROW(SnapshotFromJson("{ not json"), std::runtime_error);
}

TEST(TestJsonRejectsUnknownKeys) {
  json j = LevelJson();
  Frost(j)["motion_distance"] = 2;
  AssertJsonErrorMentions(
      j, {"skill 'frost': unknown key 'motion_distance' (did you mean 'distance'?)"});

  j = LevelJson();
  Frost(j)["colour"] = "blue";
  AssertJsonErrorMentions(j, {"skill 'frost': unknown key 'colour'"});
  bool hinted = false;
  try {
    SnapshotFromJson(j.dump());
  } catch (const std::runtime_error& e) {
    hinted = std::string(e.what()).find("did you mean") != std::string::npos;
  }
  ASSERT_FALSE(hinted);

  j = LevelJson();
  j.at("cell_tags").at(0)["ticks"] = 2;
  AssertJsonErrorMentions(j, {"cell_tags[0]: unknown key 'ticks'"});

  j = LevelJson();
  Frost(j).at("tags").at(0)["ticks"] = 2;
  AssertJsonErrorMentions(j, {"skill 'frost': tags[0]: unknown key 'ticks'"});

  j = LevelJson();
  j.at("agents").at(0).at("tags").at(0)["ticks"] = 2;
  AssertJsonErrorMentions(j, {"agents[0].tags[0]: unknown key 'ticks'"});
}

TEST(TestJsonRejectsUnknownStatus) {
  json j = LevelJson();
  j.at("agents").at(0)["statuses"] =
      json::array({json{{"status_type", "frozen"}, {"duration", 2}}});
  AssertJsonErrorMentions(j, {"agents[0].statuses[0]", "'frozen'"});
  // Slowed was removed: "slowed" (any case) and "slow" are unknown now.
  for (const char* removed : {"slowed", "Slowed", "slow"}) {
    j.at("agents").at(0)["statuses"] =
        json::array({json{{"status_type", removed}, {"duration", 2}}});
    AssertJsonErrorMentions(j, {"agents[0].statuses[0]", "unknown status '" + std::string(removed)});
  }

  j.at("agents").at(0)["statuses"] =
      json::array({json{{"status_type", "None"}, {"duration", 0}},
                   json{{"status_type", "STUNNED"}, {"duration", 2}}});
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.agents[0].statuses.size(), 2u);
  ASSERT_EQ(s.agents[0].statuses[1].type, static_cast<int>(StatusType::Stunned));
}

// =============================================================================
// Hardening: grid bounds, arrays, strict enum names
// =============================================================================

namespace {

// An AggroEnv snapshot as JSON: an FSM enemy with a patrol path.
json AggroJson() {
  AggroEnv env(10, 2, EnemyType::Zombie, 42, 0, 100);
  env.Reset(42);
  return json::parse(SnapshotToJson(env.SaveSnapshot()));
}

json& FsmAgent(json& j) {
  for (json& agent : j.at("agents")) {
    if (!agent.at("fsm").is_null()) return agent;
  }
  throw std::runtime_error("no FSM agent in the AggroEnv snapshot");
}

}  // namespace

TEST(TestJsonRejectsBadGridDimensions) {
  const int dims[][2] = {{0, 8}, {8, 0}, {-1, 8}, {8, -3}};
  for (const auto& d : dims) {
    json j = LevelJson();
    j.at("grid")["rows"] = d[0];
    j.at("grid")["cols"] = d[1];
    AssertJsonErrorMentions(j, {"grid", "must be > 0"});
  }
  // 65536 * 65536 overflows an int; 2048 * 1024 is just too big.
  const int huge[][2] = {{65536, 65536}, {2048, 1024}, {2147483647, 2}};
  for (const auto& d : huge) {
    json j = LevelJson();
    j.at("grid")["rows"] = d[0];
    j.at("grid")["cols"] = d[1];
    j.at("grid")["cells"] = json::array();
    AssertJsonErrorMentions(j, {"grid", "too many cells"});
  }
  json j = LevelJson();
  j.at("grid")["rows"] = 1024;  // 1024 * 1024 is at the cap: fine
  j.at("grid")["cols"] = 1024;
  j.at("grid")["cells"] = json::array();
  j["cell_tags"] = json::array();
  ASSERT_EQ(SnapshotFromJson(j.dump()).cells.size(), 1024u * 1024u);
}

TEST(TestJsonRejectsCellsOutsideGrid) {
  // LevelJson is 8x8; entry 5 is (0, 5).
  const int bad[][2] = {{8, 0}, {0, 8}, {-1, 0}, {0, -1}, {1, -1}, {100000, 100000}};
  for (const auto& rc : bad) {
    json j = LevelJson();
    j.at("grid").at("cells").at(5)["row"] = rc[0];
    j.at("grid").at("cells").at(5)["col"] = rc[1];
    AssertJsonErrorMentions(j, {"grid.cells[5]: (" + std::to_string(rc[0]) + ", " +
                                std::to_string(rc[1]) + ") outside 8x8"});
  }
  json j = LevelJson();
  j.at("grid")["rows"] = 6;
  j.at("grid")["cols"] = 9;
  j.at("grid")["cells"] = json::array({json{{"row", 5}, {"col", 8}, {"cell_kind", "Wall"},
                                            {"cell_origin", "Default"}},
                                       json{{"row", 6}, {"col", 0}, {"cell_kind", "Wall"},
                                            {"cell_origin", "Default"}}});
  j["cell_tags"] = json::array();
  AssertJsonErrorMentions(j, {"grid.cells[1]: (6, 0) outside 6x9"});
  j.at("grid").at("cells").erase(1);
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.cells[5 * 9 + 8].kind == CellKind::Wall);
}

TEST(TestJsonRequiresArrays) {
  json j = LevelJson();
  j.at("grid")["cells"] = json::object({{"a", j.at("grid").at("cells").at(0)}});
  AssertJsonErrorMentions(j, {"grid", "cells: type must be array, but is object"});

  j = LevelJson();
  j["annotations"] = json::object();
  AssertJsonErrorMentions(j, {"annotations: type must be array, but is object"});
  j.erase("annotations");  // Absent (v1) still loads
  SnapshotFromJson(j.dump());

  j = AggroJson();
  SnapshotFromJson(j.dump());  // Sanity
  json& fsm = FsmAgent(j).at("fsm");
  fsm["patrol_path"] = json::object({{"p", json{{"row", 1}, {"col", 1}}}});
  AssertJsonErrorMentions(j, {"agents[", "patrol_path: type must be array, but is object"});
}

TEST(TestJsonRejectsUnknownEnumNames) {
  // The key, a bad value, and the section the error must name.
  struct Case {
    const char* key;
    const char* bad;
    const char* where;
  };
  const Case cases[] = {
      {"cell_kind", "Lava", "grid.cells[3]"},     {"cell_origin", "Cave", "grid.cells[3]"},
      {"agent_type", "Dragon", "agents[0]"},      {"faction", "FOE", "agents[0]"},
      {"direction", "North", "agents[0]"},        {"color", "Purple", "agents[0]"},
  };
  for (const Case& c : cases) {
    json j = LevelJson();
    json& target = std::string(c.where) == "agents[0]" ? j.at("agents").at(0)
                                                      : j.at("grid").at("cells").at(3);
    target[c.key] = c.bad;
    AssertJsonErrorMentions(j, {c.where, "'" + std::string(c.bad) + "'"});
  }

  json j = LevelJson();
  j["annotations"] = json::array({json{{"target", "Cell"}, {"tag", "Goal"},
                                       {"pos", json{{"row", 1}, {"col", 1}}}}});
  AssertJsonErrorMentions(j, {"annotations[0]", "'Goal'"});
  j.at("annotations").at(0)["tag"] = "SynchroGoal";
  j.at("annotations").at(0)["target"] = "Wall";
  AssertJsonErrorMentions(j, {"annotations[0]", "'Wall'"});

  j = AggroJson();
  FsmAgent(j).at("fsm")["state_type"] = "Sleeping";
  AssertJsonErrorMentions(j, {"agents[", "'Sleeping'"});

  j = LevelJson();
  j["effects"] = json::array({json{{"effect_name", "hit"}, {"target_type", 0},
                                   {"target_cell", json{{"row", 1}, {"col", 1}}},
                                   {"target_actor_id", -1}, {"target_actors", json::array()},
                                   {"direction", "North"}, {"ticks_remaining", 1},
                                   {"in_telegraph", false}, {"loops_remaining", 0},
                                   {"source_id", -1}}});
  AssertJsonErrorMentions(j, {"effects[0]", "'North'"});
  j.at("effects").at(0)["direction"] = "Left";
  SnapshotFromJson(j.dump());
}

TEST(TestJsonAcceptsEveryWrittenEnumName) {
  // Every name the writers emit loads back (including "Object" and FSM
  // "None"), and legacy v1 "Synchro"/"Target" cells become Floor.
  json j = LevelJson();
  const char* kinds[] = {"Floor", "Wall", "Hazard", "HealArea", "Synchro", "Target"};
  const char* origins[] = {"Default", "Room", "Corridor", "Obstacle"};
  for (int i = 0; i < 6; ++i) j.at("grid").at("cells").at(10 + i)["cell_kind"] = kinds[i];
  for (int i = 0; i < 4; ++i) j.at("grid").at("cells").at(20 + i)["cell_origin"] = origins[i];
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.cells[13].kind == CellKind::HealArea);
  ASSERT_TRUE(s.cells[14].kind == CellKind::Floor);
  ASSERT_TRUE(s.cells[15].kind == CellKind::Floor);
  ASSERT_TRUE(s.cells[22].origin == CellOrigin::Corridor);

  const char* types[] = {"Object", "Actor", "Agent", "AgentFSM", "Companion", "Player",
                         "NPCCompanion"};
  for (int i = 0; i < 7; ++i) {
    j = LevelJson();
    j.at("agents").at(0)["agent_type"] = types[i];
    ASSERT_EQ(SnapshotFromJson(j.dump()).agents[0].type, i);
  }
  const char* factions[] = {"COMPANION", "ENEMY", "NEUTRAL"};
  const char* directions[] = {"Up", "Down", "Left", "Right"};
  const char* colors[] = {"None", "Red", "Green", "Blue"};
  for (int i = 0; i < 4; ++i) {
    j = LevelJson();
    j.at("agents").at(0)["faction"] = factions[i % 3];
    j.at("agents").at(0)["direction"] = directions[i];
    j.at("agents").at(0)["color"] = colors[i];
    s = SnapshotFromJson(j.dump());
    ASSERT_EQ(s.agents[0].faction, i % 3);
    ASSERT_EQ(s.agents[0].direction, i);
    ASSERT_EQ(s.agents[0].color, i);
  }
  const char* states[] = {"None", "Patrol", "Aggro", "ReturnToPatrol", "Telegraph", "Attack",
                          "Recovery"};
  for (int i = 0; i < 7; ++i) {
    j = AggroJson();
    FsmAgent(j).at("fsm")["state_type"] = states[i];
    s = SnapshotFromJson(j.dump());
    bool found = false;
    for (const AgentSnapshot& a : s.agents) {
      if (a.has_fsm) found = static_cast<int>(a.fsm.state_type) == i;
    }
    ASSERT_TRUE(found);
  }
  for (int t = 0; t < static_cast<int>(SemanticTag::_Count); ++t) {
    j = LevelJson();
    const std::string name = SemanticTagToString(static_cast<SemanticTag>(t));
    j["annotations"] = json::array({json{{"target", "Agent"}, {"tag", name}, {"agent_id", 0}}});
    ASSERT_TRUE(SnapshotFromJson(j.dump()).annotations[0].tag == static_cast<SemanticTag>(t));
  }
}

TEST(TestJsonEmptySkillNameSaysWhichSkill) {
  json j = LevelJson();
  j.at("skills").at(1)["name"] = "";
  AssertJsonErrorMentions(j, {"skills[1]", "without a name"});
}

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
