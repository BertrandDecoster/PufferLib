// Copyright 2024
// C API 1.5: the rules' reports (tag landings, reactions, defeats, downs) from
// the last step or an outcome preview, their events, and the level data
// (zone table, cell zones, reactions, tag statuses, weaknesses, immunities).
// The world is built in C++, saved as a JSON snapshot (v7) and loaded through
// the C API; a C++ env loaded from the same snapshot is the reference (parity).

#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_api.h"
#include "../src/core/reaction.h"
#include "../src/core/skill_config.h"
#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/env/synchro_env.h"

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
    oss << "ASSERT_EQ failed: " << (a) << " != " << (b) << " (" << #a << " != " << #b \
        << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_ERROR(text) ASSERT_EQ(std::string(companions_get_error()), std::string(text))

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// The world: the kitchen (see test_reactions.cc's MakeKitchen)
// =============================================================================

static void Require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(std::string("refused: ") + what);
}

static ReactionRule Rule(const std::string& a, const std::string& b, const std::string& result,
                         int damage = 0, const char* spreads_into = nullptr) {
  ReactionRule r;
  r.a = a;
  r.b = b;
  r.result = result;
  r.damage = damage;
  if (spreads_into) {
    r.spread = true;
    r.zone_becomes = spreads_into;
  }
  return r;
}

static ZoneDef Zone(int steps, const std::string& then, int damage) {
  ZoneDef z;
  z.steps = steps;
  z.then = then;
  z.damage = damage;
  return z;
}

// Agent indices of the kitchen
enum : int32_t { kCaster = 0, kCook = 1, kGob = 2, kImp = 3, kPal = 4, kFrosty = 5, kAgents = 6 };

// A 10x10 arena (a wall border): the caster (2, 1) holds a fireball (a
// projectile of range 3 landing "scorched" for 2 steps, then "burning"); the
// oil (2, 4), (2, 5), (2, 6), (3, 5) holds the cook (a companion weak to
// (oil, burning)), the gob (an enemy weak to (oil, burning)), the imp (immune
// to burning) and the pal; frosty stands in the lake (5, 5), chilled. Oil +
// burning -> burning spreads (1 damage) and sets the oil ablaze (the burning
// zone: 3 steps, 1 damage, then ash for 2); wet + chilled -> stunned (a tag
// status: Stunned for 2 steps). Saved as JSON before any step.
static std::string KitchenJson() {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  env.Reset();
  Grid& g = env.GetMutableGrid();
  for (int r = 0; r < 10; ++r) {
    for (int c = 0; c < 10; ++c) {
      const bool border = r == 0 || c == 0 || r == 9 || c == 9;
      g.SetCell({r, c}, border ? CellKind::Wall : CellKind::Floor);
    }
  }
  ObjectManager& objects = env.GetMutableObjectManager();
  const std::vector<Agent*> companions = objects.GetAllAgents();
  objects.UpdatePosition(companions.at(0)->GetId(), {2, 1});
  objects.UpdatePosition(companions.at(1)->GetId(), {2, 6});
  for (Agent* a : companions) a->SetMaxHealth(10);
  auto enemy = [&](Position p, int health) {
    Agent* a = objects.CreateActor<Agent>(p);
    a->SetFaction(Faction::ENEMY);
    a->SetMaxHealth(health);
    return a->GetId();
  };
  const ObjectId gob = enemy({2, 4}, 5);
  const ObjectId imp = enemy({2, 5}, 5);
  enemy({3, 5}, 5);
  const ObjectId frosty = enemy({5, 5}, 6);

  Require(env.SetReactions({Rule("oil", "burning", "burning", 1, "burning"),
                            Rule("wet", "chilled", "stunned")}),
          "reactions");
  Require(env.SetTagStatuses({{"stunned", StatusType::Stunned, 2}}), "statuses");
  Require(env.DefineZone("burning", Zone(3, "ash", 1)), "burning");
  Require(env.DefineZone("ash", Zone(2, "", 0)), "ash");
  SkillConfig fireball;
  fireball.name = "fireball";
  fireball.targeting = SkillTargeting::Projectile;
  fireball.range = 3;
  fireball.tags = {{"scorched", 2}, {"burning", kPermanentTag}};
  env.GetMutableSkillBook().Define(fireball);
  Require(env.SetCompanionSkill(companions.at(0)->GetId(), 0, "fireball"), "fireball");
  Require(env.SetWeaknesses(gob, {{"oil", "burning"}}), "gob weak_to");
  Require(env.SetWeaknesses(companions.at(1)->GetId(), {{"oil", "burning"}}), "cook weak_to");
  Require(env.SetImmunities(imp, {"burning"}), "immune");
  Require(env.ApplyTagTo(frosty, "chilled", kPermanentTag), "chilled");
  for (Position p : std::vector<Position>{{2, 4}, {2, 5}, {2, 6}, {3, 5}}) {
    Require(env.SetCellTag(p, "oil"), "oil");
  }
  Require(env.SetCellTag({5, 5}, "wet"), "wet");
  // The fireball's "scorched" is interned by no rule: only a landing interns it
  return SnapshotToJson(env.SaveSnapshot());
}

static Companions_Env* LoadKitchen(const std::string& json) {
  Companions_EnvConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.num_companions = 2;
  config.num_synchro = 1;
  config.horizon = 100;
  config.seed = 42;
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);
  if (!companions_load_snapshot_json(env, json.c_str())) {
    throw std::runtime_error(std::string("the kitchen did not load: ") + companions_get_error());
  }
  ASSERT_EQ(companions_get_agent_count(env), kAgents);
  return env;
}

static std::unique_ptr<SynchroEnv> LoadCppKitchen(const std::string& json) {
  auto env = std::make_unique<SynchroEnv>(10, 10, 2, 1, 0, 42);
  env->LoadSnapshot(SnapshotFromJson(json));
  return env;
}

static Companions_ObjectId IdAt(const Companions_Env* env, int32_t index) {
  Companions_AgentState a = {};
  ASSERT_TRUE(companions_get_agent_by_index(env, index, &a));
  return a.id;
}

static int ApiIdx(const Companions_Env* env, Companions_ObjectId id) {
  for (int32_t i = 0; i < companions_get_agent_count(env); ++i) {
    if (IdAt(env, i) == id) return i;
  }
  return -1;
}

static int CppIdx(const BaseEnv& env, ObjectId id) {
  const auto agents = env.GetObjectManager().GetAllAgents();
  for (size_t i = 0; i < agents.size(); ++i) {
    if (agents[i]->GetId() == id) return static_cast<int>(i);
  }
  return -1;
}

// Everyone stays, but agent `caster` (if >= 0) uses slot 0 aimed right
static std::vector<Companions_Action> ApiActions(int caster = -1) {
  std::vector<Companions_Action> actions(kAgents, {Companions_Movement_Stay, Companions_Interact_None});
  if (caster >= 0) actions[static_cast<size_t>(caster)] = {Companions_Movement_Right, Companions_Interact_Skill1};
  return actions;
}
static std::vector<Action> CppActions(int caster = -1) {
  std::vector<Action> actions(kAgents, EncodeAction(MovementAction::Stay));
  if (caster >= 0) {
    actions[static_cast<size_t>(caster)] = EncodeAction(MovementAction::Right, InteractAction::Skill1);
  }
  return actions;
}

static Companions_StepResult ApiStep(Companions_Env* env, int caster = -1) {
  const std::vector<Companions_Action> actions = ApiActions(caster);
  Companions_StepResult result = {};
  companions_step(env, actions.data(), kAgents, &result);
  return result;
}

// =============================================================================
// Traces: the same text from the C API's reports and the C++ env's
// =============================================================================

static std::string Name(const BaseEnv& env, TagId tag) {
  return tag == kInvalidTag ? "" : env.GetTagTable().Name(tag);
}

// The reports of `source`, the landings from `first_landing` on, by names and
// agent indices
static std::string ApiTrace(const Companions_Env* env, Companions_ReportSource source,
                            int32_t first_landing = 0) {
  std::ostringstream out;
  for (int32_t i = 0; i < companions_get_skill_use_count(env, source); ++i) {
    Companions_SkillUseInfo u = {};
    ASSERT_TRUE(companions_get_skill_use(env, source, i, &u));
    out << "use " << ApiIdx(env, u.caster) << " " << u.skill << " " << u.slot << " "
        << u.centre.row << "," << u.centre.col << ":";
    for (int32_t j = 0; j < u.affected_count; ++j) {
      out << " " << ApiIdx(env, u.affected[j]) << "/" << u.affected_effects[j];
    }
    out << "\n";
  }
  for (int32_t i = first_landing; i < companions_get_tag_landing_count(env, source); ++i) {
    Companions_TagLanding t = {};
    ASSERT_TRUE(companions_get_tag_landing(env, source, i, &t));
    out << "tag " << ApiIdx(env, t.agent) << " " << t.tag << " " << t.duration << " "
        << ApiIdx(env, t.source) << " " << t.cause << " " << t.fresh << " " << t.damage << " "
        << static_cast<int>(t.kind) << " " << t.reaction << "\n";
  }
  for (int32_t i = 0; i < companions_get_reaction_count(env, source); ++i) {
    Companions_ReactionInfo r = {};
    ASSERT_TRUE(companions_get_reaction(env, source, i, &r));
    ASSERT_EQ(r.affected_count, r.affected_total);
    out << "reaction " << r.rule << " " << r.a << "+" << r.b << "=" << r.result << " "
        << ApiIdx(env, r.trigger) << " " << r.tag << " " << ApiIdx(env, r.source) << " " << r.cause
        << " " << static_cast<int>(r.kind) << " " << r.spread << ":";
    for (int32_t j = 0; j < r.affected_count; ++j) {
      out << " " << ApiIdx(env, r.affected[j]) << "/" << r.affected_result_landed[j] << "/"
          << r.affected_defeated[j] << "/" << r.affected_damage[j];
    }
    out << " cells " << r.zone_becomes;
    for (int32_t c = 0; c < r.cell_count; ++c) {
      Companions_Position p = {};
      ASSERT_TRUE(companions_get_reaction_cell(env, source, i, c, &p));
      out << " " << p.row << "," << p.col;
    }
    out << "\n";
  }
  for (int32_t i = 0; i < companions_get_defeat_count(env, source); ++i) {
    Companions_DefeatInfo d = {};
    ASSERT_TRUE(companions_get_defeat(env, source, i, &d));
    out << "defeat " << ApiIdx(env, d.agent) << " " << d.zone << " " << d.tag << " "
        << ApiIdx(env, d.source) << " " << d.cause << " " << static_cast<int>(d.kind) << " "
        << d.reaction << "\n";
  }
  for (int32_t i = 0; i < companions_get_down_count(env, source); ++i) {
    Companions_ObjectId id = -1;
    ASSERT_TRUE(companions_get_down(env, source, i, &id));
    out << "down " << ApiIdx(env, id) << "\n";
  }
  return out.str();
}

static std::string CppTrace(const BaseEnv& env) {
  std::ostringstream out;
  for (const auto& u : env.GetLastSkillUses()) {
    out << "use " << CppIdx(env, u.caster) << " " << u.skill << " " << u.slot << " "
        << u.target.row << "," << u.target.col << ":";
    for (const auto& a : u.affected) out << " " << CppIdx(env, a.id) << "/" << a.effects;
    out << "\n";
  }
  for (const auto& t : env.GetLastTagsApplied()) {
    out << "tag " << CppIdx(env, t.agent) << " " << Name(env, t.tag) << " " << t.duration << " "
        << CppIdx(env, t.source) << " " << t.cause << " " << t.fresh << " " << t.damage << " "
        << static_cast<int>(t.kind) << " " << t.reaction << "\n";
  }
  for (const auto& r : env.GetLastReactions()) {
    const ReactionRule& rule = env.GetReactions().at(static_cast<size_t>(r.rule));
    out << "reaction " << r.rule << " " << rule.a << "+" << rule.b << "=" << rule.result << " "
        << CppIdx(env, r.trigger) << " " << Name(env, r.tag) << " " << CppIdx(env, r.source) << " "
        << r.cause << " " << static_cast<int>(r.kind) << " " << r.spread << ":";
    for (const auto& o : r.affected) {
      out << " " << CppIdx(env, o.agent) << "/" << o.result_landed << "/" << o.defeated << "/"
          << o.damage;
    }
    out << " cells " << (r.cells.empty() ? "" : rule.zone_becomes);
    for (const Position& p : r.cells) out << " " << p.row << "," << p.col;
    out << "\n";
  }
  for (const auto& d : env.GetLastDefeats()) {
    out << "defeat " << CppIdx(env, d.agent) << " " << Name(env, d.zone) << " " << Name(env, d.tag)
        << " " << CppIdx(env, d.source) << " " << d.cause << " " << static_cast<int>(d.kind) << " "
        << d.reaction << "\n";
  }
  for (ObjectId id : env.GetLastDowns()) out << "down " << CppIdx(env, id) << "\n";
  return out.str();
}

static void ExpectSame(const std::string& got, const std::string& expected, const std::string& at) {
  if (got != expected) {
    throw std::runtime_error(at + " differs:\n" + got + "--- expected ---\n" + expected);
  }
}

static Companions_Direction Facing(const Companions_Env* env, int32_t index) {
  Companions_AgentState a = {};
  ASSERT_TRUE(companions_get_agent_by_index(env, index, &a));
  return a.facing;
}

static std::vector<uint8_t> SnapshotBytes(const Companions_Env* env) {
  const int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  ASSERT_TRUE(companions_save_snapshot(env, bytes.data(), size));
  return bytes;
}

// =============================================================================
// Version
// =============================================================================

TEST(TestVersionIs150) { ASSERT_EQ(std::string(companions_version()), std::string("1.5.0")); }

// =============================================================================
// Level data
// =============================================================================

// The kitchen's level data, loaded from its v7 JSON snapshot
TEST(TestLevelDataFromAV7JsonSnapshot) {
  const std::string json = KitchenJson();
  ASSERT_TRUE(json.find("\"reactions\"") != std::string::npos);
  Companions_Env* env = LoadKitchen(json);

  // The zone table, sorted by tag
  ASSERT_EQ(companions_get_zone_def_count(env), 2);
  Companions_ZoneDefInfo z = {};
  ASSERT_TRUE(companions_get_zone_def(env, 0, &z));
  ASSERT_EQ(std::string(z.tag), std::string("ash"));
  ASSERT_EQ(z.duration, -1);
  ASSERT_EQ(z.steps, 2);
  ASSERT_EQ(std::string(z.then), std::string(""));
  ASSERT_EQ(z.damage, 0);
  ASSERT_TRUE(companions_get_zone_def(env, 1, &z));
  ASSERT_EQ(std::string(z.tag), std::string("burning"));
  ASSERT_EQ(z.steps, 3);
  ASSERT_EQ(std::string(z.then), std::string("ash"));
  ASSERT_EQ(z.damage, 1);
  Companions_ZoneDefInfo found = {};
  ASSERT_TRUE(companions_find_zone_def(env, "burning", &found));
  ASSERT_TRUE(std::memcmp(&found, &z, sizeof(z)) == 0);
  Companions_ZoneDefInfo untouched = {};
  untouched.damage = 77;
  ASSERT_FALSE(companions_find_zone_def(env, "oil", &untouched));
  ASSERT_ERROR("Unknown zone: oil");
  ASSERT_EQ(untouched.damage, 77);
  ASSERT_FALSE(companions_get_zone_def(env, 2, &untouched));
  ASSERT_ERROR("Zone index out of range");
  ASSERT_FALSE(companions_get_zone_def(env, -1, &untouched));
  ASSERT_ERROR("Zone index out of range");
  ASSERT_EQ(untouched.damage, 77);
  ASSERT_FALSE(companions_get_zone_def(env, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_find_zone_def(env, nullptr, &z));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_EQ(companions_get_zone_def_count(nullptr), 0);
  ASSERT_ERROR("Invalid environment");

  // Cell zones: the oil (a zone by name the table does not define: the defaults)
  Companions_CellZone cell = {};
  ASSERT_TRUE(companions_get_cell_zone(env, 2, 5, &cell));
  ASSERT_TRUE(cell.has_zone);
  ASSERT_EQ(std::string(cell.tag), std::string("oil"));
  ASSERT_EQ(cell.duration, -1);
  ASSERT_EQ(cell.steps, -1);
  ASSERT_EQ(std::string(cell.then), std::string(""));
  ASSERT_EQ(cell.damage, 0);
  ASSERT_EQ(companions_get_cell_tag(env, 2, 5), companions_find_tag(env, "oil"));
  ASSERT_TRUE(companions_get_cell_zone(env, 1, 1, &cell));
  ASSERT_FALSE(cell.has_zone);
  ASSERT_EQ(std::string(cell.tag), std::string(""));
  ASSERT_FALSE(companions_get_cell_zone(env, 10, 0, &cell));
  ASSERT_ERROR("Position out of bounds");
  ASSERT_FALSE(companions_get_cell_zone(env, 0, -1, &cell));
  ASSERT_ERROR("Position out of bounds");
  ASSERT_FALSE(companions_get_cell_zone(env, 1, 1, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_cell_zone(nullptr, 1, 1, &cell));
  ASSERT_ERROR("Invalid arguments");

  // The reactions, in level order
  ASSERT_EQ(companions_get_reaction_rule_count(env), 2);
  Companions_ReactionRuleInfo rule = {};
  ASSERT_TRUE(companions_get_reaction_rule(env, 0, &rule));
  ASSERT_EQ(std::string(rule.a), std::string("oil"));
  ASSERT_EQ(std::string(rule.b), std::string("burning"));
  ASSERT_EQ(std::string(rule.result), std::string("burning"));
  ASSERT_FALSE(rule.keep_a);
  ASSERT_FALSE(rule.keep_b);
  ASSERT_EQ(rule.damage, 1);
  ASSERT_TRUE(rule.spread);
  ASSERT_EQ(std::string(rule.zone_becomes), std::string("burning"));
  ASSERT_TRUE(companions_get_reaction_rule(env, 1, &rule));
  ASSERT_EQ(std::string(rule.a), std::string("wet"));
  ASSERT_EQ(std::string(rule.result), std::string("stunned"));
  ASSERT_FALSE(rule.spread);
  ASSERT_EQ(std::string(rule.zone_becomes), std::string(""));
  ASSERT_FALSE(companions_get_reaction_rule(env, 2, &rule));
  ASSERT_ERROR("Reaction rule index out of range");
  ASSERT_FALSE(companions_get_reaction_rule(env, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_EQ(companions_get_reaction_rule_count(nullptr), 0);

  // The tag statuses
  ASSERT_EQ(companions_get_tag_status_count(env), 1);
  Companions_TagStatusInfo status = {};
  ASSERT_TRUE(companions_get_tag_status(env, 0, &status));
  ASSERT_EQ(std::string(status.tag), std::string("stunned"));
  ASSERT_EQ(status.status, Companions_Status_Stunned);
  ASSERT_EQ(status.steps, 2);
  ASSERT_FALSE(companions_get_tag_status(env, 1, &status));
  ASSERT_ERROR("Tag status index out of range");
  ASSERT_FALSE(companions_get_tag_status(nullptr, 0, &status));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_EQ(companions_get_tag_status_count(nullptr), 0);

  // Weaknesses and immunities, per agent
  ASSERT_EQ(companions_get_agent_weakness_count(env, IdAt(env, kCaster)), 0);
  ASSERT_EQ(companions_get_agent_weakness_count(env, IdAt(env, kCook)), 1);
  ASSERT_EQ(companions_get_agent_weakness_count(env, IdAt(env, kGob)), 1);
  Companions_Weakness weak = {};
  ASSERT_TRUE(companions_get_agent_weakness(env, IdAt(env, kGob), 0, &weak));
  ASSERT_EQ(std::string(weak.zone), std::string("oil"));
  ASSERT_EQ(std::string(weak.tag), std::string("burning"));
  ASSERT_FALSE(companions_get_agent_weakness(env, IdAt(env, kGob), 1, &weak));
  ASSERT_ERROR("Weakness index out of range");
  ASSERT_FALSE(companions_get_agent_weakness(env, IdAt(env, kGob), 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_EQ(companions_get_agent_weakness_count(env, 9999), 0);
  ASSERT_ERROR("Agent not found");
  ASSERT_FALSE(companions_get_agent_weakness(env, 9999, 0, &weak));
  ASSERT_ERROR("Agent not found");
  ASSERT_EQ(companions_get_agent_weakness_count(nullptr, 0), 0);
  ASSERT_ERROR("Invalid environment");

  ASSERT_EQ(companions_get_agent_immunity_count(env, IdAt(env, kImp)), 1);
  ASSERT_EQ(companions_get_agent_immunity_count(env, IdAt(env, kGob)), 0);
  char tag[Companions_SKILL_NAME_LEN] = "untouched";
  ASSERT_TRUE(companions_get_agent_immunity(env, IdAt(env, kImp), 0, tag));
  ASSERT_EQ(std::string(tag), std::string("burning"));
  std::strcpy(tag, "untouched");
  ASSERT_FALSE(companions_get_agent_immunity(env, IdAt(env, kImp), 1, tag));
  ASSERT_ERROR("Immunity index out of range");
  ASSERT_FALSE(companions_get_agent_immunity(env, 9999, 0, tag));
  ASSERT_ERROR("Agent not found");
  ASSERT_EQ(std::string(tag), std::string("untouched"));
  ASSERT_FALSE(companions_get_agent_immunity(env, IdAt(env, kImp), 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_EQ(companions_get_agent_immunity_count(env, 9999), 0);
  ASSERT_ERROR("Agent not found");

  // A reset keeps the level's rules and drops the agents' (new agents)
  companions_reset(env, 7);
  ASSERT_EQ(companions_get_reaction_rule_count(env), 2);
  ASSERT_EQ(companions_get_zone_def_count(env), 2);
  ASSERT_EQ(companions_get_tag_status_count(env), 1);
  for (int32_t i = 0; i < companions_get_agent_count(env); ++i) {
    ASSERT_EQ(companions_get_agent_weakness_count(env, IdAt(env, i)), 0);
    ASSERT_EQ(companions_get_agent_immunity_count(env, IdAt(env, i)), 0);
  }
  companions_destroy(env);
}

// =============================================================================
// Reports and events of a step, and parity with the C++ env
// =============================================================================

// A scripted run, the C API and the C++ env loaded from the same snapshot:
// a first step (the lake stuns frosty), a host landing, the fireball (a
// weakness defeat, a spread through the oil with an immune imp and a
// result defeating the cook, the oil catching fire), then the fire burning
// (zone damage) and turning to ash. Every step's reports are equal.
TEST(TestReportsMatchTheCppEnvStepByStep) {
  const std::string json = KitchenJson();
  Companions_Env* api = LoadKitchen(json);
  std::unique_ptr<SynchroEnv> cpp = LoadCppKitchen(json);
  ASSERT_EQ(ApiTrace(api, Companions_Report_LastStep), std::string(""));  // A load empties them

  bool zone_damage = false;
  for (int step = 0; step < 7; ++step) {
    const int caster = step == 1 ? kCaster : -1;
    ApiStep(api, caster);
    cpp->Step(CppActions(caster));
    ExpectSame(ApiTrace(api, Companions_Report_LastStep), CppTrace(*cpp),
               "step " + std::to_string(step));
    for (const auto& t : cpp->GetLastTagsApplied()) zone_damage = zone_damage || t.damage > 0;
    if (step == 0) {
      // A host landing is reported until the next step (kind Host)
      const int32_t before = companions_get_tag_landing_count(api, Companions_Report_LastStep);
      ASSERT_TRUE(companions_apply_tag(api, IdAt(api, kPal), "marked-by-host", 3));
      Require(cpp->ApplyTagTo(cpp->GetObjectManager().GetAllAgents().at(kPal)->GetId(),
                              "marked-by-host", 3),
              "host tag");
      ASSERT_EQ(companions_get_tag_landing_count(api, Companions_Report_LastStep), before + 1);
      Companions_TagLanding host = {};
      ASSERT_TRUE(companions_get_tag_landing(api, Companions_Report_LastStep, before, &host));
      ASSERT_EQ(host.kind, Companions_TagSource_Host);
      ASSERT_EQ(std::string(host.cause), std::string("host"));
      ASSERT_EQ(host.source, -1);
      ASSERT_EQ(host.reaction, -1);
      ASSERT_EQ(host.duration, 3);
      ASSERT_TRUE(host.fresh);
      ExpectSame(ApiTrace(api, Companions_Report_LastStep), CppTrace(*cpp), "the host landing");
    }
    if (step == 1) {
      ASSERT_EQ(companions_get_reaction_count(api, Companions_Report_LastStep), 1);
      ASSERT_EQ(companions_get_defeat_count(api, Companions_Report_LastStep), 2);
      ASSERT_EQ(companions_get_down_count(api, Companions_Report_LastStep), 1);
    }
  }
  ASSERT_TRUE(zone_damage);
  // A reset empties them
  companions_reset(api, 3);
  ASSERT_EQ(ApiTrace(api, Companions_Report_LastStep), std::string(""));
  companions_destroy(api);
}

// The events of the fireball's step: TagApplied carries its landing's kind
// and reaction (a zone's, a skill's, a result's), ReactionFired and
// AgentDefeated report the reactions and defeats, and every one of them
// points at its entry in the report queries.
TEST(TestTheEventsSayWhatLandedAndWhatFired) {
  Companions_Env* env = LoadKitchen(KitchenJson());
  Companions_StepResult first = ApiStep(env);
  // The lake: frosty's wet (a zone's landing, fresh) sets off wet + chilled
  int zone_tags = 0;
  for (int32_t i = 0; i < first.event_count; ++i) {
    const Companions_Event& e = first.events[i];
    if (e.type != Companions_Event_TagApplied) {
      ASSERT_EQ(e.tag_reaction, -1);
      if (e.type != Companions_Event_ReactionFired && e.type != Companions_Event_SkillUsed &&
          e.type != Companions_Event_AgentDefeated) {
        ASSERT_EQ(e.report_index, -1);
      }
      continue;
    }
    if (e.tag_kind == Companions_TagSource_Zone) {
      ++zone_tags;
      ASSERT_EQ(e.health_source_id, -1);
      ASSERT_EQ(e.tag_reaction, -1);
    }
  }
  ASSERT_EQ(zone_tags, 5);  // Oil on four, wet on frosty
  ASSERT_EQ(companions_get_reaction_count(env, Companions_Report_LastStep), 1);

  // A host landing is no event: it is reported until the next step
  ASSERT_TRUE(companions_apply_tag(env, IdAt(env, kCaster), "host-tag", 1));

  Companions_StepResult r = ApiStep(env, kCaster);
  ASSERT_EQ(r.events_dropped, 0);
  int landings = 0, skill = 0, zone = 0, result = 0, fired = 0, defeated = 0, used = 0;
  for (int32_t i = 0; i < r.event_count; ++i) {
    const Companions_Event& e = r.events[i];
    switch (e.type) {
      case Companions_Event_SkillUsed: {
        ASSERT_EQ(e.report_index, used++);
        Companions_SkillUseInfo u = {};
        ASSERT_TRUE(companions_get_skill_use(env, Companions_Report_LastStep, e.report_index, &u));
        ASSERT_EQ(u.caster, e.subject_id);
        break;
      }
      case Companions_Event_TagApplied: {
        ASSERT_EQ(e.report_index, landings++);
        Companions_TagLanding t = {};
        ASSERT_TRUE(companions_get_tag_landing(env, Companions_Report_LastStep, e.report_index, &t));
        ASSERT_EQ(t.agent, e.subject_id);
        ASSERT_EQ(std::string(t.tag), std::string(e.effect_name));
        ASSERT_EQ(e.effect_id, companions_find_tag(env, t.tag));
        ASSERT_EQ(e.tag_kind, t.kind);
        ASSERT_EQ(e.tag_reaction, t.reaction);
        ASSERT_EQ(e.health_amount, t.damage);
        ASSERT_EQ(e.health_source_id, t.source);
        ASSERT_EQ(e.status_duration, t.duration);
        ASSERT_EQ(e.tag_fresh, t.fresh);
        ASSERT_TRUE(t.kind != Companions_TagSource_Host);  // The step dropped the host's
        if (e.tag_kind == Companions_TagSource_Skill) {
          ++skill;
          ASSERT_EQ(e.health_source_id, IdAt(env, kCaster));
          ASSERT_EQ(e.tag_reaction, -1);
        } else if (e.tag_kind == Companions_TagSource_Zone) {
          ++zone;
          ASSERT_EQ(e.tag_reaction, -1);
        } else if (e.tag_kind == Companions_TagSource_Reaction) {
          ++result;
          ASSERT_TRUE(e.tag_reaction >= 0);
          Companions_ReactionInfo from = {};
          ASSERT_TRUE(companions_get_reaction(env, Companions_Report_LastStep, e.tag_reaction, &from));
          ASSERT_EQ(std::string(from.result), std::string(e.effect_name));
        }
        break;
      }
      case Companions_Event_ReactionFired: {
        ASSERT_EQ(e.report_index, fired++);
        Companions_ReactionInfo info = {};
        ASSERT_TRUE(companions_get_reaction(env, Companions_Report_LastStep, e.report_index, &info));
        ASSERT_EQ(e.subject_id, info.trigger);
        ASSERT_EQ(e.effect_id, info.rule);
        ASSERT_EQ(std::string(e.effect_name), std::string(info.result));
        ASSERT_EQ(e.tag_kind, info.kind);
        ASSERT_EQ(e.health_source_id, info.source);
        ASSERT_EQ(e.tag_reaction, -1);
        break;
      }
      case Companions_Event_AgentDefeated: {
        ASSERT_EQ(e.report_index, defeated++);
        Companions_DefeatInfo d = {};
        ASSERT_TRUE(companions_get_defeat(env, Companions_Report_LastStep, e.report_index, &d));
        ASSERT_EQ(e.subject_id, d.agent);
        ASSERT_EQ(std::string(e.effect_name), std::string(d.tag));
        ASSERT_EQ(e.tag_kind, d.kind);
        ASSERT_EQ(e.tag_reaction, d.reaction);
        ASSERT_EQ(e.health_source_id, d.source);
        break;
      }
      default:
        ASSERT_EQ(e.report_index, -1);
        ASSERT_EQ(e.tag_reaction, -1);
        break;
    }
  }
  ASSERT_EQ(used, 1);
  ASSERT_EQ(landings, companions_get_tag_landing_count(env, Companions_Report_LastStep));
  ASSERT_EQ(fired, 1);  // The fireball's (frosty's chilled went at the first step)
  ASSERT_EQ(defeated, 2);
  ASSERT_TRUE(skill > 0);
  ASSERT_TRUE(zone > 0);
  ASSERT_TRUE(result > 0);
  // The gob, by the fireball's own tag; the cook, by the spread's result
  Companions_DefeatInfo d = {};
  ASSERT_TRUE(companions_get_defeat(env, Companions_Report_LastStep, 0, &d));
  ASSERT_EQ(d.agent, IdAt(env, kGob));
  ASSERT_EQ(d.kind, Companions_TagSource_Skill);
  ASSERT_EQ(d.reaction, -1);
  ASSERT_EQ(std::string(d.zone), std::string("oil"));
  ASSERT_EQ(std::string(d.tag), std::string("burning"));
  ASSERT_EQ(std::string(d.cause), std::string("fireball"));
  ASSERT_TRUE(companions_get_defeat(env, Companions_Report_LastStep, 1, &d));
  ASSERT_EQ(d.agent, IdAt(env, kCook));
  ASSERT_EQ(d.kind, Companions_TagSource_Reaction);
  ASSERT_TRUE(d.reaction >= 0);
  ASSERT_EQ(d.source, IdAt(env, kCaster));
  // The cook went down: an AgentDowned too
  bool downed = false;
  for (int32_t i = 0; i < r.event_count; ++i) {
    downed = downed || (r.events[i].type == Companions_Event_AgentDowned &&
                        r.events[i].subject_id == IdAt(env, kCook));
  }
  ASSERT_TRUE(downed);

  // A later step: the burning zone lands on the pal, with its damage
  Companions_StepResult burn = ApiStep(env);
  bool burnt = false;
  for (int32_t i = 0; i < burn.event_count; ++i) {
    const Companions_Event& e = burn.events[i];
    if (e.type == Companions_Event_TagApplied && e.subject_id == IdAt(env, kPal) &&
        std::string(e.effect_name) == "burning") {
      ASSERT_EQ(e.tag_kind, Companions_TagSource_Zone);
      ASSERT_EQ(e.health_amount, 1);
      burnt = true;
    }
  }
  ASSERT_TRUE(burnt);
  companions_destroy(env);
}

// The oil catching fire: the spread's cells, in row-major order, and the
// zone they became
TEST(TestAReactionSaysWhichCellsChanged) {
  Companions_Env* env = LoadKitchen(KitchenJson());
  ApiStep(env);
  ApiStep(env, kCaster);
  int32_t spread = -1;
  for (int32_t i = 0; i < companions_get_reaction_count(env, Companions_Report_LastStep); ++i) {
    Companions_ReactionInfo info = {};
    ASSERT_TRUE(companions_get_reaction(env, Companions_Report_LastStep, i, &info));
    if (info.spread) {
      spread = i;
      ASSERT_EQ(info.rule, 0);
      ASSERT_EQ(std::string(info.zone_becomes), std::string("burning"));
      ASSERT_EQ(info.cell_count, 4);
      ASSERT_EQ(info.trigger, IdAt(env, kGob));
      ASSERT_EQ(std::string(info.tag), std::string("burning"));
      ASSERT_EQ(info.kind, Companions_TagSource_Skill);
      ASSERT_EQ(std::string(info.cause), std::string("fireball"));
      // The cook (defeated by the result), the imp (immune), the pal: not the gob
      ASSERT_EQ(info.affected_count, 3);
      ASSERT_EQ(info.affected_total, 3);
      ASSERT_EQ(info.affected[0], IdAt(env, kCook));
      ASSERT_TRUE(info.affected_result_landed[0]);
      ASSERT_TRUE(info.affected_defeated[0]);
      ASSERT_EQ(info.affected_damage[0], 0);
      ASSERT_EQ(info.affected[1], IdAt(env, kImp));
      ASSERT_FALSE(info.affected_result_landed[1]);
      ASSERT_FALSE(info.affected_defeated[1]);
      ASSERT_EQ(info.affected_damage[1], 1);
      ASSERT_EQ(info.affected[2], IdAt(env, kPal));
      ASSERT_TRUE(info.affected_result_landed[2]);
      ASSERT_EQ(info.affected_damage[2], 1);
    } else {
      ASSERT_EQ(info.cell_count, 0);
      ASSERT_EQ(std::string(info.zone_becomes), std::string(""));
      Companions_Position p = {7, 7};
      ASSERT_FALSE(companions_get_reaction_cell(env, Companions_Report_LastStep, i, 0, &p));
      ASSERT_ERROR("Reaction cell index out of range");
    }
  }
  ASSERT_TRUE(spread >= 0);
  const Companions_Position expected[] = {{2, 4}, {2, 5}, {2, 6}, {3, 5}};
  for (int32_t c = 0; c < 4; ++c) {
    Companions_Position p = {};
    ASSERT_TRUE(companions_get_reaction_cell(env, Companions_Report_LastStep, spread, c, &p));
    ASSERT_EQ(p.row, expected[c].row);
    ASSERT_EQ(p.col, expected[c].col);
    Companions_CellZone zone = {};
    ASSERT_TRUE(companions_get_cell_zone(env, p.row, p.col, &zone));
    ASSERT_EQ(std::string(zone.tag), std::string("burning"));
    ASSERT_EQ(zone.steps, 3);
    ASSERT_EQ(std::string(zone.then), std::string("ash"));
    ASSERT_EQ(zone.damage, 1);
  }
  Companions_Position p = {};
  ASSERT_FALSE(companions_get_reaction_cell(env, Companions_Report_LastStep, spread, 4, &p));
  ASSERT_ERROR("Reaction cell index out of range");
  ASSERT_FALSE(companions_get_reaction_cell(env, Companions_Report_LastStep, spread, -1, &p));
  ASSERT_ERROR("Reaction cell index out of range");
  ASSERT_FALSE(companions_get_reaction_cell(env, Companions_Report_LastStep, 9, 0, &p));
  ASSERT_ERROR("Reaction index out of range");
  ASSERT_FALSE(companions_get_reaction_cell(env, Companions_Report_LastStep, spread, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  companions_destroy(env);
}

// =============================================================================
// Outcome previews
// =============================================================================

// The preview predicts exactly what the next step does when the fireball is
// its only change (its reports, field by field, after the zone landings the
// step makes first), and changes nothing in the env: its snapshot, its
// reports, its tags ("scorched" is interned by the step only).
TEST(TestAnOutcomePreviewThroughTheApi) {
  Companions_Env* env = LoadKitchen(KitchenJson());
  ApiStep(env);  // Everyone on a zone carries its tag: the next zone landings set nothing off
  const std::vector<uint8_t> before = SnapshotBytes(env);
  const std::string reports = ApiTrace(env, Companions_Report_LastStep);
  ASSERT_EQ(companions_find_tag(env, "scorched"), -1);
  ASSERT_EQ(companions_get_tag_landing_count(env, Companions_Report_Preview), 0);  // None yet
  const Companions_Direction facing = Facing(env, kCaster);

  Companions_SkillOutcome outcome = {};
  ASSERT_TRUE(companions_preview_skill_outcome(env, IdAt(env, kCaster), 0,
                                               Companions_Direction_Right, &outcome));
  ASSERT_TRUE(outcome.usable);
  ASSERT_EQ(outcome.skill_use_count, 1);
  ASSERT_EQ(outcome.reaction_count, 1);  // The fireball's (frosty's lake is quiet)
  ASSERT_EQ(outcome.defeat_count, 2);
  ASSERT_EQ(outcome.down_count, 1);
  ASSERT_EQ(outcome.tag_landing_count,
            companions_get_tag_landing_count(env, Companions_Report_Preview));
  ASSERT_EQ(outcome.skill_use_count, companions_get_skill_use_count(env, Companions_Report_Preview));
  ASSERT_EQ(outcome.reaction_count, companions_get_reaction_count(env, Companions_Report_Preview));
  ASSERT_EQ(outcome.defeat_count, companions_get_defeat_count(env, Companions_Report_Preview));
  ASSERT_EQ(outcome.down_count, companions_get_down_count(env, Companions_Report_Preview));
  Companions_TagLanding first = {};
  ASSERT_TRUE(companions_get_tag_landing(env, Companions_Report_Preview, 0, &first));
  ASSERT_EQ(std::string(first.tag), std::string("scorched"));  // Named, though not interned here

  // Nothing changed
  ASSERT_TRUE(SnapshotBytes(env) == before);
  ASSERT_EQ(ApiTrace(env, Companions_Report_LastStep), reports);
  ASSERT_EQ(companions_find_tag(env, "scorched"), -1);
  ASSERT_EQ(Facing(env, kCaster), facing);  // The preview aimed a copy

  // The preview's reports, as structs, before the step drops them
  std::vector<Companions_SkillUseInfo> uses(static_cast<size_t>(outcome.skill_use_count));
  for (int32_t i = 0; i < outcome.skill_use_count; ++i) {
    ASSERT_TRUE(companions_get_skill_use(env, Companions_Report_Preview, i, &uses[static_cast<size_t>(i)]));
  }
  std::vector<Companions_TagLanding> landings(static_cast<size_t>(outcome.tag_landing_count));
  for (int32_t i = 0; i < outcome.tag_landing_count; ++i) {
    ASSERT_TRUE(companions_get_tag_landing(env, Companions_Report_Preview, i, &landings[static_cast<size_t>(i)]));
  }
  std::vector<Companions_ReactionInfo> fired(static_cast<size_t>(outcome.reaction_count));
  for (int32_t i = 0; i < outcome.reaction_count; ++i) {
    ASSERT_TRUE(companions_get_reaction(env, Companions_Report_Preview, i, &fired[static_cast<size_t>(i)]));
  }
  std::vector<Companions_DefeatInfo> defeats(static_cast<size_t>(outcome.defeat_count));
  for (int32_t i = 0; i < outcome.defeat_count; ++i) {
    ASSERT_TRUE(companions_get_defeat(env, Companions_Report_Preview, i, &defeats[static_cast<size_t>(i)]));
  }
  const std::string preview = ApiTrace(env, Companions_Report_Preview);

  ApiStep(env, kCaster);
  // The step's zone landings come first: re-landings setting nothing off
  const int32_t total = companions_get_tag_landing_count(env, Companions_Report_LastStep);
  const int32_t zone_landings = total - outcome.tag_landing_count;
  ASSERT_EQ(zone_landings, 5);
  for (int32_t i = 0; i < zone_landings; ++i) {
    Companions_TagLanding t = {};
    ASSERT_TRUE(companions_get_tag_landing(env, Companions_Report_LastStep, i, &t));
    ASSERT_EQ(t.kind, Companions_TagSource_Zone);
  }
  ExpectSame(preview, ApiTrace(env, Companions_Report_LastStep, zone_landings), "the preview");
  for (int32_t i = 0; i < outcome.skill_use_count; ++i) {
    Companions_SkillUseInfo u = {};
    ASSERT_TRUE(companions_get_skill_use(env, Companions_Report_LastStep, i, &u));
    ASSERT_TRUE(std::memcmp(&u, &uses[static_cast<size_t>(i)], sizeof(u)) == 0);
  }
  for (int32_t i = 0; i < outcome.tag_landing_count; ++i) {
    Companions_TagLanding t = {};
    ASSERT_TRUE(companions_get_tag_landing(env, Companions_Report_LastStep, zone_landings + i, &t));
    ASSERT_TRUE(std::memcmp(&t, &landings[static_cast<size_t>(i)], sizeof(t)) == 0);
  }
  for (int32_t i = 0; i < outcome.reaction_count; ++i) {
    Companions_ReactionInfo info = {};
    ASSERT_TRUE(companions_get_reaction(env, Companions_Report_LastStep, i, &info));
    ASSERT_TRUE(std::memcmp(&info, &fired[static_cast<size_t>(i)], sizeof(info)) == 0);
  }
  for (int32_t i = 0; i < outcome.defeat_count; ++i) {
    Companions_DefeatInfo d = {};
    ASSERT_TRUE(companions_get_defeat(env, Companions_Report_LastStep, i, &d));
    ASSERT_TRUE(std::memcmp(&d, &defeats[static_cast<size_t>(i)], sizeof(d)) == 0);
  }
  ASSERT_TRUE(companions_find_tag(env, "scorched") >= 0);
  // The step dropped the preview
  ASSERT_EQ(companions_get_tag_landing_count(env, Companions_Report_Preview), 0);
  ASSERT_EQ(companions_get_skill_use_count(env, Companions_Report_Preview), 0);
  companions_destroy(env);
}

// An unusable use previews as nothing (a disabled slot, a downed caster);
// refused arguments leave the last preview's reports as they were.
TEST(TestAnOutcomePreviewOfAnUnusableSkillIsEmpty) {
  Companions_Env* env = LoadKitchen(KitchenJson());
  ApiStep(env);
  const Companions_ObjectId caster = IdAt(env, kCaster);
  Companions_SkillOutcome outcome = {};
  ASSERT_TRUE(companions_preview_skill_outcome(env, caster, 0, Companions_Direction_Right, &outcome));
  const int32_t landings = outcome.tag_landing_count;
  ASSERT_TRUE(landings > 0);

  // Refused: the reports stay the last preview's, `out` untouched
  Companions_SkillOutcome untouched = {};
  untouched.tag_landing_count = 77;
  ASSERT_FALSE(companions_preview_skill_outcome(nullptr, caster, 0, Companions_Direction_Right, &untouched));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_preview_skill_outcome(env, caster, 0, Companions_Direction_Right, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_preview_skill_outcome(env, 9999, 0, Companions_Direction_Right, &untouched));
  ASSERT_ERROR("Agent not found");
  ASSERT_FALSE(companions_preview_skill_outcome(env, IdAt(env, kGob), 0, Companions_Direction_Right, &untouched));
  ASSERT_ERROR("Not a companion");
  ASSERT_FALSE(companions_preview_skill_outcome(env, caster, 2, Companions_Direction_Right, &untouched));
  ASSERT_ERROR("Skill slot out of range");
  ASSERT_FALSE(companions_preview_skill_outcome(env, caster, -1, Companions_Direction_Right, &untouched));
  ASSERT_ERROR("Skill slot out of range");
  ASSERT_FALSE(companions_preview_skill_outcome(env, caster, 0, static_cast<Companions_Direction>(9), &untouched));
  ASSERT_ERROR("Invalid direction");
  ASSERT_EQ(untouched.tag_landing_count, 77);
  ASSERT_EQ(companions_get_tag_landing_count(env, Companions_Report_Preview), landings);

  // Slot 1 is not enabled yet: unusable, nothing resolved
  ASSERT_TRUE(companions_preview_skill_outcome(env, caster, 1, Companions_Direction_Right, &outcome));
  ASSERT_FALSE(outcome.usable);
  ASSERT_EQ(outcome.skill_use_count, 0);
  ASSERT_EQ(outcome.tag_landing_count, 0);
  ASSERT_EQ(outcome.reaction_count, 0);
  ASSERT_EQ(outcome.defeat_count, 0);
  ASSERT_EQ(outcome.down_count, 0);
  ASSERT_EQ(ApiTrace(env, Companions_Report_Preview), std::string(""));

  // A downed caster (a host effect between steps): unusable, and its down is
  // the next step's report, not the preview's
  ASSERT_TRUE(companions_spawn_effect(env, "kill", 2, 1, Companions_Direction_Up, -1));
  ASSERT_TRUE(companions_preview_skill_outcome(env, caster, 0, Companions_Direction_Right, &outcome));
  ASSERT_FALSE(outcome.usable);
  ASSERT_EQ(outcome.down_count, 0);
  ApiStep(env);
  ASSERT_EQ(companions_get_down_count(env, Companions_Report_LastStep), 1);
  companions_destroy(env);
}

// =============================================================================
// Bad arguments
// =============================================================================

TEST(TestReportQueriesRejectBadArguments) {
  Companions_Env* env = LoadKitchen(KitchenJson());
  ApiStep(env);
  ApiStep(env, kCaster);
  const auto bad = static_cast<Companions_ReportSource>(7);

  // Counts: 0 for a null env or a bad source
  ASSERT_EQ(companions_get_skill_use_count(nullptr, Companions_Report_LastStep), 0);
  ASSERT_ERROR("Invalid environment");
  ASSERT_EQ(companions_get_tag_landing_count(nullptr, Companions_Report_LastStep), 0);
  ASSERT_EQ(companions_get_reaction_count(nullptr, Companions_Report_LastStep), 0);
  ASSERT_EQ(companions_get_defeat_count(nullptr, Companions_Report_LastStep), 0);
  ASSERT_EQ(companions_get_down_count(nullptr, Companions_Report_LastStep), 0);
  ASSERT_ERROR("Invalid environment");
  ASSERT_EQ(companions_get_skill_use_count(env, bad), 0);
  ASSERT_ERROR("Invalid report source");
  ASSERT_EQ(companions_get_tag_landing_count(env, bad), 0);
  ASSERT_ERROR("Invalid report source");
  ASSERT_EQ(companions_get_reaction_count(env, bad), 0);
  ASSERT_ERROR("Invalid report source");
  ASSERT_EQ(companions_get_defeat_count(env, bad), 0);
  ASSERT_ERROR("Invalid report source");
  ASSERT_EQ(companions_get_down_count(env, bad), 0);
  ASSERT_ERROR("Invalid report source");

  // Getters: false, `out` untouched
  Companions_SkillUseInfo use = {};
  use.slot = 77;
  Companions_TagLanding landing = {};
  landing.damage = 77;
  Companions_ReactionInfo reaction = {};
  reaction.rule = 77;
  Companions_DefeatInfo defeat = {};
  defeat.reaction = 77;
  Companions_ObjectId down = 77;
  Companions_Position cell = {77, 77};
  ASSERT_FALSE(companions_get_skill_use(nullptr, Companions_Report_LastStep, 0, &use));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_skill_use(env, Companions_Report_LastStep, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_skill_use(env, bad, 0, &use));
  ASSERT_ERROR("Invalid report source");
  ASSERT_FALSE(companions_get_skill_use(env, Companions_Report_LastStep, 1, &use));
  ASSERT_ERROR("Skill use index out of range");
  ASSERT_FALSE(companions_get_skill_use(env, Companions_Report_LastStep, -1, &use));
  ASSERT_ERROR("Skill use index out of range");
  ASSERT_FALSE(companions_get_tag_landing(env, Companions_Report_LastStep, 999, &landing));
  ASSERT_ERROR("Tag landing index out of range");
  ASSERT_FALSE(companions_get_tag_landing(env, bad, 0, &landing));
  ASSERT_ERROR("Invalid report source");
  ASSERT_FALSE(companions_get_tag_landing(env, Companions_Report_LastStep, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_reaction(env, Companions_Report_LastStep, 2, &reaction));
  ASSERT_ERROR("Reaction index out of range");
  ASSERT_FALSE(companions_get_reaction(nullptr, Companions_Report_LastStep, 0, &reaction));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_reaction_cell(env, bad, 0, 0, &cell));
  ASSERT_ERROR("Invalid report source");
  ASSERT_FALSE(companions_get_defeat(env, Companions_Report_LastStep, 2, &defeat));
  ASSERT_ERROR("Defeat index out of range");
  ASSERT_FALSE(companions_get_defeat(env, Companions_Report_LastStep, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  ASSERT_FALSE(companions_get_down(env, Companions_Report_LastStep, 1, &down));
  ASSERT_ERROR("Down index out of range");
  ASSERT_FALSE(companions_get_down(env, bad, 0, &down));
  ASSERT_ERROR("Invalid report source");
  ASSERT_FALSE(companions_get_down(env, Companions_Report_LastStep, 0, nullptr));
  ASSERT_ERROR("Invalid arguments");
  // No preview yet: empty, every index out of range
  ASSERT_EQ(companions_get_reaction_count(env, Companions_Report_Preview), 0);
  ASSERT_FALSE(companions_get_reaction(env, Companions_Report_Preview, 0, &reaction));
  ASSERT_ERROR("Reaction index out of range");
  ASSERT_FALSE(companions_get_down(env, Companions_Report_Preview, 0, &down));
  ASSERT_ERROR("Down index out of range");
  ASSERT_EQ(use.slot, 77);
  ASSERT_EQ(landing.damage, 77);
  ASSERT_EQ(reaction.rule, 77);
  ASSERT_EQ(defeat.reaction, 77);
  ASSERT_EQ(down, 77);
  ASSERT_EQ(cell.row, 77);

  // The last step's skill uses are companions_get_last_skill_use's
  ASSERT_EQ(companions_get_skill_use_count(env, Companions_Report_LastStep),
            companions_get_last_skill_use_count(env));
  Companions_SkillUseInfo a = {}, b = {};
  ASSERT_TRUE(companions_get_skill_use(env, Companions_Report_LastStep, 0, &a));
  ASSERT_TRUE(companions_get_last_skill_use(env, 0, &b));
  ASSERT_TRUE(std::memcmp(&a, &b, sizeof(a)) == 0);
  companions_destroy(env);
}

// =============================================================================
// Main
// =============================================================================

int main() {
  int passed = 0;
  int failed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << std::endl;
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << std::endl;
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << "\n  Error: " << e.what() << std::endl;
      failed++;
    }
  }
  std::cout << "\nC API 1.5 tests: " << passed << " passed, " << failed << " failed" << std::endl;
  return failed > 0 ? 1 : 0;
}
