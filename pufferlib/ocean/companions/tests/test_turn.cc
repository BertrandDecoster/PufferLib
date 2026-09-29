// Copyright 2024
// Unit tests for the phased turn: a turn totals every agent's damage and
// heals (Marked on the total), and its downs, deaths and revives happen at its
// end; nothing changes HP, alive or down during a Step

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/effect_config.h"
#include "../src/core/object.h"
#include "../src/core/reaction.h"
#include "../src/core/skill_config.h"
#include "../src/core/snapshot.h"
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
// Helpers
// =============================================================================

using TurnOutcome = BaseEnv::TurnOutcome;

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

static Agent* AgentAt(BaseEnv& env, int index) {
  return env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(index)];
}

static Agent* Place(SynchroEnv& env, int index, Position p) {
  Agent* a = AgentAt(env, index);
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

static Companion* AsCompanion(Agent* a) { return dynamic_cast<Companion*>(a); }

// Down between steps (the host)
static Agent* DownCompanion(BaseEnv& env, int index) {
  Agent* a = AgentAt(env, index);
  a->TakeDamage(a->GetHealth());
  return a;
}

static const Action kStay = EncodeAction(MovementAction::Stay);
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

static void Require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(std::string("refused: ") + what);
}

// A ReactionRule by named fields
static ReactionRule Rule(const std::string& a, const std::string& b, const std::string& result,
                         int damage = 0, bool spread = false) {
  ReactionRule r;
  r.a = a;
  r.b = b;
  r.result = result;
  r.damage = damage;
  r.spread = spread;
  return r;
}

// A zone of `damage` per landing, permanent
static ZoneDef Hurting(int damage) {
  ZoneDef z;
  z.damage = damage;
  return z;
}

// A projectile (range 3, allies included) landing `tag` and dealing `damage`
// on the first agent it meets, put in the companion's slot 0
static void GiveBolt(SynchroEnv& env, int companion, const char* name, const char* tag,
                     int damage = 0) {
  SkillConfig s;
  s.name = name;
  s.targeting = SkillTargeting::Projectile;
  s.range = 3;
  if (tag) s.tags = {{tag, kPermanentTag}};
  s.damage = damage;
  env.GetMutableSkillBook().Define(s);
  Require(env.SetCompanionSkill(AgentAt(env, companion)->GetId(), 0, name), name);
}

// Around the caster (a cross), everyone on the ring pushed 1 cell away and
// tagged "gusted" (allies included), put in the companion's slot 0
static void GiveGust(SynchroEnv& env, int companion) {
  SkillConfig s;
  s.name = "gust";
  s.targeting = SkillTargeting::Self;
  s.area = SkillArea::Cross;
  s.motion = SkillMotion::PushOut;
  s.motion_distance = 1;
  s.tags = {{"gusted", kPermanentTag}};
  s.self_tags = false;
  env.GetMutableSkillBook().Define(s);
  Require(env.SetCompanionSkill(AgentAt(env, companion)->GetId(), 0, "gust"), "gust");
}

// An effect of `damage` (negative: a heal) on one cell for `filter`,
// telegraphed one step: spawned between steps, it lands during the next
// step's effect tick. Call under a ScopedEffectRegistry.
static void SpawnNextStep(BaseEnv& env, const char* name, Position cell, int damage,
                          TargetFilter filter = TargetFilter::All,
                          ObjectId source = kInvalidObjectId) {
  EffectConfig cfg;
  cfg.name = name;
  cfg.telegraph_ticks = 1;
  cfg.active_ticks = 1;
  cfg.area = {1};
  cfg.filter = filter;
  cfg.damage = damage;
  EffectConfigRegistry::Instance().RegisterConfig(cfg);
  env.SpawnEffect(name, EffectTarget::AtCell(cell), Direction::Up, source);
}

// The one TurnHealth entry of `agent` in the last step, or throws
static BaseEnv::TurnHealth TurnOf(const BaseEnv& env, const Agent* agent) {
  for (const BaseEnv::TurnHealth& t : env.GetLastTurnHealth()) {
    if (t.agent == agent->GetId()) return t;
  }
  throw std::runtime_error("no turn health for agent " + std::to_string(agent->GetId()));
}

// The oil + burning setup of the damage tests: `a` on hot oil (1 damage per
// landing) carrying burning, the rule oil + burning -> ash dealing 1. In the
// zone phase the oil lands (1) and the reaction fires (1).
static void BurningOnHotOil(SynchroEnv& env, Agent* a) {
  Require(env.SetReactions({Rule("oil", "burning", "ash", 1)}), "reactions");
  Require(env.DefineZone("oil", Hurting(1)), "oil");
  Require(env.ApplyTagTo(a->GetId(), "burning", kPermanentTag), "burning");
  Require(env.SetCellTag(a->GetPosition(), "oil"), "oil cell");
}

// =============================================================================
// The ledger: totals, Marked, heals
// =============================================================================

// Zone 1 + reaction 1 + skill 1 on a 5-HP companion: 2 HP at the end, one
// TurnHealth entry with the turn's total
TEST(TestTheTurnsDamageIsTotalledThenApplied) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Agent* b = Place(env, 1, {3, 5});
  a->SetMaxHealth(5);
  BurningOnHotOil(env, a);
  GiveBolt(env, 1, "jab", nullptr, 1);
  env.Step(With(env, 1, Use(MovementAction::Left)));
  ASSERT_EQ(a->GetHealth(), 2);
  ASSERT_EQ(b->GetHealth(), 10);
  ASSERT_EQ(env.GetLastTurnHealth().size(), static_cast<size_t>(1));
  const BaseEnv::TurnHealth t = TurnOf(env, a);
  ASSERT_EQ(t.damage, 3);
  ASSERT_EQ(t.marked_bonus, 0);
  ASSERT_EQ(t.heal, 0);
  ASSERT_EQ(t.change, -3);
  ASSERT_EQ(t.health, 2);
  ASSERT_TRUE(t.outcome == TurnOutcome::None);
  // The raw shares stay in the reports
  ASSERT_EQ(env.GetLastTagsApplied().at(0).damage, 1);
  ASSERT_EQ(env.GetLastReactions().at(0).affected.at(0).damage, 1);
  ASSERT_EQ(env.GetLastSkillUses().at(0).affected.at(0).effects,
            static_cast<unsigned>(BaseEnv::kSkillEffectDamage));
}

// Two 1-damage hits on a Marked agent: 2 x 1.5 = 3 (per hit it was 1 + 1)
TEST(TestMarkedMultipliesTheTurnsTotalOnce) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  BurningOnHotOil(env, a);
  a->ApplyStatus(StatusType::Marked, 5);
  env.Step(Stays(env));
  ASSERT_EQ(a->GetHealth(), 7);
  const BaseEnv::TurnHealth t = TurnOf(env, a);
  ASSERT_EQ(t.damage, 2);
  ASSERT_EQ(t.marked_bonus, 1);
  ASSERT_EQ(t.change, -3);
}

// Marked landed during a turn (a tag status) acts from the next turn
TEST(TestMarkedLandedThisTurnActsFromTheNextTurn) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetTagStatuses({{"exposed", StatusType::Marked, 3}}), "tag statuses");
  Require(env.DefineZone("exposed", Hurting(2)), "exposed");
  Agent* a = Place(env, 0, {3, 3});
  Require(env.SetCellTag({3, 3}, "exposed"), "exposed cell");
  env.Step(Stays(env));
  ASSERT_TRUE(a->IsMarked());
  ASSERT_EQ(a->GetHealth(), 8);  // Not Marked as the turn began: 2
  ASSERT_EQ(TurnOf(env, a).marked_bonus, 0);
  env.Step(Stays(env));
  ASSERT_EQ(a->GetHealth(), 5);  // Marked now: 3
  ASSERT_EQ(TurnOf(env, a).marked_bonus, 1);
}

// Heals come off after Marked: 2 damage (Marked: 3), heal 1: -2
TEST(TestHealsAreSubtractedAfterMarked) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  BurningOnHotOil(env, a);
  a->ApplyStatus(StatusType::Marked, 5);
  SpawnNextStep(env, "mend", {3, 3}, -1);
  env.Step(Stays(env));
  ASSERT_EQ(a->GetHealth(), 8);
  const BaseEnv::TurnHealth t = TurnOf(env, a);
  ASSERT_EQ(t.damage, 2);
  ASSERT_EQ(t.marked_bonus, 1);
  ASSERT_EQ(t.heal, 1);
  ASSERT_EQ(t.change, -2);
  ASSERT_EQ(t.health, 8);
}

// A heal the same turn saves a companion the turn's damage takes to 0
TEST(TestAHealSavesACompanionAtZeroThisTurn) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  a->SetMaxHealth(5);
  a->RestoreHealth(2);
  Require(env.SetCellTag({3, 3}, "burning", Hurting(2)), "fire");
  SpawnNextStep(env, "mend", {3, 3}, -1);
  env.Step(Stays(env));
  ASSERT_FALSE(AsCompanion(a)->IsDowned());
  ASSERT_EQ(a->GetHealth(), 1);
  ASSERT_TRUE(env.GetLastDowns().empty());
  const BaseEnv::TurnHealth t = TurnOf(env, a);
  ASSERT_EQ(t.damage, 2);
  ASSERT_EQ(t.heal, 1);
  ASSERT_EQ(t.change, -1);
  ASSERT_TRUE(t.outcome == TurnOutcome::None);
}

// A companion the zone takes to 0 still uses its skill and is still affected
// (tagged, pushed) this turn; it goes down at the end
TEST(TestACompanionAtZeroStillActsAndIsAffectedThisTurn) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Place(env, 1, {3, 4});  // A is its left ring cell
  a->SetMaxHealth(5);
  a->RestoreHealth(1);
  Agent* gob = AddEnemy(env, {1, 3});
  GiveBolt(env, 0, "zap", "zapped");
  GiveGust(env, 1);
  Require(env.SetCellTag({3, 3}, "burning", Hurting(1)), "fire");
  std::vector<Action> actions = Stays(env);
  actions[0] = Use(MovementAction::Up);
  actions[1] = Use(MovementAction::Stay);
  env.Step(actions);
  ASSERT_TRUE(Has(env, gob, "zapped"));  // Its use resolved
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastSkillUses().at(0).caster, a->GetId());
  ASSERT_TRUE(Has(env, a, "gusted"));                   // Tagged
  ASSERT_TRUE(a->GetPosition() == (Position{3, 2}));    // Pushed
  ASSERT_TRUE(AsCompanion(a)->IsDowned());              // At the end
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));
  ASSERT_TRUE(TurnOf(env, a).outcome == TurnOutcome::Downed);
}

// =============================================================================
// Deaths at the end of the turn: a dying attacker's strike
// =============================================================================

// Companion 0 on (3,3) facing a 1-HP enemy on (3,4); the enemy's strike
// (telegraph `telegraph`, then 1 damage) is aimed at the companion
static Agent* StrikeScene(SynchroEnv& env, int telegraph, int loop = 0) {
  EffectConfig cfg;
  cfg.name = "wind_up_" + std::to_string(telegraph) + "_" + std::to_string(loop);
  cfg.telegraph_ticks = telegraph;
  cfg.active_ticks = 1;
  cfg.loop = loop;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  EffectConfigRegistry::Instance().RegisterConfig(cfg);
  Agent* enemy = env.GetMutableObjectManager().GetActorAt({3, 4})
                     ? dynamic_cast<Agent*>(env.GetMutableObjectManager().GetActorAt({3, 4}))
                     : AddEnemy(env, {3, 4}, 1);
  env.SpawnEffect(cfg.name, EffectTarget::AtCell({3, 3}), Direction::Up, enemy->GetId());
  return enemy;
}

TEST(TestAnAttackerKilledThisTurnStillLandsThisTurnsStrike) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* c = Place(env, 0, {3, 3});
  Agent* enemy = StrikeScene(env, 1);
  env.Step(With(env, 0, Use(MovementAction::Right)));  // Kills it as its strike lands
  ASSERT_FALSE(enemy->IsAlive());
  ASSERT_EQ(c->GetHealth(), 9);
  ASSERT_TRUE(TurnOf(env, enemy).outcome == TurnOutcome::Died);
  ASSERT_EQ(TurnOf(env, c).damage, 1);
  env.Step(Stays(env));  // Its active phase ends: nothing more
  ASSERT_EQ(c->GetHealth(), 9);
  ASSERT_TRUE(env.GetActiveEffects().empty());
}

// Its strikes still winding up at its death never land: a second strike
// (telegraph 2) is gone at the end of the turn it dies, and its loop stops at
// its next restart
TEST(TestADeadAttackersLaterStrikesAreCancelledAtItsDeath) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* c = Place(env, 0, {3, 3});
  Agent* enemy = StrikeScene(env, 1, -1);  // Lands this turn, then loops
  StrikeScene(env, 2);                     // Would land next turn
  ASSERT_EQ(env.GetActiveEffects().size(), static_cast<size_t>(2));
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_FALSE(enemy->IsAlive());
  ASSERT_EQ(c->GetHealth(), 9);  // This turn's strike
  ASSERT_EQ(env.GetActiveEffects().size(), static_cast<size_t>(1));  // The loop, active
  ASSERT_FALSE(env.GetActiveEffects().at(0).in_telegraph);
  for (int i = 0; i < 3; ++i) env.Step(Stays(env));
  ASSERT_EQ(c->GetHealth(), 9);
  ASSERT_TRUE(env.GetActiveEffects().empty());
}

// =============================================================================
// Weakness defeats
// =============================================================================

// A defeat ends at 0 whatever the turn's heals
TEST(TestAWeaknessDefeatIsZeroWhateverTheHeals) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  GiveBolt(env, 0, "spark", "electrified");
  Place(env, 0, {5, 2});
  Agent* imp = AddEnemy(env, {5, 5}, 5);
  Require(env.SetCellTag({5, 5}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  SpawnNextStep(env, "mend", {5, 5}, -3, TargetFilter::Enemy);
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_FALSE(imp->IsAlive());
  ASSERT_EQ(imp->GetHealth(), 0);
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  const BaseEnv::TurnHealth t = TurnOf(env, imp);
  ASSERT_TRUE(t.outcome == TurnOutcome::Defeated);
  ASSERT_EQ(t.heal, 3);
  ASSERT_EQ(t.health, 0);
  ASSERT_EQ(t.change, -5);
}

// A defeated agent stays in play until the end of the turn: its own reaction
// fires and reaches it (result and damage), and a later skill pushes it
TEST(TestADefeatedAgentStillGetsResultsAndPushesThisTurn) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked", 1, true)}), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  GiveGust(env, 1);
  Place(env, 0, {3, 1});  // Sparks right: (3,2), (3,3), the imp on (3,4)
  Place(env, 1, {3, 5});  // Gusts: the imp on its left ring cell
  Agent* imp = AddEnemy(env, {3, 4}, 5);
  Require(env.SetCellTag({3, 4}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  std::vector<Action> actions = Stays(env);
  actions[0] = Use(MovementAction::Right);
  actions[1] = Use(MovementAction::Stay);
  env.Step(actions);
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));  // Once
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, imp->GetId());
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));
  ASSERT_EQ(r.affected.at(0).agent, imp->GetId());
  ASSERT_TRUE(r.affected.at(0).result_landed);
  ASSERT_EQ(r.affected.at(0).damage, 1);
  ASSERT_TRUE(Has(env, imp, "shocked"));
  ASSERT_TRUE(Has(env, imp, "gusted"));
  ASSERT_TRUE(imp->GetPosition() == (Position{3, 3}));  // Pushed
  ASSERT_FALSE(imp->IsAlive());
  ASSERT_TRUE(TurnOf(env, imp).outcome == TurnOutcome::Defeated);
  ASSERT_EQ(TurnOf(env, imp).damage, 1);
}

// =============================================================================
// Revives at the end of the turn
// =============================================================================

// The ally a revive gets up is down all turn: this turn's lethal strike on its
// cell cannot touch it, and it gets up at the end
TEST(TestARevivedAllyCannotBeDownedTheTurnItGetsUp) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 2});
  DownCompanion(env, 1);
  env.Step(Stays(env));  // Reports the down
  Require(env.SetCompanionSkill(a->GetId(), 0, "revive"), "revive");
  SpawnNextStep(env, "kill_next_step", {3, 2}, 999, TargetFilter::Companion);
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_FALSE(AsCompanion(b)->IsDowned());
  ASSERT_EQ(b->GetHealth(), 5);
  ASSERT_TRUE(env.GetLastDowns().empty());
  ASSERT_EQ(env.GetDowns(), 1);
  ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastRevives().at(0).health, 5);
  const BaseEnv::TurnHealth t = TurnOf(env, b);
  ASSERT_TRUE(t.outcome == TurnOutcome::Revived);
  ASSERT_EQ(t.damage, 0);
  ASSERT_EQ(t.change, 5);
  ASSERT_EQ(t.health, 5);
}

// Two revivers on one ally: one revive with the highest HP, credited to the
// lowest agent index giving it; the other use keeps the ally without Revive
TEST(TestTwoReviversReviveOnce) {
  for (bool second_stronger : {false, true}) {
    SynchroEnv env(10, 10, 3, 1, 0, 42);
    MakeArena(env);
    Require(env.SetContextSkills({}), "no context");  // Each its equipped skill
    SkillConfig full = *env.GetSkillBook().Find("revive");
    full.name = "fullRevive";
    full.revive_percent = 100;
    env.GetMutableSkillBook().Define(full);
    Agent* first = Place(env, 0, {3, 1});
    Agent* down = Place(env, 1, {3, 2});
    Agent* second = Place(env, 2, {3, 3});
    DownCompanion(env, 1);
    Require(env.SetCompanionSkill(first->GetId(), 0, "revive"), "revive");
    Require(env.SetCompanionSkill(second->GetId(), 0, second_stronger ? "fullRevive" : "revive"),
            "second");
    env.Step({Use(MovementAction::Right), kStay, Use(MovementAction::Left)});
    ASSERT_FALSE(AsCompanion(down)->IsDowned());
    ASSERT_EQ(down->GetHealth(), second_stronger ? 10 : 5);
    ASSERT_EQ(env.GetLastRevives().size(), static_cast<size_t>(1));
    const Agent* credited = second_stronger ? second : first;
    ASSERT_EQ(env.GetLastRevives().at(0).reviver, credited->GetId());
    ASSERT_EQ(env.GetLastRevives().at(0).revived, down->GetId());
    const auto& uses = env.GetLastSkillUses();
    ASSERT_EQ(uses.size(), static_cast<size_t>(2));
    for (const auto& use : uses) {
      ASSERT_EQ(use.affected.size(), static_cast<size_t>(1));
      ASSERT_EQ(use.affected.at(0).id, down->GetId());
      const unsigned expected = use.caster == credited->GetId() ? BaseEnv::kSkillEffectRevive : 0u;
      ASSERT_EQ(use.affected.at(0).effects, expected);
    }
    ASSERT_EQ(env.GetLastTurnHealth().size(), static_cast<size_t>(1));
    ASSERT_TRUE(TurnOf(env, down).outcome == TurnOutcome::Revived);
  }
}

// =============================================================================
// The report
// =============================================================================

// One entry per agent the turn touched, in agent-index order; copied with the
// env, cleared by the next step and by a load
TEST(TestTheTurnHealthReportOrderCopyAndClear) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Place(env, 1, {5, 5});
  Agent* c = Place(env, 2, {3, 4});
  const Snapshot saved = env.SaveSnapshot();
  Require(env.SetCellTag({3, 4}, "burning", Hurting(2)), "fire");
  Require(env.SetCellTag({3, 3}, "burning", Hurting(2)), "fire");
  env.Step(Stays(env));
  const auto& report = env.GetLastTurnHealth();
  ASSERT_EQ(report.size(), static_cast<size_t>(2));
  ASSERT_EQ(report.at(0).agent, a->GetId());
  ASSERT_EQ(report.at(1).agent, c->GetId());
  SynchroEnv copy(env);
  ASSERT_EQ(copy.GetLastTurnHealth().size(), static_cast<size_t>(2));
  SynchroEnv assigned(10, 10, 3, 1, 0, 7);
  assigned = env;
  ASSERT_EQ(assigned.GetLastTurnHealth().size(), static_cast<size_t>(2));
  ASSERT_EQ(assigned.GetLastTurnHealth().at(1).health, 8);
  env.ClearCellTags();
  env.Step(Stays(env));
  ASSERT_TRUE(env.GetLastTurnHealth().empty());
  copy.LoadSnapshot(saved);
  ASSERT_TRUE(copy.GetLastTurnHealth().empty());
}

// =============================================================================
// Copies: each env's effects feed its own ledger
// =============================================================================

// A copy (constructed, assigned, Clone()) outliving its original: a strike
// (2 damage) and a heal (1) landing in the copy's step on a 2-HP companion go
// into the copy's own ledger (its effect system's sink points at the copy):
// totalled, the companion ends at 1, not down.
TEST(TestACopysEffectsFeedItsOwnLedger) {
  ScopedEffectRegistry scoped_registry;
  for (int how = 0; how < 3; ++how) {
    std::unique_ptr<BaseEnv> copy;
    {
      SynchroEnv original(10, 10, 1, 1, 0, 42);
      MakeArena(original);
      Agent* a = Place(original, 0, {3, 3});
      a->SetMaxHealth(5);
      a->RestoreHealth(2);
      SpawnNextStep(original, "strike", {3, 3}, 2);
      SpawnNextStep(original, "mend", {3, 3}, -1);
      if (how == 0) {
        copy = std::make_unique<SynchroEnv>(original);
      } else if (how == 1) {
        auto assigned = std::make_unique<SynchroEnv>(10, 10, 1, 1, 0, 7);
        *assigned = original;
        copy = std::move(assigned);
      } else {
        copy = original.Clone();
      }
    }  // The original is gone
    copy->Step(Stays(*copy));
    Agent* a = AgentAt(*copy, 0);
    ASSERT_FALSE(AsCompanion(a)->IsDowned());
    ASSERT_EQ(a->GetHealth(), 1);
    ASSERT_EQ(copy->GetLastTurnHealth().size(), static_cast<size_t>(1));
    ASSERT_EQ(copy->GetLastTurnHealth().at(0).damage, 2);
    ASSERT_EQ(copy->GetLastTurnHealth().at(0).heal, 1);
  }
}

// =============================================================================
// An agent standing at 0 HP (a snapshot may save one)
// =============================================================================

// Hurt during a turn, it goes down at its end, as TakeDamage would down it;
// untouched, it stays up
TEST(TestAStandingAgentAtZeroHurtThisTurnGoesDown) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* hurt = Place(env, 0, {3, 3});
  Agent* spared = Place(env, 1, {5, 5});
  for (Agent* a : {hurt, spared}) a->RestoreHealth(0);
  ASSERT_TRUE(hurt->IsAffectable());
  Require(env.SetCellTag({3, 3}, "burning", Hurting(1)), "fire");
  env.Step(Stays(env));
  ASSERT_TRUE(AsCompanion(hurt)->IsDowned());
  ASSERT_TRUE(TurnOf(env, hurt).outcome == TurnOutcome::Downed);
  ASSERT_EQ(TurnOf(env, hurt).change, 0);
  ASSERT_FALSE(AsCompanion(spared)->IsDowned());
  ASSERT_EQ(env.GetLastTurnHealth().size(), static_cast<size_t>(1));
}

// =============================================================================
// Between steps: the host's primitives stay immediate
// =============================================================================

TEST(TestAHostKillBetweenStepsDownsAtOnce) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  Agent* b = Place(env, 1, {3, 5});
  env.SpawnEffect("kill", EffectTarget::AtCell({3, 3}));
  ASSERT_TRUE(AsCompanion(a)->IsDowned());
  // Marked per hit between steps: 1 stays 1, then 2 becomes 3
  b->ApplyStatus(StatusType::Marked, 3);
  env.SpawnEffect("hit", EffectTarget::AtCell({3, 5}));
  ASSERT_EQ(b->GetHealth(), 9);
  b->TakeDamage(2);
  ASSERT_EQ(b->GetHealth(), 6);
  ASSERT_TRUE(env.GetLastTurnHealth().empty());  // No turn
  env.Step(Stays(env));
  ASSERT_EQ(env.GetLastDowns().size(), static_cast<size_t>(1));  // Reported by the next step
}

// =============================================================================
// A step that throws
// =============================================================================

// A SynchroEnv whose PreStep hits companion 0 during the step, then throws
class ThrowingEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  bool armed = false;

 protected:
  void PreStep() override {
    SynchroEnv::PreStep();
    if (!armed) return;
    SpawnEffect("hit", EffectTarget::AtCell(AgentAt(*this, 0)->GetPosition()));
    throw std::runtime_error("PreStep failed");
  }
};

TEST(TestAThrowingStepAppliesTheLedger) {
  ThrowingEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  env.armed = true;
  bool threw = false;
  try {
    env.Step(Stays(env));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  ASSERT_EQ(a->GetHealth(), 9);  // What the step did stays
  env.armed = false;
  env.SaveSnapshot();  // Between two steps
  env.Step(Stays(env));
  ASSERT_EQ(a->GetHealth(), 9);  // Applied once
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

  std::cout << "Running " << tests.size() << " turn tests...\n\n";

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
