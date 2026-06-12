// Copyright 2024
// Tests for shared CSV parsing utilities (csv_utils.h) and pinning tests
// for the config loaders.
//
// The registry pinning tests capture exactly what data/agents.csv and
// data/effects.csv parse to TODAY. They were written against the original
// per-loader parsers BEFORE both loaders were switched to the shared
// csv_utils helpers, so any behavioral drift in the consolidation (e.g.
// the effect loader gaining quote handling) fails here.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/agent_config.h"
#include "../src/core/csv_utils.h"
#include "../src/core/effect_config.h"

using namespace companions;

#define TEST(name) \
  void name(); \
  struct name##_registrar { \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance; \
  void name()

#define ASSERT_TRUE(cond) \
  if (!(cond)) { \
    std::ostringstream oss; \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_FALSE(cond) \
  if (cond) { \
    std::ostringstream oss; \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_EQ(a, b) \
  if ((a) != (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

namespace {

#ifndef COMPANIONS_DATA_DIR
#error "COMPANIONS_DATA_DIR must be defined (path to the repo data/ directory)"
#endif

const std::string kDataDir = COMPANIONS_DATA_DIR;

const EffectConfig& GetEffectOrThrow(const std::string& name) {
  const EffectConfig* cfg = EffectConfigRegistry::Instance().GetConfig(name);
  if (cfg == nullptr) {
    throw std::runtime_error("effect config not found: " + name);
  }
  return *cfg;
}

const AgentConfig& GetAgentOrThrow(const std::string& name) {
  const AgentConfig* cfg = AgentConfigRegistry::Instance().GetConfig(name);
  if (cfg == nullptr) {
    throw std::runtime_error("agent config not found: " + name);
  }
  return *cfg;
}

void CheckEffect(const std::string& name, int telegraph, int active,
                 int recovery, int loop, const std::vector<int>& area,
                 TargetFilter filter, int damage, int push_dx, int push_dy,
                 int push_distance, const std::string& status_applied,
                 int status_duration, bool telegraph_visible) {
  const EffectConfig& c = GetEffectOrThrow(name);
  ASSERT_EQ(c.telegraph_ticks, telegraph);
  ASSERT_EQ(c.active_ticks, active);
  ASSERT_EQ(c.recovery_ticks, recovery);
  ASSERT_EQ(c.loop, loop);
  ASSERT_TRUE(c.area == area);
  ASSERT_TRUE(c.filter == filter);
  ASSERT_EQ(c.damage, damage);
  ASSERT_EQ(c.push_dx, push_dx);
  ASSERT_EQ(c.push_dy, push_dy);
  ASSERT_EQ(c.push_distance, push_distance);
  ASSERT_EQ(c.status_applied, status_applied);
  ASSERT_EQ(c.status_duration, status_duration);
  ASSERT_EQ(c.telegraph_visible, telegraph_visible);
  // Not present in the CSV schema; must stay at defaults.
  ASSERT_FALSE(c.radial_push);
  ASSERT_FALSE(c.apply_every_tick);
}

}  // namespace

// =============================================================================
// csv_utils unit tests
// =============================================================================
TEST(TestTrim) {
  ASSERT_EQ(Trim(""), "");
  ASSERT_EQ(Trim("   "), "");
  ASSERT_EQ(Trim("\t\r\n"), "");
  ASSERT_EQ(Trim("abc"), "abc");
  ASSERT_EQ(Trim("  abc  "), "abc");
  ASSERT_EQ(Trim("\ta b c\r\n"), "a b c");
}

TEST(TestParseCSVLine) {
  using V = std::vector<std::string>;
  ASSERT_TRUE(ParseCSVLine("") == (V{""}));
  ASSERT_TRUE(ParseCSVLine("a") == (V{"a"}));
  ASSERT_TRUE(ParseCSVLine("a,b,c") == (V{"a", "b", "c"}));
  ASSERT_TRUE(ParseCSVLine(" a , b ") == (V{"a", "b"}));
  // Trailing comma yields a trailing empty field.
  ASSERT_TRUE(ParseCSVLine("a,b,") == (V{"a", "b", ""}));
  // Quotes protect commas and are stripped.
  ASSERT_TRUE(ParseCSVLine("\"a,b\",c") == (V{"a,b", "c"}));
  ASSERT_TRUE(ParseCSVLine("x,\"hello, world\",y") ==
              (V{"x", "hello, world", "y"}));
}

// Dialect edge cases pinned to CURRENT behavior (see ParseCSVLine comment in
// csv_utils.h). These document what the parser does today, not RFC 4180.
TEST(TestParseCSVLineEdgeCases) {
  using V = std::vector<std::string>;
  // Unterminated quote: the rest of the line is one field (quote stripped).
  ASSERT_TRUE(ParseCSVLine("\"a,b") == (V{"a,b"}));
  // Doubled quote: BOTH quotes are consumed - "a""b" yields ab, NOT the
  // RFC-4180 literal-quote escape a"b.
  ASSERT_TRUE(ParseCSVLine("\"a\"\"b\"") == (V{"ab"}));
  // Trailing \r (CRLF line read with getline) is trimmed off the last field.
  ASSERT_TRUE(ParseCSVLine("a,b\r") == (V{"a", "b"}));
}

TEST(TestSplitOn) {
  using V = std::vector<std::string>;
  // getline semantics: empty input -> no tokens, trailing delim dropped.
  ASSERT_TRUE(SplitOn("", ';') == (V{}));
  ASSERT_TRUE(SplitOn("1", ';') == (V{"1"}));
  ASSERT_TRUE(SplitOn("1;0;1", ';') == (V{"1", "0", "1"}));
  ASSERT_TRUE(SplitOn("1;;1", ';') == (V{"1", "", "1"}));
  ASSERT_TRUE(SplitOn("1;0;", ';') == (V{"1", "0"}));
  ASSERT_TRUE(SplitOn(" 1 ; 0 ", ';') == (V{" 1 ", " 0 "}));
}

// =============================================================================
// Registry pinning: data/effects.csv must parse to exactly these values.
// =============================================================================
TEST(TestEffectsCsvParsesToKnownValues) {
  EffectConfigRegistry::Instance().Clear();
  ASSERT_TRUE(
      EffectConfigRegistry::Instance().LoadFromCSV(kDataDir + "/effects.csv"));

  const std::vector<std::string> expected_names = {
      "zombie_attack", "goblin_attack", "slash",      "dragon_breath",
      "fire_tower",    "wind_lever",    "brute_slam", "stun_spell",
      "slow_trap",     "heal_pulse",    "pull_hook"};
  ASSERT_TRUE(EffectConfigRegistry::Instance().GetAllNames() == expected_names);

  const std::vector<int> kSingle = {1};
  const std::vector<int> kFull3x3 = {1, 1, 1, 1, 1, 1, 1, 1, 1};
  const std::vector<int> kCross3x3 = {0, 1, 0, 1, 1, 1, 0, 1, 0};

  CheckEffect("zombie_attack", 2, 1, 2, 0, kSingle, TargetFilter::Companion,
              1, 0, 0, 0, "", 0, false);
  CheckEffect("goblin_attack", 1, 1, 1, 0, kSingle, TargetFilter::Companion,
              1, 0, 0, 0, "", 0, false);
  CheckEffect("slash", 0, 1, 0, 0, kSingle, TargetFilter::Companion,
              1, 0, 0, 0, "", 0, false);
  CheckEffect("dragon_breath", 3, 2, 3, 0, kFull3x3, TargetFilter::Companion,
              2, 0, 0, 0, "", 0, true);
  CheckEffect("fire_tower", 2, 1, 4, -1, kCross3x3, TargetFilter::All,
              1, 0, 0, 0, "", 0, true);
  CheckEffect("wind_lever", 1, 1, 0, 0, kSingle, TargetFilter::All,
              0, 0, -1, 2, "", 0, true);
  CheckEffect("brute_slam", 2, 1, 3, 0, kFull3x3, TargetFilter::Companion,
              2, 0, -1, 1, "", 0, true);
  CheckEffect("stun_spell", 0, 1, 0, 0, kSingle, TargetFilter::Enemy,
              0, 0, 0, 0, "stunned", 3, false);
  CheckEffect("slow_trap", 1, 1, 0, 0, kCross3x3, TargetFilter::All,
              0, 0, 0, 0, "slowed", 5, true);
  CheckEffect("heal_pulse", 0, 1, 0, 0, kCross3x3, TargetFilter::Companion,
              -2, 0, 0, 0, "", 0, false);
  CheckEffect("pull_hook", 1, 1, 0, 0, kSingle, TargetFilter::Enemy,
              0, 0, -1, -2, "", 0, true);

  EffectConfigRegistry::Instance().Clear();
}

// =============================================================================
// Registry pinning: data/agents.csv must parse to exactly these values.
// =============================================================================
TEST(TestAgentsCsvParsesToKnownValues) {
  AgentConfigRegistry::Instance().Clear();
  ASSERT_TRUE(
      AgentConfigRegistry::Instance().LoadFromCSV(kDataDir + "/agents.csv"));

  const std::vector<std::string> expected_names = {"zombie", "goblin",
                                                   "dragon"};
  ASSERT_TRUE(AgentConfigRegistry::Instance().GetAllTypeNames() ==
              expected_names);

  {
    const AgentConfig& c = GetAgentOrThrow("zombie");
    ASSERT_EQ(c.display_char, 'Z');
    ASSERT_EQ(c.max_health, 3);
    ASSERT_TRUE(c.faction == Faction::ENEMY);
    ASSERT_TRUE(c.cadence == (std::vector<int>{1, 0}));
    ASSERT_FALSE(c.flying);
    ASSERT_EQ(c.detection_range, 3);
    ASSERT_EQ(c.lose_target_range, 5);
    ASSERT_TRUE(c.has_attack);
    ASSERT_EQ(c.attack.name, "zombie_attack");
    ASSERT_EQ(c.attack_effect, "zombie_attack");
    ASSERT_EQ(c.attack.telegraph_ticks, 1);
    ASSERT_EQ(c.attack.attack_ticks, 1);
    ASSERT_EQ(c.attack.recovery_ticks, 2);
    ASSERT_EQ(c.attack.damage, 1);
    ASSERT_EQ(c.attack.area.width, 1);
    ASSERT_EQ(c.attack.area.height, 1);
    ASSERT_TRUE(c.attack.filter == TargetFilter::Companion);
  }
  {
    const AgentConfig& c = GetAgentOrThrow("goblin");
    ASSERT_EQ(c.display_char, 'G');
    ASSERT_EQ(c.max_health, 2);
    ASSERT_TRUE(c.faction == Faction::ENEMY);
    ASSERT_TRUE(c.cadence.empty());
    ASSERT_FALSE(c.flying);
    ASSERT_EQ(c.detection_range, 4);
    ASSERT_EQ(c.lose_target_range, 6);
    ASSERT_TRUE(c.has_attack);
    ASSERT_EQ(c.attack.name, "slash");
    ASSERT_EQ(c.attack_effect, "slash");
    ASSERT_EQ(c.attack.telegraph_ticks, 1);
    ASSERT_EQ(c.attack.attack_ticks, 0);
    ASSERT_EQ(c.attack.recovery_ticks, 0);
    ASSERT_EQ(c.attack.damage, 1);
    ASSERT_EQ(c.attack.area.width, 1);
    ASSERT_EQ(c.attack.area.height, 1);
    ASSERT_TRUE(c.attack.filter == TargetFilter::Companion);
  }
  {
    const AgentConfig& c = GetAgentOrThrow("dragon");
    ASSERT_EQ(c.display_char, 'D');
    ASSERT_EQ(c.max_health, 10);
    ASSERT_TRUE(c.faction == Faction::ENEMY);
    ASSERT_TRUE(c.cadence.empty());
    ASSERT_TRUE(c.flying);
    ASSERT_EQ(c.detection_range, 5);
    ASSERT_EQ(c.lose_target_range, 8);
    ASSERT_TRUE(c.has_attack);
    ASSERT_EQ(c.attack.name, "fire_breath");
    ASSERT_EQ(c.attack_effect, "fire_breath");
    ASSERT_EQ(c.attack.telegraph_ticks, 2);
    ASSERT_EQ(c.attack.attack_ticks, 1);
    ASSERT_EQ(c.attack.recovery_ticks, 3);
    ASSERT_EQ(c.attack.damage, 2);
    ASSERT_EQ(c.attack.area.width, 3);
    ASSERT_EQ(c.attack.area.height, 1);
    ASSERT_TRUE(c.attack.filter == TargetFilter::Companion);
  }

  AgentConfigRegistry::Instance().Clear();
}

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " tests...\n\n";
  int passed = 0, failed = 0;
  for (const auto& t : tests) {
    std::cout << "Running " << t.name << "... ";
    std::cout.flush();
    try {
      t.func();
      std::cout << "PASSED\n";
      ++passed;
    } catch (const std::exception& e) {
      std::cout << "FAILED: " << e.what() << "\n";
      ++failed;
    } catch (...) {
      std::cout << "FAILED (unknown exception)\n";
      ++failed;
    }
  }
  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
  return (failed > 0) ? 1 : 0;
}
