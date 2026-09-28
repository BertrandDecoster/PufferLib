// Copyright 2024
// Unit tests for skills: opaque agent tags, the per-env TagTable and the SkillBook

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/object.h"
#include "../src/core/skill_config.h"
#include "../src/core/tag_table.h"

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
