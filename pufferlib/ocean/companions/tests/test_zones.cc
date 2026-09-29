// Copyright 2024
// Unit tests for zones (cell tags): their lifetime, their successor, their
// damage per landing and the level's zone table

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/object.h"
#include "../src/core/skill_config.h"
#include "../src/core/snapshot.h"
#include "../src/env/aggro_env.h"
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

// A 10x10 arena: a wall border, floor inside (rows and cols 1-8). Agents are
// parked on row 8 (cols 1..n) until a test places them.
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
  }
}

static Agent* Place(SynchroEnv& env, int index, Position p) {
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(index)];
  env.GetMutableObjectManager().UpdatePosition(a->GetId(), p);
  return a;
}

static Action Use(MovementAction aim) { return EncodeAction(aim, InteractAction::Skill1); }
static const Action kStay = EncodeAction(MovementAction::Stay);
static const Action kRight = EncodeAction(MovementAction::Right);
static bool Has(const BaseEnv& env, const Agent* a, const char* tag) {
  TagId t = env.GetTagTable().Find(tag);
  return t != kInvalidTag && a->HasTag(t);
}
static TagId Id(const BaseEnv& env, const char* tag) { return env.GetTagTable().Find(tag); }

// A ZoneDef by named fields: Zone().Lasts(2).Then("smoke").Hurts(1).def
struct Zone {
  ZoneDef def;
  Zone& Lands(int duration) { def.duration = duration; return *this; }
  Zone& Lasts(int steps) { def.steps = steps; return *this; }
  Zone& Then(const std::string& tag) { def.then = tag; return *this; }
  Zone& Hurts(int damage) { def.damage = damage; return *this; }
};

// Sets a zone during the next Step (in PreStep, before the movement), as a
// rule of the env would: a timer set during a step.
class MidStepZoneEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  struct Pending {
    Position cell;
    std::string tag;
    ZoneDef def;
  };
  std::optional<Pending> pending;

 protected:
  void PreStep() override {
    SynchroEnv::PreStep();
    if (pending) {
      if (!SetCellTag(pending->cell, pending->tag, pending->def)) {
        throw std::runtime_error("zone refused");
      }
      pending.reset();
    }
  }
};

// =============================================================================
// The zone as data
// =============================================================================

// Without a definition in the table, a zone gets the defaults: permanent, no
// successor, harmless (the landing duration given), and it stays.
TEST(TestZoneDefaultsArePermanentHarmlessAndWithoutSuccessor) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", 2));
  const BaseEnv::CellTag z = env.GetCellTag({3, 2});
  ASSERT_EQ(z.tag, Id(env, "wet"));
  ASSERT_EQ(z.duration, 2);
  ASSERT_EQ(z.steps, kPermanentTag);
  ASSERT_EQ(z.then, kInvalidTag);
  ASSERT_EQ(z.damage, 0);
  Agent* a = Place(env, 0, {3, 2});
  const int health = a->GetHealth();
  for (int i = 0; i < 5; ++i) {
    env.Step({kStay});
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 0);  // None dealt
  }
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, Id(env, "wet"));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, kPermanentTag);
  ASSERT_EQ(a->GetHealth(), health);
}

TEST(TestAnExplicitZoneIsValidatedBeforeAnythingChanges) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  const std::string too_long(static_cast<size_t>(kMaxNameLength) + 1, 'x');
  ASSERT_FALSE(env.SetCellTag({3, 2}, "puddle", Zone().Lasts(0).def));
  ASSERT_FALSE(env.SetCellTag({3, 2}, "puddle", Zone().Lasts(-2).def));  // Below permanent
  ASSERT_FALSE(env.SetCellTag({3, 2}, "puddle", Zone().Lands(0).def));
  ASSERT_FALSE(env.SetCellTag({3, 2}, "puddle", Zone().Lasts(3).Then("steam").Hurts(-1).def));
  ASSERT_FALSE(env.SetCellTag({3, 2}, "puddle", Zone().Lasts(3).Then(too_long).def));
  ASSERT_FALSE(env.SetCellTag({3, 2}, too_long, Zone().def));
  ASSERT_FALSE(env.SetCellTag({12, 2}, "puddle", Zone().def));  // Out of bounds
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  ASSERT_EQ(Id(env, "puddle"), kInvalidTag);  // A refusal interns nothing
  ASSERT_EQ(Id(env, "steam"), kInvalidTag);

  ASSERT_TRUE(env.SetCellTag({3, 2}, "puddle", Zone().Lands(2).Lasts(3).Then("steam").Hurts(1).def));
  const BaseEnv::CellTag z = env.GetCellTag({3, 2});
  ASSERT_EQ(z.tag, Id(env, "puddle"));
  ASSERT_EQ(z.duration, 2);
  ASSERT_EQ(z.steps, 3);  // Set between steps: 3 steps to come
  ASSERT_EQ(z.then, Id(env, "steam"));
  ASSERT_EQ(z.damage, 1);

  ASSERT_TRUE(env.SetCellTag({3, 2}, "", Zone().Lasts(3).Hurts(2).def));  // "" clears
  const BaseEnv::CellTag cleared = env.GetCellTag({3, 2});
  ASSERT_EQ(cleared.tag, kInvalidTag);
  ASSERT_EQ(cleared.steps, kPermanentTag);
  ASSERT_EQ(cleared.then, kInvalidTag);
  ASSERT_EQ(cleared.damage, 0);
}

// =============================================================================
// The level's zone table
// =============================================================================

TEST(TestDefineZoneValidatesAndReplaces) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  const std::string too_long(static_cast<size_t>(kMaxNameLength) + 1, 'x');
  ASSERT_FALSE(env.DefineZone("", Zone().def));
  ASSERT_FALSE(env.DefineZone(too_long, Zone().def));
  ASSERT_FALSE(env.DefineZone("burning", Zone().Lasts(0).def));
  ASSERT_FALSE(env.DefineZone("burning", Zone().Lands(-3).def));
  ASSERT_FALSE(env.DefineZone("burning", Zone().Hurts(-1).def));
  ASSERT_FALSE(env.DefineZone("burning", Zone().Then(too_long).def));
  ASSERT_TRUE(env.GetZoneDefs().empty());

  ASSERT_TRUE(env.DefineZone("burning", Zone().Lasts(6).Hurts(1).def));
  ASSERT_TRUE(env.DefineZone("burning", Zone().Lasts(4).Then("smoke").Hurts(2).def));  // Replaces
  ASSERT_EQ(env.GetZoneDefs().size(), static_cast<size_t>(1));
  const ZoneDef burning = env.GetZoneDef("burning");
  ASSERT_EQ(burning.duration, kPermanentTag);
  ASSERT_EQ(burning.steps, 4);
  ASSERT_EQ(burning.then, std::string("smoke"));
  ASSERT_EQ(burning.damage, 2);
  const ZoneDef smoke = env.GetZoneDef("smoke");  // Undefined: the defaults
  ASSERT_EQ(smoke.duration, kPermanentTag);
  ASSERT_EQ(smoke.steps, kPermanentTag);
  ASSERT_EQ(smoke.then, std::string(""));
  ASSERT_EQ(smoke.damage, 0);
}

// A zone created by name takes the table's fields; the explicit duration
// overrides the landing duration only, an explicit ZoneDef everything. Each
// cell keeps its resolved copy: redefining a tag changes only later zones.
TEST(TestZonesByNameTakeTheTablesFields) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("burning", Zone().Lands(2).Lasts(6).Then("smoke").Hurts(1).def));
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning"));
  BaseEnv::CellTag z = env.GetCellTag({3, 2});
  ASSERT_EQ(z.duration, 2);
  ASSERT_EQ(z.steps, 6);
  ASSERT_EQ(z.then, Id(env, "smoke"));
  ASSERT_EQ(z.damage, 1);
  ASSERT_TRUE(env.SetCellTag({3, 3}, "burning", 5));
  z = env.GetCellTag({3, 3});
  ASSERT_EQ(z.duration, 5);
  ASSERT_EQ(z.steps, 6);
  ASSERT_EQ(z.damage, 1);
  ASSERT_TRUE(env.SetCellTag({3, 4}, "burning", Zone().def));  // The override
  z = env.GetCellTag({3, 4});
  ASSERT_EQ(z.steps, kPermanentTag);
  ASSERT_EQ(z.damage, 0);

  ASSERT_TRUE(env.DefineZone("burning", Zone().Hurts(3).def));
  ASSERT_EQ(env.GetCellTag({3, 2}).damage, 1);  // Already there: unchanged
  ASSERT_TRUE(env.SetCellTag({3, 5}, "burning"));
  ASSERT_EQ(env.GetCellTag({3, 5}).damage, 3);
}

// burning (2 steps, 1 damage) -> smoke (3 steps) -> nothing, through the table
TEST(TestTheZoneTableChainsSuccessors) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("burning", Zone().Lands(1).Lasts(2).Then("smoke").Hurts(1).def));
  ASSERT_TRUE(env.DefineZone("smoke", Zone().Lands(1).Lasts(3).def));
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(10);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning"));
  for (int i = 1; i <= 2; ++i) {
    env.Step({kStay});
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastTagsApplied()[0].tag, Id(env, "burning"));
    ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 1);
    ASSERT_EQ(a->GetHealth(), 10 - i);
  }
  const BaseEnv::CellTag smoke = env.GetCellTag({3, 2});  // Its successor, by name
  ASSERT_EQ(smoke.tag, Id(env, "smoke"));
  ASSERT_EQ(smoke.duration, 1);
  ASSERT_EQ(smoke.steps, 3);  // Its 3 next steps
  ASSERT_EQ(smoke.then, kInvalidTag);
  ASSERT_EQ(smoke.damage, 0);
  for (int i = 0; i < 3; ++i) {
    env.Step({kStay});
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastTagsApplied()[0].tag, Id(env, "smoke"));
    ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 0);
  }
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  env.Step({kStay});
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_EQ(a->GetHealth(), 8);
}

// Cycles are legal: a zone whose successor is itself renews every `steps`,
// and a -> b -> a alternates, one zone per tick.
TEST(TestZoneCyclesAdvanceOneZonePerTick) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("flicker", Zone().Lasts(2).Then("flicker").def));
  ASSERT_TRUE(env.DefineZone("tide", Zone().Lasts(1).Then("ebb").def));
  ASSERT_TRUE(env.DefineZone("ebb", Zone().Lasts(1).Then("tide").def));
  Agent* a = Place(env, 0, {3, 2});
  ASSERT_TRUE(env.SetCellTag({3, 2}, "flicker"));
  ASSERT_TRUE(env.SetCellTag({4, 2}, "tide"));
  for (int i = 1; i <= 6; ++i) {
    env.Step({kStay});
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));  // Every step
    ASSERT_EQ(env.GetLastTagsApplied()[0].agent, a->GetId());
    ASSERT_EQ(env.GetCellTag({3, 2}).tag, Id(env, "flicker"));
    ASSERT_EQ(env.GetCellTag({3, 2}).steps, i % 2 == 1 ? 1 : 2);
    ASSERT_EQ(env.GetCellTag({4, 2}).tag, Id(env, i % 2 == 1 ? "ebb" : "tide"));
  }
}

// The table is level data: copied with the env, kept across a generated
// Reset, replaced by LoadSnapshot with the snapshot's (v7; none in older ones).
TEST(TestTheZoneTableIsLevelData) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("burning", Zone().Lasts(6).Hurts(1).def));
  std::unique_ptr<BaseEnv> copy = env.Clone();
  ASSERT_EQ(copy->GetZoneDef("burning").steps, 6);
  SynchroEnv assigned(10, 10, 1, 1, 0, 7);
  assigned = env;
  ASSERT_EQ(assigned.GetZoneDef("burning").damage, 1);

  env.Reset();  // A generated level: kept
  ASSERT_EQ(env.GetZoneDef("burning").steps, 6);

  Snapshot saved = env.SaveSnapshot();
  ASSERT_TRUE(env.DefineZone("mud", Zone().def));
  env.LoadSnapshot(saved);  // The snapshot's table, exactly
  ASSERT_EQ(env.GetZoneDefs().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetZoneDef("burning").steps, 6);
  saved.zones.clear();
  env.LoadSnapshot(saved);  // A snapshot without a table: none
  ASSERT_TRUE(env.GetZoneDefs().empty());
  ASSERT_EQ(copy->GetZoneDef("burning").steps, 6);  // Deep copies keep theirs
}

// =============================================================================
// Lifetime (a step timer)
// =============================================================================

// Set between two steps with steps = 2: it lands during the 2 next steps and
// is gone after the second.
TEST(TestZoneSetBetweenStepsLastsItsSteps) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", Zone().Lands(1).Lasts(2).def));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 2);
  env.Step({kStay});
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 1);
  env.Step({kStay});
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);  // Expired, no successor
  ASSERT_TRUE(Has(env, a, "wet"));                     // Its last landing: 1 step to come
  env.Step({kStay});
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_FALSE(Has(env, a, "wet"));
}

// Set during a step with steps = 2: it also covers the rest of that step (it
// lands there, after the movement), then reads 2 and lasts the 2 next steps.
TEST(TestZoneSetDuringAStepCoversItThenItsSteps) {
  MidStepZoneEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 2});
  env.pending = MidStepZoneEnv::Pending{{3, 2}, "wet", Zone().Lands(1).Lasts(2).def};
  env.Step({kStay});  // t: set in PreStep, landed after the movement
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 2);
  env.Step({kStay});  // t + 1
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 1);
  env.Step({kStay});  // t + 2, its last
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  env.Step({kStay});
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

// An expired zone becomes its successor (undefined in the table: the
// defaults), which lands from the next step.
TEST(TestZoneExpiresIntoItsSuccessor) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  a->SetMaxHealth(5);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning", Zone().Lands(2).Lasts(1).Then("ash").Hurts(1).def));
  env.Step({kRight});  // Walks in: burns, then the zone expires
  ASSERT_TRUE(a->GetPosition() == (Position{3, 2}));
  ASSERT_TRUE(Has(env, a, "burning"));
  ASSERT_EQ(a->GetHealth(), 4);
  const BaseEnv::CellTag ash = env.GetCellTag({3, 2});
  ASSERT_EQ(ash.tag, Id(env, "ash"));
  ASSERT_EQ(ash.duration, kPermanentTag);
  ASSERT_EQ(ash.steps, kPermanentTag);
  ASSERT_EQ(ash.then, kInvalidTag);
  ASSERT_EQ(ash.damage, 0);
  ASSERT_FALSE(Has(env, a, "ash"));  // Not landed in the step it appeared at the end of
  for (int i = 0; i < 3; ++i) {
    env.Step({kStay});
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastTagsApplied()[0].tag, Id(env, "ash"));
    ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 0);
  }
  ASSERT_EQ(a->GetHealth(), 4);
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, Id(env, "ash"));
}

// A zone's timer is world state: copies keep it and tick on their own.
TEST(TestZoneTimersAreCopiedWithTheEnv) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", Zone().Lasts(3).Then("mud").Hurts(2).def));
  env.Step({kStay});
  std::unique_ptr<BaseEnv> copy = env.Clone();
  ASSERT_EQ(copy->GetCellTag({3, 2}).steps, 2);
  ASSERT_EQ(copy->GetCellTag({3, 2}).then, copy->GetTagTable().Find("mud"));
  ASSERT_EQ(copy->GetCellTag({3, 2}).damage, 2);
  copy->Step({kStay});
  copy->Step({kStay});
  ASSERT_EQ(copy->GetCellTag({3, 2}).tag, copy->GetTagTable().Find("mud"));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 2);  // The original did not tick
}

// =============================================================================
// Damage per landing
// =============================================================================

TEST(TestZoneDamagesEveryLanding) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(5);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning", Zone().Lands(1).Hurts(1).def));
  for (int i = 1; i <= 3; ++i) {
    env.Step({kStay});
    ASSERT_EQ(a->GetHealth(), 5 - i);
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    const BaseEnv::TagApplication& landed = env.GetLastTagsApplied()[0];
    ASSERT_EQ(landed.agent, a->GetId());
    ASSERT_EQ(landed.cause, std::string("zone"));
    ASSERT_EQ(landed.damage, 1);
  }
  env.Step({kRight});  // Off the zone: no landing, no damage
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_EQ(a->GetHealth(), 2);
}

// The damage goes into the turn's total: Marked applies to it (the report
// keeps the zone's own damage, as SkillConfig::damage is before Marked).
TEST(TestZoneDamageIsMarked) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(5);
  a->ApplyStatus(StatusType::Marked, 3);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning", Zone().Lands(1).Hurts(2).def));
  env.Step({kStay});
  ASSERT_EQ(a->GetHealth(), 2);  // 2 x 1.5
  ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 2);
}

// A skill motion that lands an agent on a zone lands its tag and its damage;
// the skill's own landings report no damage (the SkillUse's Damage effect
// says what a skill hurt).
TEST(TestZoneDamageFollowsSkillMotions) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 5}, "burning", Zone().Lands(1).Hurts(1).def));
  Agent* caster = Place(env, 0, {3, 1});
  Agent* beside = Place(env, 1, {2, 5});  // Orthogonal to the landing cell
  caster->SetMaxHealth(5);
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, "lightningStep"));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
  ASSERT_EQ(caster->GetHealth(), 4);
  const auto& landed = env.GetLastTagsApplied();
  ASSERT_EQ(landed.size(), static_cast<size_t>(2));
  ASSERT_EQ(landed[0].agent, caster->GetId());
  ASSERT_EQ(landed[0].cause, std::string("zone"));
  ASSERT_EQ(landed[0].damage, 1);
  ASSERT_EQ(landed[1].agent, beside->GetId());
  ASSERT_EQ(landed[1].cause, std::string("lightningStep"));
  ASSERT_EQ(landed[1].damage, 0);
}

// A zone's damage downs a companion like any damage (reported with the
// step's downs, its tags kept); downed, the zone no longer touches it.
TEST(TestZoneDamageDownsACompanionThenLeavesItUntouched) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(3);
  a->RestoreHealth(1);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning", Zone().Hurts(1).def));
  env.Step({kStay, kStay});
  ASSERT_TRUE(a->IsDowned());
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDowns()[0], a->GetId());
  ASSERT_TRUE(Has(env, a, "burning"));  // Landed, then the damage downed it
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 1);
  for (int i = 0; i < 2; ++i) {
    env.Step({kStay, kStay});
    ASSERT_TRUE(env.GetLastTagsApplied().empty());
    ASSERT_TRUE(env.GetLastDowns().empty());
    ASSERT_EQ(a->GetHealth(), 0);
    ASSERT_EQ(dynamic_cast<Companion*>(a)->GetTimesDowned(), 1);
  }
}

// An Aggro goblin with an attack, rooted for its first 3 steps, and its
// companion next to it (50 HP); on `lethal_zone`, a zone under the goblin
// whose damage per landing is the goblin's health.
struct GoblinBesideCompanion {
  AggroEnv env{10, 1, EnemyType::Goblin, 42, 0, 100};
  Companion* comp = nullptr;
  AgentFSM* enemy = nullptr;
  Position cell;
  std::vector<Action> stay;

  explicit GoblinBesideCompanion(bool lethal_zone) {
    env.Reset(42);
    comp = env.GetMutableObjectManager().GetAllCompanions()[0];
    enemy = env.GetMutableObjectManager().GetAllAgentFSMs()[0];
    enemy->GetFSMContext().has_attack = true;
    enemy->GetFSMContext().attack_effect_name = "goblin_attack";  // A builtin
    cell = enemy->GetPosition();
    bool placed = false;
    for (Position p : {Position{cell.row, cell.col - 1}, Position{cell.row, cell.col + 1},
                       Position{cell.row - 1, cell.col}, Position{cell.row + 1, cell.col}}) {
      if (!placed && env.GetGrid().IsWalkable(p) && !env.GetObjectManager().GetActorAt(p)) {
        env.GetMutableObjectManager().UpdatePosition(comp->GetId(), p);
        placed = true;
      }
    }
    if (!placed) throw std::runtime_error("no cell beside the goblin");
    comp->SetMaxHealth(50);
    enemy->ApplyStatus(StatusType::Rooted, 3);  // It stays on its cell
    if (lethal_zone && !env.SetCellTag(cell, "burning", Zone().Hurts(enemy->GetHealth()).def)) {
      throw std::runtime_error("zone refused");
    }
    stay.assign(static_cast<size_t>(env.NumAgents()), kStay);
  }
};

// The control: without the zone, the goblin strikes its companion within 20 steps.
TEST(TestTheGoblinBesideItsCompanionStrikes) {
  GoblinBesideCompanion s(false);
  for (int i = 0; i < 20; ++i) s.env.Step(s.stay);
  ASSERT_TRUE(s.enemy->IsAlive());
  ASSERT_TRUE(s.comp->GetHealth() < 50);
}

// An enemy a zone's damage kills neither acts nor strikes afterwards: it
// stays where it died, and its target takes nothing more (the control above
// shows it would have).
TEST(TestAnEnemyKilledByAZoneNeitherActsNorStrikes) {
  GoblinBesideCompanion s(true);
  s.env.Step(s.stay);  // Dies on its zone
  ASSERT_FALSE(s.enemy->IsAlive());
  const int health = s.comp->GetHealth();
  for (int i = 0; i < 20; ++i) {
    s.env.Step(s.stay);
    ASSERT_TRUE(s.enemy->GetPosition() == s.cell);
    ASSERT_TRUE(s.env.GetLastTagsApplied().empty());  // The zone skips the dead
  }
  ASSERT_EQ(s.comp->GetHealth(), health);
  ASSERT_TRUE(s.env.GetActiveEffects().empty());
}

// A Step that throws mid-way leaves the env in a step; LoadSnapshot ends it,
// so a zone set after the load reads its steps, not steps + 1.
TEST(TestLoadSnapshotEndsAStepThatThrew) {
  MidStepZoneEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Snapshot saved = env.SaveSnapshot();
  env.pending = MidStepZoneEnv::Pending{{3, 2}, "wet", Zone().Lasts(0).def};  // Refused
  bool threw = false;
  try {
    env.Step({kStay});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  env.LoadSnapshot(saved);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", Zone().Lasts(2).def));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 2);
}

// A caster its landing zone takes to 0 goes down at the end of the turn, so
// it still gets its own use (tags and damage, reported), and the others are
// hit too.
TEST(TestACasterItsLandingZoneDownsStillGetsItsOwnUse) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig blaze;
  blaze.name = "blaze";
  blaze.targeting = SkillTargeting::Self;
  blaze.area = SkillArea::Cross;  // Around the landing cell
  blaze.motion = SkillMotion::Dash;
  blaze.motion_distance = 4;
  blaze.tags = {{"hot", 1}};
  blaze.damage = 1;  // Self tags and self damage: on by default
  env.GetMutableSkillBook().Define(blaze);
  ASSERT_TRUE(env.SetCellTag({3, 5}, "burning", Zone().Hurts(1).def));
  Agent* caster = Place(env, 0, {3, 1});
  Agent* other = Place(env, 1, {2, 5});  // Up of the landing cell
  caster->SetMaxHealth(3);
  caster->RestoreHealth(1);
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, "blaze"));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(caster->IsDowned());
  ASSERT_TRUE(Has(env, caster, "hot"));  // Tags stay through a down
  ASSERT_TRUE(Has(env, other, "hot"));
  ASSERT_EQ(other->GetHealth(), 2);
  // The zone's, then the use's: the caster (the centre), then other
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(3));
  ASSERT_EQ(env.GetLastTagsApplied()[1].agent, caster->GetId());
  ASSERT_EQ(env.GetLastTagsApplied()[2].agent, other->GetId());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  const auto& affected = env.GetLastSkillUses()[0].affected;
  const unsigned hit = BaseEnv::kSkillEffectTags | BaseEnv::kSkillEffectDamage;
  ASSERT_EQ(affected.size(), static_cast<size_t>(2));
  ASSERT_EQ(affected[0].id, caster->GetId());
  ASSERT_EQ(affected[0].effects, hit);
  ASSERT_EQ(affected[1].id, other->GetId());
  ASSERT_EQ(affected[1].effects, hit);
  ASSERT_EQ(env.GetLastTurnHealth().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastTurnHealth()[0].agent, caster->GetId());
  ASSERT_EQ(env.GetLastTurnHealth()[0].damage, 2);  // Its zone's and its own
  ASSERT_TRUE(env.GetLastTurnHealth()[0].outcome == BaseEnv::TurnOutcome::Downed);
}

// =============================================================================
// Snapshots (v7: the zone table and each cell's fields)
// =============================================================================

TEST(TestADefaultZoneRoundTripsThroughASnapshot) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", 2));
  Snapshot saved = env.SaveSnapshot();
  env.ClearCellTags();
  env.LoadSnapshot(saved);
  const BaseEnv::CellTag z = env.GetCellTag({3, 2});
  ASSERT_EQ(z.tag, Id(env, "wet"));
  ASSERT_EQ(z.duration, 2);
  ASSERT_EQ(z.steps, kPermanentTag);
  ASSERT_EQ(z.then, kInvalidTag);
  ASSERT_EQ(z.damage, 0);
}

static Snapshot BinaryRoundTrip(const Snapshot& s) { return Snapshot::Deserialize(s.Serialize()); }

// A zone cell by names: tag, landing duration, steps, successor, damage
static std::string Describe(const BaseEnv& env, Position p) {
  const BaseEnv::CellTag c = env.GetCellTag(p);
  if (c.tag == kInvalidTag) return "-";
  std::ostringstream out;
  out << env.GetTagTable().Name(c.tag) << " " << c.duration << " " << c.steps << " "
      << (c.then == kInvalidTag ? "" : env.GetTagTable().Name(c.then)) << " " << c.damage;
  return out.str();
}

static void RequireSame(const std::string& a, const std::string& b, const char* what) {
  if (a != b) throw std::runtime_error(std::string(what) + ": '" + a + "' != '" + b + "'");
}

// A zone mid-life keeps its remaining steps across a save and a load, so it
// expires on the same step as in the world never saved, and becomes its
// successor from the loaded table.
TEST(TestAZoneMidLifeKeepsItsTimerAcrossSaveAndLoad) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("burning", Zone().Lasts(4).Then("smoke").Hurts(1).def));
  ASSERT_TRUE(env.DefineZone("smoke", Zone().Lasts(2).Lands(1).def));
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(20);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "burning"));
  env.Step({kStay});
  env.Step({kStay});
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, 2);  // Mid-life: 2 steps to come

  std::unique_ptr<BaseEnv> never_saved = env.Clone();
  SynchroEnv loaded(10, 10, 1, 1, 0, 7);
  loaded.GetMutableTagTable().Intern("unrelated");  // Other ids: names travel
  loaded.LoadSnapshot(BinaryRoundTrip(env.SaveSnapshot()));
  RequireSame(Describe(loaded, {3, 2}), "burning -1 2 smoke 1", "loaded");
  Agent* b = loaded.GetMutableObjectManager().GetAllAgents().at(0);
  for (int i = 1; i <= 5; ++i) {
    never_saved->Step({kStay});
    loaded.Step({kStay});
    RequireSame(Describe(loaded, {3, 2}), Describe(*never_saved, {3, 2}), "zone");
    ASSERT_EQ(b->GetHealth(), never_saved->GetMutableObjectManager().GetAllAgents().at(0)->GetHealth());
    ASSERT_EQ(loaded.GetLastTagsApplied().size(), never_saved->GetLastTagsApplied().size());
  }
  // burning expired after 2 more steps, smoke after 2 more: nothing left
  ASSERT_EQ(loaded.GetCellTag({3, 2}).tag, kInvalidTag);
  ASSERT_EQ(b->GetHealth(), 20 - 4);  // 2 burning landings before, 2 after the load
}

// Each cell keeps its own resolved copy through a save and a load: a per-cell
// override, and a cell created before its tag was redefined. The table loads
// too, so a zone created by name afterwards takes the new definition.
TEST(TestCellsKeepTheirOwnFieldsAcrossSaveAndLoad) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.DefineZone("wet", Zone().Lasts(5).def));
  ASSERT_TRUE(env.SetCellTag({2, 2}, "wet", Zone().Lands(2).Lasts(3).Then("ice").Hurts(2).def));
  ASSERT_TRUE(env.SetCellTag({2, 3}, "wet"));  // The table as it is now
  ASSERT_TRUE(env.DefineZone("wet", Zone().Lasts(9).Hurts(3).def));
  SynchroEnv loaded(10, 10, 1, 1, 0, 7);
  loaded.LoadSnapshot(BinaryRoundTrip(env.SaveSnapshot()));
  RequireSame(Describe(loaded, {2, 2}), "wet 2 3 ice 2", "override");
  RequireSame(Describe(loaded, {2, 3}), "wet -1 5  0", "created before the redefinition");
  ASSERT_TRUE(loaded.GetZoneDef("wet") == Zone().Lasts(9).Hurts(3).def);
  ASSERT_TRUE(loaded.SetCellTag({2, 4}, "wet"));
  RequireSame(Describe(loaded, {2, 4}), "wet -1 9  3", "created after the load");
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

  std::cout << "Running " << tests.size() << " zone tests...\n\n";

  int passed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    test.func();
    std::cout << "[       OK ] " << test.name << "\n";
    passed++;
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return 0;
}
