// Copyright 2024
// Unit tests for revive: skills that affect the downed, the revive builtin

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/context_skill.h"
#include "../src/core/object.h"
#include "../src/core/skill_config.h"
#include "../src/core/snapshot_json.h"
#include "../src/env/aggro_env.h"
#include "../src/env/dodge_env.h"
#include "../src/env/synchro_env.h"
#include "effect_registry_guard.h"

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
// Companion::Revive
// =============================================================================
TEST(TestReviveStandsADownedCompanion) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(5);
  c.ApplyTag(0, kPermanentTag);
  c.TakeDamage(5);
  ASSERT_TRUE(c.IsDowned());
  ASSERT_TRUE(c.Revive(3));
  ASSERT_FALSE(c.IsDowned());
  ASSERT_TRUE(c.IsAlive());
  ASSERT_TRUE(c.IsAffectable());
  ASSERT_EQ(c.GetHealth(), 3);
  ASSERT_EQ(c.GetTimesDowned(), 1);  // Every down still counts
  ASSERT_TRUE(c.HasTag(0));          // Kept through the down
  ASSERT_TRUE(c.GetStatuses().empty());

  // The health is clamped to [1, max]
  c.TakeDamage(3);
  ASSERT_TRUE(c.Revive(0));
  ASSERT_EQ(c.GetHealth(), 1);
  c.TakeDamage(1);
  ASSERT_TRUE(c.Revive(99));
  ASSERT_EQ(c.GetHealth(), 5);
  ASSERT_EQ(c.GetTimesDowned(), 3);
}

TEST(TestReviveDoesNothingToTheStandingOrTheDead) {
  Companion standing(0, {1, 1});
  standing.SetMaxHealth(5);
  standing.TakeDamage(3);
  ASSERT_FALSE(standing.Revive(4));
  ASSERT_EQ(standing.GetHealth(), 2);

  Companion dead(1, {2, 2});
  dead.SetMaxHealth(3);
  dead.RestoreHealth(0);
  dead.SetAlive(false);
  ASSERT_FALSE(dead.Revive(2));
  ASSERT_FALSE(dead.IsAlive());
  ASSERT_EQ(dead.GetHealth(), 0);
}

// =============================================================================
// Validation
// =============================================================================
static std::string ValidationError(const SkillConfig& s) {
  try {
    ValidateSkillConfig(s);
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return "";
}

static bool Mentions(const std::string& message, const std::string& part) {
  return message.find(part) != std::string::npos;
}

static SkillConfig Raise() {
  SkillConfig s;
  s.name = "raise";
  s.affects_downed = true;
  s.revive_percent = 100;
  return s;
}

TEST(TestReviveFieldsValidate) {
  ASSERT_EQ(ValidationError(Raise()), std::string(""));
  SkillConfig s = Raise();
  s.revive_percent = 0;  // Affects the downed, does nothing to them: allowed
  ASSERT_EQ(ValidationError(s), std::string(""));

  s = Raise();
  s.revive_percent = 101;
  std::string e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "revive_percent"));
  s.revive_percent = -1;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "revive_percent"));

  s = Raise();
  s.affects_downed = false;  // Reviving needs the downed
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "revive_percent") &&
              Mentions(e, "affects_downed"));

  // Only companions go down and the caster is one: without friendly fire, a
  // skill that affects the downed could never affect anyone
  s = Raise();
  s.friendly_fire = false;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "friendly_fire") &&
              Mentions(e, "affects_downed"));
  s.revive_percent = 0;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "friendly_fire"));
}

// A skill that affects the downed can only revive.
TEST(TestADownedSkillOnlyRevives) {
  SkillConfig s = Raise();
  s.tags = {{"blessed", kPermanentTag}};
  std::string e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "tags") && Mentions(e, "affects_downed"));

  s = Raise();
  s.damage = 1;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "damage") && Mentions(e, "affects_downed"));

  s = Raise();
  s.root_steps = 1;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "root_steps") &&
              Mentions(e, "affects_downed"));

  s = Raise();
  s.motion = SkillMotion::PushOut;
  s.area = SkillArea::Cross;
  s.motion_distance = 1;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "motion") && Mentions(e, "affects_downed"));

  s = Raise();
  s.motion = SkillMotion::Teleport;
  s.targeting = SkillTargeting::Self;
  s.motion_distance = 2;
  e = ValidationError(s);
  ASSERT_TRUE(Mentions(e, "raise") && Mentions(e, "motion"));

  SkillBook book;  // Define validates too
  s = Raise();
  s.damage = 2;
  bool threw = false;
  try {
    book.Define(s);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  ASSERT_TRUE(book.Find("raise") == nullptr);
}

// =============================================================================
// The builtin
// =============================================================================
TEST(TestReviveIsABuiltin) {
  SkillBook book;
  const SkillConfig* revive = book.Find("revive");
  ASSERT_TRUE(revive != nullptr);
  ASSERT_TRUE(revive->targeting == SkillTargeting::Projectile);
  ASSERT_EQ(revive->range, 1);
  ASSERT_TRUE(revive->filter == TargetFilter::Companion);
  ASSERT_TRUE(revive->area == SkillArea::Single);
  ASSERT_TRUE(revive->motion == SkillMotion::None);
  ASSERT_TRUE(revive->affects_downed);
  ASSERT_EQ(revive->revive_percent, 50);
  ASSERT_EQ(revive->cooldown, 0);
  ASSERT_TRUE(revive->friendly_fire);
  ASSERT_TRUE(revive->tags.empty());
  ASSERT_EQ(revive->damage, 0);
  ASSERT_EQ(revive->root_steps, 0);
  // Every other builtin affects the standing only
  for (const SkillConfig& s : book.All()) {
    ASSERT_EQ(s.affects_downed, s.name == "revive");
    ASSERT_EQ(s.revive_percent, s.name == "revive" ? 50 : 0);
  }
  // A level may retune it (it is not fixed like the attack)
  SkillConfig longer = *revive;
  longer.range = 2;
  book.Define(longer);
  ASSERT_EQ(book.Find("revive")->range, 2);
}

// =============================================================================
// Reviving in a step
// =============================================================================

// A 10x10 arena (from test_skills.cc): a wall border, floor inside (rows and
// cols 1-8), agents parked on row 8 (cols 1..n) until a test places them.
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

static Agent* DownCompanion(SynchroEnv& env, int index) {
  Agent* a = env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(index)];
  a->TakeDamage(a->GetHealth());
  return a;
}

static Action Use(MovementAction aim) { return EncodeAction(aim, InteractAction::Skill1); }
static const Action kStay = EncodeAction(MovementAction::Stay);
static const Action kRight = EncodeAction(MovementAction::Right);

// A lethal effect on `cell` that strikes during the next step, after the
// skills. Call under a ScopedEffectRegistry.
static void SpawnKillNextStep(BaseEnv& env, Position cell) {
  EffectConfig kill;
  kill.name = "kill_next_step";
  kill.telegraph_ticks = 1;
  kill.active_ticks = 1;
  kill.area = {1};
  kill.filter = TargetFilter::Companion;
  kill.damage = 999;
  EffectConfigRegistry::Instance().RegisterConfig(kill);
  env.SpawnEffect("kill_next_step", EffectTarget::AtCell(cell));
}

TEST(TestReviveInAStep) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 2});
  b->SetMaxHealth(5);
  DownCompanion(env, 1);
  env.Step({kStay, kStay});  // Reports the down
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastRevives().empty());

  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "revive"));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_FALSE(b->IsDowned());
  ASSERT_TRUE(b->IsAffectable());
  ASSERT_EQ(b->GetHealth(), 3);  // ceil(5 / 2)
  ASSERT_TRUE(b->GetPosition() == (Position{3, 2}));  // Where it lay
  ASSERT_EQ(env.GetDowns(), 1);
  ASSERT_TRUE(env.GetLastDowns().empty());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].caster, a->GetId());
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("revive"));
  ASSERT_EQ(env.GetLastSkillUses()[0].slot, 0);
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 2}));
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives()[0].reviver, a->GetId());
  ASSERT_EQ(env.GetLastRevives()[0].revived, b->GetId());
  ASSERT_EQ(env.GetLastRevives()[0].health, 3);  // The HP it got up with
  ASSERT_EQ(dynamic_cast<Companion*>(a)->GetCooldown(0), 0);  // No cooldown

  // It acts from the next step; the reports are per step
  ASSERT_TRUE(env.LegalActions(1).size() > static_cast<size_t>(1));
  env.Step({kStay, kRight});
  ASSERT_TRUE(b->GetPosition() == (Position{3, 3}));
  ASSERT_TRUE(env.GetLastRevives().empty());
}

// Aimed at a standing ally or at nothing: no effect, the use still reported.
TEST(TestReviveOnTheStandingOrNothing) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 2});
  b->SetMaxHealth(5);
  b->TakeDamage(3);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "revive"));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(b->GetHealth(), 2);  // Reviving is not healing
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastRevives().empty());

  env.Step({Use(MovementAction::Up), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{2, 1}));
  ASSERT_TRUE(env.GetLastRevives().empty());
}

// A standing-only skill passes over the downed; a skill that affects the
// downed passes over the standing.
TEST(TestSkillsPassOverWhomTheyDoNotAffect) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig bolt;
  bolt.name = "bolt";
  bolt.range = 2;
  bolt.tags = {{"zapped", kPermanentTag}};
  env.GetMutableSkillBook().Define(bolt);
  SkillConfig long_revive = *env.GetSkillBook().Find("revive");
  long_revive.name = "longRevive";
  long_revive.range = 2;
  env.GetMutableSkillBook().Define(long_revive);

  Agent* a = Place(env, 0, {3, 1});
  Agent* down = Place(env, 1, {3, 2});
  Agent* standing = Place(env, 2, {3, 3});
  DownCompanion(env, 1);
  ASSERT_TRUE(env.SetContextSkills({}));  // The bolt itself, not the context revive
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "bolt"));
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 3}));  // Past the downed
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].agent, standing->GetId());

  // Swap them: the downed behind a standing ally
  Place(env, 1, {5, 5});
  Place(env, 2, {3, 2});
  Place(env, 1, {3, 3});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "longRevive"));
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 3}));  // Past the standing
  ASSERT_FALSE(down->IsDowned());
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives()[0].revived, down->GetId());
}

// Two revivers on the same downed ally in one step: the first by agent index
// revives it, the other affects nothing (it no longer is downed).
TEST(TestTwoReviversReviveOnce) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* first = Place(env, 0, {3, 1});
  Agent* down = Place(env, 1, {3, 2});
  Agent* second = Place(env, 2, {3, 3});
  DownCompanion(env, 1);
  ASSERT_TRUE(env.SetCompanionSkill(first->GetId(), 0, "revive"));
  ASSERT_TRUE(env.SetCompanionSkill(second->GetId(), 0, "revive"));
  env.Step({Use(MovementAction::Right), kStay, Use(MovementAction::Left)});
  ASSERT_FALSE(down->IsDowned());
  ASSERT_EQ(down->GetHealth(), 2);  // ceil(3 / 2), once
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives()[0].reviver, first->GetId());
  ASSERT_EQ(env.GetLastRevives()[0].revived, down->GetId());
}

// Revived, then downed again in the same step: a revive, then a down, both
// reported; the counter counts every down.
TEST(TestRevivedAndDownedAgainInOneStep) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  env.Step({kStay, kStay});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "revive"));
  SpawnKillNextStep(env, {3, 2});
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(b->IsDowned());
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDowns()[0], b->GetId());
  ASSERT_EQ(env.GetDowns(), 2);
}

// The revive report is copied with the env and cleared by a snapshot load.
TEST(TestTheReviveReportIsCopiedAndCleared) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Place(env, 1, {3, 2});
  const Snapshot saved = env.SaveSnapshot();
  DownCompanion(env, 1);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "revive"));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  SynchroEnv copy(env);
  ASSERT_EQ(copy.GetLastRevives().size(), static_cast<size_t>(1));
  SynchroEnv assigned(10, 10, 2, 1, 0, 7);
  assigned = env;
  ASSERT_EQ(assigned.GetLastRevives().size(), static_cast<size_t>(1));
  env.LoadSnapshot(saved);
  ASSERT_TRUE(env.GetLastRevives().empty());
}

// =============================================================================
// Context skills
// =============================================================================
static Companion* AsCompanion(Agent* a) { return dynamic_cast<Companion*>(a); }

static size_t CountSkill1(const std::vector<Action>& actions) {
  size_t n = 0;
  for (Action act : actions) {
    if (DecodeAction(act).interact == InteractAction::Skill1) ++n;
  }
  return n;
}

TEST(TestContextConditionNames) {
  ASSERT_EQ(ContextConditionToString(ContextCondition::AdjacentDownedAlly),
            std::string("adjacent_downed_ally"));
  ASSERT_TRUE(ContextConditionFromString("adjacent_downed_ally") ==
              ContextCondition::AdjacentDownedAlly);
  ASSERT_TRUE(IsKnown(ContextCondition::AdjacentDownedAlly));
  for (int bad : {-1, 1, 99}) {
    ASSERT_FALSE(IsKnown(static_cast<ContextCondition>(bad)));
    ASSERT_EQ(ContextConditionToString(static_cast<ContextCondition>(bad)), std::string("unknown"));
  }
  bool threw = false;
  try {
    ContextConditionFromString("next_to_a_friend");
  } catch (const std::runtime_error& e) {
    threw = Mentions(e.what(), "next_to_a_friend");
  }
  ASSERT_TRUE(threw);

  const std::vector<ContextSkillRule> rules = DefaultContextSkills();
  ASSERT_EQ(rules.size(), static_cast<size_t>(1));
  ASSERT_TRUE(rules[0].condition == ContextCondition::AdjacentDownedAlly);
  ASSERT_EQ(rules[0].slot, 0);
  ASSERT_EQ(rules[0].skill, std::string("revive"));
}

// Next to a downed ally (4 neighbours), slot 0 is revive whatever it holds;
// away from it, the equipped skill again; slot 1 unchanged.
TEST(TestSlot0IsReviveNextToADownedAlly) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Companion* b = AsCompanion(Place(env, 1, {3, 2}));
  ASSERT_TRUE(env.GetContextSkills() == DefaultContextSkills());  // A fresh env
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "fireball"));
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("fireball"));  // b stands
  ASSERT_FALSE(env.IsContextSkill(*a, 0));

  DownCompanion(env, 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));
  ASSERT_TRUE(env.IsContextSkill(*a, 0));
  ASSERT_EQ(a->GetSkill(0), std::string("fireball"));  // Nothing swapped
  ASSERT_EQ(env.EffectiveSkill(*a, 1), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*a, 1));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, ""));  // The attack too
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));

  // Each of the 4 neighbours; not a diagonal, not two cells away
  for (Position p : {Position{2, 2}, Position{4, 2}, Position{3, 3}}) {
    Place(env, 0, p);
    ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));
  }
  Place(env, 0, {4, 3});
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
  Place(env, 0, {3, 4});
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));

  // An ally is of the same faction
  Place(env, 0, {3, 3});
  b->SetFaction(Faction::ENEMY);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  b->SetFaction(Faction::COMPANION);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));

  // The downed one cannot act, whatever its effective skills
  ASSERT_EQ(env.LegalActions(1).size(), static_cast<size_t>(1));
}

// A companion that cannot act (downed, dead) has no context skill: a downed
// one lying next to a downed ally shows its equipped skill.
TEST(TestADownedCompanionHasNoContextSkill) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Companion* b = AsCompanion(Place(env, 1, {3, 2}));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "fireball"));
  DownCompanion(env, 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));
  DownCompanion(env, 0);  // Both down, side by side
  ASSERT_TRUE(a->IsDowned());
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("fireball"));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
  ASSERT_EQ(env.EffectiveSkill(*b, 0), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*b, 0));
  // Revived: the context applies again (b still lies next to it)
  ASSERT_TRUE(a->Revive(1));
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("revive"));
  // Dead: none either
  a->SetAlive(false);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("fireball"));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
}

// A slot out of range has no skill: "" (as the C API reports for an agent
// without slots), never undefined behaviour.
TEST(TestEffectiveSkillOutOfRangeIsEmpty) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Place(env, 1, {3, 2});
  DownCompanion(env, 1);  // A rule applies to slot 0
  ASSERT_EQ(env.EffectiveSkill(*a, -1), std::string(""));
  ASSERT_EQ(env.EffectiveSkill(*a, kMaxSkillSlots), std::string(""));
  ASSERT_FALSE(env.IsContextSkill(*a, -1));
  ASSERT_FALSE(env.IsContextSkill(*a, kMaxSkillSlots));
}

// Using it in a step: whatever is equipped, the use is a revive, reported as
// such, and the equipped skill's cooldown is not spent.
TEST(TestAContextReviveInAStep) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Agent* b = Place(env, 1, {3, 2});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "fireball"));
  DownCompanion(env, 1);
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_FALSE(b->IsDowned());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("revive"));
  ASSERT_EQ(env.GetLastSkillUses()[0].slot, 0);
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());  // No fireball
  ASSERT_EQ(a->GetCooldown(0), 0);
  ASSERT_EQ(a->GetSkill(0), std::string("fireball"));
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("fireball"));  // b stands again
}

// Cooldowns belong to the equipped skill: the context revive neither reads nor
// spends the slot's.
TEST(TestAContextSkillIgnoresTheSlotCooldown) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Companion* a = AsCompanion(Place(env, 0, {3, 4}));
  Agent* b = Place(env, 1, {3, 2});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "fireball"));
  DownCompanion(env, 1);
  env.Step({Use(MovementAction::Up), kStay});  // Step 1: the fireball, far from b
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("fireball"));
  ASSERT_EQ(a->GetCooldown(0), 3);  // Usable again at step 5
  ASSERT_EQ(CountSkill1(env.LegalActions(0)), static_cast<size_t>(0));

  env.Step({EncodeAction(MovementAction::Left), kStay});  // Step 2: next to b
  ASSERT_TRUE(a->GetPosition() == (Position{3, 3}));
  ASSERT_EQ(a->GetCooldown(0), 2);
  // Usable at once, with every aim, though the fireball is cooling down
  ASSERT_EQ(CountSkill1(env.LegalActions(0)), static_cast<size_t>(kNumMovementActions));

  // Stepping away instead: the fireball, still blocked for its remaining steps
  SynchroEnv away(env);
  Companion* a2 = AsCompanion(away.GetMutableObjectManager().GetAllAgents()[0]);
  away.Step({EncodeAction(MovementAction::Right), kStay});  // Step 3
  ASSERT_EQ(away.EffectiveSkill(*a2, 0), std::string("fireball"));
  ASSERT_EQ(a2->GetCooldown(0), 1);
  ASSERT_EQ(CountSkill1(away.LegalActions(0)), static_cast<size_t>(0));
  away.Step({Use(MovementAction::Up), kStay});  // Step 4: dropped, the move applies
  ASSERT_TRUE(away.GetLastSkillUses().empty());
  ASSERT_TRUE(a2->GetPosition() == (Position{2, 4}));
  ASSERT_EQ(a2->GetCooldown(0), 0);
  away.Step({Use(MovementAction::Up), kStay});  // Step 5
  ASSERT_EQ(away.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(away.GetLastSkillUses()[0].skill, std::string("fireball"));

  // Reviving: the cooldown keeps ticking as before (neither reset nor spent)
  env.Step({Use(MovementAction::Left), kStay});  // Step 3
  ASSERT_FALSE(b->IsDowned());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("revive"));
  ASSERT_EQ(a->GetCooldown(0), 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("fireball"));
  ASSERT_EQ(CountSkill1(env.LegalActions(0)), static_cast<size_t>(0));
}

// The effective skill is fixed when intentions are read: a later caster whose
// downed neighbour an earlier caster revived in the same pass still uses the
// revive (it affects nothing), not its equipped skill.
TEST(TestTheContextIsReadWithTheIntentions) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Companion* first = AsCompanion(Place(env, 0, {3, 1}));
  Agent* down = Place(env, 1, {3, 2});
  Companion* second = AsCompanion(Place(env, 2, {3, 3}));
  ASSERT_TRUE(env.SetCompanionSkill(first->GetId(), 0, "fireball"));
  ASSERT_TRUE(env.SetCompanionSkill(second->GetId(), 0, "fireball"));
  DownCompanion(env, 1);
  env.Step({Use(MovementAction::Right), kStay, Use(MovementAction::Left)});
  ASSERT_FALSE(down->IsDowned());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("revive"));
  ASSERT_EQ(env.GetLastSkillUses()[1].skill, std::string("revive"));
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());  // No fireball
  ASSERT_EQ(first->GetCooldown(0), 0);
  ASSERT_EQ(second->GetCooldown(0), 0);
}

// Regression: the second reviver's equipped fireball is cooling down. The
// first revives the ally; the second still uses revive (affects nothing): its
// fireball is neither cast nor restarted, its cooldown keeps ticking.
TEST(TestALateReviverKeepsItsCooldown) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Companion* first = AsCompanion(Place(env, 0, {3, 1}));
  Agent* down = Place(env, 1, {3, 2});
  Companion* second = AsCompanion(Place(env, 2, {3, 3}));
  ASSERT_TRUE(env.SetCompanionSkill(second->GetId(), 0, "fireball"));
  second->SetCooldown(0, 2);
  DownCompanion(env, 1);
  ASSERT_EQ(CountSkill1(env.LegalActions(2)), static_cast<size_t>(kNumMovementActions));
  env.Step({Use(MovementAction::Right), kStay, Use(MovementAction::Left)});
  ASSERT_FALSE(down->IsDowned());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastSkillUses()[0].caster, first->GetId());
  ASSERT_EQ(env.GetLastSkillUses()[1].caster, second->GetId());
  ASSERT_EQ(env.GetLastSkillUses()[1].skill, std::string("revive"));
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives()[0].reviver, first->GetId());
  ASSERT_TRUE(env.GetLastTagsApplied().empty());  // No fireball
  ASSERT_EQ(second->GetCooldown(0), 1);           // Ticked, not restarted
  ASSERT_TRUE(second->GetPosition() == (Position{3, 3}));
  ASSERT_EQ(env.EffectiveSkill(*second, 0), std::string("fireball"));
  ASSERT_EQ(CountSkill1(env.LegalActions(2)), static_cast<size_t>(0));
}

// A rule giving a slot the skill it already holds is still a context use:
// neither reads nor spends the slot's cooldown (the origin decides).
TEST(TestARuleForTheEquippedSkillIsStillAContextUse) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(env.SetContextSkills({{ContextCondition::AdjacentDownedAlly, 0, "mend"}}));
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Agent* b = Place(env, 1, {3, 2});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "mend"));
  a->SetCooldown(0, 3);  // A host's cooldown on the equipped mend
  DownCompanion(env, 1);
  ASSERT_TRUE(env.IsContextSkill(*a, 0));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_FALSE(b->IsDowned());
  ASSERT_EQ(a->GetCooldown(0), 2);
}

// "" when accepted; else the message (the rules must be left unchanged).
static std::string RejectionOf(BaseEnv& env, std::vector<ContextSkillRule> rules) {
  std::string error;
  const std::vector<ContextSkillRule> before = env.GetContextSkills();
  if (env.SetContextSkills(std::move(rules), &error)) return "";
  if (!(env.GetContextSkills() == before)) return "rules changed on rejection";
  return error.empty() ? "no message" : error;
}

TEST(TestSetContextSkillsValidates) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;

  std::string e = RejectionOf(env, {{adj, 0, "nope"}});
  ASSERT_TRUE(Mentions(e, "nope") && Mentions(e, "unknown skill"));
  e = RejectionOf(env, {{adj, 0, ""}});
  ASSERT_TRUE(Mentions(e, "skill"));
  e = RejectionOf(env, {{adj, kMaxSkillSlots, "revive"}});
  ASSERT_TRUE(Mentions(e, "slot"));
  e = RejectionOf(env, {{adj, -1, "revive"}});
  ASSERT_TRUE(Mentions(e, "slot"));
  e = RejectionOf(env, {{adj, 0, "revive"}, {adj, 1, "fireball"}});
  ASSERT_TRUE(Mentions(e, "fireball") && Mentions(e, "cooldown"));
  e = RejectionOf(env, {{static_cast<ContextCondition>(99), 0, "revive"}});
  ASSERT_TRUE(Mentions(e, "condition"));
  // The throwing form (snapshots) names the skill too
  bool threw = false;
  try {
    ValidateContextSkills({{adj, 0, "nope"}}, env.GetSkillBook());
  } catch (const std::runtime_error& ex) {
    threw = Mentions(ex.what(), "nope");
  }
  ASSERT_TRUE(threw);
  ASSERT_TRUE(env.SetContextSkills({}));  // No error pointer needed

  // No rule: no override
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  ASSERT_TRUE(env.GetContextSkills().empty());
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(env.GetLastRevives().empty());

  // A rule for slot 1 leaves slot 0 alone; a zero-cooldown skill of the level
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  ASSERT_EQ(RejectionOf(env, {{adj, 1, "mend"}}), std::string(""));
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_EQ(env.EffectiveSkill(*a, 1), std::string("mend"));
  ASSERT_TRUE(env.IsContextSkill(*a, 1));
  // The first matching rule wins
  ASSERT_TRUE(env.SetContextSkills({{adj, 0, "mend"}, {adj, 0, "revive"}}));
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("mend"));
}

// Level data: a fresh env has the default rule, Reset keeps the level's, copies
// carry them.
TEST(TestContextSkillsAreLevelData) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  env.Reset();
  ASSERT_TRUE(env.GetContextSkills() == DefaultContextSkills());
  const std::vector<ContextSkillRule> custom = {
      {ContextCondition::AdjacentDownedAlly, 1, "revive"}};
  ASSERT_TRUE(env.SetContextSkills(custom));
  env.Reset();
  ASSERT_TRUE(env.GetContextSkills() == custom);
  env.Reset(7u);
  ASSERT_TRUE(env.GetContextSkills() == custom);
  SynchroEnv copy(env);
  ASSERT_TRUE(copy.GetContextSkills() == custom);
  SynchroEnv assigned(10, 10, 2, 1, 0, 7);
  ASSERT_TRUE(assigned.GetContextSkills() == DefaultContextSkills());
  assigned = env;
  ASSERT_TRUE(assigned.GetContextSkills() == custom);
  ASSERT_TRUE(env.Clone()->GetContextSkills() == custom);

  AggroEnv aggro(10, 2);
  aggro.Reset();
  ASSERT_TRUE(aggro.GetContextSkills() == DefaultContextSkills());
  ASSERT_TRUE(aggro.SetContextSkills({}));
  aggro.Reset();
  ASSERT_TRUE(aggro.GetContextSkills().empty());
}

// A snapshot carries the level's rules: a load sets them (absent = the
// default), every form keeps them, and they work in a step.
TEST(TestSnapshotsCarryTheContextSkills) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  mend.revive_percent = 100;
  env.GetMutableSkillBook().Define(mend);
  const std::vector<ContextSkillRule> rules = {
      {ContextCondition::AdjacentDownedAlly, 0, "mend"}};
  ASSERT_TRUE(env.SetContextSkills(rules));
  Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 2});
  b->SetMaxHealth(4);
  DownCompanion(env, 1);
  const Snapshot saved = env.SaveSnapshot();
  for (const Snapshot& s : {saved, Snapshot::Deserialize(saved.Serialize()),
                            SnapshotFromJson(SnapshotToJson(saved))}) {
    SynchroEnv other(10, 10, 2, 1, 0, 7);
    other.LoadSnapshot(s);
    ASSERT_TRUE(other.GetContextSkills() == rules);
    Companion* a2 = AsCompanion(other.GetMutableObjectManager().GetAllAgents()[0]);
    Agent* b2 = other.GetMutableObjectManager().GetAllAgents()[1];
    ASSERT_EQ(other.EffectiveSkill(*a2, 0), std::string("mend"));
    other.Step({Use(MovementAction::Right), kStay});
    ASSERT_FALSE(b2->IsDowned());
    ASSERT_EQ(b2->GetHealth(), 4);  // mend: 100%
  }
}

// A rule whose skill the current book lacks (a host redefined the book behind
// the rules' back) is skipped: the slot keeps its equipped skill.
TEST(TestARuleWhoseSkillTheBookLacksIsSkipped) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  ASSERT_TRUE(env.SetContextSkills({{adj, 0, "mend"}, {adj, 1, "mend"}, {adj, 1, "revive"}}));
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("mend"));
  env.GetMutableSkillBook().Reset();  // Builtins only: no mend
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
  ASSERT_EQ(env.EffectiveSkill(*a, 1), std::string("revive"));  // The next rule holds
  ASSERT_EQ(CountSkill1(env.LegalActions(0)), static_cast<size_t>(kNumMovementActions));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(env.GetLastRevives().empty());
}

// A generated Reset keeps the env's rules like max_downs, even one naming a
// skill of an earlier level (its book is the builtins): skipped, not rejected.
TEST(TestAGeneratedResetKeepsTheRules) {
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  const std::vector<ContextSkillRule> rules = {{adj, 0, "mend"}, {adj, 1, "revive"}};
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(env.SetContextSkills(rules));
  MakeArena(env);  // Resets
  ASSERT_TRUE(env.GetContextSkills() == rules);
  ASSERT_TRUE(env.GetSkillBook().Find("mend") == nullptr);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_EQ(env.EffectiveSkill(*a, 1), std::string("revive"));

  AggroEnv aggro(10, 2, EnemyType::Zombie, 42, 0, 100);
  aggro.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(aggro.SetContextSkills(rules));
  aggro.Reset(42);
  ASSERT_TRUE(aggro.GetContextSkills() == rules);
  DodgeEnv dodge(7, 2, 3, 50, 42);
  dodge.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(dodge.SetContextSkills(rules));
  dodge.Reset(42);
  ASSERT_TRUE(dodge.GetContextSkills() == rules);
}

// Every form of a saved state loads back.
static void AssertSavedStateLoads(SynchroEnv& env) {
  const Snapshot saved = env.SaveSnapshot();
  env.LoadSnapshot(saved);
  env.LoadSnapshot(Snapshot::Deserialize(saved.Serialize()));
  env.LoadSnapshot(SnapshotFromJson(SnapshotToJson(saved)));
}

// After a generated Reset keeps a rule naming an earlier level's skill, the
// saved state still loads: SaveSnapshot writes only the rules usable with the
// book (the others are inert: skipped at run time).
TEST(TestASavedStateWithAKeptRuleLoads) {
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(env.SetContextSkills({{adj, 0, "mend"}, {adj, 1, "revive"}}));
  env.Reset();  // Builtins only: the mend rule is kept, inert
  ASSERT_EQ(env.GetContextSkills().size(), static_cast<size_t>(2));
  const Snapshot saved = env.SaveSnapshot();
  ASSERT_TRUE(saved.context_skills.has_value());
  ASSERT_TRUE(*saved.context_skills ==
              (std::vector<ContextSkillRule>{{adj, 1, "revive"}}));
  AssertSavedStateLoads(env);
  ASSERT_TRUE(env.GetContextSkills() ==
              (std::vector<ContextSkillRule>{{adj, 1, "revive"}}));
}

// A rule whose skill gained a cooldown after the rules were set (a Define
// behind their back) is skipped at run time, and not saved.
TEST(TestARuleWhoseSkillGainedACooldownIsSkipped) {
  const ContextCondition adj = ContextCondition::AdjacentDownedAlly;
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig mend = *env.GetSkillBook().Find("revive");
  mend.name = "mend";
  env.GetMutableSkillBook().Define(mend);
  ASSERT_TRUE(env.SetContextSkills({{adj, 0, "mend"}}));
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Agent* b = Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string("mend"));
  mend.cooldown = 2;
  env.GetMutableSkillBook().Define(mend);
  ASSERT_EQ(env.EffectiveSkill(*a, 0), std::string(kDefaultSkill));
  ASSERT_FALSE(env.IsContextSkill(*a, 0));
  const Snapshot saved = env.SaveSnapshot();
  ASSERT_TRUE(saved.context_skills.has_value() && saved.context_skills->empty());
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(b->IsDowned());
  AssertSavedStateLoads(env);
}

// A SynchroEnv whose LoadSnapshot fails after the base load, on demand.
class FailingLoadEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  bool fail = false;
  void LoadSnapshot(const Snapshot& snapshot) override {
    SynchroEnv::LoadSnapshot(snapshot);
    if (fail) throw std::runtime_error("load failed after the base load");
  }
};

// A generated Reset that fails keeps the env's rules all the same.
TEST(TestAFailedResetKeepsTheRules) {
  const std::vector<ContextSkillRule> rules = {
      {ContextCondition::AdjacentDownedAlly, 1, "revive"}};
  FailingLoadEnv env(10, 10, 2, 1, 0, 42);
  ASSERT_TRUE(env.SetContextSkills(rules));
  env.fail = true;
  bool threw = false;
  try {
    env.Reset();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  ASSERT_TRUE(env.GetContextSkills() == rules);
}

// =============================================================================
// Previews (PreviewSkill) of skills around the downed
// =============================================================================

// Preview of agent 0's slot 0 aimed `aim`, then the step of that use (the
// others stay), which must match: its SkillUse's skill, centre, affected.
static BaseEnv::SkillPreview PreviewThenStep(SynchroEnv& env, Direction aim, size_t n) {
  Companion& caster = *AsCompanion(env.GetMutableObjectManager().GetAllAgents()[0]);
  const std::vector<uint8_t> before = env.SaveSnapshot().Serialize();
  BaseEnv::SkillPreview p = env.PreviewSkill(caster, 0, aim);
  ASSERT_TRUE(env.SaveSnapshot().Serialize() == before);  // Pure
  std::vector<Action> actions(n, kStay);
  actions[0] = Use(DirectionToMovement(aim));
  env.Step(actions);
  ASSERT_TRUE(p.usable);
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, p.skill);
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == p.centre);
  ASSERT_TRUE(env.GetLastSkillUses()[0].affected == p.affected);
  return p;
}

// A projectile passes over a downed ally; the context revive reaches it.
TEST(TestPreviewPassesOverTheDownedAndTheContextReviveReachesThem) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig bolt;
  bolt.name = "bolt";
  bolt.range = 3;
  bolt.tags = {{"zapped", kPermanentTag}};
  env.GetMutableSkillBook().Define(bolt);
  Agent* a = Place(env, 0, {3, 1});
  Agent* down = Place(env, 1, {4, 1});
  Agent* standing = Place(env, 2, {6, 1});
  DownCompanion(env, 1);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "bolt"));
  env.Step({kStay, kStay, kStay});  // Reports the down

  // Next to a downed ally, slot 0 is revive: the preview says so
  BaseEnv::SkillPreview p = env.PreviewSkill(*AsCompanion(a), 0, Direction::Down);
  ASSERT_EQ(p.skill, std::string("revive"));
  ASSERT_TRUE(p.usable);
  ASSERT_TRUE(p.centre == (Position{4, 1}));
  ASSERT_TRUE(p.affected == (std::vector<ObjectId>{down->GetId()}));

  // Without the context rule, the bolt flies over the downed ally
  ASSERT_TRUE(env.SetContextSkills({}));
  p = PreviewThenStep(env, Direction::Down, 3);
  ASSERT_EQ(p.skill, std::string("bolt"));
  ASSERT_TRUE(p.centre == (Position{6, 1}));
  ASSERT_TRUE(p.affected == (std::vector<ObjectId>{standing->GetId()}));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].agent, standing->GetId());
}

// A revive retuned to range 2 reaches a downed ally past a standing one, and
// the step revives exactly whom the preview said.
TEST(TestPreviewRetunedReviveReachesPastAStandingAlly) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig revive = *env.GetSkillBook().Find("revive");
  revive.range = 2;
  env.GetMutableSkillBook().Define(revive);
  Agent* a = Place(env, 0, {3, 1});
  Place(env, 1, {3, 2});
  Agent* down = Place(env, 2, {3, 3});
  DownCompanion(env, 2);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "revive"));
  BaseEnv::SkillPreview p = PreviewThenStep(env, Direction::Right, 3);
  ASSERT_TRUE(p.centre == (Position{3, 3}));
  ASSERT_TRUE(p.affected == (std::vector<ObjectId>{down->GetId()}));
  ASSERT_FALSE(down->IsDowned());
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives()[0].revived, down->GetId());
}

// A downed companion can use nothing: its equipped skill, not usable.
TEST(TestPreviewOfADownedCasterIsNotUsable) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 1});
  Place(env, 1, {3, 2});
  Agent* a = DownCompanion(env, 0);
  DownCompanion(env, 1);
  BaseEnv::SkillPreview p = env.PreviewSkill(*AsCompanion(a), 0, Direction::Right);
  ASSERT_FALSE(p.usable);
  ASSERT_EQ(p.skill, std::string(kDefaultSkill));  // No context while downed
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

  std::cout << "Running " << tests.size() << " revive tests...\n\n";

  int passed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    try {
      test.func();
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << ": " << e.what() << "\n";
      continue;
    }
    std::cout << "[       OK ] " << test.name << "\n";
    passed++;
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
