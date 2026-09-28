// Copyright 2024
// Unit tests for skills: opaque agent tags, the per-env TagTable, the SkillBook
// and the line and landing rules of skill motion

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/grid.h"
#include "../src/core/object.h"
#include "../src/core/skill_config.h"
#include "../src/core/tag_table.h"
#include "../src/env/skill_motion.h"
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
// TagTable Tests
// =============================================================================

TEST(TestTagTableInterns) {
  TagTable t;
  TagId burning = t.Intern("burning");
  ASSERT_EQ(t.Intern("burning"), burning);
  TagId electrified = t.Intern("electrified");
  ASSERT_TRUE(electrified != burning);
  ASSERT_EQ(t.Find("electrified"), electrified);
  ASSERT_EQ(t.Find("wet"), kInvalidTag);
  ASSERT_EQ(t.Name(burning), std::string("burning"));
  ASSERT_EQ(t.Name(99), std::string(""));
  ASSERT_EQ(t.Size(), 2);
}

// =============================================================================
// Agent Tag Tests
// =============================================================================

TEST(TestAgentTagDurations) {
  Agent a(0, {1, 1});
  a.ApplyTag(0, 2);
  ASSERT_TRUE(a.HasTag(0));
  a.TickTags();                 // 2 -> 1
  ASSERT_TRUE(a.HasTag(0));
  a.TickTags();                 // 1 -> 0: gone
  ASSERT_FALSE(a.HasTag(0));
}

TEST(TestAgentTagRefreshKeepsLonger) {
  Agent a(0, {1, 1});
  a.ApplyTag(0, 3);
  a.ApplyTag(0, 1);             // shorter: ignored
  ASSERT_EQ(a.GetTags()[0].duration, 3);
  a.ApplyTag(0, kPermanentTag); // permanent wins
  ASSERT_EQ(a.GetTags()[0].duration, kPermanentTag);
  a.ApplyTag(0, 5);
  ASSERT_EQ(a.GetTags()[0].duration, kPermanentTag);
  for (int i = 0; i < 10; ++i) a.TickTags();
  ASSERT_TRUE(a.HasTag(0));
  a.RemoveTag(0);
  ASSERT_FALSE(a.HasTag(0));
  a.ApplyTag(1, 0);             // zero duration: no-op
  ASSERT_FALSE(a.HasTag(1));
}

// =============================================================================
// SkillBook Tests
// =============================================================================

TEST(TestSkillBookBuiltins) {
  SkillBook book;
  const SkillConfig* fireball = book.Find("fireball");
  ASSERT_TRUE(fireball != nullptr);
  ASSERT_TRUE(fireball->targeting == SkillTargeting::Ground);
  ASSERT_EQ(fireball->range, 3);
  ASSERT_TRUE(fireball->area == SkillArea::Cross);
  ASSERT_TRUE(fireball->motion == SkillMotion::PushOut);
  ASSERT_EQ(fireball->motion_distance, 1);
  ASSERT_EQ(fireball->tags.size(), static_cast<size_t>(1));
  ASSERT_EQ(fireball->tags[0].tag, std::string("burning"));

  const SkillConfig* step = book.Find("lightningStep");
  ASSERT_TRUE(step != nullptr);
  ASSERT_TRUE(step->targeting == SkillTargeting::Self);
  ASSERT_TRUE(step->motion == SkillMotion::Dash);
  ASSERT_EQ(step->motion_distance, 4);
  ASSERT_TRUE(step->area == SkillArea::Cross);
  ASSERT_TRUE(step->tag_path);
  ASSERT_EQ(step->tags.size(), static_cast<size_t>(1));
  ASSERT_EQ(step->tags[0].tag, std::string("electrified"));

  const SkillConfig* tp = book.Find("teleport");
  ASSERT_TRUE(tp != nullptr && tp->motion == SkillMotion::Teleport);
  ASSERT_EQ(tp->motion_distance, 3);
  ASSERT_TRUE(tp->tags.empty());

  const SkillConfig* vortex = book.Find("vortex");
  ASSERT_TRUE(vortex != nullptr);
  ASSERT_TRUE(vortex->targeting == SkillTargeting::Ground);
  ASSERT_EQ(vortex->range, 3);
  ASSERT_TRUE(vortex->motion == SkillMotion::PullIn);
  ASSERT_EQ(vortex->root_steps, 1);

  // Friendly fire everywhere but the default attack; the caster spares itself
  // only where noted.
  for (const SkillConfig& s : book.All()) {
    ASSERT_EQ(s.friendly_fire, s.name != kDefaultSkill);
    ASSERT_TRUE(s.self_motion);
    ASSERT_TRUE(s.self_damage);
    ASSERT_EQ(s.damage, s.name == kDefaultSkill ? 1 : 0);
  }
  ASSERT_TRUE(fireball->self_tags && fireball->self_root);
  ASSERT_FALSE(step->self_tags);  // It lands on the centre of its own cross
  ASSERT_TRUE(step->self_root);
  ASSERT_TRUE(tp->self_tags && tp->self_root);
  ASSERT_TRUE(vortex->self_tags);
  ASSERT_FALSE(vortex->self_root);

  ASSERT_TRUE(book.Find("frost") == nullptr);
  ASSERT_TRUE(SkillMovesCaster(*step));
  ASSERT_TRUE(SkillMovesCaster(*tp));
  ASSERT_FALSE(SkillMovesCaster(*fireball));
}

TEST(TestSkillBookDefineReplaces) {
  SkillBook book;
  SkillConfig frost;
  frost.name = "frost";
  frost.targeting = SkillTargeting::Projectile;
  frost.range = 2;
  frost.tags = {{"chilled", kPermanentTag}};
  book.Define(frost);
  ASSERT_EQ(book.Find("frost")->range, 2);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(6));
  SkillConfig short_fireball = *book.Find("fireball");
  short_fireball.range = 2;
  book.Define(short_fireball);                  // a level retunes a builtin
  ASSERT_EQ(book.Find("fireball")->range, 2);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(6));  // replaced, not appended
  book.Reset();                                 // back to builtins only
  ASSERT_EQ(book.Find("fireball")->range, 3);
  ASSERT_TRUE(book.Find("frost") == nullptr);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(5));
  ASSERT_TRUE(book.Find(kDefaultSkill) != nullptr);  // Reset keeps the attack
}

TEST(TestSkillEnumStringsRoundTrip) {
  for (SkillTargeting t : {SkillTargeting::Self, SkillTargeting::Ground,
                           SkillTargeting::Projectile}) {
    ASSERT_TRUE(SkillTargetingFromString(SkillTargetingToString(t)) == t);
  }
  for (SkillArea a : {SkillArea::Single, SkillArea::Cross}) {
    ASSERT_TRUE(SkillAreaFromString(SkillAreaToString(a)) == a);
  }
  for (SkillMotion m : {SkillMotion::None, SkillMotion::Dash,
                        SkillMotion::Teleport, SkillMotion::PushOut,
                        SkillMotion::PullIn}) {
    ASSERT_TRUE(SkillMotionFromString(SkillMotionToString(m)) == m);
  }

  int thrown = 0;
  try { SkillTargetingFromString("beam"); } catch (const std::runtime_error&) { ++thrown; }
  try { SkillAreaFromString("ring"); } catch (const std::runtime_error&) { ++thrown; }
  try { SkillMotionFromString("blink"); } catch (const std::runtime_error&) { ++thrown; }
  ASSERT_EQ(thrown, 3);
}

// =============================================================================
// Skill motion Tests (line and landing rules)
// =============================================================================

// A 10x10 arena shared by env-level tests: a wall border, floor inside (rows
// and cols 1-8). Agents are parked on row 8 (cols 1..n) until a test places
// them; tests only use rows 1-6. SynchroEnv is not movable (BaseEnv owns
// unique_ptrs and declares a destructor), so the arena fills a caller's env:
// `SynchroEnv env(10, 10, n, 1, 0, 42); MakeArena(env);`.
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

static Position Dash(SynchroEnv& env, Agent* a, std::vector<Position>* crossed = nullptr) {
  return ResolveDash(env.GetGrid(), env.GetObjectManager(), a->GetPosition(),
                     0, 1, 4, a->GetId(), crossed);  // toward the right
}

static Position Teleport(SynchroEnv& env, Agent* a) {
  return ResolveTeleport(env.GetGrid(), env.GetObjectManager(), a->GetPosition(),
                         0, 1, 3, a->GetId());
}

static Position Ground(SynchroEnv& env, Position from, int range = 3) {
  return ResolveGroundTarget(env.GetGrid(), from, 0, 1, range);
}

TEST(TestDirectionDelta) {
  int dr = -9, dc = -9;
  DirectionDelta(Direction::Right, dr, dc);
  ASSERT_EQ(dr, 0);
  ASSERT_EQ(dc, 1);
  DirectionDelta(Direction::Up, dr, dc);
  Position up = ApplyMovement({5, 5}, MovementAction::Up);
  ASSERT_EQ(dr, up.row - 5);
  ASSERT_EQ(dc, up.col - 5);
  DirectionDelta(Direction::Down, dr, dc);
  Position down = ApplyMovement({5, 5}, MovementAction::Down);
  ASSERT_EQ(dr, down.row - 5);
  ASSERT_EQ(dc, down.col - 5);
  DirectionDelta(Direction::Left, dr, dc);
  ASSERT_EQ(dr, 0);
  ASSERT_EQ(dc, -1);
}

TEST(TestGroundTargetClear) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(Ground(env, {3, 1}) == (Position{3, 4}));
}

TEST(TestGroundTargetCrossesHolesAndAgents) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 2}, CellKind::Hazard);
  Place(env, 1, {3, 3});
  ASSERT_TRUE(Ground(env, {3, 1}) == (Position{3, 4}));
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Hazard);  // may land on a hole
  ASSERT_TRUE(Ground(env, {3, 1}) == (Position{3, 4}));
}

TEST(TestGroundTargetStoppedByWall) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
  ASSERT_TRUE(Ground(env, {3, 1}) == (Position{3, 3}));
  env.GetMutableGrid().SetCell({3, 2}, CellKind::Wall);
  ASSERT_TRUE(Ground(env, {3, 1}) == (Position{3, 1}));  // wall adjacent: the caster's own cell
}

TEST(TestDashClearLaneAndPath) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  std::vector<Position> crossed;
  ASSERT_TRUE(Dash(env, a, &crossed) == (Position{3, 5}));
  ASSERT_EQ(crossed.size(), static_cast<size_t>(3));  // (3,2) (3,3) (3,4)
  ASSERT_TRUE(crossed[0] == (Position{3, 2}));
  ASSERT_TRUE(crossed[1] == (Position{3, 3}));
  ASSERT_TRUE(crossed[2] == (Position{3, 4}));
}

TEST(TestResolveZeroDirectionStays) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 3});
  std::vector<Position> crossed{{1, 1}};  // Stale content is cleared
  ASSERT_TRUE(ResolveDash(env.GetGrid(), env.GetObjectManager(), a->GetPosition(),
                          0, 0, 4, a->GetId(), &crossed) == (Position{3, 3}));
  ASSERT_TRUE(crossed.empty());
  ASSERT_TRUE(ResolveGroundTarget(env.GetGrid(), {3, 3}, 0, 0, 3) == (Position{3, 3}));
  ASSERT_TRUE(ResolveTeleport(env.GetGrid(), env.GetObjectManager(), {3, 3}, 0, 0, 3,
                              a->GetId()) == (Position{3, 3}));
}

TEST(TestCanLand) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  const Grid& g = env.GetGrid();
  const ObjectManager& om = env.GetObjectManager();
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {3, 3});
  ASSERT_TRUE(CanLand(g, om, {3, 2}, a->GetId()));   // empty floor
  ASSERT_TRUE(CanLand(g, om, {3, 1}, a->GetId()));   // the mover's own cell
  ASSERT_FALSE(CanLand(g, om, {3, 3}, a->GetId()));  // another living agent
  b->SetAlive(false);
  ASSERT_TRUE(CanLand(g, om, {3, 3}, a->GetId()));   // a dead actor does not block
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Hazard);
  ASSERT_FALSE(CanLand(g, om, {3, 4}, a->GetId()));  // hole
  ASSERT_FALSE(CanLand(g, om, {0, 4}, a->GetId()));  // wall
  ASSERT_FALSE(CanLand(g, om, {-1, 4}, a->GetId())); // out of bounds
}

TEST(TestDashStopsBeforeWall) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(Dash(env, a) == (Position{3, 3}));
}

TEST(TestDashWallRightAwayStays) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 2}, CellKind::Wall);
  Agent* a = Place(env, 0, {3, 1});
  std::vector<Position> crossed;
  ASSERT_TRUE(Dash(env, a, &crossed) == (Position{3, 1}));
  ASSERT_TRUE(crossed.empty());
}

TEST(TestDashStopsAtBorder) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 6});
  ASSERT_TRUE(Dash(env, a) == (Position{3, 8}));
}

TEST(TestDashCrossesHole) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 3}, CellKind::Hazard);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(Dash(env, a) == (Position{3, 5}));
}

TEST(TestDashNeverLandsInHole) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 5}, CellKind::Hazard);
  Agent* a = Place(env, 0, {3, 1});
  std::vector<Position> crossed;
  ASSERT_TRUE(Dash(env, a, &crossed) == (Position{3, 4}));
  ASSERT_EQ(crossed.size(), static_cast<size_t>(2));  // only up to the landing cell
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Hazard);
  ASSERT_TRUE(Dash(env, a) == (Position{3, 3}));
}

TEST(TestDashCrossesAgentsButNeverLandsOnOne) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Place(env, 1, {3, 3});
  ASSERT_TRUE(Dash(env, a) == (Position{3, 5}));  // through
  Place(env, 1, {3, 5});
  ASSERT_TRUE(Dash(env, a) == (Position{3, 4}));  // short of it
}

TEST(TestTeleportClear) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 4}));
}

TEST(TestTeleportJumpsWalls) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 2}, CellKind::Wall);
  env.GetMutableGrid().SetCell({3, 3}, CellKind::Wall);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 4}));
}

TEST(TestTeleportFallsBackCloser) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 3}));
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Hazard);
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 3}));
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Floor);
  Place(env, 1, {3, 4});
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 3}));
}

TEST(TestTeleportOutOfBoundsFallsBack) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 7});  // +3 out of the grid, +2 the border wall
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 8}));
}

TEST(TestTeleportNowhereStays) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  for (int c = 2; c <= 4; ++c) env.GetMutableGrid().SetCell({3, c}, CellKind::Wall);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(Teleport(env, a) == (Position{3, 1}));
}

// =============================================================================
// Skill use in Step Tests
// =============================================================================

static Action Use(MovementAction aim) { return EncodeAction(aim, InteractAction::Skill1); }
static const Action kStay = EncodeAction(MovementAction::Stay);
static Companion* AsCompanion(Agent* a) { return dynamic_cast<Companion*>(a); }
static bool Has(const BaseEnv& env, const Agent* a, const char* tag) {
  TagId t = env.GetTagTable().Find(tag);
  return t != kInvalidTag && a->HasTag(t);
}

// Define validates: true when it throws.
static bool DefineThrows(SkillBook& book, const SkillConfig& s) {
  try {
    book.Define(s);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

TEST(TestTeleportThroughStep) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "teleport"));
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(a->GetExecutedAction().interact == InteractAction::Skill1);
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("teleport"));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
  ASSERT_EQ(AsCompanion(a)->GetCooldown(0), 4);
}

TEST(TestLightningStepElectrifiesPathAndLandingCross) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* crossed = Place(env, 1, {3, 3});   // dashed through
  Agent* beside = Place(env, 2, {2, 5});    // above the landing cell (3,5)
  Agent* away = Place(env, 3, {5, 5});      // 2 below the landing cell: untouched
  env.SetCompanionSkill(caster->GetId(), 0, "lightningStep");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(Has(env, crossed, "electrified"));
  ASSERT_TRUE(Has(env, beside, "electrified"));
  ASSERT_FALSE(Has(env, away, "electrified"));
  ASSERT_FALSE(Has(env, caster, "electrified"));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastTagsApplied()[0].cause, std::string("lightningStep"));
  ASSERT_EQ(env.GetLastTagsApplied()[0].source, caster->GetId());
  ASSERT_TRUE(env.GetLastTagsApplied()[0].fresh);
}

TEST(TestFireballBurnsTheCross) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});    // target (3,4)
  Agent* center = Place(env, 1, {3, 4});
  Agent* up = Place(env, 2, {2, 4});
  Agent* far = Place(env, 3, {3, 6});       // 2 from the target: untouched
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
  ASSERT_TRUE(Has(env, center, "burning"));
  ASSERT_TRUE(Has(env, up, "burning"));
  ASSERT_FALSE(Has(env, far, "burning"));
  ASSERT_FALSE(Has(env, caster, "burning"));
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));  // casting = not moving
  ASSERT_TRUE(AsCompanion(caster)->GetDirection() == Direction::Right);  // aimed
}

TEST(TestFireballFliesThroughAgentsWallStopsIt) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* crossed = Place(env, 1, {3, 2});   // not a stop; ring of the target (3,3)
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 3}));
  ASSERT_TRUE(Has(env, crossed, "burning"));
}

TEST(TestSkillOnCooldownIsDroppedAndMovementApplies) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  env.SetCompanionSkill(a->GetId(), 0, "lightningStep");
  env.Step({Use(MovementAction::Right)});  // t0: dash to (3,5), cd 3
  ASSERT_TRUE(a->GetPosition() == (Position{3, 5}));
  env.Step({Use(MovementAction::Left)});   // t1: cd 2 -> plain move
  ASSERT_TRUE(a->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(env.GetLastSkillUses().empty());
  ASSERT_TRUE(a->GetExecutedAction().interact == InteractAction::None);
  env.Step({kStay});                       // t2: cd 1
  env.Step({Use(MovementAction::Left)});   // t3: cd 0 -> dash
  ASSERT_TRUE(a->GetPosition() == (Position{3, 1}));
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
}

// No skill set: the slot holds the default attack, so Skill1 strikes the faced
// cell instead of moving.
TEST(TestUnsetSlotAttacksInsteadOfMoving) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 1}));
  ASSERT_TRUE(AsCompanion(a)->GetDirection() == Direction::Right);  // aimed
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 2}));
}

TEST(TestSetCompanionSkillValidates) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_FALSE(env.SetCompanionSkill(a->GetId(), 0, "nope"));
  ASSERT_FALSE(env.SetCompanionSkill(a->GetId(), 2, "vortex"));  // no slot 3
  ASSERT_FALSE(env.SetCompanionSkill(a->GetId(), -1, "vortex"));
  ASSERT_FALSE(env.SetCompanionSkill(kInvalidObjectId, 0, "vortex"));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 1, "vortex"));   // slot 2 exists
  ASSERT_EQ(AsCompanion(a)->GetSkill(1), std::string("vortex"));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 1, ""));         // clears: back to attack
  ASSERT_EQ(AsCompanion(a)->GetSkill(1), std::string(kDefaultSkill));
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "vortex"));
  AsCompanion(a)->SetCooldown(0, 3);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, ""));
  ASSERT_EQ(AsCompanion(a)->GetSkill(0), std::string(kDefaultSkill));
  ASSERT_EQ(AsCompanion(a)->GetCooldown(0), 0);
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, kDefaultSkill));  // explicitly, too
  ASSERT_EQ(AsCompanion(a)->GetSkill(0), std::string(kDefaultSkill));
}

TEST(TestTwoDashersSameLandingFirstIndexWins) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({2, 5}, CellKind::Wall);  // b's dash up can only reach (3,5)
  Agent* a = Place(env, 0, {3, 1});  // dashes right to (3,5)
  Agent* b = Place(env, 1, {4, 5});
  env.SetCompanionSkill(a->GetId(), 0, "lightningStep");
  env.SetCompanionSkill(b->GetId(), 0, "lightningStep");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Up)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(b->GetPosition() == (Position{4, 5}));  // (3,5) taken first: stays
}

TEST(TestProjectileSkillFromLevelData) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig frost;
  frost.name = "frost";
  frost.targeting = SkillTargeting::Projectile;
  frost.range = 2;
  frost.tags = {{"chilled", 2}};
  env.GetMutableSkillBook().Define(frost);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* near = Place(env, 1, {3, 3});
  Agent* behind = Place(env, 2, {3, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "frost");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(Has(env, near, "chilled"));
  ASSERT_FALSE(Has(env, behind, "chilled"));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 3}));
  env.Step({kStay, kStay, kStay});  // duration 2: still there
  ASSERT_TRUE(Has(env, near, "chilled"));
  env.Step({kStay, kStay, kStay});  // expired
  ASSERT_FALSE(Has(env, near, "chilled"));
}

TEST(TestProjectileStopsOnFirstAgent) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig bolt;
  bolt.name = "bolt";
  bolt.targeting = SkillTargeting::Projectile;
  bolt.range = 4;
  bolt.tags = {{"zapped", 1}};
  env.GetMutableSkillBook().Define(bolt);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* first = Place(env, 1, {3, 2});   // adjacent: hit at once
  Agent* second = Place(env, 2, {3, 4});  // shielded by the first
  env.SetCompanionSkill(caster->GetId(), 0, "bolt");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 2}));
  ASSERT_TRUE(Has(env, first, "zapped"));
  ASSERT_FALSE(Has(env, second, "zapped"));
}

TEST(TestStunnedCompanionCannotUseSkill) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  env.SetCompanionSkill(a->GetId(), 0, "teleport");
  a->ApplyStatus(StatusType::Stunned, 2);
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 1}));
  ASSERT_EQ(AsCompanion(a)->GetCooldown(0), 0);
  ASSERT_TRUE(env.GetLastSkillUses().empty());
}

TEST(TestHostTagPrimitives) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "wet", 1));
  ASSERT_TRUE(Has(env, a, "wet"));
  ASSERT_FALSE(env.ApplyTagTo(a->GetId(), "", 1));
  ASSERT_FALSE(env.ApplyTagTo(kInvalidObjectId, "wet", 1));
  ASSERT_TRUE(env.RemoveTagFrom(a->GetId(), "wet"));
  ASSERT_FALSE(Has(env, a, "wet"));
  ASSERT_TRUE(env.RemoveTagFrom(a->GetId(), "never_seen"));
  ASSERT_FALSE(env.RemoveTagFrom(kInvalidObjectId, "wet"));
}

// Names longer than kMaxNameLength never enter the env (the C API hands names
// out in fixed-size buffers): refused, and not interned either.
TEST(TestHostPrimitivesRejectOverlongNames) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  const std::string max_name(kMaxNameLength, 'm');
  const std::string too_long(kMaxNameLength + 1, 'x');

  ASSERT_FALSE(env.ApplyTagTo(a->GetId(), too_long, 2));
  ASSERT_FALSE(env.SetCellTag({3, 2}, too_long, 2));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  ASSERT_EQ(env.GetTagTable().Find(too_long), kInvalidTag);
  ASSERT_TRUE(a->GetTags().empty());

  // Define refuses such a skill (ValidateSkillConfig), so it cannot be slotted.
  SkillConfig wordy;
  wordy.name = too_long;
  ASSERT_TRUE(DefineThrows(env.GetMutableSkillBook(), wordy));
  ASSERT_FALSE(env.SetCompanionSkill(a->GetId(), 0, too_long));
  ASSERT_EQ(AsCompanion(a)->GetSkill(0), std::string(kDefaultSkill));

  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), max_name, 2));
  ASSERT_TRUE(env.SetCellTag({3, 2}, max_name, 2));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, env.GetTagTable().Find(max_name));
}

TEST(TestCloneKeepsSkillsTagsAndCooldowns) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig frost;
  frost.name = "frost";
  frost.targeting = SkillTargeting::Projectile;
  frost.range = 3;
  frost.cooldown = 2;
  frost.tags = {{"chilled", kPermanentTag}};
  env.GetMutableSkillBook().Define(frost);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* target = Place(env, 1, {3, 3});
  env.SetCompanionSkill(caster->GetId(), 0, "frost");
  env.Step({Use(MovementAction::Right), kStay});

  std::unique_ptr<BaseEnv> copy = env.Clone();
  const auto* c = dynamic_cast<const Companion*>(
      copy->GetObjectManager().GetActor(caster->GetId()));
  const auto* t = dynamic_cast<const Agent*>(
      copy->GetObjectManager().GetActor(target->GetId()));
  ASSERT_TRUE(c != nullptr && t != nullptr);
  ASSERT_EQ(c->GetSkill(0), std::string("frost"));
  ASSERT_EQ(c->GetCooldown(0), 2);
  ASSERT_TRUE(Has(*copy, t, "chilled"));
  ASSERT_TRUE(copy->GetSkillBook().Find("frost") != nullptr);
  ASSERT_EQ(copy->GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(copy->GetLastTagsApplied().size(), static_cast<size_t>(1));

  SynchroEnv assigned(10, 10, 2, 1, 0, 7);
  assigned = env;
  ASSERT_TRUE(assigned.GetSkillBook().Find("frost") != nullptr);
  ASSERT_TRUE(Has(assigned, assigned.GetObjectManager().GetAllAgents()[1], "chilled"));
}

// One step filling both per-step reports: companion 0 attacks (its default
// slot), companion 1 fireballs companion 2.
static void StepFillingReports(SynchroEnv& env) {
  Place(env, 0, {3, 1});
  Agent* caster = Place(env, 1, {5, 1});  // target (5,4)
  Place(env, 2, {5, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Right), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
}

TEST(TestResetClearsStepReports) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  StepFillingReports(env);
  env.Reset();  // ids are re-issued: old reports would name the new world's agents
  ASSERT_TRUE(env.GetLastSkillUses().empty());
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

TEST(TestLoadSnapshotClearsStepReports) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Snapshot saved = env.SaveSnapshot();
  StepFillingReports(env);
  env.LoadSnapshot(saved);
  ASSERT_TRUE(env.GetLastSkillUses().empty());
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

TEST(TestSkillBookIgnoresEmptyName) {
  SkillBook book;
  SkillConfig unnamed;
  unnamed.tags = {{"ghost", kPermanentTag}};
  book.Define(unnamed);
  unnamed.range = -1;  // Invalid too, but unnamed: still silently ignored
  book.Define(unnamed);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(5));
  ASSERT_TRUE(book.Find("") == nullptr);
}

TEST(TestSkillBookDefineValidates) {
  SkillBook book;
  SkillConfig frost;
  frost.name = "frost";
  frost.range = -1;
  ASSERT_TRUE(DefineThrows(book, frost));
  ASSERT_TRUE(book.Find("frost") == nullptr);
  SkillConfig bad_fireball = *book.Find("fireball");
  bad_fireball.tags = {{"", 2}};
  ASSERT_TRUE(DefineThrows(book, bad_fireball));
  ASSERT_EQ(book.Find("fireball")->tags[0].tag, std::string("burning"));  // Unchanged
  bad_fireball = *book.Find("fireball");
  bad_fireball.name = std::string(kMaxNameLength + 1, 'f');
  ASSERT_TRUE(DefineThrows(book, bad_fireball));
  ASSERT_EQ(book.All().size(), static_cast<size_t>(5));
  frost.range = 2;
  frost.damage = -1;
  ASSERT_TRUE(DefineThrows(book, frost));
  ASSERT_TRUE(book.Find("frost") == nullptr);
  frost.damage = 0;
  frost.range = 2;
  ASSERT_FALSE(DefineThrows(book, frost));
  ASSERT_EQ(book.Find("frost")->range, 2);
}

// An empty-named skill is never defined, and a cleared slot is the attack: it
// never fires.
TEST(TestEmptyNamedSkillNeverFires) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig unnamed;
  unnamed.range = 3;
  unnamed.tags = {{"ghost", kPermanentTag}};
  env.GetMutableSkillBook().Define(unnamed);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* target = Place(env, 1, {3, 2});
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, ""));
  env.Step({EncodeAction(MovementAction::Right, InteractAction::Attack), kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_FALSE(Has(env, target, "ghost"));
  ASSERT_EQ(target->GetHealth(), target->GetMaxHealth());  // An ally: no friendly fire
}

// A skill tag of duration 0 would land nothing: Define refuses the skill.
TEST(TestZeroDurationSkillTagIsRefused) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig splash;
  splash.name = "splash";
  splash.targeting = SkillTargeting::Projectile;
  splash.range = 3;
  splash.tags = {{"wet", 0}};
  ASSERT_TRUE(DefineThrows(env.GetMutableSkillBook(), splash));
  Agent* caster = Place(env, 0, {3, 1});
  ASSERT_FALSE(env.SetCompanionSkill(caster->GetId(), 0, "splash"));
}

TEST(TestApplyTagToRejectsBadDurations) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_FALSE(env.ApplyTagTo(a->GetId(), "wet", 0));
  ASSERT_FALSE(env.ApplyTagTo(a->GetId(), "wet", -2));
  ASSERT_FALSE(Has(env, a, "wet"));
  ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "wet", kPermanentTag));
  ASSERT_TRUE(Has(env, a, "wet"));
}

// =============================================================================
// Push out, pull in, Rooted
// =============================================================================

TEST(TestFireballPushesTheRingOut) {
  SynchroEnv env(10, 10, 5, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});   // target (3,4)
  Agent* center = Place(env, 1, {3, 4});
  Agent* up = Place(env, 2, {2, 4});
  Agent* right = Place(env, 3, {3, 5});
  Agent* down = Place(env, 4, {4, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay, kStay});
  ASSERT_TRUE(center->GetPosition() == (Position{3, 4}));  // the centre stays
  ASSERT_TRUE(up->GetPosition() == (Position{1, 4}));
  ASSERT_TRUE(right->GetPosition() == (Position{3, 6}));
  ASSERT_TRUE(down->GetPosition() == (Position{5, 4}));
  for (Agent* a : {center, up, right, down}) ASSERT_TRUE(Has(env, a, "burning"));
}

TEST(TestFireballPushNeverLandsInHole) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({1, 4}, CellKind::Hazard);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* up = Place(env, 1, {2, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(up->GetPosition() == (Position{2, 4}));
}

TEST(TestFireballPushBlockedByAnotherActor) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* up = Place(env, 1, {2, 4});
  Agent* blocker = Place(env, 2, {1, 4});  // outside the cross: not pushed
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(up->GetPosition() == (Position{2, 4}));
  ASSERT_TRUE(blocker->GetPosition() == (Position{1, 4}));
}

TEST(TestVortexPullsTheOneAboveFirst) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});   // target (3,4), empty
  Agent* up = Place(env, 1, {2, 4});
  Agent* right = Place(env, 2, {3, 5});
  Agent* down = Place(env, 3, {4, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_TRUE(up->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(right->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(down->GetPosition() == (Position{4, 4}));
  for (Agent* a : {up, right, down}) ASSERT_TRUE(a->HasStatus(StatusType::Rooted));
  ASSERT_FALSE(caster->HasStatus(StatusType::Rooted));
}

TEST(TestVortexPriorityRightThenDownThenLeft) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* down = Place(env, 1, {4, 4});
  Agent* right = Place(env, 2, {3, 5});
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(right->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(down->GetPosition() == (Position{4, 4}));
}

TEST(TestVortexPullsDownBeforeLeft) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {1, 4});   // aims down: target (4,4)
  Agent* left = Place(env, 1, {4, 3});
  Agent* down = Place(env, 2, {5, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Down), kStay, kStay});
  ASSERT_TRUE(down->GetPosition() == (Position{4, 4}));
  ASSERT_TRUE(left->GetPosition() == (Position{4, 3}));
}

TEST(TestVortexOccupiedOrHoleCentrePullsNobody) {
  {
    SynchroEnv env(10, 10, 3, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});
    Agent* center = Place(env, 1, {3, 4});
    Agent* up = Place(env, 2, {2, 4});
    env.SetCompanionSkill(caster->GetId(), 0, "vortex");
    env.Step({Use(MovementAction::Right), kStay, kStay});
    ASSERT_TRUE(up->GetPosition() == (Position{2, 4}));
    ASSERT_TRUE(center->HasStatus(StatusType::Rooted));  // the centre is in the area too
    ASSERT_TRUE(up->HasStatus(StatusType::Rooted));
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({3, 4}, CellKind::Hazard);
    Agent* caster = Place(env, 0, {3, 1});
    Agent* up = Place(env, 1, {2, 4});
    env.SetCompanionSkill(caster->GetId(), 0, "vortex");
    env.Step({Use(MovementAction::Right), kStay});
    ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
    ASSERT_TRUE(up->GetPosition() == (Position{2, 4}));  // never into a hole
    ASSERT_TRUE(up->HasStatus(StatusType::Rooted));
  }
}

TEST(TestRootedForExactlyTheNextStep) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* target = Place(env, 1, {2, 4});
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Right), kStay});             // t: pulled to (3,4), rooted
  ASSERT_TRUE(target->GetPosition() == (Position{3, 4}));
  env.Step({kStay, EncodeAction(MovementAction::Right)});    // t+1: can't move
  ASSERT_TRUE(target->GetPosition() == (Position{3, 4}));
  env.Step({kStay, EncodeAction(MovementAction::Right)});    // t+2: free again
  ASSERT_TRUE(target->GetPosition() == (Position{3, 5}));
}

TEST(TestRootedCanCastButNotMoveBySkill) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  Agent* b = Place(env, 1, {5, 5});
  env.SetCompanionSkill(a->GetId(), 0, "teleport");
  env.SetCompanionSkill(b->GetId(), 0, "fireball");
  a->ApplyStatus(StatusType::Rooted, 2);
  b->ApplyStatus(StatusType::Rooted, 2);
  env.Step({Use(MovementAction::Right), Use(MovementAction::Left)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 1}));        // teleport dropped, walking blocked
  ASSERT_EQ(AsCompanion(a)->GetCooldown(0), 0);
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("fireball"));
}

TEST(TestRootedCannotLightningStep) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  env.SetCompanionSkill(a->GetId(), 0, "lightningStep");
  a->ApplyStatus(StatusType::Rooted, 2);
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(a->GetPosition() == (Position{3, 1}));
  ASSERT_TRUE(env.GetLastSkillUses().empty());
}

TEST(TestRootedCanStillBePushed) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* up = Place(env, 1, {2, 4});
  up->ApplyStatus(StatusType::Rooted, 2);
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(up->GetPosition() == (Position{1, 4}));
}

TEST(TestRootedStatusStrings) {
  ASSERT_TRUE(StatusTypeFromString("rooted") == StatusType::Rooted);
  ASSERT_TRUE(StatusTypeFromString("ROOTED") == StatusType::Rooted);
  ASSERT_EQ(StatusTypeToString(StatusType::Rooted), std::string("rooted"));
  ASSERT_TRUE(StatusTypeFromString(StatusTypeToString(StatusType::Rooted)) ==
              StatusType::Rooted);
}

// =============================================================================
// Resolution order and area edge cases
// =============================================================================

TEST(TestLaterCasterActsFromWhereAnEarlierSkillMovedIt) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});   // vortex right: target (3,4)
  Agent* b = Place(env, 1, {2, 4});   // above the target: pulled in first
  env.SetCompanionSkill(a->GetId(), 0, "vortex");
  env.SetCompanionSkill(b->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), Use(MovementAction::Down)});
  ASSERT_TRUE(b->GetPosition() == (Position{3, 4}));
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(2));
  ASSERT_EQ(env.GetLastSkillUses()[1].skill, std::string("fireball"));
  // 3 down from the pulled cell (3,4), not from (2,4), which would give (5,4).
  ASSERT_TRUE(env.GetLastSkillUses()[1].target == (Position{6, 4}));
  // Rooted by the earlier vortex, it still cast this step; root blocks next step.
  ASSERT_TRUE(b->HasStatus(StatusType::Rooted));
  ASSERT_EQ(AsCompanion(b)->GetCooldown(0), 3);
  env.Step({kStay, EncodeAction(MovementAction::Right)});
  ASSERT_TRUE(b->GetPosition() == (Position{3, 4}));
}

// =============================================================================
// Friendly fire and the caster's own skill
// =============================================================================

// A plain (non-companion) agent of `faction`: SynchroEnv only spawns companions.
static Agent* AddAgent(SynchroEnv& env, Position p, Faction faction) {
  Agent* a = env.GetMutableObjectManager().CreateActor<Agent>(p);
  a->SetFaction(faction);
  return a;
}

// A copy of `base` under `name`, defined in the env's book.
static SkillConfig DefineCopy(SynchroEnv& env, const char* base, const char* name) {
  SkillConfig s = *env.GetSkillBook().Find(base);
  s.name = name;
  return s;
}

// Friendly fire is on by default: the caster's own fireball burns it and
// pushes it off the ring, like anyone else there.
TEST(TestCasterOnTheRingOfItsOwnFireballBurnsAndIsPushed) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
  Agent* caster = Place(env, 0, {3, 2});  // the wall stops the target at (3,3)
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right)});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 3}));
  ASSERT_TRUE(Has(env, caster, "burning"));
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));  // left ring cell, pushed left
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].agent, caster->GetId());
  ASSERT_EQ(env.GetLastTagsApplied()[0].source, caster->GetId());
}

// The caster on the highest-priority ring cell of its own vortex is pulled in
// (vortex has self_root = false: not rooted); the ally left on the ring is.
TEST(TestCasterOnTheRingOfItsOwnVortexIsPulledNotRooted) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.GetMutableGrid().SetCell({4, 4}, CellKind::Wall);
  Agent* caster = Place(env, 0, {2, 4});  // aims down: the wall stops the target at (3,4)
  Agent* ally = Place(env, 1, {3, 5});    // right ring cell: after "up"
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Down), kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 4}));
  ASSERT_FALSE(caster->HasStatus(StatusType::Rooted));
  ASSERT_TRUE(ally->GetPosition() == (Position{3, 5}));
  ASSERT_TRUE(ally->HasStatus(StatusType::Rooted));
}

// A ground skill stopped by an adjacent wall lands on the caster's own cell.
TEST(TestGroundSkillAgainstAWallLandsOnTheCaster) {
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});  // the border wall is at (3,0)
    Agent* ally = Place(env, 1, {2, 1});    // up ring cell
    env.SetCompanionSkill(caster->GetId(), 0, "fireball");
    env.Step({Use(MovementAction::Left), kStay});
    ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 1}));
    ASSERT_TRUE(Has(env, caster, "burning"));
    ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));  // the centre is not pushed
    ASSERT_TRUE(Has(env, ally, "burning"));
    ASSERT_TRUE(ally->GetPosition() == (Position{1, 1}));
  }
  {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    Agent* caster = Place(env, 0, {3, 1});
    Agent* ally = Place(env, 1, {2, 1});
    env.SetCompanionSkill(caster->GetId(), 0, "vortex");
    env.Step({Use(MovementAction::Left), kStay});
    ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 1}));
    ASSERT_TRUE(ally->GetPosition() == (Position{2, 1}));  // the centre is occupied
    ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));
    ASSERT_FALSE(caster->HasStatus(StatusType::Rooted));   // self_root = false
    ASSERT_TRUE(ally->HasStatus(StatusType::Rooted));
  }
}

// With self_root on (the default), a vortex on the caster's own cell roots it.
TEST(TestSelfRootRootsTheCasterOnItsOwnArea) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  SkillConfig sticky = DefineCopy(env, "vortex", "stickyVortex");
  sticky.self_root = true;
  env.GetMutableSkillBook().Define(sticky);
  Agent* caster = Place(env, 0, {3, 1});
  env.SetCompanionSkill(caster->GetId(), 0, "stickyVortex");
  env.Step({Use(MovementAction::Left)});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 1}));
  ASSERT_TRUE(caster->HasStatus(StatusType::Rooted));
}

// self_tags / self_motion spare the caster one effect each; allies still get it.
TEST(TestSelfTagsAndSelfMotionSpareOnlyTheCaster) {
  for (int variant = 0; variant < 2; ++variant) {
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.GetMutableGrid().SetCell({3, 4}, CellKind::Wall);
    SkillConfig s = DefineCopy(env, "fireball", "myFireball");
    if (variant == 0) s.self_tags = false;
    if (variant == 1) s.self_motion = false;
    env.GetMutableSkillBook().Define(s);
    Agent* caster = Place(env, 0, {3, 2});  // left ring cell of (3,3)
    Agent* ally = Place(env, 1, {2, 3});    // up ring cell
    env.SetCompanionSkill(caster->GetId(), 0, "myFireball");
    env.Step({Use(MovementAction::Right), kStay});
    ASSERT_EQ(Has(env, caster, "burning"), variant == 1);
    ASSERT_TRUE(caster->GetPosition() == (variant == 0 ? Position{3, 1} : Position{3, 2}));
    ASSERT_TRUE(Has(env, ally, "burning"));
    ASSERT_TRUE(ally->GetPosition() == (Position{1, 3}));
  }
}

// A lightning step's caster lands on the centre of its own cross; the builtin
// spares it (self_tags = false), a copy with self_tags on electrifies it.
TEST(TestLightningStepSelfTags) {
  for (bool self_tags : {false, true}) {
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    SkillConfig s = DefineCopy(env, "lightningStep", "myStep");
    s.self_tags = self_tags;
    env.GetMutableSkillBook().Define(s);
    Agent* caster = Place(env, 0, {3, 1});
    env.SetCompanionSkill(caster->GetId(), 0, "myStep");
    env.Step({Use(MovementAction::Right)});
    ASSERT_TRUE(caster->GetPosition() == (Position{3, 5}));
    ASSERT_EQ(Has(env, caster, "electrified"), self_tags);
  }
}

// friendly_fire = false: allies (companions) and the caster are untouched (no
// tag, no push); enemies and neutrals in the area are affected.
TEST(TestNoFriendlyFireSparesAlliesAndTheCaster) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig s = DefineCopy(env, "fireball", "safeFireball");
  s.friendly_fire = false;
  env.GetMutableSkillBook().Define(s);
  Agent* caster = Place(env, 0, {3, 1});   // target (3,4)
  Agent* ally = Place(env, 1, {2, 4});     // up ring cell
  Agent* enemy = AddAgent(env, {3, 5}, Faction::ENEMY);      // right ring cell
  Agent* neutral = AddAgent(env, {4, 4}, Faction::NEUTRAL);  // down ring cell
  env.SetCompanionSkill(caster->GetId(), 0, "safeFireball");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay});  // one action per agent
  ASSERT_FALSE(Has(env, ally, "burning"));
  ASSERT_TRUE(ally->GetPosition() == (Position{2, 4}));
  ASSERT_TRUE(Has(env, enemy, "burning"));
  ASSERT_TRUE(enemy->GetPosition() == (Position{3, 6}));
  ASSERT_TRUE(Has(env, neutral, "burning"));
  ASSERT_TRUE(neutral->GetPosition() == (Position{5, 4}));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(2));

  // On its own cell (a wall right beside it) the caster is spared too.
  Place(env, 0, {6, 1});
  AsCompanion(caster)->SetCooldown(0, 0);
  env.Step({Use(MovementAction::Left), kStay, kStay, kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{6, 1}));
  ASSERT_FALSE(Has(env, caster, "burning"));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

// friendly_fire = false: a vortex skips an ally of higher priority, pulls and
// roots the enemy, and leaves the ally unrooted.
TEST(TestNoFriendlyFireVortexPullsTheEnemyNotTheAlly) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig s = DefineCopy(env, "vortex", "safeVortex");
  s.friendly_fire = false;
  env.GetMutableSkillBook().Define(s);
  Agent* caster = Place(env, 0, {3, 1});  // target (3,4), empty
  Agent* ally = Place(env, 1, {2, 4});    // up: first in priority
  Agent* enemy = AddAgent(env, {3, 5}, Faction::ENEMY);
  env.SetCompanionSkill(caster->GetId(), 0, "safeVortex");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(ally->GetPosition() == (Position{2, 4}));
  ASSERT_FALSE(ally->HasStatus(StatusType::Rooted));
  ASSERT_TRUE(enemy->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(enemy->HasStatus(StatusType::Rooted));
}

// friendly_fire = false: a projectile flies past allies (it cannot affect
// them) and stops on the first agent it can.
TEST(TestNoFriendlyFireProjectilePassesAllies) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig bolt;
  bolt.name = "bolt";
  bolt.targeting = SkillTargeting::Projectile;
  bolt.range = 5;
  bolt.friendly_fire = false;
  bolt.tags = {{"chilled", kPermanentTag}};
  env.GetMutableSkillBook().Define(bolt);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* ally = Place(env, 1, {3, 2});
  Agent* enemy = AddAgent(env, {3, 4}, Faction::ENEMY);
  env.SetCompanionSkill(caster->GetId(), 0, "bolt");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
  ASSERT_FALSE(Has(env, ally, "chilled"));
  ASSERT_TRUE(Has(env, enemy, "chilled"));
}

TEST(TestEnemyFilteredVortexIgnoresCompanions) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig vortex = *env.GetSkillBook().Find("vortex");
  vortex.name = "enemyVortex";
  vortex.filter = TargetFilter::Enemy;
  env.GetMutableSkillBook().Define(vortex);
  Agent* caster = Place(env, 0, {3, 1});  // target (3,4), empty
  Agent* up = Place(env, 1, {2, 4});
  Agent* right = Place(env, 2, {3, 5});
  env.SetCompanionSkill(caster->GetId(), 0, "enemyVortex");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_TRUE(up->GetPosition() == (Position{2, 4}));
  ASSERT_TRUE(right->GetPosition() == (Position{3, 5}));
  ASSERT_FALSE(up->HasStatus(StatusType::Rooted));
  ASSERT_FALSE(right->HasStatus(StatusType::Rooted));
}

TEST(TestVortexPullsTheLeftOneWhenAlone) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});  // target (3,4)
  Agent* left = Place(env, 1, {3, 3});
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 4}));
  ASSERT_TRUE(left->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(left->HasStatus(StatusType::Rooted));
}

TEST(TestVortexSkipsADeadAgentOnTheRing) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});  // target (3,4)
  Agent* dead = Place(env, 1, {2, 4});    // above: first in priority, but dead
  Agent* right = Place(env, 2, {3, 5});
  dead->SetAlive(false);
  env.SetCompanionSkill(caster->GetId(), 0, "vortex");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(dead->GetPosition() == (Position{2, 4}));
  ASSERT_FALSE(dead->HasStatus(StatusType::Rooted));
  ASSERT_TRUE(right->GetPosition() == (Position{3, 4}));
  ASSERT_TRUE(right->HasStatus(StatusType::Rooted));
}

// =============================================================================
// Zone (cell) tag Tests
// =============================================================================

TEST(TestZoneTagsWhoeverStandsThere) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, env.GetTagTable().Find("wet"));
  ASSERT_EQ(env.GetCellTag({3, 2}).duration, kPermanentTag);
  ASSERT_EQ(env.GetCellTag({3, 3}).tag, kInvalidTag);
  Agent* a = Place(env, 0, {3, 1});
  env.Step({EncodeAction(MovementAction::Right)});
  ASSERT_TRUE(Has(env, a, "wet"));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].cause, std::string("zone"));
  ASSERT_EQ(env.GetLastTagsApplied()[0].source, kInvalidObjectId);
  ASSERT_EQ(env.GetLastTagsApplied()[0].agent, a->GetId());
  ASSERT_TRUE(env.GetLastTagsApplied()[0].fresh);
  env.Step({kStay});  // Still there: re-applied, not fresh
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_FALSE(env.GetLastTagsApplied()[0].fresh);
  env.Step({EncodeAction(MovementAction::Right)});  // Off the zone: nothing lands
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
  ASSERT_TRUE(Has(env, a, "wet"));  // Permanent
}

TEST(TestZoneFreshMeansTheAgentDidNotCarryTheTag) {
  {  // Standing still on a duration-1 zone: the tag expires at every Step
     // start, so every landing is fresh.
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", 1));
    Agent* a = Place(env, 0, {3, 2});
    for (int i = 0; i < 3; ++i) {
      env.Step({kStay});
      ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
      ASSERT_EQ(env.GetLastTagsApplied()[0].cause, std::string("zone"));
      ASSERT_TRUE(env.GetLastTagsApplied()[0].fresh);
      ASSERT_TRUE(Has(env, a, "wet"));
    }
  }
  {  // Arriving on a permanent zone while already carrying its tag: not fresh.
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
    Agent* a = Place(env, 0, {3, 1});
    ASSERT_TRUE(env.ApplyTagTo(a->GetId(), "wet", kPermanentTag));
    env.Step({EncodeAction(MovementAction::Right)});
    ASSERT_TRUE(a->GetPosition() == (Position{3, 2}));
    ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
    ASSERT_EQ(env.GetLastTagsApplied()[0].cause, std::string("zone"));
    ASSERT_FALSE(env.GetLastTagsApplied()[0].fresh);
  }
}

TEST(TestZoneSkipsADeadAgent) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
  Agent* dead = Place(env, 0, {3, 2});
  dead->SetAlive(false);
  env.Step({kStay, kStay});
  ASSERT_FALSE(Has(env, dead, "wet"));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

TEST(TestZoneSetUnderAStandingAgentLandsNextStep) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 2});
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
  ASSERT_FALSE(Has(env, a, "wet"));  // Nothing lands outside a Step
  env.Step({kStay});
  ASSERT_TRUE(Has(env, a, "wet"));
  ASSERT_EQ(env.GetLastTagsApplied().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastTagsApplied()[0].cause, std::string("zone"));
  ASSERT_EQ(env.GetLastTagsApplied()[0].agent, a->GetId());
  ASSERT_TRUE(env.GetLastTagsApplied()[0].fresh);
}

TEST(TestTimedZoneKeepsTheTagWhileStandingThere) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", 2));
  Agent* a = Place(env, 0, {3, 1});
  env.Step({EncodeAction(MovementAction::Right)});  // Lands with 2 left
  for (int i = 0; i < 3; ++i) {
    env.Step({kStay});  // Ticked to 1 at the start, re-landed at 2
    ASSERT_TRUE(Has(env, a, "wet"));
    ASSERT_EQ(a->GetTags()[0].duration, 2);
  }
  // The last landing was during the last step on the zone (t): the tag is
  // present after steps t and t+1, gone after t+2.
  env.Step({EncodeAction(MovementAction::Right)});  // t+1: walks off
  ASSERT_TRUE(Has(env, a, "wet"));
  env.Step({kStay});  // t+2
  ASSERT_FALSE(Has(env, a, "wet"));
}

TEST(TestZoneTagsFollowSkillMotions) {
  {  // A lightningStep lands on an oil cell
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.SetCellTag({3, 5}, "oil", kPermanentTag);
    Agent* a = Place(env, 0, {3, 1});
    env.SetCompanionSkill(a->GetId(), 0, "lightningStep");
    env.Step({Use(MovementAction::Right), kStay});
    ASSERT_TRUE(a->GetPosition() == (Position{3, 5}));
    ASSERT_TRUE(Has(env, a, "oil"));
  }
  {  // A fireball pushes `up` from (2,4) onto a wet cell (1,4)
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.SetCellTag({1, 4}, "wet", kPermanentTag);
    Agent* caster = Place(env, 0, {3, 1});  // target (3,4)
    Agent* up = Place(env, 1, {2, 4});
    env.SetCompanionSkill(caster->GetId(), 0, "fireball");
    env.Step({Use(MovementAction::Right), kStay});
    ASSERT_TRUE(up->GetPosition() == (Position{1, 4}));
    ASSERT_TRUE(Has(env, up, "wet"));
    const auto& landed = env.GetLastTagsApplied();
    ASSERT_EQ(landed.size(), static_cast<size_t>(2));
    ASSERT_EQ(landed[0].cause, std::string("fireball"));
    ASSERT_EQ(landed[1].cause, std::string("zone"));
  }
  {  // A vortex pulls `up` from (2,4) onto a wet centre (3,4)
    SynchroEnv env(10, 10, 2, 1, 0, 42);
    MakeArena(env);
    env.SetCellTag({3, 4}, "wet", kPermanentTag);
    Agent* caster = Place(env, 0, {3, 1});
    Agent* up = Place(env, 1, {2, 4});
    env.SetCompanionSkill(caster->GetId(), 0, "vortex");
    env.Step({Use(MovementAction::Right), kStay});
    ASSERT_TRUE(up->GetPosition() == (Position{3, 4}));
    ASSERT_TRUE(Has(env, up, "wet"));
  }
  {  // A teleport lands on a wet cell
    SynchroEnv env(10, 10, 1, 1, 0, 42);
    MakeArena(env);
    env.SetCellTag({3, 4}, "wet", kPermanentTag);
    Agent* a = Place(env, 0, {3, 1});
    env.SetCompanionSkill(a->GetId(), 0, "teleport");
    env.Step({Use(MovementAction::Right)});
    ASSERT_TRUE(a->GetPosition() == (Position{3, 4}));
    ASSERT_TRUE(Has(env, a, "wet"));
  }
}

TEST(TestZonesApplyBeforeSkills) {
  // A target walking into water and hit the same step: "wet" lands first.
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  env.SetCellTag({3, 3}, "wet", kPermanentTag);
  Agent* caster = Place(env, 0, {3, 1});  // fireball target (3,4)
  Agent* walker = Place(env, 1, {3, 4});  // walks left onto the wet cell, on the ring
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), EncodeAction(MovementAction::Left)});
  const auto& landed = env.GetLastTagsApplied();
  ASSERT_TRUE(landed.size() >= 2);
  ASSERT_EQ(landed[0].cause, std::string("zone"));
  ASSERT_EQ(landed[0].agent, walker->GetId());
  ASSERT_EQ(landed[1].cause, std::string("fireball"));
  ASSERT_EQ(landed[1].agent, walker->GetId());
}

TEST(TestClearCellTag) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", kPermanentTag));
  ASSERT_TRUE(env.SetCellTag({3, 2}, "", 0));
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);
  Agent* a = Place(env, 0, {3, 1});
  env.Step({EncodeAction(MovementAction::Right)});
  ASSERT_FALSE(Has(env, a, "wet"));
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

TEST(TestSetCellTagRejectsOutOfBoundsAndBadDurations) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  ASSERT_FALSE(env.SetCellTag({-1, 2}, "wet", kPermanentTag));
  ASSERT_FALSE(env.SetCellTag({3, 10}, "wet", kPermanentTag));
  ASSERT_EQ(env.GetCellTag({-1, 2}).tag, kInvalidTag);
  ASSERT_EQ(env.GetCellTag({3, 10}).tag, kInvalidTag);
  ASSERT_TRUE(env.SetCellTag({3, 2}, "wet", 3));
  ASSERT_FALSE(env.SetCellTag({3, 2}, "oil", 0));   // Unchanged
  ASSERT_FALSE(env.SetCellTag({3, 2}, "oil", -2));  // Unchanged
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, env.GetTagTable().Find("wet"));
  ASSERT_EQ(env.GetCellTag({3, 2}).duration, 3);
  ASSERT_EQ(env.GetTagTable().Find("oil"), kInvalidTag);  // Not interned either
}

TEST(TestResetAndLoadReplaceZonesWithSnapshots) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.SetCellTag({3, 2}, "wet", kPermanentTag);
  env.Reset();
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, kInvalidTag);

  // A snapshot carries its zones: loading replaces the env's with them.
  env.SetCellTag({3, 2}, "wet", kPermanentTag);
  Snapshot saved = env.SaveSnapshot();
  env.SetCellTag({4, 4}, "oil", kPermanentTag);
  env.LoadSnapshot(saved);
  ASSERT_EQ(env.GetCellTag({3, 2}).tag, env.GetTagTable().Find("wet"));
  ASSERT_EQ(env.GetCellTag({4, 4}).tag, kInvalidTag);
}

TEST(TestResetWithD4ClearsCellTags) {
  SynchroEnv env(10, 10, 1, 1, 0, 42, 1);  // A rotation
  env.SetCellTag({2, 3}, "wet", kPermanentTag);
  env.Reset();  // A generated level has no zones: LoadSnapshot clears them
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      ASSERT_EQ(env.GetCellTag({r, c}).tag, kInvalidTag);
    }
  }
}

TEST(TestCloneKeepsCellTags) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  env.SetCellTag({3, 2}, "wet", 2);
  std::unique_ptr<BaseEnv> copy = env.Clone();
  ASSERT_EQ(copy->GetCellTag({3, 2}).tag, copy->GetTagTable().Find("wet"));
  ASSERT_EQ(copy->GetCellTag({3, 2}).duration, 2);

  SynchroEnv assigned(10, 10, 1, 1, 0, 7);
  assigned = env;
  ASSERT_EQ(assigned.GetCellTag({3, 2}).tag, assigned.GetTagTable().Find("wet"));
  env.SetCellTag({3, 2}, "", 0);  // Deep copies: they keep theirs
  ASSERT_TRUE(assigned.GetCellTag({3, 2}).tag != kInvalidTag);
  ASSERT_TRUE(copy->GetCellTag({3, 2}).tag != kInvalidTag);
}

// =============================================================================
// Default attack (no empty slots) and skill damage
// =============================================================================

// Throws a std::runtime_error whose message contains `needle`.
static bool DefineThrowsMentioning(SkillBook& book, const SkillConfig& s, const std::string& needle) {
  try {
    book.Define(s);
  } catch (const std::runtime_error& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

TEST(TestAttackBuiltin) {
  ASSERT_EQ(std::string(kDefaultSkill), std::string("attack"));
  SkillBook book;
  const SkillConfig* a = book.Find(kDefaultSkill);
  ASSERT_TRUE(a != nullptr);
  ASSERT_TRUE(a->targeting == SkillTargeting::Projectile);
  ASSERT_EQ(a->range, 1);
  ASSERT_TRUE(a->area == SkillArea::Single);
  ASSERT_TRUE(a->filter == TargetFilter::All);
  ASSERT_TRUE(a->motion == SkillMotion::None);
  ASSERT_EQ(a->damage, 1);
  ASSERT_FALSE(a->friendly_fire);
  ASSERT_EQ(a->cooldown, 0);
  ASSERT_TRUE(a->tags.empty());
  ASSERT_EQ(a->root_steps, 0);
  ValidateSkillConfig(*a);  // The builtin itself is a valid config
}

TEST(TestEveryCompanionStartsWithTheAttackInBothSlots) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  env.Reset();
  for (Agent* agent : env.GetMutableObjectManager().GetAllAgents()) {
    Companion* c = AsCompanion(agent);
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->GetSkill(0), std::string(kDefaultSkill));
    ASSERT_EQ(c->GetSkill(1), std::string(kDefaultSkill));
  }
  Companion fresh(0, {0, 0});
  ASSERT_EQ(fresh.GetSkill(0), std::string(kDefaultSkill));
  ASSERT_EQ(fresh.GetSkill(1), std::string(kDefaultSkill));
}

// The attack is fixed: a level cannot redefine it, and the book is unchanged.
TEST(TestAttackCannotBeRedefined) {
  SkillBook book;
  const size_t before = book.All().size();
  SkillConfig strong = *book.Find(kDefaultSkill);
  strong.damage = 5;
  ASSERT_TRUE(DefineThrowsMentioning(book, strong, "'attack' is the fixed default skill"));
  ASSERT_EQ(book.Find(kDefaultSkill)->damage, 1);
  ASSERT_EQ(book.All().size(), before);
  SkillConfig same = *book.Find(kDefaultSkill);  // Even unchanged
  ASSERT_TRUE(DefineThrows(book, same));
}

TEST(TestDefaultAttackStrikesTheFacedEnemyEveryStep) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* enemy = AddAgent(env, {3, 2}, Faction::ENEMY);
  ASSERT_EQ(enemy->GetHealth(), 3);
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));  // stays put
  ASSERT_EQ(enemy->GetHealth(), 2);
  ASSERT_EQ(caster->GetHealth(), caster->GetMaxHealth());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string(kDefaultSkill));
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 2}));
  ASSERT_EQ(env.GetLastSkillUses()[0].slot, 0);
  ASSERT_EQ(AsCompanion(caster)->GetCooldown(0), 0);
  env.Step({Use(MovementAction::Right), kStay});  // cooldown 0: again at once
  ASSERT_EQ(enemy->GetHealth(), 1);
  ASSERT_TRUE(enemy->IsAlive());
}

TEST(TestDefaultAttackKillsAndADeadEnemyIsNoTarget) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* enemy = AddAgent(env, {3, 2}, Faction::ENEMY);
  enemy->SetMaxHealth(1);
  env.Step({Use(MovementAction::Right), kStay});
  ASSERT_EQ(enemy->GetHealth(), 0);
  ASSERT_FALSE(enemy->IsAlive());
  env.Step({Use(MovementAction::Right), kStay});  // strikes the empty air
  ASSERT_EQ(enemy->GetHealth(), 0);
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));
}

TEST(TestDefaultAttackSparesAnAdjacentAlly) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* ally = Place(env, 1, {3, 2});
  Agent* enemy = AddAgent(env, {3, 3}, Faction::ENEMY);  // Range 1: out of reach
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));
  ASSERT_EQ(ally->GetHealth(), ally->GetMaxHealth());
  ASSERT_EQ(enemy->GetHealth(), enemy->GetMaxHealth());
  ASSERT_EQ(caster->GetHealth(), caster->GetMaxHealth());
  ASSERT_EQ(env.GetLastSkillUses().size(), static_cast<size_t>(1));
}

TEST(TestDefaultAttackIntoAWallDoesNothing) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});  // (3,0) is a wall
  env.Step({Use(MovementAction::Left)});
  ASSERT_TRUE(caster->GetPosition() == (Position{3, 1}));
  ASSERT_TRUE(AsCompanion(caster)->GetDirection() == Direction::Left);
  ASSERT_EQ(caster->GetHealth(), caster->GetMaxHealth());
  ASSERT_TRUE(env.GetLastSkillUses()[0].target == (Position{3, 1}));  // Stopped on its cell
  ASSERT_TRUE(env.GetLastTagsApplied().empty());
}

TEST(TestASkillInSlotZeroReplacesTheAttack) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* enemy = AddAgent(env, {3, 2}, Faction::ENEMY);
  ASSERT_TRUE(env.SetCompanionSkill(caster->GetId(), 0, "teleport"));
  env.Step({Use(MovementAction::Right), kStay});  // Slot 0: teleport
  ASSERT_EQ(env.GetLastSkillUses()[0].skill, std::string("teleport"));
  ASSERT_EQ(enemy->GetHealth(), enemy->GetMaxHealth());
}

// damage lands on everyone the skill affects (friendly fire on by default).
TEST(TestSkillDamageHitsEveryoneAffected) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  SkillConfig quake;
  quake.name = "quake";
  quake.targeting = SkillTargeting::Ground;
  quake.range = 3;
  quake.area = SkillArea::Cross;
  quake.damage = 2;
  env.GetMutableSkillBook().Define(quake);
  Agent* caster = Place(env, 0, {3, 1});  // centre (3,4)
  Agent* centre = Place(env, 1, {3, 4});
  Agent* up = Place(env, 2, {2, 4});
  Agent* enemy = AddAgent(env, {3, 5}, Faction::ENEMY);
  Agent* far = AddAgent(env, {3, 6}, Faction::ENEMY);
  env.SetCompanionSkill(caster->GetId(), 0, "quake");
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay, kStay});
  ASSERT_EQ(centre->GetHealth(), 1);
  ASSERT_EQ(up->GetHealth(), 1);
  ASSERT_EQ(enemy->GetHealth(), 1);
  ASSERT_EQ(far->GetHealth(), 3);
  ASSERT_EQ(caster->GetHealth(), 3);
}

// With friendly fire, a caster standing in its own area takes the damage,
// unless self_damage spares it.
TEST(TestSelfDamage) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  SkillConfig nova;
  nova.name = "nova";
  nova.targeting = SkillTargeting::Self;
  nova.area = SkillArea::Cross;
  nova.damage = 1;
  env.GetMutableSkillBook().Define(nova);
  Agent* caster = Place(env, 0, {3, 1});
  Agent* ally = Place(env, 1, {3, 2});
  env.SetCompanionSkill(caster->GetId(), 0, "nova");
  env.Step({Use(MovementAction::Stay), kStay});
  ASSERT_EQ(caster->GetHealth(), 2);
  ASSERT_EQ(ally->GetHealth(), 2);

  nova.self_damage = false;
  env.GetMutableSkillBook().Define(nova);
  env.Step({Use(MovementAction::Stay), kStay});
  ASSERT_EQ(caster->GetHealth(), 2);  // spared
  ASSERT_EQ(ally->GetHealth(), 1);
}

// Order inside a skill: tags, then damage, then root and area motion. An agent
// the damage kills still got the tags, but is neither rooted nor moved.
TEST(TestDamageAfterTagsBeforeRootAndMotion) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  SkillConfig blast = DefineCopy(env, "fireball", "blast");  // cross, push_out 1
  blast.damage = 3;
  blast.root_steps = 1;
  env.GetMutableSkillBook().Define(blast);
  Agent* caster = Place(env, 0, {3, 1});                  // centre (3,4)
  Agent* doomed = AddAgent(env, {2, 4}, Faction::ENEMY);  // up ring cell, 3 HP
  Agent* tough = AddAgent(env, {3, 5}, Faction::ENEMY);   // right ring cell
  tough->SetMaxHealth(5);
  env.SetCompanionSkill(caster->GetId(), 0, "blast");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_FALSE(doomed->IsAlive());
  ASSERT_TRUE(Has(env, doomed, "burning"));
  ASSERT_TRUE(doomed->GetPosition() == (Position{2, 4}));  // not pushed
  ASSERT_FALSE(doomed->IsRooted());
  ASSERT_EQ(tough->GetHealth(), 2);
  ASSERT_TRUE(Has(env, tough, "burning"));
  ASSERT_TRUE(tough->GetPosition() == (Position{3, 6}));   // pushed
  ASSERT_TRUE(tough->IsRooted());
}

// =============================================================================
// A corpse never hides a living agent in the actor grid
// =============================================================================

// Companion 0 kills a 1-HP enemy on (3,2) with its default attack, then walks
// onto the corpse. Companion 1 waits at (3,5) with a fireball (centre (3,2)
// when aimed left).
struct CorpseScene {
  ObjectId walker, caster, enemy;
};
static CorpseScene WalkOntoACorpse(SynchroEnv& env) {
  MakeArena(env);
  Agent* walker = Place(env, 0, {3, 1});
  Agent* caster = Place(env, 1, {3, 5});
  Agent* enemy = AddAgent(env, {3, 2}, Faction::ENEMY);
  enemy->SetMaxHealth(1);
  env.SetCompanionSkill(caster->GetId(), 0, "fireball");
  env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_FALSE(enemy->IsAlive());
  env.Step({EncodeAction(MovementAction::Right), kStay, kStay});
  ASSERT_TRUE(walker->GetPosition() == (Position{3, 2}));
  ASSERT_TRUE(env.GetObjectManager().GetActorAt({3, 2}) == walker);
  return {walker->GetId(), caster->GetId(), enemy->GetId()};
}

// In `e` (a copy, or the env after a load): the living walker is who stands on
// the corpse's cell, nobody can land there, and a fireball there burns it.
static void ExpectTheLivingOneOnTheCorpseCell(BaseEnv& e, const CorpseScene& s) {
  const Actor* there = e.GetObjectManager().GetActorAt({3, 2});
  ASSERT_TRUE(there != nullptr);
  ASSERT_EQ(there->GetId(), s.walker);
  ASSERT_FALSE(CanLand(e.GetGrid(), e.GetObjectManager(), {3, 2}, s.caster));
  ASSERT_FALSE(e.GetObjectManager().GetActor(s.enemy)->IsAlive());
  e.Step({kStay, Use(MovementAction::Left), kStay});
  ASSERT_EQ(e.GetLastSkillUses().size(), static_cast<size_t>(1));
  const auto* walker = dynamic_cast<const Agent*>(e.GetObjectManager().GetActor(s.walker));
  ASSERT_TRUE(Has(e, walker, "burning"));
}

TEST(TestCorpseNeverHidesTheLivingAfterSnapshotLoad) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  CorpseScene s = WalkOntoACorpse(env);
  Snapshot saved = env.SaveSnapshot();
  env.LoadSnapshot(saved);
  ExpectTheLivingOneOnTheCorpseCell(env, s);
}

TEST(TestCorpseNeverHidesTheLivingAfterClone) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  CorpseScene s = WalkOntoACorpse(env);
  std::unique_ptr<BaseEnv> copy = env.Clone();
  ExpectTheLivingOneOnTheCorpseCell(*copy, s);
}

TEST(TestCorpseNeverHidesTheLivingAfterCopyAssign) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  CorpseScene s = WalkOntoACorpse(env);
  SynchroEnv assigned(10, 10, 2, 1, 0, 7);
  assigned = env;
  ExpectTheLivingOneOnTheCorpseCell(assigned, s);
  // BaseEnv assignment copy-constructs its ObjectManager: assign one directly too
  ObjectManager objects(10, 10);
  objects = env.GetObjectManager();
  ASSERT_EQ(objects.GetActorAt({3, 2})->GetId(), s.walker);
  ASSERT_FALSE(CanLand(env.GetGrid(), objects, {3, 2}, s.caster));
}

// A dead actor put on a living one's cell (a host moving a corpse) never
// takes the cell.
TEST(TestCorpseMovedOntoTheLivingNeverTakesTheCell) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  CorpseScene s = WalkOntoACorpse(env);
  ObjectManager& om = env.GetMutableObjectManager();
  om.UpdatePosition(s.enemy, {5, 5});
  om.UpdatePosition(s.walker, {5, 5});
  om.UpdatePosition(s.enemy, {3, 2});
  om.UpdatePosition(s.enemy, {5, 5});
  ASSERT_EQ(om.GetActorAt({5, 5})->GetId(), s.walker);
}

// =============================================================================
// LegalActions lists usable skill actions
// =============================================================================

// The Skill1 actions (any aim) among `agent_idx`'s legal actions.
static std::vector<Action> LegalSkillActions(const BaseEnv& env, int agent_idx) {
  std::vector<Action> skills;
  for (Action a : env.LegalActions(agent_idx)) {
    if (DecodeAction(a).interact == InteractAction::Skill1) skills.push_back(a);
  }
  return skills;
}

static bool Contains(const std::vector<Action>& actions, Action a) {
  for (Action b : actions) {
    if (b == a) return true;
  }
  return false;
}

TEST(TestLegalActionsListTheSkillForEveryAim) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 1});  // A wall on the left: aiming into it is still legal
  std::vector<Action> skills = LegalSkillActions(env, 0);
  ASSERT_EQ(skills.size(), static_cast<size_t>(kNumMovementActions));
  for (int m = 0; m < kNumMovementActions; ++m) {
    ASSERT_TRUE(Contains(skills, Use(static_cast<MovementAction>(m))));
  }
  // Movement is listed as before (interact None)
  ASSERT_TRUE(Contains(env.LegalActions(0), kStay));
  ASSERT_TRUE(Contains(env.LegalActions(0), EncodeAction(MovementAction::Right)));
  ASSERT_FALSE(Contains(env.LegalActions(0), EncodeAction(MovementAction::Left)));
}

TEST(TestLegalActionsDropUnusableSkills) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Agent* a = Place(env, 0, {3, 1});
  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, "teleport"));
  ASSERT_EQ(LegalSkillActions(env, 0).size(), static_cast<size_t>(kNumMovementActions));
  a->ApplyStatus(StatusType::Rooted, 2);  // A rooted caster can't teleport
  ASSERT_TRUE(LegalSkillActions(env, 0).empty());
  a->ClearStatus(StatusType::Rooted);
  env.Step({Use(MovementAction::Right)});  // Cooldown 4
  ASSERT_TRUE(LegalSkillActions(env, 0).empty());
  ASSERT_TRUE(Contains(env.LegalActions(0), kStay));

  ASSERT_TRUE(env.SetCompanionSkill(a->GetId(), 0, ""));  // The attack: cooldown 0
  a->ApplyStatus(StatusType::Rooted, 2);                   // Doesn't move the caster
  ASSERT_EQ(LegalSkillActions(env, 0).size(), static_cast<size_t>(kNumMovementActions));
  a->ApplyStatus(StatusType::Stunned, 2);                  // Stunned: forced to stay
  ASSERT_TRUE(LegalSkillActions(env, 0).empty());
  ASSERT_EQ(env.LegalActions(0).size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.LegalActions(0)[0] == kStay);
}

// Any stunned agent is forced to stay (GatherIntentions): Stay is all it has.
TEST(TestLegalActionsOfTheStunnedAreStayOnly) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 3});
  Agent* enemy = AddAgent(env, {5, 5}, Faction::ENEMY);
  enemy->ApplyStatus(StatusType::Stunned, 2);
  ASSERT_EQ(env.LegalActions(1).size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.LegalActions(1)[0] == kStay);
  ASSERT_EQ(env.LegalActions(0).size(), static_cast<size_t>(2 * kNumMovementActions));
}

TEST(TestLegalActionsOfNonCompanionsAndTheDeadListNoSkill) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 1});
  AddAgent(env, {5, 5}, Faction::ENEMY);
  ASSERT_TRUE(LegalSkillActions(env, 1).empty());
  ASSERT_EQ(env.LegalActions(1).size(), static_cast<size_t>(kNumMovementActions));
  env.GetMutableObjectManager().GetAllAgents()[0]->SetAlive(false);
  ASSERT_EQ(env.LegalActions(0).size(), static_cast<size_t>(1));
  ASSERT_TRUE(env.LegalActions(0)[0] == kStay);
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

  std::cout << "Running " << tests.size() << " skill tests...\n\n";

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
