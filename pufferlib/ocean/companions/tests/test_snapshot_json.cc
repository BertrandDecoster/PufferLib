// Copyright 2024
// Test suite for JSON Snapshot serialization

#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/core/types.h"
#include "../src/core/cell.h"
#include "../src/core/fsm/enemies.h"
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
  size_t pos = tampered.find("\"version\": 7");
  ASSERT_TRUE(pos != std::string::npos);
  tampered.replace(pos, 12, "\"version\": 8");  // One above current

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
  ASSERT_EQ(j.at("version").get<int>(), 7);
  // v6: the env's context rules, always written
  ASSERT_EQ(j.at("context_skills"),
            json::array({json{{"condition", "adjacent_downed_ally"}, {"slot", 0},
                              {"skill", "revive"}}}));
  for (const json& skill : j.at("skills")) {
    const bool revive = skill.at("name").get<std::string>() == "revive";
    ASSERT_EQ(skill.at("affects_downed").get<bool>(), revive);
    ASSERT_EQ(skill.at("revive_percent").get<int>(), revive ? 50 : 0);
  }
  const json& agent = j.at("agents").at(0);
  ASSERT_EQ(agent.at("downed").get<bool>(), false);  // v5: downs
  ASSERT_EQ(agent.at("times_downed").get<int>(), 0);
  ASSERT_EQ(j.at("max_downs").get<int>(), 3);
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

// Every form keeps the builtin revive a revive (a save + reload once turned it
// into a no-op).
TEST(TestJsonKeepsTheRevive) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  SynchroEnv other(8, 8, 1, 1, 0, 7);
  other.LoadSnapshot(JsonRoundTrip(env.SaveSnapshot()));
  const SkillConfig* revive = other.GetSkillBook().Find("revive");
  ASSERT_TRUE(revive != nullptr);
  ASSERT_TRUE(revive->affects_downed);
  ASSERT_EQ(revive->revive_percent, 50);
  AssertSkillEq(*revive, *SkillBook().Find("revive"));
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

// Slots always hold a real skill: a slot naming a skill that is neither a
// builtin nor one of the snapshot's skills is rejected ("" still means attack).
TEST(TestJsonUnknownSlotSkillIsRejected) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  const json good = json::parse(SnapshotToJson(env.SaveSnapshot()));
  json j = good;
  j.at("agents").at(0)["skills"] = json({"", "meteor"});
  AssertJsonErrorMentions(j, {"agent #0", "skills[1]", "\"meteor\"", "unknown skill"});

  // The struct path too (LoadSnapshot validates before any change)
  Snapshot snap = env.SaveSnapshot();
  snap.agents[0].skills = {"meteor"};
  ASSERT_THROW(env.LoadSnapshot(snap), std::runtime_error);
  ASSERT_EQ(FirstCompanion(env)->GetSkill(0), std::string(kDefaultSkill));

  // Builtins, the explicit default and the snapshot's own skills all load
  j = good;
  j["skills"] = json::array({json{{"name", "meteor"}}});
  j.at("agents").at(0)["skills"] = json({"meteor", "fireball"});
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  ASSERT_EQ(FirstCompanion(env)->GetSkill(0), std::string("meteor"));
  ASSERT_EQ(FirstCompanion(env)->GetSkill(1), std::string("fireball"));
  j.at("agents").at(0)["skills"] = json({kDefaultSkill, ""});
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  ASSERT_EQ(FirstCompanion(env)->GetSkill(1), std::string(kDefaultSkill));
}

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

// An enemy class is restored by its "kind": a misspelled one (or one on an
// agent that has no FSM) is rejected, not loaded as a plain AgentFSM.
TEST(TestJsonRejectsUnknownEnemyKind) {
  json j = AggroJson();
  ASSERT_EQ(FsmAgent(j).at("kind").get<std::string>(), std::string("Zombie"));
  SnapshotFromJson(j.dump());  // Sanity
  for (const char* bad : {"zombie", "Zombi", "Troll"}) {
    FsmAgent(j)["kind"] = bad;
    AssertJsonErrorMentions(j, {"agent #", "unknown kind \"" + std::string(bad) + "\"",
                                "Zombie, Goblin, Dragon"});
  }
  j = LevelJson();
  j.at("agents").at(0)["kind"] = "Zombie";
  AssertJsonErrorMentions(j, {"agent #0", "only for an AgentFSM agent"});
}

// Downs (v5): max_downs >= 1; a downed agent is a companion at 0 HP that went
// down at least once; times_downed >= 0.
TEST(TestJsonRejectsBadDowns) {
  json j = LevelJson();
  ASSERT_EQ(j.at("agents").at(0).at("agent_type").get<std::string>(), std::string("Player"));  // A Companion
  j["max_downs"] = 0;
  AssertJsonErrorMentions(j, {"max_downs", ">= 1", "(got 0)"});
  j = LevelJson();
  j.at("agents").at(0)["downed"] = true;  // health is not 0
  j.at("agents").at(0)["times_downed"] = 1;
  AssertJsonErrorMentions(j, {"agent #0", "downed", "0 HP"});
  j = LevelJson();
  j.at("agents").at(0)["times_downed"] = -1;
  AssertJsonErrorMentions(j, {"agent #0", "times_downed", ">= 0"});
  j = LevelJson();
  j.at("agents").at(0)["downed"] = true;  // Down, but never went down
  j.at("agents").at(0)["health"] = 0;
  j.at("agents").at(0)["times_downed"] = 0;
  AssertJsonErrorMentions(j, {"agent #0", "times_downed", ">= 1 when downed"});
  j = LevelJson();
  j.at("agents").at(0)["downed"] = true;  // With a status
  j.at("agents").at(0)["health"] = 0;
  j.at("agents").at(0)["times_downed"] = 1;
  j.at("agents").at(0)["statuses"] = json::array({json{{"status_type", "stunned"}, {"duration", 2}}});
  AssertJsonErrorMentions(j, {"agent #0", "downed with statuses, a downed companion has none"});
  j = LevelJson();
  j.at("agents").at(0)["downed"] = "yes";
  AssertJsonErrorMentions(j, {"agents[0]: downed: type must be boolean"});
  j = LevelJson();
  j["max_downs"] = "three";
  AssertJsonErrorMentions(j, {"max_downs: type must be number"});
  for (const char* key : {"downed", "times_downed"}) {
    j = AggroJson();
    json& enemy = FsmAgent(j);
    enemy["health"] = 0;
    enemy[key] = key == std::string("downed") ? json(true) : json(1);
    if (key == std::string("downed")) enemy["times_downed"] = 1;
    AssertJsonErrorMentions(j, {"agent #", "downed / times_downed on a non-companion (only a companion goes down)"});
  }
}

// Only companion entries carry the downs keys (always); an enemy's has none.
TEST(TestJsonDownsKeysOnlyOnCompanions) {
  json j = AggroJson();
  int companions = 0;
  for (const json& agent : j.at("agents")) {
    const std::string type = agent.at("agent_type").get<std::string>();
    const bool companion = type == "Companion" || type == "Player" || type == "NPCCompanion";
    ASSERT_EQ(agent.contains("downed"), companion);
    ASSERT_EQ(agent.contains("times_downed"), companion);
    if (companion) ++companions;
  }
  ASSERT_TRUE(companions > 0);
  ASSERT_FALSE(FsmAgent(j).contains("downed"));
  SnapshotFromJson(j.dump());  // And it loads
}

// A v4 level (no downs keys) loads: nobody down, the default max_downs.
TEST(TestJsonV4SnapshotLoadsWithoutDowns) {
  json j = LevelJson();
  j["version"] = 4;
  j.erase("max_downs");
  for (json& agent : j.at("agents")) {
    agent.erase("downed");
    agent.erase("times_downed");
  }
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.max_downs, 3);
  ASSERT_FALSE(s.agents[0].downed);
  ASSERT_EQ(s.agents[0].times_downed, 0);
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  env.LoadSnapshot(s);
  ASSERT_EQ(env.GetMaxDowns(), BaseEnv::kDefaultMaxDowns);
  ASSERT_EQ(env.GetDowns(), 0);
}

// Context skills (v6): absent = the default rules, present = exactly them
// (empty included); they round-trip.
TEST(TestJsonContextSkills) {
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  const std::vector<ContextSkillRule> rules = {{adj, 1, "frost"}, {adj, 0, "revive"}};
  json j = LevelJson();
  Frost(j)["cooldown"] = 0;
  j["context_skills"] = json::array({json{{"condition", "adjacent_downed_ally"}, {"slot", 1},
                                          {"skill", "frost"}},
                                     json{{"condition", "adjacent_downed_ally"}, {"slot", 0},
                                          {"skill", "revive"}}});
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.context_skills.has_value());
  ASSERT_TRUE(*s.context_skills == rules);
  ASSERT_TRUE(*JsonRoundTrip(s).context_skills == rules);
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  env.LoadSnapshot(s);
  ASSERT_TRUE(env.GetContextSkills() == rules);

  j["context_skills"] = json::array();
  s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.context_skills.has_value() && s.context_skills->empty());
  ASSERT_EQ(json::parse(SnapshotToJson(s)).at("context_skills"), json::array());
  env.LoadSnapshot(s);
  ASSERT_TRUE(env.GetContextSkills().empty());

  j.erase("context_skills");
  s = SnapshotFromJson(j.dump());
  ASSERT_FALSE(s.context_skills.has_value());
  ASSERT_FALSE(json::parse(SnapshotToJson(s)).contains("context_skills"));  // Stays absent
  env.LoadSnapshot(s);
  ASSERT_TRUE(env.GetContextSkills() == DefaultContextSkills());
}

TEST(TestJsonRejectsBadContextSkills) {
  auto with_rule = [](json rule) {
    json j = LevelJson();
    j["context_skills"] = json::array({json{{"condition", "adjacent_downed_ally"}, {"slot", 0},
                                            {"skill", "revive"}},
                                       rule});
    return j;
  };
  const json good{{"condition", "adjacent_downed_ally"}, {"slot", 1}, {"skill", "revive"}};
  SnapshotFromJson(with_rule(good).dump());  // Sanity

  json rule = good;
  rule["condition"] = "next_to_a_friend";
  AssertJsonErrorMentions(with_rule(rule), {"context_skills[1]", "condition",
                                            "unknown context condition 'next_to_a_friend'"});
  rule = good;
  rule["slot"] = kMaxSkillSlots;
  AssertJsonErrorMentions(with_rule(rule), {"context_skills[1]", "slot: out of range"});
  rule = good;
  rule["skill"] = "meteor";
  AssertJsonErrorMentions(with_rule(rule), {"context_skills[1]", "unknown skill 'meteor'"});
  rule = good;
  rule["skill"] = "frost";  // LevelJson's frost, given a cooldown
  json j = with_rule(rule);
  Frost(j)["cooldown"] = 2;
  AssertJsonErrorMentions(j, {"context_skills[1]", "'frost' has cooldown 2"});
  rule = good;
  rule["when"] = "always";
  AssertJsonErrorMentions(with_rule(rule), {"context_skills[1]: unknown key 'when'"});
  for (const char* key : {"condition", "slot", "skill"}) {
    rule = good;
    rule.erase(key);
    AssertJsonErrorMentions(with_rule(rule),
                            {"context_skills[1]: key '" + std::string(key) + "' not found"});
  }
  rule = good;
  rule["slot"] = "0";
  AssertJsonErrorMentions(with_rule(rule), {"context_skills[1]: slot: type must be number"});
  j = LevelJson();
  j["context_skills"] = json::object();
  AssertJsonErrorMentions(j, {"context_skills: type must be array"});

  // Absent: the default rules, checked against the level's book too
  j = LevelJson();
  for (json& skill : j.at("skills")) {
    if (skill.at("name") == "revive") skill["cooldown"] = 3;
  }
  AssertJsonErrorMentions(j, {"context_skills[0]", "'revive' has cooldown 3"});
  j["context_skills"] = json::array();  // No rule uses it: it loads
  SnapshotFromJson(j.dump());
}

TEST(TestJsonRejectsBadReviveFields) {
  json j = LevelJson();
  Frost(j)["revive_percent"] = 50;  // Without affects_downed
  AssertJsonErrorMentions(j, {"skill 'frost'", "revive_percent needs affects_downed"});
  j = LevelJson();
  Frost(j)["affects_downed"] = "yes";
  AssertJsonErrorMentions(j, {"affects_downed: type must be boolean"});
  j = LevelJson();
  Frost(j).erase("tags");
  Frost(j)["affects_downed"] = true;
  Frost(j)["revive_percent"] = 101;
  AssertJsonErrorMentions(j, {"skill 'frost'", "revive_percent must be in [0, 100] (got 101)"});
  Frost(j)["revive_percent"] = 100;
  const Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.skills.back().affects_downed);
  ASSERT_EQ(s.skills.back().revive_percent, 100);
  j.at("agents").at(0)["max_health"] = 0;
  j.at("agents").at(0)["health"] = 0;
  AssertJsonErrorMentions(j, {"agent #0", "max_health must be >= 1 (got 0)"});
}

// A v5 level (no context skills, no revive keys) loads: the default rules,
// its skills affect the standing.
TEST(TestJsonV5SnapshotLoadsWithTheDefaultRules) {
  json j = LevelJson();
  j["version"] = 5;
  j.erase("context_skills");
  for (json& skill : j.at("skills")) {
    if (skill.at("name") == "revive") continue;  // v5 files had no builtin revive
    skill.erase("affects_downed");
    skill.erase("revive_percent");
  }
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_FALSE(s.context_skills.has_value());
  ASSERT_FALSE(s.skills.back().affects_downed);
  ASSERT_EQ(s.skills.back().revive_percent, 0);
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  ASSERT_TRUE(env.SetContextSkills({}));
  env.LoadSnapshot(s);
  ASSERT_TRUE(env.GetContextSkills() == DefaultContextSkills());
}

// Every listed kind survives a save / load with its class.
TEST(TestEnemyKindsRoundTrip) {
  for (const EnemyKind& kind : EnemyKinds()) {
    json j = AggroJson();
    FsmAgent(j)["kind"] = kind.name;
    AggroEnv env(10, 2, EnemyType::Zombie, 42, 0, 100);
    env.LoadSnapshot(SnapshotFromJson(j.dump()));
    json saved = json::parse(SnapshotToJson(env.SaveSnapshot()));
    ASSERT_EQ(FsmAgent(saved).at("kind").get<std::string>(), std::string(kind.name));
  }
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

const std::nullopt_t kAbsent = std::nullopt;

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

// A 2x2 world with one companion and every v7 field off its default; cells
// with every mix of present and absent fields.
Snapshot V7Snapshot() {
  Snapshot s;
  s.rows = 2;
  s.cols = 2;
  s.cells.resize(4);
  AgentSnapshot a;
  a.type = static_cast<int>(ObjectType::Companion);
  a.position = {1, 1};
  a.prev_position = {1, 1};
  a.weak_to = {{"wet", "electrified"}, {"oil", "burning"}};
  a.immune = {"burning", "stunned"};
  s.agents.push_back(a);
  AgentSnapshot enemy;  // Any agent type carries them
  enemy.id = 1;
  enemy.type = static_cast<int>(ObjectType::Agent);
  enemy.faction = static_cast<int>(Faction::ENEMY);
  enemy.position = {0, 0};
  enemy.prev_position = {0, 0};
  enemy.weak_to = {{"wet", "chilled"}};
  s.agents.push_back(enemy);
  s.zones = {{"burning", MakeZone(2, 4, "smoke", 1)},
             {"smoke", MakeZone(kPermanentTag, 2, "burning", 0)},
             {"wet", ZoneDef{}}};
  s.reactions = {MakeReaction("wet", "electrified", "shocked", {"wet"}, 1, true, "wet"),
                 MakeReaction("wet", "chilled", "stunned", {}, 0, false, "")};
  s.tag_statuses = {{"stunned", StatusType::Stunned, 3}, {"rooted", StatusType::Rooted, 1}};
  s.cell_tags = {MakeCell({0, 1}, "burning", 3, 2, std::string("smoke"), 1),
                 MakeCell({1, 0}, "wet", kAbsent, kAbsent, kAbsent, kAbsent),
                 MakeCell({0, 0}, "oil", kAbsent, 7, std::string(), kAbsent)};
  return s;
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
  for (size_t i = 0; i < a.cell_tags.size(); ++i) {
    const CellTagSnapshot& x = a.cell_tags[i];
    const CellTagSnapshot& y = b.cell_tags[i];
    ASSERT_TRUE(x.cell == y.cell);
    ASSERT_EQ(x.tag, y.tag);
    ASSERT_TRUE(x.duration == y.duration);
    ASSERT_TRUE(x.steps == y.steps);
    ASSERT_TRUE(x.then == y.then);
    ASSERT_TRUE(x.damage == y.damage);
  }
}

// A zone cell of `env` by names: "tag duration steps then damage"
std::string Describe(const BaseEnv& env, Position p) {
  const BaseEnv::CellTag c = env.GetCellTag(p);
  if (c.tag == kInvalidTag) return "-";
  return env.GetTagTable().Name(c.tag) + " " + std::to_string(c.duration) + " " +
         std::to_string(c.steps) + " " +
         (c.then == kInvalidTag ? std::string() : env.GetTagTable().Name(c.then)) + " " +
         std::to_string(c.damage);
}

}  // namespace

TEST(TestJsonV7FieldsRoundTrip) {
  const Snapshot s = V7Snapshot();
  const Snapshot back = JsonRoundTrip(s);
  AssertV7Eq(s, back);
  // Binary and JSON agree
  AssertV7Eq(s, Snapshot::Deserialize(back.Serialize()));
}

// The shapes a level tool writes: the table keyed by tag, the rules as
// objects, statuses by name, the cells' fields only when present.
TEST(TestJsonV7Keys) {
  json j = json::parse(SnapshotToJson(V7Snapshot()));
  ASSERT_EQ(j.at("version").get<int>(), 7);
  ASSERT_EQ(j.at("zones").at("burning"),
            (json{{"duration", 2}, {"steps", 4}, {"then", "smoke"}, {"damage", 1}}));
  ASSERT_EQ(j.at("zones").at("wet"),
            (json{{"duration", -1}, {"steps", -1}, {"then", ""}, {"damage", 0}}));
  ASSERT_EQ(j.at("reactions").at(0),
            (json{{"a", "wet"}, {"b", "electrified"}, {"result", "shocked"}, {"keep", {"wet"}},
                  {"damage", 1}, {"spread", true}, {"zone_becomes", "wet"}}));
  ASSERT_EQ(j.at("tag_statuses").at(1),
            (json{{"tag", "rooted"}, {"status", "rooted"}, {"steps", 1}}));
  const json& agent = j.at("agents").at(0);
  ASSERT_EQ(agent.at("weak_to"),
            json::array({json{{"zone", "wet"}, {"tag", "electrified"}},
                         json{{"zone", "oil"}, {"tag", "burning"}}}));
  ASSERT_EQ(agent.at("immune"), json({"burning", "stunned"}));
  ASSERT_FALSE(j.at("agents").at(1).contains("immune"));  // Written when there are some
  ASSERT_EQ(j.at("cell_tags").at(0),
            (json{{"row", 0}, {"col", 1}, {"tag", "burning"}, {"duration", 3}, {"steps", 2},
                  {"then", "smoke"}, {"damage", 1}}));
  ASSERT_EQ(j.at("cell_tags").at(1), (json{{"row", 1}, {"col", 0}, {"tag", "wet"}}));
  ASSERT_EQ(j.at("cell_tags").at(2),
            (json{{"row", 0}, {"col", 0}, {"tag", "oil"}, {"steps", 7}, {"then", ""}}));

  // A saved env writes every cell field, resolved
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  ASSERT_TRUE(env.DefineZone("burning", MakeZone(2, 4, "smoke", 1)));
  ASSERT_TRUE(env.SetCellTag({2, 3}, "burning"));
  j = json::parse(SnapshotToJson(env.SaveSnapshot()));
  ASSERT_EQ(j.at("cell_tags").at(0),
            (json{{"row", 2}, {"col", 3}, {"tag", "burning"}, {"duration", 2}, {"steps", 4},
                  {"then", "smoke"}, {"damage", 1}}));
  ASSERT_EQ(j.at("reactions"), json::array());
  ASSERT_EQ(j.at("tag_statuses"), json::array());
  ASSERT_FALSE(j.at("agents").at(0).contains("weak_to"));
}

// What a level tool writes: a zone table and bare cells. A cell without
// fields takes the table's for its tag (the defaults for a tag it does not
// define); a field present is the cell's own. Omitted table fields are the
// ZoneDef defaults.
TEST(TestJsonCellsTakeTheTablesFieldsTheyOmit) {
  json j = LevelJson();
  j["zones"] = json{{"burning", json{{"duration", 2}, {"steps", 4}, {"then", "smoke"},
                                     {"damage", 1}}},
                    {"smoke", json{{"steps", 2}}}};
  j["cell_tags"] = json::array(
      {json{{"row", 1}, {"col", 1}, {"tag", "burning"}},
       json{{"row", 1}, {"col", 2}, {"tag", "burning"}, {"steps", 2}, {"damage", 0}},
       json{{"row", 1}, {"col", 3}, {"tag", "burning"}, {"then", ""}, {"duration", -1}},
       json{{"row", 1}, {"col", 4}, {"tag", "smoke"}},
       json{{"row", 1}, {"col", 5}, {"tag", "wet"}}});
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  ASSERT_EQ(Describe(env, {1, 1}), std::string("burning 2 4 smoke 1"));
  ASSERT_EQ(Describe(env, {1, 2}), std::string("burning 2 2 smoke 0"));
  ASSERT_EQ(Describe(env, {1, 3}), std::string("burning -1 4  1"));
  ASSERT_EQ(Describe(env, {1, 4}), std::string("smoke -1 2  0"));
  ASSERT_EQ(Describe(env, {1, 5}), std::string("wet -1 -1  0"));
  ASSERT_TRUE(env.GetZoneDef("smoke") == MakeZone(kPermanentTag, 2, "", 0));
}

// The table loads before the cells wherever the file lists it: here the
// cells come first in the text.
TEST(TestJsonTheTableLoadsWhereverTheFileListsIt) {
  json j = LevelJson();
  j.erase("cell_tags");
  j.erase("zones");
  std::string text = j.dump();
  const std::string head = "{\"cell_tags\":[{\"row\":2,\"col\":2,\"tag\":\"burning\"}],";
  const std::string tail = ",\"zones\":{\"burning\":{\"steps\":3,\"damage\":2}}}";
  text = head + text.substr(1, text.size() - 2) + tail;
  ASSERT_TRUE(text.find("cell_tags") < text.find("\"zones\""));
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  env.LoadSnapshot(SnapshotFromJson(text));
  ASSERT_EQ(Describe(env, {2, 2}), std::string("burning -1 3  2"));
}

// A JSON level without the v7 keys has none of it; bare cells get the
// defaults (their duration kept).
TEST(TestJsonWithoutV7KeysLoadsWithNone) {
  SynchroEnv src(8, 8, 1, 1, 0, 42);
  ASSERT_TRUE(src.DefineZone("wet", MakeZone(5, 3, "ice", 2)));
  ASSERT_TRUE(src.SetReactions({MakeReaction("wet", "chilled", "stunned", {}, 0, false, "")}));
  ASSERT_TRUE(src.SetTagStatuses({{"stunned", StatusType::Stunned, 2}}));
  Agent* a = src.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(src.SetWeaknesses(a->GetId(), {{"wet", "electrified"}}));
  ASSERT_TRUE(src.SetImmunities(a->GetId(), {"wet"}));
  ASSERT_TRUE(src.SetCellTag({2, 3}, "wet", 2));
  json j = json::parse(SnapshotToJson(src.SaveSnapshot()));
  j["version"] = 6;
  j.erase("zones");
  j.erase("reactions");
  j.erase("tag_statuses");
  for (json& agent : j.at("agents")) {
    agent.erase("weak_to");
    agent.erase("immune");
  }
  for (json& zone : j.at("cell_tags")) {  // What a v6 file has: the duration
    zone.erase("steps");
    zone.erase("then");
    zone.erase("damage");
  }
  SynchroEnv env(8, 8, 1, 1, 0, 7);
  env.LoadSnapshot(src.SaveSnapshot());  // It had them all
  env.LoadSnapshot(SnapshotFromJson(j.dump()));
  ASSERT_TRUE(env.GetZoneDefs().empty());
  ASSERT_TRUE(env.GetReactions().empty());
  ASSERT_TRUE(env.GetTagStatuses().empty());
  Agent* b = env.GetMutableObjectManager().GetAllAgents()[0];
  ASSERT_TRUE(env.GetWeaknesses(b->GetId()).empty());
  ASSERT_TRUE(env.GetImmunities(b->GetId()).empty());
  ASSERT_EQ(Describe(env, {2, 3}), std::string("wet 2 -1  0"));
}

TEST(TestJsonRejectsBadV7Data) {
  json j = LevelJson();
  j["zones"] = json{{"burning", json{{"stepz", 3}}}};
  AssertJsonErrorMentions(j, {"zones['burning']", "unknown key 'stepz'"});
  j["zones"] = json::array();
  AssertJsonErrorMentions(j, {"zones", "object"});
  j["zones"] = json{{"burning", json{{"steps", "three"}}}};
  AssertJsonErrorMentions(j, {"zones['burning']: steps"});
  j["zones"] = json{{"burning", json{{"steps", 0}}}};
  AssertJsonErrorMentions(j, {"zones['burning']: steps"});
  j.erase("zones");

  j.at("cell_tags").at(0)["steps"] = 0;
  AssertJsonErrorMentions(j, {"zone at (2, 2): steps"});
  j.at("cell_tags").at(0)["steps"] = 2;
  j.at("cell_tags").at(0)["dammage"] = 1;
  AssertJsonErrorMentions(j, {"cell_tags[0]", "unknown key 'dammage'"});
  j.at("cell_tags").at(0).erase("dammage");

  j["reactions"] = json::array({json{{"a", "wet"}, {"b", "ice"}}});
  AssertJsonErrorMentions(j, {"reactions[0]", "key 'result' not found"});
  j["reactions"] = json::array({json{{"a", "wet"}, {"b", "ice"}, {"result", "x"}, {"keeps", {}}}});
  AssertJsonErrorMentions(j, {"reactions[0]", "unknown key 'keeps'"});
  j["reactions"] = json::array({json{{"a", "wet"}, {"b", "ice"}, {"result", "x"}, {"keep", {"fire"}}}});
  AssertJsonErrorMentions(j, {"reactions[0] ('wet' + 'ice'): keep: 'fire' is neither a nor b"});
  j["reactions"] = json::array({json{{"a", "wet"}, {"b", "ice"}, {"result", "x"}, {"damage", -1}}});
  AssertJsonErrorMentions(j, {"reactions[0]", "damage: negative"});
  j.erase("reactions");

  j["tag_statuses"] = json::array({json{{"tag", "stunned"}, {"status", "slowed"}}});
  AssertJsonErrorMentions(j, {"tag_statuses[0]", "unknown status 'slowed'"});
  j["tag_statuses"] = json::array({json{{"tag", "stunned"}, {"status", "none"}}});
  AssertJsonErrorMentions(j, {"tag_statuses[0]", "status"});
  j["tag_statuses"] = json::array({json{{"tag", "stunned"}, {"status", "Stunned"}, {"steps", 0}}});
  AssertJsonErrorMentions(j, {"tag_statuses[0] ('stunned'): steps"});
  j["tag_statuses"] = json::array({json{{"tag", "stunned"}}});
  AssertJsonErrorMentions(j, {"tag_statuses[0]", "key 'status' not found"});
  j.erase("tag_statuses");

  json& agent = j.at("agents").at(0);
  agent["weak_to"] = json::array({json{{"zone", "wet"}, {"tags", "electrified"}}});
  AssertJsonErrorMentions(j, {"agents[0].weak_to[0]", "unknown key 'tags'"});
  agent["weak_to"] = json::array({json{{"zone", "wet"}, {"tag", "x"}}, json{{"zone", "wet"}, {"tag", "x"}}});
  AssertJsonErrorMentions(j, {"agent #0", "weak_to[1] ('wet', 'x'): twice"});
  agent.erase("weak_to");
  agent["immune"] = json::array({"burning", 3});
  AssertJsonErrorMentions(j, {"agents[0]: immune"});
  agent["immune"] = json::array({"burning", ""});
  AssertJsonErrorMentions(j, {"agent #0", "immune[1] (''): tag: empty"});
  agent.erase("immune");
  SnapshotFromJson(j.dump());  // Back to a valid level

  // Statuses by name, case-insensitive like the agents'; steps default to 1,
  // the optional reaction fields to their defaults
  j["tag_statuses"] = json::array({json{{"tag", "stunned"}, {"status", "Stunned"}}});
  j["reactions"] = json::array({json{{"a", "wet"}, {"b", "ice"}, {"result", "x"}}});
  Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_TRUE(s.tag_statuses == (std::vector<TagStatusRule>{{"stunned", StatusType::Stunned, 1}}));
  ASSERT_TRUE(s.reactions == (std::vector<ReactionRule>{MakeReaction("wet", "ice", "x", {}, 0, false, "")}));
}

// Every object is strict, the root and the agents included: a misspelt key
// (a rule that would silently not load) is an error.
TEST(TestJsonRejectsUnknownRootAndAgentKeys) {
  for (const char* key : {"reaction", "tag_status", "zone", "tagStatuses", "Zones"}) {
    json j = LevelJson();
    j[key] = json::array();
    AssertJsonErrorMentions(j, {"snapshot: unknown key '" + std::string(key) + "'"});
  }
  for (const char* key : {"weakTo", "immunities", "weak", "has_fsm", "downs"}) {
    json j = LevelJson();
    j.at("agents").at(0)[key] = json::array();
    AssertJsonErrorMentions(j, {"agents[0]: unknown key '" + std::string(key) + "'"});
  }
  // Every key the writer emits is known, the optional ones included
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  ASSERT_TRUE(env.SetWeaknesses(env.GetMutableObjectManager().GetAllAgents()[0]->GetId(),
                                {{"wet", "electrified"}}));
  ASSERT_TRUE(env.SetImmunities(env.GetMutableObjectManager().GetAllAgents()[0]->GetId(), {"wet"}));
  SnapshotFromJson(SnapshotToJson(env.SaveSnapshot()));
  SnapshotFromJson(AggroJson().dump());  // An FSM enemy with a kind
}

// Integers are integers: a bool, a float or a number out of int's range is an
// error naming the key, never a silent conversion (true -> 1, 2.5 -> 2,
// 4294967295 -> -1).
TEST(TestJsonIntegersAreStrict) {
  const std::vector<json> not_ints = {true, 2.5, 4294967295.0, json(4294967295u),
                                      json(-2147483649LL), "3"};
  for (const json& v : not_ints) {
    json j = LevelJson();
    j["zones"] = json{{"burning", json{{"steps", v}}}};
    AssertJsonErrorMentions(j, {"zones['burning']: steps"});
    j = LevelJson();
    j.at("cell_tags").at(0)["damage"] = v;
    AssertJsonErrorMentions(j, {"cell_tags[0]: damage"});
    j = LevelJson();
    j["reactions"] =
        json::array({json{{"a", "wet"}, {"b", "ice"}, {"result", "x"}, {"damage", v}}});
    AssertJsonErrorMentions(j, {"reactions[0]: damage"});
    j = LevelJson();
    j["tag_statuses"] =
        json::array({json{{"tag", "stunned"}, {"status", "stunned"}, {"steps", v}}});
    AssertJsonErrorMentions(j, {"tag_statuses[0]: steps"});
    // Older fields too
    j = LevelJson();
    j.at("agents").at(0)["health"] = v;
    AssertJsonErrorMentions(j, {"agents[0]", "health"});
    j = LevelJson();
    j["tick"] = v;
    AssertJsonErrorMentions(j, {"tick"});
    j = LevelJson();
    j.at("agents").at(0).at("position")["row"] = v;
    AssertJsonErrorMentions(j, {"agents[0]", "row"});
    j = LevelJson();
    j.at("agents").at(0)["cooldowns"] = json::array({0, v});
    AssertJsonErrorMentions(j, {"agents[0]", "cooldowns[1]"});
  }
  json j = LevelJson();
  j.at("agents").at(0)["health"] = 2.5;
  AssertJsonErrorMentions(j, {"must be an integer"});
  j = LevelJson();
  j.at("agents").at(0)["health"] = json(4294967295u);
  AssertJsonErrorMentions(j, {"out of range"});
  j = LevelJson();
  j["zones"] = json{{"burning", json{{"steps", 2147483647}}}};  // An int, over the timer cap
  AssertJsonErrorMentions(j, {"zones['burning']: steps"});
  j = LevelJson();
  j["zones"] = json{{"burning", json{{"steps", -1}, {"duration", 3}}}};  // Negative ints are ints
  SnapshotFromJson(j.dump());
}

// The RNG states (the root's rng_state / inc, an FSM's rng_state / rng_inc)
// are unsigned 64-bit integers: a bool, a float, a negative number or a
// string is an error naming the key, never a silent conversion. The full
// uint64_t range loads.
TEST(TestJsonRngStatesAreStrict) {
  const std::vector<json> not_uint64s = {true, 2.5, -1, json(-9223372036854775807LL),
                                         18446744073709551616.0, "3"};
  for (const json& v : not_uint64s) {
    json j = LevelJson();
    j.at("rng_state")["state"] = v;
    AssertJsonErrorMentions(j, {"rng_state: state"});
    j = LevelJson();
    j.at("rng_state")["inc"] = v;
    AssertJsonErrorMentions(j, {"rng_state: inc"});
    j = AggroJson();
    FsmAgent(j).at("fsm")["rng_state"] = v;
    AssertJsonErrorMentions(j, {"rng_state"});
    j = AggroJson();
    FsmAgent(j).at("fsm")["rng_inc"] = v;
    AssertJsonErrorMentions(j, {"rng_inc"});
  }
  json j = LevelJson();
  j.at("rng_state")["state"] = -1;
  AssertJsonErrorMentions(j, {"out of range"});
  j.at("rng_state")["state"] = 2.5;
  AssertJsonErrorMentions(j, {"must be an integer"});
  j.at("rng_state")["state"] = json(18446744073709551615ULL);
  j.at("rng_state")["inc"] = 0;
  const Snapshot s = SnapshotFromJson(j.dump());
  ASSERT_EQ(s.rng_state, 18446744073709551615ULL);
  ASSERT_EQ(s.rng_inc, 0ULL);
  j = AggroJson();
  FsmAgent(j).at("fsm")["rng_state"] = json(18446744073709551615ULL);
  SnapshotFromJson(j.dump());
}

// The rng_state object holds its two keys only, like the root and the agents
TEST(TestJsonRngStateRejectsUnknownKeys) {
  json j = LevelJson();
  j.at("rng_state")["seed"] = 1;
  AssertJsonErrorMentions(j, {"rng_state: unknown key 'seed'"});
  j = LevelJson();
  j["rng_state"] = json::array({1, 2});
  AssertJsonErrorMentions(j, {"rng_state", "must be object"});
  j = LevelJson();
  j.at("rng_state").erase("inc");
  AssertJsonErrorMentions(j, {"rng_state: key 'inc' not found"});
}

// "none" is a status name the agents' statuses accept, but not a tag status
TEST(TestJsonTagStatusNoneIsNotAStatus) {
  json j = LevelJson();
  j["tag_statuses"] = json::array({json{{"tag", "stunned"}, {"status", "none"}}});
  AssertJsonErrorMentions(j, {"tag_statuses[0] ('stunned'): status: none is not a status"});
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
