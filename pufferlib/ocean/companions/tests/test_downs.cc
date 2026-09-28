// Copyright 2024
// Unit tests for downs: companions going down, the downed state, the team's counter

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/fsm/fsm_state.h"
#include "../src/core/object.h"
#include "../src/env/aggro_env.h"
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
// The downed state
// =============================================================================
TEST(TestCompanionGoesDownAtZeroHealth) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(3);
  c.TakeDamage(5);
  ASSERT_EQ(c.GetHealth(), 0);
  ASSERT_TRUE(c.IsAlive());    // Down, not dead
  ASSERT_TRUE(c.IsDowned());
  ASSERT_FALSE(c.IsAffectable());
  ASSERT_EQ(c.GetTimesDowned(), 1);
}

TEST(TestNonCompanionsStillDie) {
  Agent enemy(0, {1, 1});
  enemy.SetFaction(Faction::ENEMY);
  enemy.SetMaxHealth(2);
  enemy.TakeDamage(2);
  ASSERT_FALSE(enemy.IsAlive());
  ASSERT_FALSE(enemy.IsDowned());
}

// Downed: statuses cleared as it goes down, tags kept; then nothing lands.
TEST(TestADownedCompanionIsUntouched) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(3);
  c.ApplyTag(0, kPermanentTag);
  c.ApplyStatus(StatusType::Marked, 3);
  c.TakeDamage(3);
  ASSERT_TRUE(c.GetStatuses().empty());
  ASSERT_TRUE(c.HasTag(0));
  c.TakeDamage(1);
  c.Heal(2);
  c.ApplyTag(1, 2);
  c.ApplyStatus(StatusType::Stunned, 2);
  ASSERT_EQ(c.GetHealth(), 0);
  ASSERT_EQ(c.GetTimesDowned(), 1);
  ASSERT_FALSE(c.HasTag(1));
  ASSERT_TRUE(c.GetStatuses().empty());
}

// The dead are untouched too: 0 HP has its consequence once per life.
TEST(TestTheDeadAreUntouched) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(3);
  c.RestoreHealth(0);
  c.SetAlive(false);
  c.TakeDamage(1);
  ASSERT_FALSE(c.IsDowned());  // No down after death
  ASSERT_EQ(c.GetTimesDowned(), 0);

  Agent enemy(1, {2, 2});
  enemy.SetFaction(Faction::ENEMY);
  enemy.SetMaxHealth(2);
  enemy.TakeDamage(2);
  enemy.Heal(2);
  ASSERT_EQ(enemy.GetHealth(), 0);
  ASSERT_FALSE(enemy.IsAlive());
}

TEST(TestDownsAreReportedOnce) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(1);
  c.TakeDamage(1);
  ASSERT_EQ(c.TakeUnreportedDowns(), 1);
  ASSERT_EQ(c.TakeUnreportedDowns(), 0);
  c.RestoreDowns(true, 2);  // A snapshot load: already reported
  ASSERT_TRUE(c.IsDowned());
  ASSERT_EQ(c.GetTimesDowned(), 2);
  ASSERT_EQ(c.TakeUnreportedDowns(), 0);
}

TEST(TestRestoreHealthHasNoSideEffect) {
  Companion c(0, {1, 1});
  c.SetMaxHealth(4);
  c.RestoreHealth(0);   // A snapshot load: no down, no count
  ASSERT_EQ(c.GetHealth(), 0);
  ASSERT_FALSE(c.IsDowned());
  ASSERT_EQ(c.GetTimesDowned(), 0);
  c.RestoreHealth(9);   // Clamped
  ASSERT_EQ(c.GetHealth(), 4);
}

// =============================================================================
// The env and the downed: they do not act and are not affected
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
static bool Has(const BaseEnv& env, const Agent* a, const char* tag) {
  TagId t = env.GetTagTable().Find(tag);
  return t != kInvalidTag && a->HasTag(t);
}

TEST(TestADownedCompanionStaysPutAndHasOnlyStay) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 1});
  Agent* down = DownCompanion(env, 0);
  ASSERT_TRUE(down->IsDowned());
  ASSERT_EQ(env.LegalActions(0).size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.LegalActions(0)[0] == kStay);
  env.Step({EncodeAction(MovementAction::Right), kStay});
  ASSERT_TRUE(down->GetPosition() == (Position{3, 1}));
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(env.GetLastSkillUses().empty());
}

// A projectile flies past a downed ally; a zone lands nothing on it.
TEST(TestNothingLandsOnADownedCompanion) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig bolt;
  bolt.name = "bolt";
  bolt.targeting = SkillTargeting::Projectile;
  bolt.range = 4;
  bolt.damage = 1;
  bolt.tags = {{"zapped", 1}};
  env.GetMutableSkillBook().Define(bolt);
  Agent* caster = Place(env, 0, {3, 1});
  Place(env, 1, {3, 2});
  Agent* behind = Place(env, 2, {3, 4});
  Agent* down = DownCompanion(env, 1);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, "bolt"));
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_FALSE(Has(env, down, "zapped"));
  ASSERT_FALSE(Has(env, down, "wet"));
  ASSERT_TRUE(Has(env, behind, "zapped"));
  for (const auto& landed : env.GetLastTagsApplied()) ASSERT_TRUE(landed.agent != down->GetId());
}

// Puts the companion on a free walkable neighbour of the enemy.
static bool PlaceNextTo(AggroEnv& env, Companion* comp, const AgentFSM* enemy) {
  const Position e = enemy->GetPosition();
  for (Position p : {Position{e.row, e.col - 1}, Position{e.row, e.col + 1},
                     Position{e.row - 1, e.col}, Position{e.row + 1, e.col}}) {
    if (env.GetGrid().IsWalkable(p) && !env.GetObjectManager().GetActorAt(p)) {
      env.GetMutableObjectManager().UpdatePosition(comp->GetId(), p);
      return true;
    }
  }
  return false;
}

// Enemies ignore the downed: an Aggro enemy next to a downed companion does
// not target it, and it takes no further damage.
TEST(TestEnemiesIgnoreADownedCompanion) {
  AggroEnv env(10, 1, EnemyType::Goblin, 42, 0, 100);
  env.Reset(42);
  Companion* comp = env.GetMutableObjectManager().GetAllCompanions()[0];
  AgentFSM* enemy = env.GetMutableObjectManager().GetAllAgentFSMs()[0];
  ASSERT_TRUE(PlaceNextTo(env, comp, enemy));
  comp->TakeDamage(comp->GetHealth());
  ASSERT_TRUE(comp->IsDowned());
  const std::vector<Action> stay(static_cast<size_t>(env.NumAgents()), kStay);  // The enemy's too
  for (int i = 0; i < 6; ++i) {
    env.Step(stay);
    ASSERT_EQ(env.GetTick(), i + 1);  // The step ran
    ASSERT_TRUE(enemy->GetFSMContext().target_id != comp->GetId());
  }
  ASSERT_EQ(comp->GetTimesDowned(), 1);
}

// An enemy whose strike downs its target drops it once its attack sequence
// ends: it no longer reports the downed companion as its target.
TEST(TestAnEnemyDropsATargetItDowned) {
  AggroEnv env(10, 1, EnemyType::Goblin, 42, 0, 100);
  env.Reset(42);
  Companion* comp = env.GetMutableObjectManager().GetAllCompanions()[0];
  AgentFSM* enemy = env.GetMutableObjectManager().GetAllAgentFSMs()[0];
  enemy->GetFSMContext().has_attack = true;
  enemy->GetFSMContext().attack_effect_name = "goblin_attack";  // A builtin
  ASSERT_TRUE(PlaceNextTo(env, comp, enemy));
  comp->RestoreHealth(1);
  const std::vector<Action> stay(static_cast<size_t>(env.NumAgents()), kStay);
  for (int i = 0; i < 20 && !comp->IsDowned(); ++i) env.Step(stay);
  ASSERT_TRUE(comp->IsDowned());
  auto striking = [&] {
    const std::string s = enemy->GetCurrentState()->GetName();
    return s == "Telegraph" || s == "Attack" || s == "Recovery";
  };
  for (int i = 0; i < 20 && striking(); ++i) env.Step(stay);
  ASSERT_FALSE(striking());
  ASSERT_TRUE(enemy->GetFSMContext().target_id != comp->GetId());
}

// =============================================================================
// The team's downs: the level is lost (TeamDown) at max_downs, or when every
// companion is down at once
// =============================================================================
static Companion* AsCompanion(Agent* a) { return dynamic_cast<Companion*>(a); }

// A lethal effect on `cell` that strikes during the next step (the builtin
// "kill" strikes as it spawns). Call under a ScopedEffectRegistry.
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

TEST(TestTheThirdDownLosesTheLevel) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_EQ(env.GetMaxDowns(), 3);
  Companion* a = AsCompanion(Place(env, 0, {3, 1}));
  Place(env, 1, {5, 5});
  a->TakeDamage(a->GetHealth());                // Down 1
  a->RestoreDowns(false, a->GetTimesDowned());  // Stands in for a revive (phase 2)
  a->RestoreHealth(1);
  a->TakeDamage(1);                             // Down 2
  ASSERT_EQ(env.GetDowns(), 2);
  ASSERT_FALSE(env.IsTeamDown());
  a->RestoreDowns(false, a->GetTimesDowned());
  a->RestoreHealth(1);
  a->TakeDamage(1);                             // Down 3
  ASSERT_EQ(env.GetDowns(), 3);
  ASSERT_TRUE(env.IsTeamDown());
  env.Step({kStay, kStay});
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
}

TEST(TestEveryCompanionDownLosesTheLevel) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  DownCompanion(env, 0);
  ASSERT_FALSE(env.IsTeamDown());
  DownCompanion(env, 1);  // 2 downs < 3, but nobody is left
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_TRUE(env.IsDone());
}

TEST(TestMaxDownsIsLevelData) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_FALSE(env.SetMaxDowns(0));
  ASSERT_TRUE(env.SetMaxDowns(1));
  DownCompanion(env, 0);
  ASSERT_TRUE(env.IsTeamDown());
  auto copy = env.Clone();  // Copied with the env, kept across Reset
  ASSERT_EQ(copy->GetMaxDowns(), 1);
  env.Reset(42);
  ASSERT_EQ(env.GetMaxDowns(), 1);
}

TEST(TestResetClearsTheDowns) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  DownCompanion(env, 0);
  DownCompanion(env, 1);
  env.Reset(42);
  ASSERT_EQ(env.GetDowns(), 0);
  ASSERT_FALSE(env.IsDone());
}

// A success latched on the step the team goes down wins: the end reason is
// Success (ComputeEndReason's priority). Companion 0 covers the only goal;
// companion 1 goes down during the step (max_downs 1).
TEST(TestASuccessOnTheTeamDownStepIsASuccess) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetMaxDowns(1));
  const Position goal = env.GetSynchroPositions()[0];
  Place(env, 0, goal);
  const Position other = goal == Position{5, 5} ? Position{3, 3} : Position{5, 5};
  Agent* victim = Place(env, 1, other);
  SpawnKillNextStep(env, other);
  ASSERT_FALSE(victim->IsDowned());
  ASSERT_FALSE(env.IsDone());
  StepResult result = env.Step({kStay, kStay});
  ASSERT_TRUE(victim->IsDowned());
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
}

// A dead companion does not stand: one down and the other dead is every
// companion down at once. A dead one alone is not.
TEST(TestADeadCompanionDoesNotStand) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* dead = env.GetMutableObjectManager().GetAllAgents()[1];
  dead->SetAlive(false);
  ASSERT_FALSE(env.IsTeamDown());
  DownCompanion(env, 0);
  ASSERT_EQ(env.GetDowns(), 1);
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_TRUE(env.IsDone());
}

// TeamDown ends a multi-companion AggroEnv (whatever its lens says): the
// second companion goes down during a step, which reports done.
TEST(TestTeamDownEndsAMultiCompanionAggroEnv) {
  ScopedEffectRegistry scoped_registry;
  AggroEnv env(10, 2, EnemyType::Goblin, 42, 0, 100);
  env.Reset(42);
  const std::vector<Action> stay(static_cast<size_t>(env.NumAgents()), kStay);  // The enemy's too
  auto companions = env.GetMutableObjectManager().GetAllCompanions();
  ASSERT_EQ(companions.size(), static_cast<size_t>(2));
  companions[0]->TakeDamage(companions[0]->GetHealth());
  StepResult first = env.Step(stay);
  ASSERT_FALSE(first.done);  // One down, one standing
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  SpawnKillNextStep(env, companions[1]->GetPosition());
  ASSERT_FALSE(companions[1]->IsDowned());
  StepResult last = env.Step(stay);
  ASSERT_TRUE(companions[1]->IsDowned());
  ASSERT_TRUE(last.done);
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
}

// Each down is reported once, by the step it happened in; a down between
// steps (a host effect) by the next step. The next step's reports are empty.
TEST(TestTheStepReportsEachDownOnce) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const std::vector<Action> stay(3, kStay);
  Agent* between = DownCompanion(env, 0);  // Between steps
  ASSERT_TRUE(env.GetLastDowns().empty());  // Not reported until a step
  env.Step(stay);
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDowns()[0], between->GetId());
  env.Step(stay);
  ASSERT_TRUE(env.GetLastDowns().empty());

  Agent* during = Place(env, 1, {3, 3});  // During a step
  SpawnKillNextStep(env, {3, 3});
  env.Step(stay);
  ASSERT_TRUE(during->IsDowned());
  ASSERT_FALSE(env.IsDone());  // Companion 2 stands
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastDowns()[0], during->GetId());
  env.Step(stay);
  ASSERT_TRUE(env.GetLastDowns().empty());
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

  std::cout << "Running " << tests.size() << " downs tests...\n\n";

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
