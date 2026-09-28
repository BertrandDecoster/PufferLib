// Copyright 2024
// Unit tests for revive: skills that affect the downed, the revive builtin

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/object.h"
#include "../src/core/skill_config.h"
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
    test.func();
    std::cout << "[       OK ] " << test.name << "\n";
    passed++;
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return 0;
}
