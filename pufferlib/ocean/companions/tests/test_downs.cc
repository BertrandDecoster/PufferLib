// Copyright 2024
// Unit tests for downs: companions going down, the downed state, the team's counter

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/object.h"
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
