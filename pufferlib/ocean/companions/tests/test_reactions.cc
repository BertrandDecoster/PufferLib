// Copyright 2024
// Unit tests for reactions, weaknesses, immunities and tag statuses: the
// combo rules as level data, resolved inside each tag landing

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/object.h"
#include "../src/core/reaction.h"
#include "../src/core/skill_config.h"
#include "../src/core/snapshot.h"
#include "../src/env/synchro_env.h"

using namespace companions;

// =============================================================================
// Test macros
// =============================================================================
#define TEST(name)                                        \
  void name();                                            \
  struct name##_registrar {                               \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance;                                      \
  void name()

#define ASSERT_TRUE(cond)                                               \
  if (!(cond)) {                                                        \
    std::ostringstream oss;                                             \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__        \
        << ":" << __LINE__;                                             \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_FALSE(cond)                                               \
  if (cond) {                                                            \
    std::ostringstream oss;                                             \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__       \
        << ":" << __LINE__;                                             \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_EQ(a, b)                                                  \
  if ((a) != (b)) {                                                      \
    std::ostringstream oss;                                             \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at "         \
        << __FILE__ << ":" << __LINE__;                                 \
    throw std::runtime_error(oss.str());                                \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Helpers
// =============================================================================

using TagSource = BaseEnv::TagSource;

// A 10x10 arena: a wall border, floor inside (rows and cols 1-8). The
// companions (10 HP) are parked on row 8 (cols 1..n) until a test places them.
static void MakeArena(SynchroEnv& env) {
  env.Reset();
  Grid& g = env.GetMutableGrid();
  for (int r = 0; r < 10; ++r) {
    for (int c = 0; c < 10; ++c) {
      bool border = r == 0 || c == 0 || r == 9 || c == 9;
      g.SetCell({r, c}, border ? CellKind::Wall : CellKind::Floor);
    }
  }
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  for (size_t i = 0; i < agents.size(); ++i) {
    env.GetMutableObjectManager().UpdatePosition(agents[i]->GetId(),
                                                 {8, 1 + static_cast<int>(i)});
    agents[i]->SetMaxHealth(10);
  }
}

static Agent* Place(SynchroEnv& env, int index, Position p) {
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(index)];
  env.GetMutableObjectManager().UpdatePosition(a->GetId(), p);
  return a;
}

// A plain enemy (no FSM: it follows its action) with `health`
static Agent* AddEnemy(SynchroEnv& env, Position p, int health = 5) {
  Agent* a = env.GetMutableObjectManager().CreateActor<Agent>(p);
  a->SetFaction(Faction::ENEMY);
  a->SetMaxHealth(health);
  return a;
}

static const Action kStay = EncodeAction(MovementAction::Stay);
static const Action kRight = EncodeAction(MovementAction::Right);
static Action Use(MovementAction aim) { return EncodeAction(aim, InteractAction::Skill1); }
static std::vector<Action> Stays(const BaseEnv& env) {
  return std::vector<Action>(static_cast<size_t>(env.NumAgents()), kStay);
}
// Stays, but agent `index` does `action`
static std::vector<Action> With(const BaseEnv& env, int index, Action action) {
  std::vector<Action> actions = Stays(env);
  actions[static_cast<size_t>(index)] = action;
  return actions;
}

static TagId Id(const BaseEnv& env, const char* tag) { return env.GetTagTable().Find(tag); }
static bool Has(const BaseEnv& env, const Agent* a, const char* tag) {
  TagId t = Id(env, tag);
  return t != kInvalidTag && a->HasTag(t);
}

// A projectile (range 3) landing `tag` on the first agent it meets, put in
// the companion's slot 0
static void GiveBolt(SynchroEnv& env, int companion, const char* name, const char* tag) {
  SkillConfig s;
  s.name = name;
  s.targeting = SkillTargeting::Projectile;
  s.range = 3;
  s.tags = {{tag, kPermanentTag}};
  env.GetMutableSkillBook().Define(s);
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(companion)];
  if (!env.SetCompanionSkill(a->GetId(), 0, name)) throw std::runtime_error("skill refused");
}

// A ReactionRule by named fields: Rule("wet", "electrified", "shocked").Hurts(1).r
struct Rule {
  ReactionRule r;
  Rule(const std::string& a, const std::string& b, const std::string& result) {
    r.a = a;
    r.b = b;
    r.result = result;
  }
  Rule& Keep(std::vector<std::string> keep) { r.keep = std::move(keep); return *this; }
  Rule& Hurts(int damage) { r.damage = damage; return *this; }
  Rule& Spreads(const std::string& becomes = "") {
    r.spread = true;
    r.zone_becomes = becomes;
    return *this;
  }
};

// A ZoneDef by named fields (as in test_zones.cc)
struct Zone {
  ZoneDef def;
  Zone& Lands(int duration) { def.duration = duration; return *this; }
  Zone& Lasts(int steps) { def.steps = steps; return *this; }
  Zone& Then(const std::string& tag) { def.then = tag; return *this; }
  Zone& Hurts(int damage) { def.damage = damage; return *this; }
};

static void Require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(std::string("refused: ") + what);
}

static void SetZones(SynchroEnv& env, const std::vector<Position>& cells, const char* tag) {
  for (Position p : cells) Require(env.SetCellTag(p, tag), tag);
}

// The combos_duo rules (CombosLevelData.htn's locationCombo / comboSpreads),
// with a damage of 1 for the spreading ones. Their results are triggers
// (wet + electrified -> electrified): in a zone they re-fire every step, once
// per agent (TestAResultThatIsATriggerReFiresOncePerAgentInTheZone), so the
// real level will name a result that is not a trigger (-> shocked).
static std::vector<ReactionRule> CombosRules() {
  return {Rule("wet", "electrified", "electrified").Hurts(1).Spreads("wet").r,
          Rule("oil", "burning", "burning").Hurts(1).Spreads("burning").r,
          Rule("wet", "chilled", "stunned").r,
          Rule("ice", "chilled", "stunned").r};
}

// =============================================================================
// The data
// =============================================================================

TEST(TestReactionsAreValidatedBeforeAnyChange) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetReactions({Rule("wet", "electrified", "shocked").r}));
  const std::vector<ReactionRule> before = env.GetReactions();
  const std::string too_long(static_cast<size_t>(kMaxNameLength) + 1, 'x');
  ReactionRule becomes_without_spread = Rule("pa", "pb", "pc").r;
  becomes_without_spread.zone_becomes = "pd";
  const std::vector<std::vector<ReactionRule>> bad = {
      {Rule("", "pb", "pc").r},
      {Rule("pa", "", "pc").r},
      {Rule("pa", "pb", "").r},
      {Rule(too_long, "pb", "pc").r},
      {Rule("pa", "pa", "pc").r},
      {Rule("pa", "pb", "pc").Keep({"pd"}).r},
      {Rule("pa", "pb", "pc").Keep({"pa", "pa"}).r},
      {Rule("pa", "pb", "pc").Hurts(-1).r},
      {becomes_without_spread},
      {Rule("pa", "pb", "pc").Spreads(too_long).r},
      {Rule("pa", "pb", "pc").r, Rule("pb", "pa", "pd").r},  // The same unordered pair
  };
  for (const std::vector<ReactionRule>& rules : bad) {
    std::string error;
    ASSERT_FALSE(env.SetReactions(rules, &error));
    ASSERT_FALSE(error.empty());
    ASSERT_TRUE(env.GetReactions() == before);
  }
  for (const char* tag : {"pa", "pb", "pc", "pd"}) ASSERT_EQ(Id(env, tag), kInvalidTag);

  // Valid: keep both, a result equal to an original, a spread without zone_becomes
  ASSERT_TRUE(env.SetReactions({Rule("pa", "pb", "pa").Keep({"pb", "pa"}).Spreads().r,
                                Rule("pa", "pc", "pd").r}));
  ASSERT_EQ(env.GetReactions().size(), static_cast<size_t>(2));
  ASSERT_TRUE(env.SetReactions({}));
  ASSERT_TRUE(env.GetReactions().empty());
}

TEST(TestTagStatusesWeaknessesAndImmunitiesAreValidated) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  const std::string too_long(static_cast<size_t>(kMaxNameLength) + 1, 'x');
  std::string error;
  const std::vector<std::vector<TagStatusRule>> bad_statuses = {
      {{"", StatusType::Stunned, 3}},
      {{too_long, StatusType::Stunned, 3}},
      {{"zz", StatusType::None, 3}},
      {{"zz", static_cast<StatusType>(2), 3}},  // Slowed, removed
      {{"zz", StatusType::Stunned, 0}},
      {{"zz", StatusType::Stunned, 3}, {"zz", StatusType::Rooted, 1}},
  };
  for (const auto& rules : bad_statuses) {
    error.clear();
    ASSERT_FALSE(env.SetTagStatuses(rules, &error));
    ASSERT_FALSE(error.empty());
  }
  ASSERT_TRUE(env.GetTagStatuses().empty());

  ASSERT_FALSE(env.SetWeaknesses(kInvalidObjectId, {{"zw", "zs"}}, &error));
  ASSERT_FALSE(env.SetWeaknesses(a->GetId(), {{"", "zs"}}, &error));
  ASSERT_FALSE(env.SetWeaknesses(a->GetId(), {{"zw", too_long}}, &error));
  ASSERT_FALSE(env.SetWeaknesses(a->GetId(), {{"zw", "zs"}, {"zw", "zs"}}, &error));
  ASSERT_TRUE(env.GetWeaknesses(a->GetId()).empty());
  ASSERT_FALSE(env.SetImmunities(kInvalidObjectId, {"zi"}, &error));
  ASSERT_FALSE(env.SetImmunities(a->GetId(), {""}, &error));
  ASSERT_FALSE(env.SetImmunities(a->GetId(), {"zi", "zi"}, &error));
  ASSERT_TRUE(env.GetImmunities(a->GetId()).empty());
  for (const char* tag : {"zz", "zw", "zs", "zi"}) ASSERT_EQ(Id(env, tag), kInvalidTag);

  ASSERT_TRUE(env.SetTagStatuses(
      {{"stunned", StatusType::Stunned, 3}, {"marked", StatusType::Marked, 2}}));
  ASSERT_EQ(env.GetTagStatuses().size(), static_cast<size_t>(2));
  // Ordered pairs: (wet, electrified) and (electrified, wet) are two weaknesses
  ASSERT_TRUE(env.SetWeaknesses(a->GetId(), {{"wet", "electrified"}, {"electrified", "wet"}}));
  ASSERT_TRUE(env.GetWeaknesses(a->GetId()) ==
              (std::vector<TagWeakness>{{"wet", "electrified"}, {"electrified", "wet"}}));
  ASSERT_TRUE(env.SetImmunities(a->GetId(), {"stunned"}));
  ASSERT_TRUE(env.GetImmunities(a->GetId()) == std::vector<std::string>{"stunned"});
}

// Reactions and tag statuses are level data (like the zone table): copied
// with the env, kept across a generated Reset, cleared by LoadSnapshot until
// snapshot v7 carries them. Weaknesses and immunities belong to the agents:
// copied with them, gone when the agents are re-created.
TEST(TestReactionDataIsLevelDataAndAgentData) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.SetTagStatuses({{"stunned", StatusType::Stunned, 3}}), "statuses");
  Require(env.SetWeaknesses(a->GetId(), {{"wet", "electrified"}}), "weak_to");
  Require(env.SetImmunities(a->GetId(), {"stunned"}), "immune");

  std::unique_ptr<BaseEnv> copy = env.Clone();
  ASSERT_TRUE(copy->GetReactions() == env.GetReactions());
  ASSERT_TRUE(copy->GetTagStatuses() == env.GetTagStatuses());
  ASSERT_TRUE(copy->GetWeaknesses(a->GetId()) == env.GetWeaknesses(a->GetId()));
  ASSERT_TRUE(copy->GetImmunities(a->GetId()) == env.GetImmunities(a->GetId()));
  SynchroEnv assigned(10, 10, 1, 1, 0, 7);
  assigned = env;
  ASSERT_TRUE(assigned.GetReactions() == env.GetReactions());
  ASSERT_TRUE(assigned.GetImmunities(a->GetId()) == std::vector<std::string>{"stunned"});

  env.Reset();  // A generated level: the level data stays, the agents are new
  ASSERT_EQ(env.GetReactions().size(), static_cast<size_t>(4));
  ASSERT_EQ(env.GetTagStatuses().size(), static_cast<size_t>(1));
  Agent* b = env.GetMutableObjectManager().GetAllAgents().at(0);
  ASSERT_TRUE(env.GetWeaknesses(b->GetId()).empty());
  ASSERT_TRUE(env.GetImmunities(b->GetId()).empty());
  // ... and still works: the resolved tags survived the Reset
  Require(env.ApplyTagTo(b->GetId(), "wet", kPermanentTag), "wet");
  Require(env.ApplyTagTo(b->GetId(), "chilled", kPermanentTag), "chilled");
  ASSERT_TRUE(Has(env, b, "stunned"));
  ASSERT_TRUE(b->IsStunned());

  env.LoadSnapshot(env.SaveSnapshot());
  ASSERT_TRUE(env.GetReactions().empty());
  ASSERT_TRUE(env.GetTagStatuses().empty());
  ASSERT_EQ(copy->GetReactions().size(), static_cast<size_t>(4));  // Deep copies keep theirs
}

// =============================================================================
// Reactions
// =============================================================================

// Either tag landing on an agent carrying the other reacts; the originals go,
// the result lands, and the reaction is reported.
TEST(TestAReactionIsUnordered) {
  for (bool electrified_first : {false, true}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Agent* a = Place(env, 0, {3, 3});
    Require(env.SetReactions({Rule("wet", "electrified", "shocked").r}), "reactions");
    const char* first = electrified_first ? "electrified" : "wet";
    const char* second = electrified_first ? "wet" : "electrified";
    Require(env.ApplyTagTo(a->GetId(), first, kPermanentTag), first);
    ASSERT_TRUE(env.GetLastReactions().empty());
    Require(env.ApplyTagTo(a->GetId(), second, kPermanentTag), second);
    ASSERT_FALSE(Has(env, a, "wet"));
    ASSERT_FALSE(Has(env, a, "electrified"));
    ASSERT_TRUE(Has(env, a, "shocked"));

    ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
    const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
    ASSERT_EQ(r.rule, 0);
    ASSERT_EQ(r.trigger, a->GetId());
    ASSERT_EQ(r.tag, Id(env, second));
    ASSERT_EQ(r.source, kInvalidObjectId);
    ASSERT_EQ(r.cause, std::string("host"));
    ASSERT_TRUE(r.kind == TagSource::Host);
    ASSERT_FALSE(r.spread);
    ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.affected.at(0).agent, a->GetId());
    ASSERT_TRUE(r.affected.at(0).result_landed);
    ASSERT_FALSE(r.affected.at(0).defeated);
    ASSERT_EQ(r.affected.at(0).damage, 0);

    // The landings: the two host tags, then the result (kind Reaction, the
    // triggering landing's source and cause, its reaction's index)
    const auto& landed = env.GetLastTagsApplied();
    ASSERT_EQ(landed.size(), static_cast<size_t>(3));
    ASSERT_TRUE(landed.at(0).kind == TagSource::Host);
    ASSERT_EQ(landed.at(0).reaction, -1);
    ASSERT_TRUE(landed.at(1).kind == TagSource::Host);
    ASSERT_EQ(landed.at(2).tag, Id(env, "shocked"));
    ASSERT_TRUE(landed.at(2).kind == TagSource::Reaction);
    ASSERT_EQ(landed.at(2).reaction, 0);
    ASSERT_EQ(landed.at(2).cause, std::string("host"));
    ASSERT_EQ(landed.at(2).source, kInvalidObjectId);
  }
}

TEST(TestKeepLeavesTheListedOriginals) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetReactions({Rule("oil", "burning", "smoking").Keep({"burning"}).r}), "reactions");
  Require(env.ApplyTagTo(a->GetId(), "oil", kPermanentTag), "oil");
  Require(env.ApplyTagTo(a->GetId(), "burning", kPermanentTag), "burning");
  ASSERT_FALSE(Has(env, a, "oil"));
  ASSERT_TRUE(Has(env, a, "burning"));
  ASSERT_TRUE(Has(env, a, "smoking"));
}

// A result's `fresh` reads the agent before the reaction removed its
// originals: a result that is one of them (wet + electrified -> electrified)
// is not fresh on the agent that carried it, fresh on one that did not.
TEST(TestAResultIsFreshOnlyIfTheAgentDidNotCarryIt) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "electrified").Spreads().r}),
          "reactions");
  Agent* a = Place(env, 0, {3, 3});
  Agent* b = Place(env, 1, {3, 4});
  SetZones(env, {{3, 3}, {3, 4}}, "wet");
  env.Step(Stays(env));  // Both wet
  Require(env.ApplyTagTo(a->GetId(), "electrified", kPermanentTag), "electrified");
  std::vector<BaseEnv::TagApplication> results;
  for (const auto& t : env.GetLastTagsApplied()) {
    if (t.kind == TagSource::Reaction) results.push_back(t);
  }
  ASSERT_EQ(results.size(), static_cast<size_t>(2));
  ASSERT_EQ(results.at(0).agent, a->GetId());
  ASSERT_FALSE(results.at(0).fresh);  // It carried electrified a moment before
  ASSERT_EQ(results.at(1).agent, b->GetId());
  ASSERT_TRUE(results.at(1).fresh);
  ASSERT_TRUE(Has(env, a, "electrified"));
  ASSERT_TRUE(Has(env, b, "electrified"));
}

TEST(TestAReactionDealsItsDamage) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetReactions({Rule("wet", "electrified", "shocked").Hurts(2).r}), "reactions");
  Require(env.ApplyTagTo(a->GetId(), "wet", kPermanentTag), "wet");
  Require(env.ApplyTagTo(a->GetId(), "electrified", kPermanentTag), "electrified");
  ASSERT_EQ(a->GetHealth(), 8);
  ASSERT_EQ(env.GetLastReactions().at(0).affected.at(0).damage, 2);
}

// A result lands, goes through weaknesses, but never triggers a reaction
TEST(TestResultsNeverTriggerReactions) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetReactions({Rule("pa", "pb", "pc").r, Rule("pc", "pd", "pe").r}), "reactions");
  Require(env.ApplyTagTo(a->GetId(), "pd", kPermanentTag), "pd");
  Require(env.ApplyTagTo(a->GetId(), "pb", kPermanentTag), "pb");
  Require(env.ApplyTagTo(a->GetId(), "pa", kPermanentTag), "pa");
  ASSERT_TRUE(Has(env, a, "pc"));  // The result, next to pd: no chain
  ASSERT_TRUE(Has(env, a, "pd"));
  ASSERT_FALSE(Has(env, a, "pe"));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  // A landing of pc from anything else does react with pd
  Require(env.ApplyTagTo(a->GetId(), "pc", kPermanentTag), "pc");
  ASSERT_TRUE(Has(env, a, "pe"));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastReactions().at(1).rule, 1);
}

// One reaction per landing: the first rule, in level order, pairing the tag
// with one the agent carries
TEST(TestTheFirstMatchingRuleFires) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetReactions(
              {Rule("wet", "chilled", "stunned").r, Rule("ice", "chilled", "frozen").r}),
          "reactions");
  Require(env.ApplyTagTo(a->GetId(), "ice", kPermanentTag), "ice");
  Require(env.ApplyTagTo(a->GetId(), "wet", kPermanentTag), "wet");
  Require(env.ApplyTagTo(a->GetId(), "chilled", kPermanentTag), "chilled");
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastReactions().at(0).rule, 0);
  ASSERT_TRUE(Has(env, a, "stunned"));
  ASSERT_FALSE(Has(env, a, "frozen"));
  ASSERT_TRUE(Has(env, a, "ice"));  // The other rule's original, untouched
}

// Skills, zones and the host all land tags that react; each landing says
// what landed it (TagApplication::kind)
TEST(TestSkillAndZoneLandingsReact) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked").r}), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Agent* caster = Place(env, 0, {3, 1});
  Agent* target = AddEnemy(env, {3, 3});
  Require(env.ApplyTagTo(target->GetId(), "wet", kPermanentTag), "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_TRUE(Has(env, target, "shocked"));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, target->GetId());
  ASSERT_EQ(r.source, caster->GetId());
  ASSERT_EQ(r.cause, std::string("spark"));
  ASSERT_TRUE(r.kind == TagSource::Skill);
  const auto& landed = env.GetLastTagsApplied();
  ASSERT_EQ(landed.size(), static_cast<size_t>(2));
  ASSERT_TRUE(landed.at(0).kind == TagSource::Skill);
  ASSERT_TRUE(landed.at(1).kind == TagSource::Reaction);
  ASSERT_EQ(landed.at(1).source, caster->GetId());
  ASSERT_EQ(landed.at(1).cause, std::string("spark"));

  // A zone: the target, electrified again, walks into a puddle
  Require(env.ApplyTagTo(target->GetId(), "electrified", kPermanentTag), "electrified");
  Require(env.SetCellTag({3, 4}, "wet"), "puddle");
  env.Step(With(env, 1, kRight));
  ASSERT_TRUE(target->GetPosition() == (Position{3, 4}));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastReactions().at(0).kind == TagSource::Zone);
  ASSERT_EQ(env.GetLastReactions().at(0).cause, std::string("zone"));
  ASSERT_TRUE(env.GetLastTagsApplied().at(0).kind == TagSource::Zone);
  ASSERT_FALSE(Has(env, target, "electrified"));
}

// The host's landings are reported until the next Step clears the reports
TEST(TestHostLandingsAreReportedUntilTheNextStep) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Require(env.ApplyTagTo(a->GetId(), "blessed", 2), "blessed");
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  const BaseEnv::TagApplication& t = env.GetLastTagsApplied().at(0);
  ASSERT_EQ(t.agent, a->GetId());
  ASSERT_EQ(t.tag, Id(env, "blessed"));
  ASSERT_EQ(t.duration, 2);
  ASSERT_EQ(t.source, kInvalidObjectId);
  ASSERT_EQ(t.cause, std::string("host"));
  ASSERT_TRUE(t.kind == TagSource::Host);
  ASSERT_TRUE(t.fresh);
  env.Step(Stays(env));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_TRUE(Has(env, a, "blessed"));  // Still there: 2 steps
}

// A downed (or dead) agent gets no landing, so it never reacts, and a spread
// passes it over.
TEST(TestADownedAgentNeverReacts) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {3, 1});
  Agent* down = Place(env, 1, {3, 4});
  Agent* gob = AddEnemy(env, {3, 3});
  SetZones(env, {{3, 3}, {3, 4}, {4, 4}}, "wet");
  env.Step(Stays(env));  // Both wet
  ASSERT_TRUE(Has(env, down, "wet"));
  down->TakeDamage(down->GetHealth());
  ASSERT_TRUE(down->IsDowned());
  ASSERT_FALSE(env.ApplyTagTo(down->GetId(), "electrified", kPermanentTag));
  ASSERT_TRUE(env.GetLastReactions().empty());

  env.Step(With(env, 0, Use(MovementAction::Right)));  // The spark hits the gob in the lake
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));
  ASSERT_EQ(r.affected.at(0).agent, gob->GetId());
  ASSERT_TRUE(Has(env, down, "wet"));  // Untouched
  ASSERT_FALSE(Has(env, down, "electrified"));
  ASSERT_EQ(down->GetHealth(), 0);
}

// =============================================================================
// Spread, asked of the map
// =============================================================================

// A spreading reaction on an agent standing on a zone providing a or b
// reaches every affectable agent of that zone's connected region (4
// neighbours), in agent-index order, the trigger included: not a pool of the
// same zone that only touches it diagonally, not an agent beside the region
// on dry land.
TEST(TestASpreadCoversTheConnectedRegionOnly) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked").Hurts(1).Spreads().r}),
          "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {2, 1});
  Agent* swimmer = Place(env, 1, {3, 5});  // Index 1, in the lake
  Agent* gob = AddEnemy(env, {2, 4});      // Index 2, in the lake: the target
  Agent* far = AddEnemy(env, {4, 7});      // Index 3, in a pool touching the lake diagonally
  Agent* ashore = AddEnemy(env, {2, 7});   // Index 4, beside the lake
  SetZones(env, {{2, 4}, {2, 5}, {2, 6}, {3, 5}, {3, 6}}, "wet");
  SetZones(env, {{4, 7}, {5, 7}}, "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));

  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(2));
  ASSERT_EQ(r.affected.at(0).agent, swimmer->GetId());  // Agent-index order
  ASSERT_EQ(r.affected.at(1).agent, gob->GetId());
  for (const auto& o : r.affected) {
    ASSERT_TRUE(o.result_landed);
    ASSERT_EQ(o.damage, 1);
  }
  ASSERT_TRUE(Has(env, swimmer, "shocked"));
  ASSERT_FALSE(Has(env, swimmer, "wet"));  // Its originals went too
  ASSERT_EQ(swimmer->GetHealth(), 9);
  ASSERT_EQ(gob->GetHealth(), 4);
  ASSERT_FALSE(Has(env, far, "shocked"));
  ASSERT_TRUE(Has(env, far, "wet"));
  ASSERT_EQ(far->GetHealth(), 5);
  ASSERT_FALSE(Has(env, ashore, "shocked"));
  ASSERT_EQ(ashore->GetHealth(), 5);
  ASSERT_EQ(env.GetCellTag({2, 4}).tag, Id(env, "wet"));  // No zone_becomes: unchanged
}

// Carrying a or b is not enough: the zone under the agent must provide one
// of them. A wet agent on another zone reacts alone, and nothing becomes
// zone_becomes.
TEST(TestASpreadNeedsTheZoneUnderTheAgent) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked").Hurts(1).Spreads("steam").r}),
          "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {2, 1});
  Agent* neighbour = Place(env, 1, {3, 4});
  Agent* gob = AddEnemy(env, {2, 4});
  SetZones(env, {{2, 4}}, "ice");
  SetZones(env, {{3, 4}}, "wet");
  Require(env.ApplyTagTo(gob->GetId(), "wet", kPermanentTag), "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_FALSE(env.GetLastReactions().at(0).spread);
  ASSERT_EQ(env.GetLastReactions().at(0).affected.size(), static_cast<size_t>(1));
  ASSERT_TRUE(Has(env, gob, "shocked"));
  ASSERT_FALSE(Has(env, neighbour, "shocked"));
  ASSERT_EQ(env.GetCellTag({2, 4}).tag, Id(env, "ice"));
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, Id(env, "wet"));
}

// Oil + burning, spreading over the kitchen oil: everyone on it gets the
// result (the gob, weak to (oil, burning), is defeated: it still stood on
// oil), then the region becomes the table's burning zone (a step timer: 3
// steps, 1 damage per landing), which then burns out and leaves nothing. A
// disconnected oil cell stays oil.
TEST(TestZoneBecomesTakesTheTablesFieldsAndBurnsOut) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.DefineZone("burning", Zone().Lasts(3).Hurts(1).def), "burning zone");
  GiveBolt(env, 0, "fireball", "burning");
  Place(env, 0, {2, 1});
  Agent* cook = Place(env, 1, {2, 4});  // Index 1, on the oil: the target
  Agent* gob = AddEnemy(env, {3, 6});   // Index 2, on the oil
  Require(env.SetWeaknesses(gob->GetId(), {{"oil", "burning"}}), "weak_to");
  const std::vector<Position> kitchen = {{2, 4}, {2, 5}, {2, 6}, {3, 6}};
  SetZones(env, kitchen, "oil");
  SetZones(env, {{6, 6}}, "oil");
  env.Step(With(env, 0, Use(MovementAction::Right)));

  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.rule, 1);
  ASSERT_EQ(r.trigger, cook->GetId());
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(2));
  ASSERT_EQ(r.affected.at(0).agent, cook->GetId());
  ASSERT_EQ(r.affected.at(0).damage, 1);
  ASSERT_FALSE(r.affected.at(0).defeated);
  ASSERT_EQ(r.affected.at(1).agent, gob->GetId());
  ASSERT_TRUE(r.affected.at(1).result_landed);
  ASSERT_TRUE(r.affected.at(1).defeated);
  ASSERT_EQ(r.affected.at(1).damage, 0);  // Defeated: no damage after
  ASSERT_FALSE(gob->IsAlive());
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDefeats().at(0).agent, gob->GetId());
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == TagSource::Reaction);
  ASSERT_EQ(env.GetLastDefeats().at(0).reaction, 0);
  ASSERT_EQ(env.GetLastDefeats().at(0).cause, std::string("fireball"));
  ASSERT_FALSE(Has(env, cook, "oil"));
  ASSERT_TRUE(Has(env, cook, "burning"));
  ASSERT_EQ(cook->GetHealth(), 9);

  for (Position p : kitchen) {
    const BaseEnv::CellTag z = env.GetCellTag(p);
    ASSERT_EQ(z.tag, Id(env, "burning"));
    ASSERT_EQ(z.steps, 3);  // Set during the step: its 3 next steps
    ASSERT_EQ(z.damage, 1);
  }
  ASSERT_EQ(env.GetCellTag({6, 6}).tag, Id(env, "oil"));

  for (int i = 1; i <= 3; ++i) {  // The cook stays in the fire
    env.Step(Stays(env));
    ASSERT_EQ(cook->GetHealth(), 9 - i);
    ASSERT_TRUE(env.GetLastReactions().empty());  // No oil left on it
  }
  for (Position p : kitchen) ASSERT_EQ(env.GetCellTag(p).tag, kInvalidTag);  // The oil is gone
  env.Step(Stays(env));
  ASSERT_EQ(cook->GetHealth(), 6);
}

// A zone_becomes with a successor: the region's zones follow the table
TEST(TestZoneBecomesFollowsTheSuccessorChain) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("oil", "burning", "burning").Spreads("fire").r}), "reactions");
  Require(env.DefineZone("fire", Zone().Lasts(1).Then("ash").def), "fire");
  Require(env.DefineZone("ash", Zone().Lasts(2).def), "ash");
  Agent* a = Place(env, 0, {3, 3});
  SetZones(env, {{3, 3}, {3, 4}}, "oil");
  env.Step(Stays(env));  // Oiled
  Require(env.ApplyTagTo(a->GetId(), "burning", kPermanentTag), "burning");  // Between steps
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, Id(env, "fire"));
  ASSERT_EQ(env.GetCellTag({3, 4}).steps, 1);  // Set between steps: its next step
  env.Step(Stays(env));
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, Id(env, "ash"));
  ASSERT_EQ(env.GetCellTag({3, 4}).steps, 2);
  env.Step(Stays(env));
  env.Step(Stays(env));
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, kInvalidTag);
}

// The zone's own damage belongs to the zone that landed: an agent whose
// reaction changed its own cell still takes the OLD zone's damage that step.
TEST(TestAReactionChangingItsOwnCellStillTakesTheOldZonesDamage) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("oil", "burning", "burning").Spreads("ash").r}), "reactions");
  Require(env.DefineZone("oil", Zone().Hurts(1).def), "oil");  // Hot oil
  Agent* a = Place(env, 0, {3, 3});
  Require(env.ApplyTagTo(a->GetId(), "burning", kPermanentTag), "burning");
  Require(env.SetCellTag({3, 4}, "oil"), "oil cell");
  env.Step(With(env, 0, kRight));
  ASSERT_TRUE(a->GetPosition() == (Position{3, 4}));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, Id(env, "ash"));
  ASSERT_EQ(env.GetLastTagsApplied().at(0).tag, Id(env, "oil"));
  ASSERT_EQ(env.GetLastTagsApplied().at(0).damage, 1);
  ASSERT_EQ(a->GetHealth(), 9);
}

// A trap of the rules (kept: the level names another result): a result that
// is one of its triggers (wet + electrified -> electrified), spreading with
// zone_becomes = the other one (wet). Everyone in the lake keeps the result,
// so each step every agent's wet landing re-fires it, in agent-index order:
// N agents, N firings, each over the whole region (N damage each), and each
// one re-sets the lake (its lifetime starts again).
TEST(TestAResultThatIsATriggerReFiresOncePerAgentInTheZone) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.DefineZone("wet", Zone().Lasts(5).def), "wet");
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {2, 1});  // Ashore
  const std::vector<Agent*> swimmers = {AddEnemy(env, {2, 4}, 10), AddEnemy(env, {2, 5}, 10),
                                        AddEnemy(env, {3, 5}, 10)};
  const std::vector<Position> lake = {{2, 4}, {2, 5}, {2, 6}, {3, 5}, {3, 6}};
  SetZones(env, lake, "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));  // One firing, from the spark
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  for (const Agent* s : swimmers) ASSERT_EQ(s->GetHealth(), 9);

  for (int step = 1; step <= 2; ++step) {  // Nobody casts any more
    env.Step(Stays(env));
    const auto& fired = env.GetLastReactions();
    ASSERT_EQ(fired.size(), swimmers.size());
    for (size_t i = 0; i < fired.size(); ++i) {
      ASSERT_EQ(fired.at(i).trigger, swimmers.at(i)->GetId());
      ASSERT_TRUE(fired.at(i).kind == TagSource::Zone);
      ASSERT_EQ(fired.at(i).affected.size(), swimmers.size());
    }
    for (const Agent* s : swimmers) ASSERT_EQ(s->GetHealth(), 9 - 3 * step);
    for (Position p : lake) ASSERT_EQ(env.GetCellTag(p).steps, 5);  // Re-set, never runs out
  }
}

// =============================================================================
// Weaknesses, asked of the map
// =============================================================================

// (wet, electrified): a wet imp on dry land is not defeated by a spark (it
// reacts, alone); in the lake, the same spark defeats it (the env's kill: an
// enemy dies). A defeated agent gets nothing more, but its reaction still
// spreads over the lake (here with nobody else in it).
TEST(TestAWeaknessIsReadFromTheMapNotFromTheTags) {
  for (bool in_lake : {false, true}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Require(env.SetReactions(CombosRules()), "reactions");
    GiveBolt(env, 0, "spark", "electrified");
    Agent* caster = Place(env, 0, {5, 2});
    Agent* imp = AddEnemy(env, {5, 5});
    Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
    if (in_lake) {
      Require(env.SetCellTag({5, 5}, "wet"), "lake");
    } else {
      Require(env.ApplyTagTo(imp->GetId(), "wet", kPermanentTag), "wet");  // Dripping, ashore
    }
    env.Step(With(env, 0, Use(MovementAction::Right)));
    if (!in_lake) {
      ASSERT_TRUE(imp->IsAlive());
      ASSERT_TRUE(env.GetLastDefeats().empty());
      ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
      ASSERT_FALSE(env.GetLastReactions().at(0).spread);
      ASSERT_EQ(imp->GetHealth(), 4);             // The reaction's damage
      ASSERT_TRUE(Has(env, imp, "electrified"));  // The result
      ASSERT_FALSE(Has(env, imp, "wet"));
    } else {
      ASSERT_FALSE(imp->IsAlive());
      ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
      const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
      ASSERT_EQ(r.trigger, imp->GetId());
      ASSERT_TRUE(r.spread);
      ASSERT_TRUE(r.affected.empty());  // Not the defeated imp
      ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
      const BaseEnv::DefeatReport& d = env.GetLastDefeats().at(0);
      ASSERT_EQ(d.agent, imp->GetId());
      ASSERT_EQ(d.zone, Id(env, "wet"));
      ASSERT_EQ(d.tag, Id(env, "electrified"));
      ASSERT_EQ(d.source, caster->GetId());
      ASSERT_EQ(d.cause, std::string("spark"));
      ASSERT_TRUE(d.kind == TagSource::Skill);
      ASSERT_EQ(d.reaction, -1);  // Not a result
      ASSERT_TRUE(Has(env, imp, "wet"));  // It keeps what it carried
      ASSERT_EQ(imp->GetHealth(), 0);
      ASSERT_TRUE(Has(env, imp, "electrified"));
      ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
      ASSERT_EQ(env.GetLastSkillUses().at(0).affected.size(), static_cast<size_t>(1));
      ASSERT_EQ(env.GetLastSkillUses().at(0).affected.at(0).effects,
                static_cast<unsigned>(BaseEnv::kSkillEffectTags));
    }
  }
}

// A defeated trigger gets nothing more, so a reaction that does not spread
// (the rule does not, or the zone under it provides neither tag) has nobody
// left to affect: nothing fires, nothing is reported.
TEST(TestADefeatedTriggerAloneReactsToNothing) {
  for (bool spreading_rule : {false, true}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Rule rule = Rule("wet", "electrified", "shocked").Hurts(1);
    if (spreading_rule) rule.Spreads("steam");
    Require(env.SetReactions({rule.r}), "reactions");
    GiveBolt(env, 0, "spark", "electrified");
    Place(env, 0, {5, 2});
    Agent* imp = AddEnemy(env, {5, 5});
    const char* floor = spreading_rule ? "metal" : "wet";  // Metal provides neither tag
    Require(env.SetCellTag({5, 5}, floor), floor);
    Require(env.SetWeaknesses(imp->GetId(), {{floor, "electrified"}}), "weak_to");
    Require(env.ApplyTagTo(imp->GetId(), "wet", kPermanentTag), "wet");
    env.Step(With(env, 0, Use(MovementAction::Right)));
    ASSERT_FALSE(imp->IsAlive());
    ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
    ASSERT_TRUE(env.GetLastReactions().empty());
    ASSERT_TRUE(Has(env, imp, "wet"));  // Kept: nothing more reaches it
    ASSERT_FALSE(Has(env, imp, "shocked"));
    ASSERT_EQ(env.GetCellTag({5, 5}).tag, Id(env, floor));
  }
}

// (P, S) is ordered: (wet, electrified) is not (electrified, wet)
TEST(TestAWeaknessIsAnOrderedPair) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* imp = AddEnemy(env, {5, 5});
  Require(env.SetCellTag({5, 5}, "electrified"), "charged floor");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  Require(env.ApplyTagTo(imp->GetId(), "wet", kPermanentTag), "wet");
  ASSERT_TRUE(imp->IsAlive());
  ASSERT_TRUE(env.GetLastDefeats().empty());
  Require(env.SetWeaknesses(imp->GetId(), {{"electrified", "wet"}}), "weak_to");
  Require(env.ApplyTagTo(imp->GetId(), "wet", kPermanentTag), "wet");
  ASSERT_FALSE(imp->IsAlive());
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == TagSource::Host);
  ASSERT_EQ(env.GetLastDefeats().at(0).cause, std::string("host"));
}

// A zone's own landing can defeat: a companion goes down (the env's kill),
// and the landing reports no zone damage (none was dealt: it stopped there).
TEST(TestAZoneLandingDefeatsACompanionDownWithoutZoneDamage) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.DefineZone("burning", Zone().Hurts(2).def), "burning");
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetWeaknesses(a->GetId(), {{"burning", "burning"}}), "weak_to");
  Require(env.SetCellTag({3, 3}, "burning"), "fire");
  env.Step(Stays(env));
  auto* comp = dynamic_cast<Companion*>(a);
  ASSERT_TRUE(comp->IsDowned());
  ASSERT_TRUE(comp->IsAlive());
  ASSERT_EQ(comp->GetTimesDowned(), 1);
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied().at(0).damage, 0);
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == TagSource::Zone);
  ASSERT_EQ(env.GetLastDefeats().at(0).cause, std::string("zone"));
}

// A weakness defeats whatever the health (Marked or not)
TEST(TestADefeatIgnoresHealth) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* imp = AddEnemy(env, {5, 5}, 999);
  imp->ApplyStatus(StatusType::Marked, 5);
  Require(env.SetCellTag({5, 5}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  Require(env.ApplyTagTo(imp->GetId(), "electrified", kPermanentTag), "electrified");
  ASSERT_FALSE(imp->IsAlive());
  ASSERT_EQ(imp->GetHealth(), 0);
}

// The landing report says no zone damage when a reaction's damage left the
// agent unaffectable: an enemy killed, a companion downed
TEST(TestTheZoneDamageIsZeroWhenTheReactionDownedOrKilled) {
  for (bool companion : {false, true}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Require(env.SetReactions({Rule("oil", "burning", "burning").Hurts(10).r}), "reactions");
    Require(env.DefineZone("oil", Zone().Hurts(2).def), "oil");
    Agent* a = companion ? Place(env, 0, {3, 3}) : AddEnemy(env, {3, 3}, 3);
    Require(env.ApplyTagTo(a->GetId(), "burning", kPermanentTag), "burning");
    Require(env.SetCellTag({3, 4}, "oil"), "oil cell");
    env.Step(With(env, companion ? 0 : 1, kRight));
    ASSERT_FALSE(a->IsAffectable());
    ASSERT_EQ(a->IsAlive(), companion);  // A companion goes down
    ASSERT_EQ(env.GetLastTagsApplied().at(0).tag, Id(env, "oil"));
    ASSERT_EQ(env.GetLastTagsApplied().at(0).damage, 0);
    ASSERT_EQ(env.GetLastReactions().at(0).affected.at(0).damage, 10);
  }
}

// A zone's landing that defeats (weak to (oil, oil)) deals no zone damage
// (reported 0), but the reaction it starts still spreads: the companion on
// the same hot oil (landed before it, 2 damage) gets the outcome, and the oil
// becomes ash.
TEST(TestAZoneLandingDefeatStillSpreadsItsReaction) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("oil", "burning", "burning").Hurts(1).Spreads("ash").r}),
          "reactions");
  Require(env.DefineZone("oil", Zone().Hurts(2).def), "hot oil");
  Agent* cook = Place(env, 0, {3, 5});  // Index 0: its zone lands first
  Agent* gob = AddEnemy(env, {3, 3});
  Require(env.SetWeaknesses(gob->GetId(), {{"oil", "oil"}}), "weak_to");
  Require(env.ApplyTagTo(gob->GetId(), "burning", kPermanentTag), "burning");
  SetZones(env, {{3, 4}, {3, 5}}, "oil");
  env.Step(With(env, 1, kRight));
  ASSERT_TRUE(gob->GetPosition() == (Position{3, 4}));
  ASSERT_FALSE(gob->IsAlive());
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == TagSource::Zone);

  const auto& landed = env.GetLastTagsApplied();
  ASSERT_EQ(landed.size(), static_cast<size_t>(3));
  ASSERT_EQ(landed.at(0).agent, cook->GetId());
  ASSERT_EQ(landed.at(0).damage, 2);
  ASSERT_EQ(landed.at(1).agent, gob->GetId());
  ASSERT_EQ(landed.at(1).damage, 0);  // Defeated: none dealt
  ASSERT_EQ(landed.at(2).agent, cook->GetId());
  ASSERT_TRUE(landed.at(2).kind == TagSource::Reaction);

  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_TRUE(r.kind == TagSource::Zone);
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));
  ASSERT_EQ(r.affected.at(0).agent, cook->GetId());
  ASSERT_EQ(r.affected.at(0).damage, 1);
  ASSERT_FALSE(Has(env, cook, "oil"));
  ASSERT_TRUE(Has(env, cook, "burning"));
  ASSERT_EQ(cook->GetHealth(), 7);
  ASSERT_EQ(gob->GetHealth(), 0);
  ASSERT_EQ(env.GetCellTag({3, 4}).tag, Id(env, "ash"));
  ASSERT_EQ(env.GetCellTag({3, 5}).tag, Id(env, "ash"));
}

// A spark through the lake: the gob it hits reacts, the reaction spreads,
// and its result (electrified) defeats the imp standing in the lake, weak to
// (wet, electrified). The companion swimming there takes the damage too.
TEST(TestAResultSpreadingThroughTheLakeDefeatsTheImp) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Agent* caster = Place(env, 0, {2, 1});
  Agent* swimmer = Place(env, 1, {3, 5});
  Agent* gob = AddEnemy(env, {2, 4});
  Agent* imp = AddEnemy(env, {3, 6});
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  Require(env.SetWeaknesses(gob->GetId(), {{"wet", "chilled"}}), "weak_to");
  const std::vector<Position> lake = {{2, 4}, {2, 5}, {2, 6}, {3, 5}, {3, 6}};
  SetZones(env, lake, "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));

  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(3));
  ASSERT_EQ(r.affected.at(0).agent, swimmer->GetId());
  ASSERT_EQ(r.affected.at(1).agent, gob->GetId());
  ASSERT_EQ(r.affected.at(2).agent, imp->GetId());
  ASSERT_TRUE(r.affected.at(2).defeated);
  ASSERT_FALSE(imp->IsAlive());
  ASSERT_TRUE(gob->IsAlive());
  ASSERT_EQ(gob->GetHealth(), 4);
  ASSERT_EQ(swimmer->GetHealth(), 9);
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  const BaseEnv::DefeatReport& d = env.GetLastDefeats().at(0);
  ASSERT_EQ(d.agent, imp->GetId());
  ASSERT_EQ(d.zone, Id(env, "wet"));
  ASSERT_EQ(d.tag, Id(env, "electrified"));
  ASSERT_EQ(d.source, caster->GetId());
  ASSERT_EQ(d.cause, std::string("spark"));
  ASSERT_TRUE(d.kind == TagSource::Reaction);
  ASSERT_EQ(d.reaction, 0);  // The result of the first reaction
  for (Position p : lake) ASSERT_EQ(env.GetCellTag(p).tag, Id(env, "wet"));  // Becomes wet
}

// The gob on the oil, hit by the fireball itself, is defeated there (weak to
// (oil, burning)) and gets nothing more, but its reaction still spreads (as
// in the HTN rules): the cook on the same oil gets the outcome, and the oil
// catches fire.
TEST(TestAGobDefeatedOnTheOilStillSetsItAblaze) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.DefineZone("burning", Zone().Lasts(6).Hurts(1).def), "burning zone");
  GiveBolt(env, 0, "fireball", "burning");
  Agent* caster = Place(env, 0, {2, 1});
  Agent* cook = Place(env, 1, {2, 6});
  Agent* gob = AddEnemy(env, {2, 4});
  Require(env.SetWeaknesses(gob->GetId(), {{"oil", "burning"}}), "weak_to");
  const std::vector<Position> oil = {{2, 4}, {2, 5}, {2, 6}};
  SetZones(env, oil, "oil");
  env.Step(With(env, 0, Use(MovementAction::Right)));

  ASSERT_FALSE(gob->IsAlive());
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDefeats().at(0).agent, gob->GetId());
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == TagSource::Skill);
  ASSERT_TRUE(Has(env, gob, "oil"));  // Nothing more: its originals stay

  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.rule, 1);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_EQ(r.source, caster->GetId());
  ASSERT_EQ(r.cause, std::string("fireball"));
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));  // The cook, not the gob
  ASSERT_EQ(r.affected.at(0).agent, cook->GetId());
  ASSERT_TRUE(r.affected.at(0).result_landed);
  ASSERT_EQ(r.affected.at(0).damage, 1);
  ASSERT_FALSE(Has(env, cook, "oil"));
  ASSERT_TRUE(Has(env, cook, "burning"));
  ASSERT_EQ(cook->GetHealth(), 9);
  for (Position p : oil) ASSERT_EQ(env.GetCellTag(p).tag, Id(env, "burning"));
}

// An oiled gob walking into the fire burns (the zone's damage) and reacts
// (oil + burning: the oil is consumed, 1 damage), but fire is not oil: its
// (oil, burning) weakness does not defeat it.
TEST(TestAnOiledGobWalkingIntoFireBurnsButIsNotDefeated) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.DefineZone("burning", Zone().Lasts(6).Hurts(1).def), "burning zone");
  Agent* gob = AddEnemy(env, {4, 4});
  Require(env.SetWeaknesses(gob->GetId(), {{"oil", "burning"}}), "weak_to");
  Require(env.ApplyTagTo(gob->GetId(), "oil", kPermanentTag), "oil");
  SetZones(env, {{4, 5}, {4, 6}}, "burning");
  env.Step(With(env, 1, kRight));
  ASSERT_TRUE(gob->GetPosition() == (Position{4, 5}));
  ASSERT_TRUE(gob->IsAlive());
  ASSERT_TRUE(env.GetLastDefeats().empty());
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastReactions().at(0).kind == TagSource::Zone);
  ASSERT_FALSE(Has(env, gob, "oil"));
  ASSERT_TRUE(Has(env, gob, "burning"));
  ASSERT_EQ(gob->GetHealth(), 3);  // 1 from the reaction, 1 from the fire
  ASSERT_EQ(env.GetLastTagsApplied().at(0).damage, 1);
}

// =============================================================================
// Immunities
// =============================================================================

// An immune agent gets the tag from no source: the host (false), a skill
// (no Tags effect, in the preview and in the use), a zone (no landing, so no
// zone damage either).
TEST(TestImmunityBlocksEveryLanding) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  GiveBolt(env, 0, "daze", "stunned");
  Agent* caster = Place(env, 0, {5, 2});
  Agent* imp = AddEnemy(env, {5, 4});
  Require(env.SetImmunities(imp->GetId(), {"stunned"}), "immune");
  ASSERT_FALSE(env.ApplyTagTo(imp->GetId(), "stunned", kPermanentTag));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_FALSE(Has(env, imp, "stunned"));

  auto* comp = dynamic_cast<Companion*>(caster);
  const BaseEnv::SkillPreview preview = env.PreviewSkill(*comp, 0, Direction::Right);
  ASSERT_EQ(preview.affected.size(), static_cast<size_t>(1));
  ASSERT_EQ(preview.affected.at(0).effects, 0u);
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastSkillUses().at(0).affected.size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.at(0) == preview.affected.at(0));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_FALSE(Has(env, imp, "stunned"));

  Require(env.DefineZone("stunned", Zone().Hurts(2).def), "zone");
  Require(env.SetCellTag({5, 4}, "stunned"), "stunning floor");
  env.Step(Stays(env));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_EQ(imp->GetHealth(), 5);

  // A skill with another tag still lands that one
  SkillConfig two = *env.GetSkillBook().Find("daze");
  two.name = "soak";
  two.tags = {{"stunned", kPermanentTag}, {"wet", kPermanentTag}};
  env.GetMutableSkillBook().Define(two);
  Require(env.SetCompanionSkill(caster->GetId(), 0, "soak"), "soak");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastSkillUses().at(0).affected.at(0).effects,
            static_cast<unsigned>(BaseEnv::kSkillEffectTags));
  ASSERT_TRUE(Has(env, imp, "wet"));
  ASSERT_FALSE(Has(env, imp, "stunned"));
}

// wet + chilled -> stunned on an imp immune to stunned: the reaction fires
// (its originals go, its damage applies) but the result does not land, so
// no status either.
TEST(TestAnImpImmuneToStunnedIsNotStunned) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "chilled", "stunned").Hurts(1).r}), "reactions");
  Require(env.SetTagStatuses({{"stunned", StatusType::Stunned, 3}}), "statuses");
  GiveBolt(env, 0, "frost", "chilled");
  Place(env, 0, {5, 2});
  Agent* imp = AddEnemy(env, {5, 4});
  Require(env.SetImmunities(imp->GetId(), {"stunned"}), "immune");
  Require(env.ApplyTagTo(imp->GetId(), "wet", kPermanentTag), "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastReactions().at(0).affected.size(), static_cast<size_t>(1));
  ASSERT_FALSE(env.GetLastReactions().at(0).affected.at(0).result_landed);
  ASSERT_EQ(env.GetLastReactions().at(0).affected.at(0).damage, 1);
  ASSERT_FALSE(Has(env, imp, "stunned"));
  ASSERT_FALSE(Has(env, imp, "wet"));
  ASSERT_FALSE(Has(env, imp, "chilled"));
  ASSERT_FALSE(imp->IsStunned());
  ASSERT_EQ(imp->GetHealth(), 4);
}

// =============================================================================
// Tag statuses
// =============================================================================

// wet + chilled -> stunned, and stunned applies Stunned for 3 steps as it
// lands (a step timer: landed during a step, it covers the 3 next ones).
TEST(TestWetAndChilledStunForThreeSteps) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  Require(env.SetTagStatuses({{"stunned", StatusType::Stunned, 3}}), "statuses");
  GiveBolt(env, 0, "frost", "chilled");
  Place(env, 0, {5, 2});
  Agent* gob = AddEnemy(env, {5, 4});
  Require(env.ApplyTagTo(gob->GetId(), "wet", kPermanentTag), "wet");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_TRUE(Has(env, gob, "stunned"));
  ASSERT_TRUE(gob->IsStunned());
  ASSERT_EQ(gob->GetStatuses().size(), static_cast<size_t>(1));
  ASSERT_EQ(gob->GetStatuses().at(0).duration, 3);
  for (int i = 0; i < 3; ++i) {
    env.Step(With(env, 1, EncodeAction(MovementAction::Down)));
    ASSERT_TRUE(gob->GetPosition() == (Position{5, 4}));  // Forced to stay
  }
  ASSERT_FALSE(gob->IsStunned());
  env.Step(With(env, 1, EncodeAction(MovementAction::Down)));
  ASSERT_TRUE(gob->GetPosition() == (Position{6, 4}));
}

// A host landing between steps: the status covers the n next steps
TEST(TestATagStatusFromTheHostCoversTheNextSteps) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetTagStatuses({{"rooted", StatusType::Rooted, 2}}), "statuses");
  Agent* a = Place(env, 0, {3, 3});
  Require(env.ApplyTagTo(a->GetId(), "rooted", 1), "rooted");
  ASSERT_EQ(a->GetStatuses().size(), static_cast<size_t>(1));
  ASSERT_EQ(a->GetStatuses().at(0).duration, 2);
  env.Step(With(env, 0, kRight));
  env.Step(With(env, 0, kRight));
  ASSERT_TRUE(a->GetPosition() == (Position{3, 3}));
  env.Step(With(env, 0, kRight));
  ASSERT_TRUE(a->GetPosition() == (Position{3, 4}));
}

// =============================================================================
// Determinism
// =============================================================================

// The same world, stepped twice (the original and its clone), resolves the
// same reactions and defeats.
TEST(TestAClonedWorldResolvesTheSameReactions) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions(CombosRules()), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {2, 1});
  Place(env, 1, {3, 5});
  AddEnemy(env, {2, 4});
  Agent* imp = AddEnemy(env, {3, 6});
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  SetZones(env, {{2, 4}, {2, 5}, {2, 6}, {3, 5}, {3, 6}}, "wet");
  std::unique_ptr<BaseEnv> clone = env.Clone();
  const std::vector<Action> actions = With(env, 0, Use(MovementAction::Right));
  env.Step(actions);
  clone->Step(actions);
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(clone->GetLastReactions().size(), static_cast<size_t>(1));
  const auto& x = env.GetLastReactions().at(0);
  const auto& y = clone->GetLastReactions().at(0);
  ASSERT_EQ(x.rule, y.rule);
  ASSERT_EQ(x.trigger, y.trigger);
  ASSERT_EQ(x.affected.size(), y.affected.size());
  for (size_t j = 0; j < x.affected.size(); ++j) {
    ASSERT_EQ(x.affected.at(j).agent, y.affected.at(j).agent);
    ASSERT_EQ(x.affected.at(j).defeated, y.affected.at(j).defeated);
    ASSERT_EQ(x.affected.at(j).damage, y.affected.at(j).damage);
  }
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_EQ(clone->GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied().size(), clone->GetLastTagsApplied().size());
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

  std::cout << "Running " << tests.size() << " reaction tests...\n\n";

  int passed = 0;
  int failed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << "\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << ": " << e.what() << "\n";
      failed++;
    }
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size() << " tests passed.\n";
  return failed == 0 ? 0 : 1;
}
