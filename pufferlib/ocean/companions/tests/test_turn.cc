// Copyright 2024
// Unit tests for the phased turn: a turn totals every agent's damage and
// heals (Marked on the total), and its downs, deaths and revives happen at its
// end; nothing changes HP, alive or down during a Step. Every skill use is
// planned from the world as the turn began (simultaneous casters). Every
// motion of the turn (walks, dashes, teleports, pushes, pulls) is one phase,
// and the zones land once, on the final cells.

#include <algorithm>
#include <iostream>
#include <map>
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
#include "../src/env/skill_motion.h"
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
// tag phase the oil lands (1) and the reaction fires (1).
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

// A companion the zone of its final cell takes to 0 (pushed onto the fire)
// still uses its skill and is still affected (tagged) this turn; it goes
// down at the end
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
  Require(env.SetCellTag({3, 2}, "burning", Hurting(1)), "fire");
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

// A defeated agent stays in play until the end of the turn: pushed along the
// lake this turn, it is electrified where it stands after the motion (the
// storm's cell), defeated there, and its own reaction still fires and
// reaches it (result and damage); the push that moved it hits it too
TEST(TestAnAgentPushedOntoItsWeaknessIsDefeatedAndStillGetsItsResult) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked", 1, true)}), "reactions");
  SkillConfig storm;  // Electrifies the cell 2 ahead
  storm.name = "storm";
  storm.targeting = SkillTargeting::Ground;
  storm.range = 2;
  storm.tags = {{"electrified", kPermanentTag}};
  env.GetMutableSkillBook().Define(storm);
  Require(env.SetCompanionSkill(AgentAt(env, 0)->GetId(), 0, "storm"), "storm");
  GiveGust(env, 1);
  Place(env, 0, {3, 1});  // Storms right: (3,3)
  Place(env, 1, {3, 5});  // Gusts: the imp on its left ring cell, pushed to (3,3)
  Agent* imp = AddEnemy(env, {3, 4}, 5);
  for (Position p : {Position{3, 3}, Position{3, 4}}) Require(env.SetCellTag(p, "wet"), "lake");
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

// Pushed, then defeated, then still hit: the gust pushes the imp onto the
// lake (the motion phase), the tag phase lands the lake on its final cell
// (weak to wet there: defeated); still in play, the gust's hits then tag it
// (and report the push that moved it); it dies at the end of the turn. (The
// zone it began the turn on, dry, lands nothing.)
TEST(TestADefeatedAgentIsStillPushedThisTurn) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  GiveGust(env, 1);
  Agent* gust = Place(env, 1, {3, 5});  // Gusts: the imp on its left ring cell, pushed to (3,3)
  Agent* imp = AddEnemy(env, {3, 4}, 5);
  Require(env.SetCellTag({3, 3}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "wet"}}), "weak_to");
  env.Step(With(env, 1, Use(MovementAction::Stay)));
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastDefeats().at(0).kind == BaseEnv::TagSource::Zone);
  ASSERT_TRUE(imp->GetPosition() == (Position{3, 3}));  // Pushed onto the lake
  ASSERT_TRUE(Has(env, imp, "gusted"));                  // Hit after its defeat
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  // The caster on its own centre (spared its tags), then the imp
  ASSERT_TRUE(env.GetLastSkillUses().at(0).affected ==
              (std::vector<BaseEnv::AffectedAgent>{
                  {gust->GetId(), 0},
                  {imp->GetId(), BaseEnv::kSkillEffectTags | BaseEnv::kSkillEffectMotion}}));
  ASSERT_FALSE(imp->IsAlive());  // At the end
  ASSERT_TRUE(TurnOf(env, imp).outcome == TurnOutcome::Defeated);
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
// Skill uses planned from the world as the turn began
// =============================================================================

static std::string TagName(const BaseEnv& env, TagId tag) {
  return tag == kInvalidTag ? std::string("-") : env.GetTagTable().Name(tag);
}

// The last step's reports and the world it left, by ROLE (ids and agent
// indices dropped), the lines sorted: two worlds that differ only in their
// agents' order must give the same text. Without `credits`, what a reaction
// credits (its triggering tag, its results' source and cause: the first
// matching skill landing in report order, a report-only effect of the
// indices) is left out.
static std::string RoleTrace(const BaseEnv& env, const std::map<ObjectId, std::string>& roles,
                             bool credits = true) {
  auto role = [&roles](ObjectId id) {
    auto it = roles.find(id);
    return it == roles.end() ? std::string("-") : it->second;
  };
  std::vector<std::string> lines;
  for (const auto& u : env.GetLastSkillUses()) {
    std::ostringstream out;
    out << "use " << role(u.caster) << " " << u.skill << " " << u.slot << " " << u.target.row
        << "," << u.target.col << ":";
    std::vector<std::string> affected;
    for (const auto& a : u.affected) {
      affected.push_back(role(a.id) + "/" + std::to_string(a.effects));
    }
    std::sort(affected.begin(), affected.end());
    for (const std::string& a : affected) out << " " << a;
    lines.push_back(out.str());
  }
  for (const auto& t : env.GetLastTagsApplied()) {
    std::ostringstream out;
    const bool credit = credits || t.kind != BaseEnv::TagSource::Reaction;
    out << "tag " << role(t.agent) << " " << TagName(env, t.tag) << " " << t.duration << " "
        << (credit ? role(t.source) + " " + t.cause : std::string("credit")) << " " << t.fresh
        << " " << t.damage << " " << static_cast<int>(t.kind) << " " << (t.reaction >= 0);
    lines.push_back(out.str());
  }
  for (const auto& r : env.GetLastReactions()) {
    lines.push_back("reaction " + std::to_string(r.rule) + " " + role(r.trigger) + " " +
                    (credits ? TagName(env, r.tag) : std::string("credit")) + " " +
                    std::to_string(r.affected.size()));
  }
  for (const auto& d : env.GetLastDefeats()) lines.push_back("defeat " + role(d.agent));
  for (ObjectId id : env.GetLastDowns()) lines.push_back("down " + role(id));
  for (const auto& r : env.GetLastRevives()) {
    lines.push_back("revive " + role(r.reviver) + " " + role(r.revived));
  }
  for (const auto& t : env.GetLastTurnHealth()) {
    lines.push_back("health " + role(t.agent) + " " + std::to_string(t.damage) + " " +
                    std::to_string(t.heal) + " " + std::to_string(t.change) + " " +
                    std::to_string(static_cast<int>(t.outcome)));
  }
  for (const Agent* a : env.GetObjectManager().GetAllAgents()) {
    std::ostringstream out;
    out << "agent " << role(a->GetId()) << " " << a->GetHealth() << " " << a->IsAffectable()
        << " " << a->GetPosition().row << "," << a->GetPosition().col << ":";
    std::vector<std::string> marks;
    for (const AgentTag& t : a->GetTags()) {
      marks.push_back(TagName(env, t.id) + "/" + std::to_string(t.duration));
    }
    for (const auto& st : a->GetStatuses()) {
      marks.push_back("s" + std::to_string(static_cast<int>(st.type)) + "/" +
                      std::to_string(st.duration));
    }
    if (const auto* c = dynamic_cast<const Companion*>(a)) {
      marks.push_back("cooldown/" + std::to_string(c->GetCooldown(0)));
    }
    std::sort(marks.begin(), marks.end());
    for (const std::string& m : marks) out << " " << m;
    lines.push_back(out.str());
  }
  std::sort(lines.begin(), lines.end());
  std::ostringstream out;
  for (const std::string& l : lines) out << l << "\n";
  return out.str();
}

using Affected = std::vector<BaseEnv::AffectedAgent>;
constexpr unsigned kTagsFx = BaseEnv::kSkillEffectTags;
constexpr unsigned kRootFx = BaseEnv::kSkillEffectRoot;
constexpr unsigned kMotionFx = BaseEnv::kSkillEffectMotion;

static int IndexOf(const BaseEnv& env, ObjectId id) {
  return dynamic_cast<const Agent*>(env.GetObjectManager().GetActor(id))->GetAgentIndex();
}

// Three casters, whatever their indices: V's vortex (right, centre (3,4))
// pulls F from its up ring cell; F's fireball aims down from (2,4), where F
// began the turn (centre (5,4), not (6,4) from the cell it is pulled to), and
// burns and pushes the gob E on its right ring cell; B's bolt (down) zaps V;
// the gob G on the vortex's right ring cell is rooted. Sequential casters
// made F's aim depend on who came first; the plans read the world as the turn
// began, so only the report order depends on the indices.
TEST(TestSwappingTwoCastersIndicesChangesNoOutcome) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 3, 1, 0, 42);
    MakeArena(env);
    const int v = swapped ? 2 : 0, f = 1, b = swapped ? 0 : 2;
    Agent* vortex = Place(env, v, {3, 1});
    Agent* fire = Place(env, f, {2, 4});
    Agent* bolt = Place(env, b, {1, 1});
    Agent* e = AddEnemy(env, {5, 5});
    Agent* g = AddEnemy(env, {3, 5});
    Require(env.SetCompanionSkill(vortex->GetId(), 0, "vortex"), "vortex");
    Require(env.SetCompanionSkill(fire->GetId(), 0, "fireball"), "fireball");
    GiveBolt(env, b, "zap", "zapped");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(v)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(f)] = Use(MovementAction::Down);
    actions[static_cast<size_t>(b)] = Use(MovementAction::Down);
    env.Step(actions);
    ASSERT_TRUE(fire->GetPosition() == (Position{3, 4}));  // Pulled
    ASSERT_TRUE(fire->IsRooted());
    ASSERT_TRUE(e->GetPosition() == (Position{5, 6}));     // Pushed
    ASSERT_TRUE(Has(env, e, "burning"));
    ASSERT_TRUE(g->GetPosition() == (Position{3, 5}));
    ASSERT_TRUE(g->IsRooted());
    ASSERT_TRUE(Has(env, vortex, "zapped"));
    const auto& uses = env.GetLastSkillUses();
    ASSERT_EQ(uses.size(), static_cast<size_t>(3));
    for (size_t i = 1; i < uses.size(); ++i) {  // Reported in caster index order
      ASSERT_TRUE(IndexOf(env, uses[i - 1].caster) < IndexOf(env, uses[i].caster));
    }
    for (const auto& u : uses) {
      if (u.caster == fire->GetId()) ASSERT_TRUE(u.target == (Position{5, 4}));
    }
    traces[swapped] = RoleTrace(env, {{vortex->GetId(), "V"},
                                      {fire->GetId(), "F"},
                                      {bolt->GetId(), "B"},
                                      {e->GetId(), "E"},
                                      {g->GetId(), "G"}});
  }
  if (traces[0] != traces[1]) {
    throw std::runtime_error("the order matters:\n" + traces[0] + "--- swapped ---\n" + traces[1]);
  }
}

// A bolt's line is traced as the turn began: it stops on the gob's cell (3,2).
// The gob walks on along the line to (3,3), out of it: missed (the line is
// not traced again). A blast's gob walking off its ring dodges it too.
TEST(TestAnEnemyWalkingAwayIsMissed) {
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {3, 1});
    GiveBolt(env, 0, "zap", "zapped", 2);
    Agent* gob = AddEnemy(env, {3, 2});
    env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Right)});
    ASSERT_TRUE(gob->GetPosition() == (Position{3, 3}));
    ASSERT_FALSE(Has(env, gob, "zapped"));
    ASSERT_EQ(gob->GetHealth(), 5);
    ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).target == (Position{3, 2}));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.empty());
  }
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    SkillConfig blast;  // A cross 3 ahead, burning and 1 damage, no motion
    blast.name = "blast";
    blast.targeting = SkillTargeting::Ground;
    blast.range = 3;
    blast.area = SkillArea::Cross;
    blast.tags = {{"burning", kPermanentTag}};
    blast.damage = 1;
    env.GetMutableSkillBook().Define(blast);
    Agent* caster = Place(env, 0, {3, 1});  // Right: centre (3,4)
    Require(env.SetCompanionSkill(caster->GetId(), 0, "blast"), "blast");
    Agent* gob = AddEnemy(env, {2, 4});     // Its up ring cell: walks up, out
    env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Up)});
    ASSERT_TRUE(gob->GetPosition() == (Position{1, 4}));
    ASSERT_FALSE(Has(env, gob, "burning"));
    ASSERT_EQ(gob->GetHealth(), 5);
    ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.empty());
  }
}

// The cells are fixed as the turn began, the hits land on whoever stands on
// them after the motion phase: a gob walking onto the fireball's ring burns,
// and is pushed (the forced moves come last, on whoever stands on the ring
// then: up, to (1,4)); a gob walking onto the bolt's impact cell is hit; one
// walking into the bolt's line before that cell is not (the line is not
// traced again).
TEST(TestAnAgentWalkingIntoTheAreaIsHit) {
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});  // Fireball right: centre (3,4)
    Require(env.SetCompanionSkill(caster->GetId(), 0, "fireball"), "fireball");
    Agent* gob = AddEnemy(env, {2, 5});     // Walks left onto the up ring cell (2,4)
    env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Left)});
    ASSERT_TRUE(gob->GetPosition() == (Position{1, 4}));
    ASSERT_TRUE(Has(env, gob, "burning"));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).affected ==
                (Affected{{gob->GetId(), kTagsFx | kMotionFx}}));
  }
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {5, 1});
    GiveBolt(env, 0, "zap", "zapped", 1);      // Right: nobody on the line, centre (5,4)
    Agent* in_line = AddEnemy(env, {6, 3});    // Walks up into the line: (5,3)
    Agent* on_impact = AddEnemy(env, {6, 4});  // Walks up onto the impact cell: (5,4)
    const Action up = EncodeAction(MovementAction::Up);
    env.Step({Use(MovementAction::Right), up, up});
    ASSERT_TRUE(in_line->GetPosition() == (Position{5, 3}));
    ASSERT_TRUE(on_impact->GetPosition() == (Position{5, 4}));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).target == (Position{5, 4}));
    ASSERT_TRUE(Has(env, on_impact, "zapped"));
    ASSERT_EQ(on_impact->GetHealth(), 4);
    ASSERT_FALSE(Has(env, in_line, "zapped"));
    ASSERT_EQ(in_line->GetHealth(), 5);
  }
}

// Exposes the intents phase
class PlanningEnv : public SynchroEnv {
 public:
  using SynchroEnv::SynchroEnv;
  void Gather(const std::vector<Action>& actions) { GatherIntentions(actions); }
};

// The cooldown is spent as the use is planned (the intents phase), and a use
// whose target walked out of its area still happened: spent, reported,
// affecting nobody.
TEST(TestTheCooldownIsSpentWhenTheUseIsPlanned) {
  {
    PlanningEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});
    Require(env.SetCompanionSkill(caster->GetId(), 0, "fireball"), "fireball");
    env.Gather({Use(MovementAction::Right)});
    ASSERT_EQ(AsCompanion(caster)->GetCooldown(0), 3);
  }
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});
    GiveBolt(env, 0, "zap", "zapped");
    SkillConfig zap = *env.GetSkillBook().Find("zap");
    zap.cooldown = 3;
    env.GetMutableSkillBook().Define(zap);
    Agent* gob = AddEnemy(env, {3, 2});  // Stops the bolt as the turn begins, walks away
    env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Down)});
    ASSERT_TRUE(gob->GetPosition() == (Position{4, 2}));
    ASSERT_FALSE(Has(env, gob, "zapped"));
    ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.empty());
    ASSERT_EQ(AsCompanion(caster)->GetCooldown(0), 3);
    env.Step(Stays(env));
    ASSERT_EQ(AsCompanion(caster)->GetCooldown(0), 2);
  }
}

// A caster down as the turn begins cannot cast; one the turn takes to 0 still
// casts (it goes down at the end)
TEST(TestACasterDownAtTurnStartCannotCastOneGoingDownStillCasts) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* down = Place(env, 0, {3, 1});
  Agent* going = Place(env, 1, {5, 1});
  Require(env.SetCompanionSkill(down->GetId(), 0, "fireball"), "fireball");
  GiveBolt(env, 1, "zap", "zapped");
  going->SetMaxHealth(5);
  going->RestoreHealth(1);
  Require(env.SetCellTag({5, 1}, "burning", Hurting(1)), "fire");
  Agent* gob = AddEnemy(env, {5, 3});
  DownCompanion(env, 0);
  env.Step({Use(MovementAction::Right), Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses().at(0).caster, going->GetId());
  ASSERT_TRUE(Has(env, gob, "zapped"));
  ASSERT_EQ(AsCompanion(down)->GetCooldown(0), 0);  // No use
  ASSERT_TRUE(AsCompanion(down)->IsDowned());
  ASSERT_TRUE(AsCompanion(going)->IsDowned());      // At the end
}

// A bolt stopped as the turn began by the first gob (3,2), which walks away
// (down): its cells are the turn's start's, so it hits nobody, not the gob
// behind (3,4) either
TEST(TestAProjectileStopsOnTheFirstAgentAsTheTurnBegan) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 1});
  GiveBolt(env, 0, "zap", "zapped");
  Agent* first = AddEnemy(env, {3, 2});
  Agent* behind = AddEnemy(env, {3, 4});
  env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Down), kStay});
  ASSERT_TRUE(first->GetPosition() == (Position{4, 2}));
  ASSERT_TRUE(env.GetLastSkillUses().at(0).target == (Position{3, 2}));
  ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.empty());
  ASSERT_FALSE(Has(env, first, "zapped"));
  ASSERT_FALSE(Has(env, behind, "zapped"));
}

// A vortex (right: centre (3,4)) plans its pull as the turn begins: the ally
// above (first by ring priority), as its preview says. The forced moves come
// last, on whoever stands on the ring after the walks: the ally walks away up
// (it dodges: not pulled, out of the cells, not rooted), so the pull takes
// the next ring thing by priority, the gob on the right ring cell.
TEST(TestAPullTakesTheRingThingAsTheTurnBegan) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* ally = Place(env, 1, {2, 4});
  Agent* gob = AddEnemy(env, {3, 5});
  Require(env.SetCompanionSkill(caster->GetId(), 0, "vortex"), "vortex");
  const BaseEnv::SkillPreview p = env.PreviewSkill(*AsCompanion(caster), 0, Direction::Right);
  ASSERT_TRUE(p.affected ==
              (Affected{{ally->GetId(), kRootFx | kMotionFx}, {gob->GetId(), kRootFx}}));
  env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Up), kStay});
  ASSERT_TRUE(ally->GetPosition() == (Position{1, 4}));
  ASSERT_FALSE(ally->IsRooted());
  ASSERT_TRUE(gob->GetPosition() == (Position{3, 4}));  // Pulled
  ASSERT_TRUE(gob->IsRooted());
  ASSERT_TRUE(env.GetLastSkillUses().at(0).affected ==
              (Affected{{gob->GetId(), kRootFx | kMotionFx}}));
}

// Two dashes planned onto one cell (3,5): the lower index lands there; the
// other falls back along its line to the next free cell (4,5) and keeps its
// planned area and centre (3,5), so its cross electrifies the winner there.
TEST(TestADashLosingItsLandingFallsBackAndKeepsItsArea) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({2, 5}, CellKind::Wall);  // b's dash up stops at (3,5)
  Agent* a = Place(env, 0, {3, 1});                      // Dashes right to (3,5)
  Agent* b = Place(env, 1, {6, 5});                      // Dashes up to (3,5)
  Require(env.SetCompanionSkill(a->GetId(), 0, "lightningStep"), "a");
  Require(env.SetCompanionSkill(b->GetId(), 0, "lightningStep"), "b");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Up)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(b->GetPosition() == (Position{4, 5}));
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_TRUE(env.GetLastSkillUses().at(1).target == (Position{3, 5}));
  ASSERT_TRUE(Has(env, a, "electrified"));  // On b's centre
  ASSERT_TRUE(Has(env, b, "electrified"));  // On a's down ring cell
}

// A tag_path dash's cells are fixed as the turn began (its path, and its
// area around the planned landing): a gob walking off the area dodges; one
// walking onto the path is hit there. The dashes come before the walks: the
// dash has passed, it lands on (3,5).
TEST(TestADashPathIsItsCellsAsTheTurnBegan) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});  // lightningStep right: path (3,2)-(3,4), lands (3,5)
  Require(env.SetCompanionSkill(caster->GetId(), 0, "lightningStep"), "lightningStep");
  Agent* off = AddEnemy(env, {2, 5});   // Above the landing (the area's ring); walks up, off it
  Agent* onto = AddEnemy(env, {2, 3});  // Walks down, onto the path (3,3)
  env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Up),
            EncodeAction(MovementAction::Down)});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(off->GetPosition() == (Position{1, 5}));
  ASSERT_TRUE(onto->GetPosition() == (Position{3, 3}));
  ASSERT_FALSE(Has(env, off, "electrified"));
  ASSERT_TRUE(Has(env, onto, "electrified"));
  ASSERT_TRUE(env.GetLastSkillUses().at(0).target == (Position{3, 5}));
  // The caster on its own centre (self_tags spares it), then the gob
  ASSERT_TRUE(env.GetLastSkillUses().at(0).affected ==
              (Affected{{caster->GetId(), 0}, {onto->GetId(), kTagsFx}}));
}

// Pushes and pulls move the things (not only agents) on the ring as the turn
// began: a boulder pushed off a fireball's ring, one pulled into a vortex
TEST(TestPushesAndPullsMoveThingsPlannedAsTheTurnBegan) {
  for (const char* skill : {"fireball", "vortex"}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});  // Right: centre (3,4)
    Require(env.SetCompanionSkill(caster->GetId(), 0, skill), skill);
    Actor* boulder = env.GetMutableObjectManager().CreateActor<Actor>({3, 5});  // Right ring cell
    env.Step({Use(MovementAction::Right)});
    const bool push = std::string(skill) == "fireball";
    ASSERT_TRUE(boulder->GetPosition() == (push ? Position{3, 6} : Position{3, 4}));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).affected.empty());  // Agents only
  }
}

// =============================================================================
// One motion phase
// =============================================================================

using OddMotion = BaseEnv::OddMotion;
constexpr unsigned kNoFx = 0;

// A Self cross pushing everyone on its ring `distance` cells away from the
// caster (allies included), no tag, put in the companion's slot 0 as `name`
static void GivePusher(SynchroEnv& env, int companion, const char* name, int distance) {
  SkillConfig s;
  s.name = name;
  s.targeting = SkillTargeting::Self;
  s.area = SkillArea::Cross;
  s.motion = SkillMotion::PushOut;
  s.motion_distance = distance;
  env.GetMutableSkillBook().Define(s);
  Require(env.SetCompanionSkill(AgentAt(env, companion)->GetId(), 0, name), name);
}

// A thing (a living actor, not an agent) on `p`
static Actor* AddBoulder(SynchroEnv& env, Position p) {
  return env.GetMutableObjectManager().CreateActor<Actor>(p);
}

static Action Walk(MovementAction m) { return EncodeAction(m); }

// What the last step's use by `caster` did to `agent` (its SkillEffect
// flags), -1 when the use did not affect it
static int EffectsOn(const BaseEnv& env, const Agent* caster, const Agent* agent) {
  for (const auto& u : env.GetLastSkillUses()) {
    if (u.caster != caster->GetId()) continue;
    for (const auto& a : u.affected) {
      if (a.id == agent->GetId()) return static_cast<int>(a.effects);
    }
  }
  return -1;
}

// The zones that landed on `agent` in the last step, by name
static std::vector<std::string> ZonesLanded(const BaseEnv& env, const Agent* agent) {
  std::vector<std::string> out;
  for (const auto& t : env.GetLastTagsApplied()) {
    if (t.agent == agent->GetId() && t.kind == BaseEnv::TagSource::Zone) {
      out.push_back(TagName(env, t.tag));
    }
  }
  return out;
}

// A forced move's line (ForcedMovePath): straight on an axis, else the
// rounded line toward its end; cut before the first wall or hole
TEST(TestAForcedMovesLine) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  using Cells = std::vector<Position>;
  Cells cells{{1, 1}};  // Stale content is replaced
  ForcedMovePath(env.GetGrid(), {4, 4}, 0, 3, cells);
  ASSERT_TRUE(cells == (Cells{{4, 5}, {4, 6}, {4, 7}}));
  ForcedMovePath(env.GetGrid(), {4, 4}, 1, 2, cells);
  ASSERT_TRUE(cells == (Cells{{5, 5}, {5, 6}}));
  ForcedMovePath(env.GetGrid(), {4, 4}, -1, -2, cells);
  ASSERT_TRUE(cells == (Cells{{3, 3}, {3, 2}}));
  ForcedMovePath(env.GetGrid(), {4, 4}, 3, 1, cells);
  ASSERT_TRUE(cells == (Cells{{5, 4}, {6, 5}, {7, 5}}));
  ForcedMovePath(env.GetGrid(), {4, 4}, 1, 1, cells);
  ASSERT_TRUE(cells == (Cells{{5, 5}}));
  ForcedMovePath(env.GetGrid(), {4, 4}, 0, 0, cells);
  ASSERT_TRUE(cells.empty());
  env.GetMutableGrid().SetCell({4, 6}, CellKind::Hazard);
  ForcedMovePath(env.GetGrid(), {4, 4}, 0, 3, cells);
  ASSERT_TRUE(cells == (Cells{{4, 5}}));  // Stops before the hole
  ForcedMovePath(env.GetGrid(), {4, 7}, 0, 3, cells);
  ASSERT_TRUE(cells == (Cells{{4, 8}}));  // And before the wall
}

// Two pushes right on one gob (a gust around (4,2), a shove centred on (4,2)
// from above) add up: 2 cells right, and both uses moved it. The gust's
// caster, the shove's centre, is not pushed (only the ring is).
TEST(TestForcedMovesOnOneAgentAddUp) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* gust = Place(env, 0, {4, 2});
  GivePusher(env, 0, "gust1", 1);
  Agent* shover = Place(env, 1, {1, 2});  // Aims down: centre (4,2)
  SkillConfig shove;
  shove.name = "shove";
  shove.targeting = SkillTargeting::Ground;
  shove.range = 3;
  shove.area = SkillArea::Cross;
  shove.motion = SkillMotion::PushOut;
  shove.motion_distance = 1;
  env.GetMutableSkillBook().Define(shove);
  Require(env.SetCompanionSkill(shover->GetId(), 0, "shove"), "shove");
  Agent* gob = AddEnemy(env, {4, 3});
  env.Step({Use(MovementAction::Right), Use(MovementAction::Down), kStay});
  ASSERT_TRUE(gob->GetPosition() == (Position{4, 5}));
  ASSERT_EQ(EffectsOn(env, gust, gob), static_cast<int>(kMotionFx));
  ASSERT_EQ(EffectsOn(env, shover, gob), static_cast<int>(kMotionFx));
  ASSERT_TRUE(gust->GetPosition() == (Position{4, 2}));
  ASSERT_TRUE(env.GetLastOddMotions().empty());  // An axis sum
}

// Pushes right and left on one gob cancel: it stays, and neither use moved
// it (no Motion), though both reached it
TEST(TestOpposedPushesCancel) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* left = Place(env, 0, {4, 2});
  Agent* right = Place(env, 1, {4, 4});
  GivePusher(env, 0, "gust1", 1);
  Require(env.SetCompanionSkill(right->GetId(), 0, "gust1"), "gust1");
  Agent* gob = AddEnemy(env, {4, 3});
  env.Step({Use(MovementAction::Right), Use(MovementAction::Left), kStay});
  ASSERT_TRUE(gob->GetPosition() == (Position{4, 3}));
  ASSERT_EQ(EffectsOn(env, left, gob), static_cast<int>(kNoFx));
  ASSERT_EQ(EffectsOn(env, right, gob), static_cast<int>(kNoFx));
}

// A push 2 right (a gust around (4,3)) and a push 1 down (a gust around
// (3,4)) on the gob on (4,4): (1, 2), off the axes. It travels the line
// toward (5,6): (5,5), then (5,6), and stops at the last free cell (a boulder
// on (5,6): (5,5)). Every such sum is recorded: the step's report, and a
// count for the env's life (copied with it, kept by Reset).
TEST(TestAnUnevenSumTravelsInAStraightLineAndIsRecorded) {
  for (bool blocked : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 3});
    GivePusher(env, 0, "gust2", 2);
    Place(env, 1, {3, 4});
    GivePusher(env, 1, "gust1", 1);
    Agent* gob = AddEnemy(env, {4, 4});
    if (blocked) AddBoulder(env, {5, 6});
    ASSERT_EQ(env.GetOddMotionCount(), 0);
    env.Step({Use(MovementAction::Right), Use(MovementAction::Right), kStay});
    ASSERT_TRUE(gob->GetPosition() == (blocked ? Position{5, 5} : Position{5, 6}));
    ASSERT_EQ(env.GetLastOddMotions().size(), static_cast<size_t>(1));
    const OddMotion odd = env.GetLastOddMotions().at(0);
    ASSERT_EQ(odd.actor, gob->GetId());
    ASSERT_EQ(odd.dr, 1);
    ASSERT_EQ(odd.dc, 2);
    ASSERT_EQ(env.GetOddMotionCount(), 1);
    std::unique_ptr<BaseEnv> copy = env.Clone();
    ASSERT_EQ(copy->GetOddMotionCount(), 1);
    ASSERT_EQ(copy->GetLastOddMotions().size(), static_cast<size_t>(1));
    env.Step(Stays(env));
    ASSERT_TRUE(env.GetLastOddMotions().empty());  // The step's report
    ASSERT_EQ(env.GetOddMotionCount(), 1);         // The env's count
    env.Reset();
    ASSERT_EQ(env.GetOddMotionCount(), 1);
  }
}

// The forced moves come last, on whoever stands on their cells after the
// walks: an ally on a gust's ring walking away dodges it (out of the ring,
// untouched); one whose walk is blocked (a gob stays where it walks) or runs
// into a wall is still on the ring: pushed (right, to (4,4)) and gusted
TEST(TestAWalkDodgesAPush) {
  for (int scene = 0; scene < 3; ++scene) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {4, 2});
    GiveGust(env, 0);
    Agent* ally = Place(env, 1, {4, 3});  // The right ring cell; walks up to (3,3)
    if (scene == 1) AddEnemy(env, {3, 3});
    if (scene == 2) env.GetMutableGrid().SetCell({3, 3}, CellKind::Wall);
    std::vector<Action> actions = Stays(env);
    actions[0] = Use(MovementAction::Right);
    actions[1] = Walk(MovementAction::Up);
    env.Step(actions);
    ASSERT_TRUE(ally->GetPosition() == (scene == 0 ? Position{3, 3} : Position{4, 4}));
    ASSERT_EQ(EffectsOn(env, caster, ally),
              scene == 0 ? -1 : static_cast<int>(kTagsFx | kMotionFx));
  }
}

// The same for a gob, and a walker stepping INTO a ring is pushed: a gob on
// a gust's ring walking up dodges it; one whose walk is blocked is pushed; one
// walking down into the ring (3,3) -> (4,3) is pushed on to (4,4). The
// executed action (the C API's) is the walk the walker made, Stay if it did
// not walk, whatever pushed it after.
TEST(TestAGobWalkingOffARingDodgesItsPush) {
  for (int scene = 0; scene < 3; ++scene) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 2});
    GiveGust(env, 0);
    Agent* gob = AddEnemy(env, scene == 2 ? Position{3, 3} : Position{4, 3});
    if (scene == 1) AddEnemy(env, {3, 3});
    std::vector<Action> actions = Stays(env);
    actions[0] = Use(MovementAction::Right);
    actions[1] = Walk(scene == 2 ? MovementAction::Down : MovementAction::Up);
    env.Step(actions);
    const Position expected[] = {{3, 3}, {4, 4}, {4, 4}};
    ASSERT_TRUE(gob->GetPosition() == expected[scene]);
    const MovementAction executed[] = {MovementAction::Up, MovementAction::Stay,
                                       MovementAction::Down};
    ASSERT_TRUE(gob->GetExecutedAction().movement == executed[scene]);
  }
}

// The layers: teleports, then dashes, then walks, then forced moves, each
// seeing the final result of those before. A companion dashing off a gust's
// ring is not pushed; a teleport (index 1) lands on (3,5) before a dash
// (index 0) claims it (the dash falls back along its line); a dash (index 1)
// takes (3,5) before a companion (index 0) walks onto it (the walk is then
// blocked); a dasher landing on a gust's ring is pushed (left, to (3,4)).
TEST(TestTheLayersTeleportsDashesWalksThenForcedMoves) {
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {4, 2});
    GiveGust(env, 0);
    Agent* dasher = Place(env, 1, {4, 3});  // On the ring; dashes up to (1,3)
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    env.Step({Use(MovementAction::Right), Use(MovementAction::Up)});
    ASSERT_TRUE(dasher->GetPosition() == (Position{1, 3}));
    ASSERT_EQ(EffectsOn(env, caster, dasher), -1);
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({3, 6}, CellKind::Wall);  // The dash right ends on (3,5)
    Agent* dasher = Place(env, 0, {3, 1});
    Agent* teleporter = Place(env, 1, {6, 5});  // Teleports up 3: (3,5)
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    Require(env.SetCompanionSkill(teleporter->GetId(), 0, "teleport"), "teleport");
    env.Step({Use(MovementAction::Right), Use(MovementAction::Up)});
    ASSERT_TRUE(teleporter->GetPosition() == (Position{3, 5}));
    ASSERT_TRUE(dasher->GetPosition() == (Position{3, 4}));
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({3, 6}, CellKind::Wall);
    Agent* walker = Place(env, 0, {2, 5});  // Walks down onto (3,5)
    Agent* dasher = Place(env, 1, {3, 1});
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    env.Step({Walk(MovementAction::Down), Use(MovementAction::Right)});
    ASSERT_TRUE(dasher->GetPosition() == (Position{3, 5}));
    ASSERT_TRUE(walker->GetPosition() == (Position{2, 5}));
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* dasher = Place(env, 0, {3, 1});  // Dashes right 4: (3,5)
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    Agent* caster = Place(env, 1, {3, 6});  // Gusts: (3,5) is its left ring cell
    GiveGust(env, 1);
    env.Step({Use(MovementAction::Right), Use(MovementAction::Stay)});
    ASSERT_TRUE(dasher->GetPosition() == (Position{3, 4}));
    ASSERT_EQ(EffectsOn(env, caster, dasher), static_cast<int>(kTagsFx | kMotionFx));
  }
}

// Within a layer the motions are simultaneous and the lower index only
// breaks exact ties: two things pushed onto one cell (4,6) (gusts around
// (4,4) and (4,8)), the lower index gets it, whether companions or gobs, and
// swapping the indices flips the winner. Across layers there is no tie: a
// companion walks onto (4,6) before a gob is pushed there (the push is
// blocked), whatever their indices.
TEST(TestTiesInALayerGoToTheLowerIndex) {
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 4});
    GiveGust(env, 0);
    Agent* walker = Place(env, 1, {3, 6});  // Walks down onto (4,6)
    Agent* gob = AddEnemy(env, {4, 5});     // Pushed right onto (4,6)
    env.Step({Use(MovementAction::Right), Walk(MovementAction::Down), kStay});
    ASSERT_TRUE(walker->GetPosition() == (Position{4, 6}));
    ASSERT_TRUE(gob->GetPosition() == (Position{4, 5}));
  }
  for (bool companions : {true, false}) {
    for (bool swapped : {false, true}) {
      SynchroEnv env(10, 10, companions ? 4 : 2, 1, 0, 42);
      MakeArena(env);
      Place(env, 0, {4, 4});
      GiveGust(env, 0);
      Agent* right = Place(env, 1, {4, 8});
      Require(env.SetCompanionSkill(right->GetId(), 0, "gust"), "gust");
      const Position cells[] = {{4, 5}, {4, 7}};
      Agent* low;   // The lower index
      Agent* high;
      if (companions) {
        low = Place(env, 2, cells[swapped ? 1 : 0]);
        high = Place(env, 3, cells[swapped ? 0 : 1]);
      } else {
        low = AddEnemy(env, cells[swapped ? 1 : 0]);
        high = AddEnemy(env, cells[swapped ? 0 : 1]);
      }
      std::vector<Action> actions = Stays(env);
      actions[0] = Use(MovementAction::Right);
      actions[1] = Use(MovementAction::Left);
      env.Step(actions);
      ASSERT_TRUE(low->GetPosition() == (Position{4, 6}));
      ASSERT_TRUE(high->GetPosition() == cells[swapped ? 0 : 1]);
    }
  }
}

// Within a layer the motions are simultaneous: a gob pushed right onto the
// cell of a gob pushed down that same layer gets it (a cell whose occupant
// leaves in the layer is free), whichever index is lower (in rank order,
// one at a time, the lower one would find the cell still held)
TEST(TestALayerIsSimultaneous) {
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 2});  // Gusts: (4,3) is its right ring cell
    GiveGust(env, 0);
    Place(env, 1, {3, 4});  // Gusts: (4,4) is its down ring cell
    GiveGust(env, 1);
    Agent* first = AddEnemy(env, swapped ? Position{4, 4} : Position{4, 3});
    Agent* second = AddEnemy(env, swapped ? Position{4, 3} : Position{4, 4});
    Agent* pushed_right = swapped ? second : first;
    Agent* pushed_down = swapped ? first : second;
    env.Step({Use(MovementAction::Stay), Use(MovementAction::Stay), kStay, kStay});
    ASSERT_TRUE(pushed_right->GetPosition() == (Position{4, 4}));
    ASSERT_TRUE(pushed_down->GetPosition() == (Position{5, 4}));
  }
}

// A cell whose occupant really leaves this turn is free: the gob pushed onto
// (4,4) as the gob there walks up
TEST(TestAPushIntoACellLeftThisTurn) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {4, 2});
  GiveGust(env, 0);
  Agent* pushed = AddEnemy(env, {4, 3});
  Agent* leaver = AddEnemy(env, {4, 4});
  env.Step({Use(MovementAction::Right), kStay, Walk(MovementAction::Up)});
  ASSERT_TRUE(pushed->GetPosition() == (Position{4, 4}));
  ASSERT_TRUE(leaver->GetPosition() == (Position{3, 4}));
}

// ...and blocked if the leaver ends up staying: its walk is blocked (a gob
// stays on (3,4)), so the push is too
TEST(TestAPushIntoACellWhoseLeaverStays) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {4, 2});
  GiveGust(env, 0);
  Agent* pushed = AddEnemy(env, {4, 3});
  Agent* leaver = AddEnemy(env, {4, 4});
  AddEnemy(env, {3, 4});
  env.Step({Use(MovementAction::Right), kStay, Walk(MovementAction::Up), kStay});
  ASSERT_TRUE(pushed->GetPosition() == (Position{4, 3}));
  ASSERT_TRUE(leaver->GetPosition() == (Position{4, 4}));
}

// A push 3 right from (4,3) stops at the first blocker on its path, on the
// last free cell before it (it never hops over one): a boulder, a hole or a
// wall on (4,5), or a companion walking onto (4,5) this turn (a cell held at
// the end of the turn): (4,4)
TEST(TestAPushStopsAtTheFirstBlocker) {
  for (int scene = 0; scene < 4; ++scene) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 2});
    GivePusher(env, 0, "gust3", 3);
    Agent* walker = Place(env, 1, {3, 5});
    Agent* gob = AddEnemy(env, {4, 3});
    if (scene == 0) AddBoulder(env, {4, 5});
    if (scene == 1) env.GetMutableGrid().SetCell({4, 5}, CellKind::Hazard);
    if (scene == 2) env.GetMutableGrid().SetCell({4, 5}, CellKind::Wall);
    std::vector<Action> actions = Stays(env);
    actions[0] = Use(MovementAction::Right);
    if (scene == 3) actions[1] = Walk(MovementAction::Down);
    env.Step(actions);
    ASSERT_TRUE(gob->GetPosition() == (Position{4, 4}));
    ASSERT_TRUE(walker->GetPosition() == (scene == 3 ? Position{4, 5} : Position{3, 5}));
  }
}

// A dash jumps holes but never ends on one, and stops at the first blocker.
// Planned as the turn began: a hole on (3,3), a boulder on (3,5): it lands on
// (3,4). A teleport landing on its path first (the teleports' layer comes
// before) holds that cell: the dash stops before it, on (3,2), and keeps its
// planned centre (3,5) and path (the teleporter on it is electrified).
TEST(TestADashStopsAtTheFirstActorButJumpsHoles) {
  {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({3, 3}, CellKind::Hazard);
    Agent* dasher = Place(env, 0, {3, 1});
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    AddBoulder(env, {3, 5});
    ASSERT_TRUE(env.PreviewSkill(*AsCompanion(dasher), 0, Direction::Right).caster_landing ==
                (Position{3, 4}));
    env.Step({Use(MovementAction::Right)});
    ASSERT_TRUE(dasher->GetPosition() == (Position{3, 4}));
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* dasher = Place(env, 0, {3, 1});
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    Agent* teleporter = Place(env, 1, {6, 3});  // Teleports up 3: (3,3)
    Require(env.SetCompanionSkill(teleporter->GetId(), 0, "teleport"), "teleport");
    env.Step({Use(MovementAction::Right), Use(MovementAction::Up)});
    ASSERT_TRUE(teleporter->GetPosition() == (Position{3, 3}));
    ASSERT_TRUE(dasher->GetPosition() == (Position{3, 2}));
    ASSERT_TRUE(env.GetLastSkillUses().at(0).target == (Position{3, 5}));
    ASSERT_TRUE(Has(env, teleporter, "electrified"));
  }
}

// Reviewer I1: a motion is never stopped by the end of a motion that is
// itself blocked. Dasher C (6,3) dashes up to (3,3) (a wall above), across
// (4,3), where a teleport lands first: C stops on (5,3). So (3,3), on A's
// line, stays free: A dashes right from (3,1) to (3,5).
TEST(TestADashIsNotBlockedByADashThatIsItselfBlocked) {
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 3, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({2, 3}, CellKind::Wall);
    Agent* a = Place(env, swapped ? 1 : 0, {3, 1});
    Agent* c = Place(env, swapped ? 0 : 1, {6, 3});
    Agent* t = Place(env, 2, {4, 6});  // Teleports left 3: (4,3)
    Require(env.SetCompanionSkill(a->GetId(), 0, "lightningStep"), "a");
    Require(env.SetCompanionSkill(c->GetId(), 0, "lightningStep"), "c");
    Require(env.SetCompanionSkill(t->GetId(), 0, "teleport"), "t");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(a->GetAgentIndex())] = Use(MovementAction::Right);
    actions[static_cast<size_t>(c->GetAgentIndex())] = Use(MovementAction::Up);
    actions[2] = Use(MovementAction::Left);
    env.Step(actions);
    ASSERT_TRUE(t->GetPosition() == (Position{4, 3}));
    ASSERT_TRUE(c->GetPosition() == (Position{5, 3}));
    ASSERT_TRUE(a->GetPosition() == (Position{3, 5}));
  }
}

// The reviewer's two dashers, under the layers: A (3,1) dashes right to
// (3,5), B (6,5) dashes up through (3,5) toward (2,5); a gob walks down onto
// A's line (3,3). The walks come after the dashes: A has passed and lands on
// (3,5), which holds B's line at the end of the dashes' layer: B stops before
// it, on (4,5). (Had the gob stood on A's line as the turn began, A's plan
// would stop before it and B would reach (2,5).)
TEST(TestTwoCrossingDashesAndALateWalker) {
  for (bool on_line : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* a = Place(env, 0, {3, 1});
    Require(env.SetCompanionSkill(a->GetId(), 0, "lightningStep"), "a");
    Agent* b = Place(env, 1, {6, 5});
    Require(env.SetCompanionSkill(b->GetId(), 0, "lightningStep"), "b");
    Agent* gob = AddEnemy(env, on_line ? Position{3, 3} : Position{2, 3});
    env.Step({Use(MovementAction::Right), Use(MovementAction::Up),
              on_line ? kStay : Walk(MovementAction::Down)});
    ASSERT_TRUE(gob->GetPosition() == (Position{3, 3}));
    ASSERT_TRUE(a->GetPosition() == (on_line ? Position{3, 2} : Position{3, 5}));
    ASSERT_TRUE(b->GetPosition() == (on_line ? Position{2, 5} : Position{4, 5}));
  }
}

// Reviewer I2, in the forced moves' layer: F1 (index lowest) is pushed 2
// right through (4,4) toward (4,5); F2 is pushed onto (4,5), F3 onto (4,4).
// F1 would win (4,5) from F2 (the lower index), but its path is held by F3's
// end: it stops before it (it stays), and F2 keeps (4,5) (it does not fall
// back for a winner that never comes). Whatever the indices of F2 and F3.
TEST(TestAPathBlockedMotionNeverTakesTheCellBeyond) {
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 3, 1, 0, 42);
    MakeArena(env);
    Place(env, 0, {4, 2});  // Pushes (4,3) 2 right
    GivePusher(env, 0, "gust2", 2);
    Place(env, 1, {2, 5});  // Pushes (3,5) down 1
    GivePusher(env, 1, "gust1", 1);
    Place(env, 2, {6, 4});  // Pushes (5,4) up 1
    Require(env.SetCompanionSkill(AgentAt(env, 2)->GetId(), 0, "gust1"), "gust1");
    Agent* f1 = AddEnemy(env, {4, 3});
    Agent* f2 = AddEnemy(env, swapped ? Position{5, 4} : Position{3, 5});
    Agent* f3 = AddEnemy(env, swapped ? Position{3, 5} : Position{5, 4});
    if (swapped) std::swap(f2, f3);
    env.Step({Use(MovementAction::Stay), Use(MovementAction::Stay), Use(MovementAction::Stay),
              kStay, kStay, kStay});
    ASSERT_TRUE(f1->GetPosition() == (Position{4, 3}));
    ASSERT_TRUE(f2->GetPosition() == (Position{4, 5}));
    ASSERT_TRUE(f3->GetPosition() == (Position{4, 4}));
  }
}

// A body down as the turn begins is a static blocker, never moved: it blocks
// a walk onto it, a push through it, a dash through it (planned: it stops
// before it) and a teleport onto it (planned: it falls back)
TEST(TestADownedBodyBlocksEveryMotion) {
  SynchroEnv env(10, 10, 5, 1, 0, 42);
  MakeArena(env);
  Agent* body = Place(env, 0, {4, 5});
  Agent* walker = Place(env, 1, {3, 5});      // Walks down onto the body
  Place(env, 2, {4, 3});                      // Pushes the gob on (4,4) 2 right, through it
  GivePusher(env, 2, "gust2", 2);
  Agent* dasher = Place(env, 3, {6, 5});      // Dashes up through it
  Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
  Agent* teleporter = Place(env, 4, {4, 8});  // Teleports 3 left, onto it
  Require(env.SetCompanionSkill(teleporter->GetId(), 0, "teleport"), "teleport");
  Agent* gob = AddEnemy(env, {4, 4});
  DownCompanion(env, 0);
  std::vector<Action> actions = Stays(env);
  actions[1] = Walk(MovementAction::Down);
  actions[2] = Use(MovementAction::Right);
  actions[3] = Use(MovementAction::Up);
  actions[4] = Use(MovementAction::Left);
  env.Step(actions);
  ASSERT_TRUE(body->GetPosition() == (Position{4, 5}));
  ASSERT_TRUE(AsCompanion(body)->IsDowned());
  ASSERT_TRUE(walker->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(gob->GetPosition() == (Position{4, 4}));
  ASSERT_TRUE(dasher->GetPosition() == (Position{5, 5}));
  ASSERT_TRUE(teleporter->GetPosition() == (Position{4, 6}));
}

// Zones land once per turn, on each agent's final cell, after the motion
// phase: a gob pushed 3 cells across wet, wet, then oil gets oil only, once;
// a dash crossing a wet cell gets nothing from it (its landing has no zone);
// a teleport landing on oil gets it once
TEST(TestZonesLandOnceOnTheFinalCell) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {4, 2});
  GivePusher(env, 0, "gust3", 3);
  Agent* gob = AddEnemy(env, {4, 3});
  Require(env.SetCellTag({4, 4}, "wet"), "wet");
  Require(env.SetCellTag({4, 5}, "wet"), "wet");
  Require(env.SetCellTag({4, 6}, "oil"), "oil");
  Agent* dasher = Place(env, 1, {6, 1});  // Dashes right to (6,5) across wet (6,3)
  Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
  Require(env.SetCellTag({6, 3}, "wet"), "wet");
  Agent* teleporter = Place(env, 2, {2, 1});  // Teleports right onto oil (2,4)
  Require(env.SetCompanionSkill(teleporter->GetId(), 0, "teleport"), "teleport");
  Require(env.SetCellTag({2, 4}, "oil"), "oil");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Right), Use(MovementAction::Right),
            kStay});
  ASSERT_TRUE(gob->GetPosition() == (Position{4, 6}));
  ASSERT_TRUE(dasher->GetPosition() == (Position{6, 5}));
  ASSERT_TRUE(teleporter->GetPosition() == (Position{2, 4}));
  ASSERT_TRUE(ZonesLanded(env, gob) == (std::vector<std::string>{"oil"}));
  ASSERT_TRUE(ZonesLanded(env, dasher).empty());
  ASSERT_TRUE(ZonesLanded(env, teleporter) == (std::vector<std::string>{"oil"}));
  ASSERT_FALSE(Has(env, gob, "wet"));
  ASSERT_FALSE(Has(env, dasher, "wet"));
}

// Every agent affectable as the turn began gets its final cell's zone once,
// moved or not: one standing on wet; a gob whose walk off the oil is blocked
// (a thing stays where it walks: a walk into any living actor that stays is
// blocked)
TEST(TestAnAgentThatDidNotMoveStillLandsItsZone) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* stander = Place(env, 0, {4, 4});
  Require(env.SetCellTag({4, 4}, "wet"), "wet");
  Agent* gob = AddEnemy(env, {5, 4});  // Walks down into a boulder
  AddBoulder(env, {6, 4});
  Require(env.SetCellTag({5, 4}, "oil"), "oil");
  env.Step({kStay, Walk(MovementAction::Down)});
  ASSERT_TRUE(gob->GetPosition() == (Position{5, 4}));
  ASSERT_TRUE(ZonesLanded(env, stander) == (std::vector<std::string>{"wet"}));
  ASSERT_TRUE(ZonesLanded(env, gob) == (std::vector<std::string>{"oil"}));
}

// Walk vs walk keeps its rules (the walks' layer), whatever the agents'
// kinds (a companion and a gob walking onto one cell): two walks onto one
// cell both stay,
// a swap cancels, a walk into a cell succeeds only if its occupant leaves (a
// chain moves on, a ring of 4 rotates), a walk into a wall or into one that
// stays does not.
TEST(TestWalkCollisionsAreUnchanged) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* c0 = Place(env, 0, {1, 1});  // Right onto (1,2), as g0 walks left onto it
  Agent* c1 = Place(env, 1, {3, 1});  // Right, swapping with c2
  Agent* c2 = Place(env, 2, {3, 2});  // Left
  Agent* g0 = AddEnemy(env, {1, 3});
  Agent* g1 = AddEnemy(env, {5, 1});  // A chain right
  Agent* g2 = AddEnemy(env, {5, 2});
  Agent* g3 = AddEnemy(env, {5, 3});
  Agent* r0 = AddEnemy(env, {7, 5});  // A ring: right, down, left, up
  Agent* r1 = AddEnemy(env, {7, 6});
  Agent* r2 = AddEnemy(env, {8, 6});
  Agent* r3 = AddEnemy(env, {8, 5});
  Agent* w = AddEnemy(env, {1, 7});   // Up into the border wall
  Agent* s = AddEnemy(env, {3, 7});   // Down onto one that stays
  AddEnemy(env, {4, 7});
  const Action r = Walk(MovementAction::Right), l = Walk(MovementAction::Left);
  const Action u = Walk(MovementAction::Up), d = Walk(MovementAction::Down);
  env.Step({r, r, l, l, r, r, r, r, d, l, u, u, d, kStay});
  ASSERT_TRUE(c0->GetPosition() == (Position{1, 1}));
  ASSERT_TRUE(g0->GetPosition() == (Position{1, 3}));
  ASSERT_TRUE(c1->GetPosition() == (Position{3, 1}));
  ASSERT_TRUE(c2->GetPosition() == (Position{3, 2}));
  ASSERT_TRUE(g1->GetPosition() == (Position{5, 2}));
  ASSERT_TRUE(g2->GetPosition() == (Position{5, 3}));
  ASSERT_TRUE(g3->GetPosition() == (Position{5, 4}));
  ASSERT_TRUE(r0->GetPosition() == (Position{7, 6}));
  ASSERT_TRUE(r1->GetPosition() == (Position{8, 6}));
  ASSERT_TRUE(r2->GetPosition() == (Position{8, 5}));
  ASSERT_TRUE(r3->GetPosition() == (Position{7, 5}));
  ASSERT_TRUE(w->GetPosition() == (Position{1, 7}));
  ASSERT_TRUE(s->GetPosition() == (Position{3, 7}));
  ASSERT_TRUE(c0->GetExecutedAction().movement == MovementAction::Stay);  // Blocked
  ASSERT_TRUE(g1->GetExecutedAction().movement == MovementAction::Right);
}

// Motions that depend on each other in a cycle: four gobs on a square pushed
// round it (right, down, left, up: gusts around (3,2), (2,4), (4,5), (5,3)),
// each onto the cell another leaves in the same layer: they all move. Without
// the push on the last one, it stays, so the one pushed onto its cell stays,
// and so on round the square: everyone stays. Choices only advance: the
// solver ends.
TEST(TestTheMotionSolverTerminatesOnACycle) {
  for (bool whole : {true, false}) {
    SynchroEnv env(10, 10, 4, 1, 0, 42);
    MakeArena(env);
    const Position casters[] = {{3, 2}, {2, 4}, {4, 5}, {5, 3}};
    for (int i = 0; i < 4; ++i) {
      Place(env, i, casters[i]);
      GiveGust(env, i);
    }
    const Position square[] = {{3, 3}, {3, 4}, {4, 4}, {4, 3}};
    Agent* gobs[4];
    for (int i = 0; i < 4; ++i) gobs[i] = AddEnemy(env, square[i]);
    std::vector<Action> actions = Stays(env);
    for (int i = 0; i < (whole ? 4 : 3); ++i) actions[static_cast<size_t>(i)] = Use(MovementAction::Stay);
    env.Step(actions);
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(gobs[i]->GetPosition() == (whole ? square[(i + 1) % 4] : square[i]));
    }
  }
}

// The motion phase's outcome depends on the agents' indices only through a
// tie (two motions of one layer onto one cell): a companion pushed 2 right
// through the cell a companion walks onto stops before it (the walks come
// first: it stays); a dash along the line a gob then walks onto passes (the
// dashes come first); two dashes crossing, one ending on the other's line
// (that one stops before it); the same whatever the companions' indices.
TEST(TestSwappingIndicesChangesNoMotionOutcome) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 5, 1, 0, 42);
    MakeArena(env);
    const int g = swapped ? 4 : 0, p = swapped ? 3 : 1, w = 2, d = swapped ? 1 : 3,
              e = swapped ? 0 : 4;
    Agent* gust = Place(env, g, {4, 2});
    GivePusher(env, g, "gust2", 2);
    Agent* pushed = Place(env, p, {4, 3});
    Agent* walker = Place(env, w, {3, 4});
    Agent* dasher = Place(env, d, {6, 1});  // Right 4: (6,5)
    Require(env.SetCompanionSkill(dasher->GetId(), 0, "lightningStep"), "dash");
    Agent* crosser = Place(env, e, {8, 5});  // Up 4 toward (4,5), across (6,5)
    Require(env.SetCompanionSkill(crosser->GetId(), 0, "lightningStep"), "dash");
    Agent* gob = AddEnemy(env, {5, 3});
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(g)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(w)] = Walk(MovementAction::Down);
    actions[static_cast<size_t>(d)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(e)] = Use(MovementAction::Up);
    actions[5] = Walk(MovementAction::Down);  // The gob
    env.Step(actions);
    ASSERT_TRUE(pushed->GetPosition() == (Position{4, 3}));
    ASSERT_TRUE(walker->GetPosition() == (Position{4, 4}));
    ASSERT_TRUE(dasher->GetPosition() == (Position{6, 5}));
    ASSERT_TRUE(crosser->GetPosition() == (Position{7, 5}));  // Before D's landing
    ASSERT_TRUE(gob->GetPosition() == (Position{6, 3}));
    traces[swapped] = RoleTrace(env, {{gust->GetId(), "G"},
                                      {pushed->GetId(), "P"},
                                      {walker->GetId(), "W"},
                                      {dasher->GetId(), "D"},
                                      {crosser->GetId(), "E"},
                                      {gob->GetId(), "X"}});
  }
  if (traces[0] != traces[1]) {
    throw std::runtime_error("the order matters:\n" + traces[0] + "--- swapped ---\n" + traces[1]);
  }
}

// =============================================================================
// One tag phase: every tag of the turn lands together, reactions resolve
// together
// =============================================================================

// Throws unless the two traces are the same
static void RequireSameTraces(const std::string traces[2]) {
  if (traces[0] != traces[1]) {
    throw std::runtime_error("the order matters:\n" + traces[0] + "--- swapped ---\n" + traces[1]);
  }
}

// A ground skill landing `tag` on the cell `range` ahead (no motion), put in
// the companion's slot 0 as `name`
static void GiveStorm(SynchroEnv& env, int companion, const char* name, const char* tag,
                      int range) {
  SkillConfig s;
  s.name = name;
  s.targeting = SkillTargeting::Ground;
  s.range = range;
  s.tags = {{tag, kPermanentTag}};
  env.GetMutableSkillBook().Define(s);
  Require(env.SetCompanionSkill(AgentAt(env, companion)->GetId(), 0, name), name);
}

// A's douse (wet) and B's spark (electrified) land on the gob in one turn:
// every tag lands, then the reactions resolve, so the combo fires once, in
// both index orders. Its credit is the first matching skill landing in
// report order (the lower caster index's: report-only), the outcome the same.
TEST(TestBothHalvesOfAComboLandingTheSameTurnReactOnce) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int d = swapped ? 1 : 0, s = 1 - d;
    Require(env.SetReactions({Rule("wet", "electrified", "shocked", 1)}), "reactions");
    GiveBolt(env, d, "douse", "wet");
    GiveBolt(env, s, "spark", "electrified");
    Agent* douse = Place(env, d, {4, 1});  // Right: (4,2), (4,3), the gob
    Agent* spark = Place(env, s, {1, 4});  // Down: (2,4), (3,4), the gob
    Agent* gob = AddEnemy(env, {4, 4});
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(d)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(s)] = Use(MovementAction::Down);
    env.Step(actions);
    ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
    const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
    ASSERT_EQ(r.trigger, gob->GetId());
    const Agent* first = AgentAt(env, 0);  // The first skill landing in report order
    ASSERT_EQ(r.source, first->GetId());
    ASSERT_EQ(r.tag, Id(env, first == douse ? "wet" : "electrified"));
    ASSERT_TRUE(r.kind == BaseEnv::TagSource::Skill);
    ASSERT_TRUE(Has(env, gob, "shocked"));
    ASSERT_FALSE(Has(env, gob, "wet"));
    ASSERT_FALSE(Has(env, gob, "electrified"));
    ASSERT_EQ(gob->GetHealth(), 4);
    const auto& landed = env.GetLastTagsApplied();
    ASSERT_EQ(landed.size(), static_cast<size_t>(3));  // Both halves, then the result
    ASSERT_TRUE(landed.at(2).kind == BaseEnv::TagSource::Reaction);
    traces[swapped] =
        RoleTrace(env, {{douse->GetId(), "D"}, {spark->GetId(), "S"}, {gob->GetId(), "G"}},
                  false);
  }
  RequireSameTraces(traces);
}

// The gob carries wet; a spark (electrified) and a frost bolt (chilled) land
// on it in one turn. Both would pair with its wet: the first matching rule in
// level order fires (wet + electrified), and takes the wet the second needed.
// Casters used to resolve one by one: the rule depended on who came first.
TEST(TestTheOrderOfTwoCastersChangesNoReaction) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int s = swapped ? 1 : 0, f = 1 - s;
    Require(env.SetReactions({Rule("wet", "electrified", "shocked", 1),
                              Rule("wet", "chilled", "stunned", 1)}),
            "reactions");
    GiveBolt(env, s, "spark", "electrified");
    GiveBolt(env, f, "frost", "chilled");
    Agent* spark = Place(env, s, {4, 1});  // Right: the gob
    Agent* frost = Place(env, f, {1, 4});  // Down: the gob
    Agent* gob = AddEnemy(env, {4, 4});
    Require(env.ApplyTagTo(gob->GetId(), "wet", kPermanentTag), "wet");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(s)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(f)] = Use(MovementAction::Down);
    env.Step(actions);
    ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastReactions().at(0).rule, 0);
    ASSERT_EQ(env.GetLastReactions().at(0).source, spark->GetId());
    ASSERT_TRUE(Has(env, gob, "shocked"));
    ASSERT_FALSE(Has(env, gob, "stunned"));
    ASSERT_TRUE(Has(env, gob, "chilled"));  // Landed, nothing left to pair with
    ASSERT_FALSE(Has(env, gob, "wet"));
    ASSERT_FALSE(Has(env, gob, "electrified"));
    ASSERT_EQ(gob->GetHealth(), 4);
    traces[swapped] =
        RoleTrace(env, {{spark->GetId(), "S"}, {frost->GetId(), "F"}, {gob->GetId(), "G"}});
  }
  RequireSameTraces(traces);
}

// The gob stands in a lake that was just set (it carries no wet yet): the
// lake lands wet and a spark lands electrified in the same turn, and they
// react (spreading over the lake: the imp in it too, once). The credit is
// the first matching landing that is not a zone's (the zone is the stage,
// the skill the actor), though the lake's comes first in report order.
TEST(TestAZoneAndASkillLandingTogetherReact) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked", 1, true)}), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  Agent* spark = Place(env, 0, {4, 1});  // Right: the gob
  Agent* gob = AddEnemy(env, {4, 4});
  Agent* imp = AddEnemy(env, {4, 5});
  for (Position p : {Position{4, 4}, Position{4, 5}}) Require(env.SetCellTag(p, "wet"), "lake");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_EQ(r.tag, Id(env, "electrified"));
  ASSERT_TRUE(r.kind == BaseEnv::TagSource::Skill);
  ASSERT_EQ(r.source, spark->GetId());
  ASSERT_EQ(r.cause, std::string("spark"));
  ASSERT_TRUE(r.spread);
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(2));
  for (Agent* a : {gob, imp}) {
    ASSERT_TRUE(Has(env, a, "shocked"));
    ASSERT_FALSE(Has(env, a, "wet"));
    ASSERT_EQ(a->GetHealth(), 4);
  }
  const auto& landed = env.GetLastTagsApplied();  // Zones, the spark, the results
  ASSERT_EQ(landed.size(), static_cast<size_t>(5));
  ASSERT_TRUE(landed.at(0).kind == BaseEnv::TagSource::Zone);
  ASSERT_TRUE(landed.at(1).kind == BaseEnv::TagSource::Zone);
  ASSERT_TRUE(landed.at(2).kind == BaseEnv::TagSource::Skill);
  ASSERT_TRUE(landed.at(3).kind == BaseEnv::TagSource::Reaction);
  ASSERT_TRUE(landed.at(4).kind == BaseEnv::TagSource::Reaction);
}

// A weakness reads the zone under the agent's FINAL cell (the map as the turn
// began): the imp, weak to (wet, electrified), is pushed from dry land into
// the lake as a storm electrifies the lake cell: defeated (the storm's
// landing credited)
TEST(TestAWeaknessReadsTheZoneUnderTheFinalCell) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  GiveStorm(env, 0, "storm", "electrified", 2);
  GiveGust(env, 1);
  Agent* storm = Place(env, 0, {3, 1});  // Storms (3,3)
  Place(env, 1, {3, 5});                 // Gusts: the imp on its left ring cell, to (3,3)
  Agent* imp = AddEnemy(env, {3, 4}, 5);
  Require(env.SetCellTag({3, 3}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "electrified"}}), "weak_to");
  std::vector<Action> actions = Stays(env);
  actions[0] = Use(MovementAction::Right);
  actions[1] = Use(MovementAction::Stay);
  env.Step(actions);
  ASSERT_TRUE(imp->GetPosition() == (Position{3, 3}));
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  const BaseEnv::DefeatReport& d = env.GetLastDefeats().at(0);
  ASSERT_EQ(d.agent, imp->GetId());
  ASSERT_EQ(d.zone, Id(env, "wet"));
  ASSERT_EQ(d.tag, Id(env, "electrified"));
  ASSERT_TRUE(d.kind == BaseEnv::TagSource::Skill);
  ASSERT_EQ(d.source, storm->GetId());
  ASSERT_TRUE(Has(env, imp, "wet"));  // The lake landed on its final cell
  ASSERT_FALSE(imp->IsAlive());
}

// Two fireballs on the oiled gob in one turn: one firing per (agent, rule)
// per turn, so the oil is ignited once (each on the oil takes 1, not 2)
TEST(TestTwoCastersIgniteTheOilOnce) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ReactionRule ignite = Rule("oil", "burning", "ablaze", 1, true);
  ignite.keep = {"oil"};
  ignite.zone_becomes = "fire";
  Require(env.SetReactions({ignite}), "reactions");
  GiveBolt(env, 0, "fireball", "burning");
  GiveBolt(env, 1, "fireball", "burning");
  Place(env, 0, {2, 1});  // Right: (2,2), (2,3), the gob
  Place(env, 1, {5, 4});  // Up: (4,4), (3,4), the gob
  Agent* gob = AddEnemy(env, {2, 4});
  Agent* cook = AddEnemy(env, {2, 6});
  const std::vector<Position> oil = {{2, 4}, {2, 5}, {2, 6}};
  for (Position p : oil) Require(env.SetCellTag(p, "oil"), "oil");
  std::vector<Action> actions = Stays(env);
  actions[0] = Use(MovementAction::Right);
  actions[1] = Use(MovementAction::Up);
  env.Step(actions);
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  const BaseEnv::ReactionReport& r = env.GetLastReactions().at(0);
  ASSERT_EQ(r.trigger, gob->GetId());
  ASSERT_EQ(r.affected.size(), static_cast<size_t>(2));
  ASSERT_EQ(r.cells.size(), oil.size());
  ASSERT_EQ(gob->GetHealth(), 4);
  ASSERT_EQ(cook->GetHealth(), 4);
  for (Position p : oil) ASSERT_EQ(env.GetCellTag(p).tag, Id(env, "fire"));
}

// The reports of the tag phase: every zone landing (agent-index order), then
// every skill landing (caster index order, each use's hits in its order),
// then the results. The spark's landing on the wet gob no longer fires before
// the douse lands on the imp.
TEST(TestTheTagPhaseReportsZonesThenSkillsByCasterIndex) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Require(env.SetReactions({Rule("wet", "electrified", "shocked")}), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  GiveBolt(env, 1, "douse", "wet");
  Agent* spark = Place(env, 0, {4, 1});  // Right: the gob
  Agent* douse = Place(env, 1, {1, 4});  // Down: the imp first
  Agent* gob = AddEnemy(env, {4, 4});
  Agent* imp = AddEnemy(env, {3, 4});
  Require(env.ApplyTagTo(gob->GetId(), "wet", kPermanentTag), "wet");
  for (Position p : {Position{4, 1}, Position{1, 4}}) Require(env.SetCellTag(p, "mud"), "mud");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Down), kStay, kStay});
  const auto& landed = env.GetLastTagsApplied();
  ASSERT_EQ(landed.size(), static_cast<size_t>(5));
  const std::vector<std::pair<ObjectId, BaseEnv::TagSource>> expected = {
      {spark->GetId(), BaseEnv::TagSource::Zone},
      {douse->GetId(), BaseEnv::TagSource::Zone},
      {gob->GetId(), BaseEnv::TagSource::Skill},
      {imp->GetId(), BaseEnv::TagSource::Skill},
      {gob->GetId(), BaseEnv::TagSource::Reaction}};
  for (size_t i = 0; i < expected.size(); ++i) {
    ASSERT_EQ(landed.at(i).agent, expected[i].first);
    ASSERT_TRUE(landed.at(i).kind == expected[i].second);
  }
  ASSERT_EQ(landed.at(2).source, spark->GetId());
  ASSERT_EQ(landed.at(3).source, douse->GetId());
  ASSERT_EQ(landed.at(4).reaction, 0);
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastReactions().at(0).source, spark->GetId());
}

// Two firings on one agent in one turn (a + b, c + d: independent pairs, each
// brought by a caster): their results land together, then ONE weakness check
// over them: X, weak to (mud, rc) then (mud, ra), is defeated by rc (its
// first weakness), whatever the casters' order. Only the rc firing reports it
// defeated.
TEST(TestSkillReactionResultsGoThroughOneWeaknessCheck) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int b = swapped ? 1 : 0, d = 1 - b;
    Require(env.SetReactions({Rule("pa", "pb", "ra"), Rule("pc", "pd", "rc")}), "reactions");
    GiveBolt(env, b, "bee", "pb");
    GiveBolt(env, d, "dee", "pd");
    Agent* bee = Place(env, b, {4, 1});  // Right: X
    Agent* dee = Place(env, d, {1, 4});  // Down: X
    Agent* x = AddEnemy(env, {4, 4});
    Require(env.SetCellTag({4, 4}, "mud"), "mud");
    for (const char* t : {"pa", "pc"}) Require(env.ApplyTagTo(x->GetId(), t, kPermanentTag), t);
    Require(env.SetWeaknesses(x->GetId(), {{"mud", "rc"}, {"mud", "ra"}}), "weak_to");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(b)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(d)] = Use(MovementAction::Down);
    env.Step(actions);
    const auto& fired = env.GetLastReactions();
    ASSERT_EQ(fired.size(), static_cast<size_t>(2));
    ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
    const BaseEnv::DefeatReport& defeat = env.GetLastDefeats().at(0);
    ASSERT_EQ(defeat.agent, x->GetId());
    ASSERT_EQ(defeat.tag, Id(env, "rc"));
    ASSERT_TRUE(defeat.kind == BaseEnv::TagSource::Reaction);
    ASSERT_EQ(fired.at(static_cast<size_t>(defeat.reaction)).rule, 1);
    for (const auto& r : fired) {
      ASSERT_EQ(r.affected.size(), static_cast<size_t>(1));
      ASSERT_TRUE(r.affected.at(0).result_landed);
      ASSERT_EQ(r.affected.at(0).defeated, r.rule == 1);
    }
    ASSERT_TRUE(Has(env, x, "ra"));
    ASSERT_TRUE(Has(env, x, "rc"));
    ASSERT_FALSE(x->IsAlive());
    traces[swapped] =
        RoleTrace(env, {{bee->GetId(), "B"}, {dee->GetId(), "D"}, {x->GetId(), "X"}});
  }
  RequireSameTraces(traces);
}

// A status landed this turn acts from the next turn: the snared gob (Rooted,
// 1 step) and the caster, stunned this turn by its ally's frost bolt
// (chilled: Stunned, 1 step), still acts this turn (its use was planned as
// the turn began); next turn the gob cannot walk and the caster stays; the
// turn after, both walk.
TEST(TestASkillsRootActsFromTheNextTurn) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig snare;
  snare.name = "snare";
  snare.targeting = SkillTargeting::Ground;
  snare.range = 2;
  snare.root_steps = 1;
  env.GetMutableSkillBook().Define(snare);
  Agent* caster = Place(env, 0, {3, 1});  // Snares (3,3)
  Require(env.SetCompanionSkill(caster->GetId(), 0, "snare"), "snare");
  GiveBolt(env, 1, "frost", "chilled");
  Place(env, 1, {6, 1});  // Up: (5,1), (4,1), the caster
  Require(env.SetTagStatuses({{"chilled", StatusType::Stunned, 1}}), "tag statuses");
  Agent* gob = AddEnemy(env, {3, 3});
  env.Step({Use(MovementAction::Right), Use(MovementAction::Up), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));  // The caster's too
  ASSERT_TRUE(gob->IsRooted());
  ASSERT_TRUE(caster->IsStunned());
  env.Step({Walk(MovementAction::Down), kStay, Walk(MovementAction::Right)});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));
  ASSERT_TRUE(gob->GetPosition() == (Position{3, 3}));
  env.Step({Walk(MovementAction::Down), kStay, Walk(MovementAction::Right)});
  ASSERT_TRUE(caster->GetPosition() == (Position{4, 1}));
  ASSERT_TRUE(gob->GetPosition() == (Position{3, 4}));
}

// Two sparks on the wet gob, the rule keeping the wet: one firing per (agent,
// rule) per turn, whichever landings brought the halves (1 damage, not 2)
TEST(TestOneFiringPerAgentPerRulePerTurn) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ReactionRule shock = Rule("wet", "electrified", "shocked", 1);
  shock.keep = {"wet"};
  Require(env.SetReactions({shock}), "reactions");
  GiveBolt(env, 0, "spark", "electrified");
  GiveBolt(env, 1, "spark", "electrified");
  Place(env, 0, {4, 1});  // Right: the gob
  Place(env, 1, {1, 4});  // Down: the gob
  Agent* gob = AddEnemy(env, {4, 4});
  Require(env.ApplyTagTo(gob->GetId(), "wet", kPermanentTag), "wet");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Down), kStay});
  ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(1));
  ASSERT_EQ(TurnOf(env, gob).damage, 1);
  ASSERT_EQ(gob->GetHealth(), 4);
  ASSERT_TRUE(Has(env, gob, "shocked"));
  ASSERT_TRUE(Has(env, gob, "wet"));
  ASSERT_FALSE(Has(env, gob, "electrified"));
}

// Two rules writing one oil region in one turn: F's fireball on gob X
// (oil + burning -> fire, rule 0) and C's frost bolt on gob Y (oil + chilled
// -> ice, rule 1). The rule first in level order wins the cells, in both
// caster orders (the firings' order is the reports' only).
TEST(TestTheFirstRuleWinsACellTwoRulesWrite) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int f = swapped ? 1 : 0, c = 1 - f;
    ReactionRule fire = Rule("oil", "burning", "ablaze", 0, true);
    fire.zone_becomes = "fire";
    ReactionRule ice = Rule("oil", "chilled", "frozen", 0, true);
    ice.zone_becomes = "ice";
    Require(env.SetReactions({fire, ice}), "reactions");
    GiveBolt(env, f, "fireball", "burning");
    GiveBolt(env, c, "frost", "chilled");
    Agent* fb = Place(env, f, {2, 1});  // Right: (2,2), (2,3), gob X
    Agent* fr = Place(env, c, {5, 5});  // Up: (4,5), (3,5), gob Y
    Agent* x = AddEnemy(env, {2, 4});
    Agent* y = AddEnemy(env, {3, 5});
    const std::vector<Position> oil = {{2, 4}, {2, 5}, {3, 5}};
    for (Position p : oil) Require(env.SetCellTag(p, "oil"), "oil");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(f)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(c)] = Use(MovementAction::Up);
    env.Step(actions);
    ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(2));
    for (Position p : oil) ASSERT_EQ(env.GetCellTag(p).tag, Id(env, "fire"));
    traces[swapped] = RoleTrace(env, {{fb->GetId(), "F"}, {fr->GetId(), "C"}, {x->GetId(), "X"},
                                      {y->GetId(), "Y"}});
    for (Position p : oil) traces[swapped] += TagName(env, env.GetCellTag(p).tag) + "\n";
  }
  RequireSameTraces(traces);
}

// "Taken by an earlier firing" is per trigger: A's spreading shock (wet +
// electrified, over the lake) takes B's wet, but B's own firing (wet +
// chilled, brought by its frost) still fires: every firing is found, then
// applied. The same in both caster orders.
TEST(TestAnotherAgentsSpreadDoesNotStopAFiring) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int s = swapped ? 1 : 0, f = 1 - s;
    Require(env.SetReactions({Rule("wet", "electrified", "shocked", 0, true),
                              Rule("wet", "chilled", "stunned")}),
            "reactions");
    GiveBolt(env, s, "spark", "electrified");
    GiveBolt(env, f, "frost", "chilled");
    Agent* spark = Place(env, s, {4, 1});  // Right: A
    Agent* frost = Place(env, f, {1, 5});  // Down: B
    Agent* a = AddEnemy(env, {4, 4});
    Agent* b = AddEnemy(env, {4, 5});
    for (Position p : {Position{4, 4}, Position{4, 5}}) Require(env.SetCellTag(p, "wet"), "lake");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(s)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(f)] = Use(MovementAction::Down);
    env.Step(actions);
    ASSERT_EQ(env.GetLastReactions().size(), static_cast<size_t>(2));
    ASSERT_TRUE(Has(env, a, "shocked"));
    ASSERT_TRUE(Has(env, b, "shocked"));  // A's spread
    ASSERT_TRUE(Has(env, b, "stunned"));  // B's own
    ASSERT_FALSE(Has(env, b, "wet"));
    traces[swapped] = RoleTrace(env, {{spark->GetId(), "S"}, {frost->GetId(), "F"},
                                      {a->GetId(), "A"}, {b->GetId(), "B"}});
  }
  RequireSameTraces(traces);
}

// A weakness defeat is credited like a reaction: the first landing of S that
// is not a zone's. The imp in the lake, weak to (wet, wet), gets the lake's
// wet (reported first) and the douse's: the douse defeats it.
TEST(TestAWeaknessDefeatCreditsTheSkillOverTheZone) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  GiveBolt(env, 0, "douse", "wet");
  Agent* douse = Place(env, 0, {4, 1});  // Right: the imp
  Agent* imp = AddEnemy(env, {4, 4});
  Require(env.SetCellTag({4, 4}, "wet"), "lake");
  Require(env.SetWeaknesses(imp->GetId(), {{"wet", "wet"}}), "weak_to");
  env.Step(With(env, 0, Use(MovementAction::Right)));
  ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
  const BaseEnv::DefeatReport& d = env.GetLastDefeats().at(0);
  ASSERT_EQ(d.agent, imp->GetId());
  ASSERT_TRUE(d.kind == BaseEnv::TagSource::Skill);
  ASSERT_EQ(d.source, douse->GetId());
  ASSERT_EQ(d.cause, std::string("douse"));
  ASSERT_FALSE(imp->IsAlive());
}

// One weakness check per agent, by the agent's own weakness order among the
// tags that landed this turn: X on mud, weak to (mud, s2) then (mud, s1), gets
// s1 and s2 from two casters: defeated by s2 in both caster orders (landing
// by landing, the first caster's tag would decide)
TEST(TestAWeaknessIsChosenByTheAgentsOwnOrder) {
  std::string traces[2];
  for (bool swapped : {false, true}) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    const int one = swapped ? 1 : 0, two = 1 - one;
    GiveBolt(env, one, "first", "s1");
    GiveBolt(env, two, "second", "s2");
    Agent* c1 = Place(env, one, {4, 1});  // Right: X
    Agent* c2 = Place(env, two, {1, 4});  // Down: X
    Agent* x = AddEnemy(env, {4, 4});
    Require(env.SetCellTag({4, 4}, "mud"), "mud");
    Require(env.SetWeaknesses(x->GetId(), {{"mud", "s2"}, {"mud", "s1"}}), "weak_to");
    std::vector<Action> actions = Stays(env);
    actions[static_cast<size_t>(one)] = Use(MovementAction::Right);
    actions[static_cast<size_t>(two)] = Use(MovementAction::Down);
    env.Step(actions);
    ASSERT_EQ(env.GetLastDefeats().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastDefeats().at(0).tag, Id(env, "s2"));
    ASSERT_EQ(env.GetLastDefeats().at(0).source, c2->GetId());
    ASSERT_FALSE(x->IsAlive());
    traces[swapped] = RoleTrace(env, {{c1->GetId(), "1"}, {c2->GetId(), "2"}, {x->GetId(), "X"}});
  }
  RequireSameTraces(traces);
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
