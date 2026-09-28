// Copyright 2024
// Unit tests for zones (cell tags): their lifetime, their successor and their
// damage per landing

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

// Sets a zone during the next Step (in PreStep, before the movement), as a
// rule of the env would: a timer set during a step.
class MidStepZoneEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  std::optional<std::pair<Position, ZoneSpec>> pending;

 protected:
  void PreStep() override {
    SynchroEnv::PreStep();
    if (pending) {
      if (!SetCellTag(pending->first, pending->second)) throw std::runtime_error("zone refused");
      pending.reset();
    }
  }
};

// =============================================================================
// The zone as data
// =============================================================================

// The duration-only primitive gives the defaults: permanent, no successor, no
// damage, and the zone stays.
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
    ASSERT_EQ(env.GetLastTagsApplied()[0].damage, 0);
  }
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, Id(env, "wet"));
  ASSERT_EQ(env.GetCellTag({3, 2}).steps, kPermanentTag);
  ASSERT_EQ(a->GetHealth(), health);
}

TEST(TestZoneSpecIsValidatedBeforeAnythingChanges) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  const std::string too_long(static_cast<size_t>(kMaxNameLength) + 1, 'x');
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", kPermanentTag, 0}));   // steps 0
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", kPermanentTag, -2}));  // below permanent
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", 0}));                  // duration 0
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", kPermanentTag, 3, "steam", -1}));
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", kPermanentTag, 3, too_long}));
  ASSERT_FALSE(env.SetCellTag({3, 2}, ZoneSpec{too_long}));
  ASSERT_FALSE(env.SetCellTag({12, 2}, ZoneSpec{"puddle"}));  // Out of bounds
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  ASSERT_EQ(Id(env, "puddle"), kInvalidTag);  // A refusal interns nothing
  ASSERT_EQ(Id(env, "steam"), kInvalidTag);

  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"puddle", 2, 3, "steam", 1}));
  const BaseEnv::CellTag z = env.GetCellTag({3, 2});
  ASSERT_EQ(z.tag, Id(env, "puddle"));
  ASSERT_EQ(z.duration, 2);
  ASSERT_EQ(z.steps, 3);  // Set between steps: 3 steps to come
  ASSERT_EQ(z.then, Id(env, "steam"));
  ASSERT_EQ(z.damage, 1);

  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{}));  // "" clears, whatever the fields
  const BaseEnv::CellTag cleared = env.GetCellTag({3, 2});
  ASSERT_EQ(cleared.tag, kInvalidTag);
  ASSERT_EQ(cleared.steps, kPermanentTag);
  ASSERT_EQ(cleared.then, kInvalidTag);
  ASSERT_EQ(cleared.damage, 0);
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
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"wet", 1, 2}));
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
  env.pending = std::make_pair(Position{3, 2}, ZoneSpec{"wet", 1, 2});
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

// An expired zone becomes its successor: a zone with default fields
// (permanent, landing a permanent tag, harmless, without successor), which
// lands from the next step.
TEST(TestZoneExpiresIntoItsSuccessor) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  a->SetMaxHealth(5);
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"burning", 2, 1, "ash", 1}));
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
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"wet", kPermanentTag, 3, "mud", 2}));
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
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"burning", 1, kPermanentTag, "", 1}));
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

// The damage goes through Agent::TakeDamage: Marked applies (the report keeps
// the zone's own damage, as SkillConfig::damage is before Marked).
TEST(TestZoneDamageIsMarked) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  a->SetMaxHealth(5);
  a->ApplyStatus(StatusType::Marked, 3);
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"burning", 1, kPermanentTag, "", 2}));
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
  ASSERT_TRUE(env.SetCellTag({3, 5}, ZoneSpec{"burning", 1, kPermanentTag, "", 1}));
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
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"burning", kPermanentTag, kPermanentTag, "", 1}));
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

// A caster its landing zone downs gets nothing from its own skill: the use
// reports neither Tags nor Damage on it (what the use DID).
TEST(TestACasterDownedByItsLandingZoneGetsNothingFromItsSkill) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  SkillConfig blaze;
  blaze.name = "blaze";
  blaze.targeting = SkillTargeting::Self;
  blaze.motion = SkillMotion::Dash;
  blaze.motion_distance = 4;
  blaze.tags = {{"hot", 1}};
  blaze.damage = 1;  // Self tags and self damage: on by default
  env.GetMutableSkillBook().Define(blaze);
  ASSERT_TRUE(env.SetCellTag({3, 5}, ZoneSpec{"burning", kPermanentTag, kPermanentTag, "", 1}));
  Agent* caster = Place(env, 0, {3, 1});
  caster->SetMaxHealth(3);
  caster->RestoreHealth(1);
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, "blaze"));
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(caster->IsDowned());
  ASSERT_FALSE(Has(env, caster, "hot"));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));  // The zone's only
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  const auto& affected = env.GetLastSkillUses()[0].affected;
  ASSERT_EQ(affected.size(), static_cast<size_t>(1));
  ASSERT_EQ(affected[0].id, caster->GetId());
  ASSERT_EQ(affected[0].effects, 0u);
}

// =============================================================================
// Snapshots (the new fields come with snapshot v7)
// =============================================================================

TEST(TestADefaultZoneRoundTripsThroughASnapshot) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, ZoneSpec{"wet", 2}));
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
