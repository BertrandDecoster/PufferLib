// Copyright 2024
// Unit tests for skills: opaque agent tags, the per-env TagTable, the SkillBook
// and the line and landing rules of skill motion

#include <iostream>
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
  ASSERT_EQ(book.All().size(), static_cast<size_t>(5));
  SkillConfig short_fireball = *book.Find("fireball");
  short_fireball.range = 2;
  book.Define(short_fireball);                  // a level retunes a builtin
  ASSERT_EQ(book.Find("fireball")->range, 2);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(5));  // replaced, not appended
  book.Reset();                                 // back to builtins only
  ASSERT_EQ(book.Find("fireball")->range, 3);
  ASSERT_TRUE(book.Find("frost") == nullptr);
  ASSERT_EQ(book.All().size(), static_cast<size_t>(4));
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
