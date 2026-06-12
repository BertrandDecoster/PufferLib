// Copyright 2024
// Round-trip symmetry tests for the centralized enum<->string converters
// (src/core/enum_strings.h).
//
// For every covered enum, every enumerator must satisfy
//   FromString(ToString(v)) == v
// and ToString must emit the exact wire string pinned here (the JSON wire
// format is additionally locked by the golden-fixture suite).
//
// Each enum has an "exhaustiveness anchor": a switch with no default over
// the enum. This file is compiled with -Werror=switch (/we4062 on MSVC),
// so adding an enumerator without updating the anchor (and therefore the
// table next to it) is a COMPILE error, not a silent fallthrough.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/enum_strings.h"

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

template <typename T>
struct EnumEntry {
  T value;
  const char* name;
};

// Checks ToString emits the pinned wire string and FromString inverts it.
template <typename T, typename ToStr, typename FromStr>
void CheckRoundTrip(const std::vector<EnumEntry<T>>& table, ToStr to_string,
                    FromStr from_string) {
  for (const auto& entry : table) {
    ASSERT_EQ(to_string(entry.value), std::string(entry.name));
    ASSERT_TRUE(from_string(entry.name) == entry.value);
  }
}

// ---------------------------------------------------------------------------
// Tables + exhaustiveness anchors (see file header).
// ---------------------------------------------------------------------------

const std::vector<EnumEntry<Direction>> kDirections = {
    {Direction::Up, "Up"},
    {Direction::Down, "Down"},
    {Direction::Left, "Left"},
    {Direction::Right, "Right"},
};
void DirectionAnchor(Direction v) {
  switch (v) {
    case Direction::Up:
    case Direction::Down:
    case Direction::Left:
    case Direction::Right:
      break;
  }
}

const std::vector<EnumEntry<Faction>> kFactions = {
    {Faction::COMPANION, "COMPANION"},
    {Faction::ENEMY, "ENEMY"},
    {Faction::NEUTRAL, "NEUTRAL"},
};
void FactionAnchor(Faction v) {
  switch (v) {
    case Faction::COMPANION:
    case Faction::ENEMY:
    case Faction::NEUTRAL:
      break;
  }
}

const std::vector<EnumEntry<ObjectType>> kObjectTypes = {
    {ObjectType::Object, "Object"},
    {ObjectType::Actor, "Actor"},
    {ObjectType::Agent, "Agent"},
    {ObjectType::AgentFSM, "AgentFSM"},
    {ObjectType::Companion, "Companion"},
    {ObjectType::Player, "Player"},
    {ObjectType::NPCCompanion, "NPCCompanion"},
};
void ObjectTypeAnchor(ObjectType v) {
  switch (v) {
    case ObjectType::Object:
    case ObjectType::Actor:
    case ObjectType::Agent:
    case ObjectType::AgentFSM:
    case ObjectType::Companion:
    case ObjectType::Player:
    case ObjectType::NPCCompanion:
      break;
  }
}

// Wire quirk (pinned): status names are written lowercase.
const std::vector<EnumEntry<StatusType>> kStatusTypes = {
    {StatusType::None, "none"},
    {StatusType::Stunned, "stunned"},
    {StatusType::Slowed, "slowed"},
    {StatusType::Marked, "marked"},
};
void StatusTypeAnchor(StatusType v) {
  switch (v) {
    case StatusType::None:
    case StatusType::Stunned:
    case StatusType::Slowed:
    case StatusType::Marked:
      break;
  }
}

const std::vector<EnumEntry<ActorColor>> kActorColors = {
    {ActorColor::None, "None"},
    {ActorColor::Red, "Red"},
    {ActorColor::Green, "Green"},
    {ActorColor::Blue, "Blue"},
};
void ActorColorAnchor(ActorColor v) {
  switch (v) {
    case ActorColor::None:
    case ActorColor::Red:
    case ActorColor::Green:
    case ActorColor::Blue:
      break;
  }
}

const std::vector<EnumEntry<CellKind>> kCellKinds = {
    {CellKind::Floor, "Floor"},
    {CellKind::Wall, "Wall"},
    {CellKind::Hazard, "Hazard"},
    {CellKind::HealArea, "HealArea"},
};
void CellKindAnchor(CellKind v) {
  switch (v) {
    case CellKind::Floor:
    case CellKind::Wall:
    case CellKind::Hazard:
    case CellKind::HealArea:
      break;
  }
}

const std::vector<EnumEntry<CellOrigin>> kCellOrigins = {
    {CellOrigin::Default, "Default"},
    {CellOrigin::Room, "Room"},
    {CellOrigin::Corridor, "Corridor"},
    {CellOrigin::Obstacle, "Obstacle"},
};
void CellOriginAnchor(CellOrigin v) {
  switch (v) {
    case CellOrigin::Default:
    case CellOrigin::Room:
    case CellOrigin::Corridor:
    case CellOrigin::Obstacle:
      break;
  }
}

const std::vector<EnumEntry<FSMStateType>> kFSMStateTypes = {
    {FSMStateType::None, "None"},
    {FSMStateType::Patrol, "Patrol"},
    {FSMStateType::Aggro, "Aggro"},
    {FSMStateType::ReturnToPatrol, "ReturnToPatrol"},
    {FSMStateType::Telegraph, "Telegraph"},
    {FSMStateType::Attack, "Attack"},
    {FSMStateType::Recovery, "Recovery"},
};
void FSMStateTypeAnchor(FSMStateType v) {
  switch (v) {
    case FSMStateType::None:
    case FSMStateType::Patrol:
    case FSMStateType::Aggro:
    case FSMStateType::ReturnToPatrol:
    case FSMStateType::Telegraph:
    case FSMStateType::Attack:
    case FSMStateType::Recovery:
      break;
  }
}

const std::vector<EnumEntry<SemanticTag>> kSemanticTags = {
    {SemanticTag::SynchroGoal, "SynchroGoal"},
    {SemanticTag::AggroTarget, "AggroTarget"},
    {SemanticTag::QuestPickup, "QuestPickup"},
    {SemanticTag::SafeZone, "SafeZone"},
    {SemanticTag::TargetMob, "TargetMob"},
    {SemanticTag::SkillGiver, "SkillGiver"},
    {SemanticTag::Escort, "Escort"},
    {SemanticTag::HtnName, "HtnName"},
    {SemanticTag::Room, "Room"},
};
void SemanticTagAnchor(SemanticTag v) {
  switch (v) {
    case SemanticTag::SynchroGoal:
    case SemanticTag::AggroTarget:
    case SemanticTag::QuestPickup:
    case SemanticTag::SafeZone:
    case SemanticTag::TargetMob:
    case SemanticTag::SkillGiver:
    case SemanticTag::Escort:
    case SemanticTag::HtnName:
    case SemanticTag::Room:
    case SemanticTag::_Count:
      break;
  }
}

}  // namespace

// =============================================================================
// Round-trip symmetry, one test per enum.
// =============================================================================
TEST(TestDirectionRoundTrip) {
  DirectionAnchor(Direction::Up);
  CheckRoundTrip(kDirections, DirectionToString, DirectionFromString);
}

TEST(TestFactionRoundTrip) {
  FactionAnchor(Faction::COMPANION);
  CheckRoundTrip(kFactions, FactionToString, FactionFromString);
}

TEST(TestObjectTypeRoundTrip) {
  ObjectTypeAnchor(ObjectType::Object);
  CheckRoundTrip(kObjectTypes, ObjectTypeToString, ObjectTypeFromString);
}

TEST(TestStatusTypeRoundTrip) {
  StatusTypeAnchor(StatusType::None);
  CheckRoundTrip(kStatusTypes, StatusTypeToString, StatusTypeFromString);
}

TEST(TestActorColorRoundTrip) {
  ActorColorAnchor(ActorColor::None);
  CheckRoundTrip(kActorColors, ActorColorToString, ActorColorFromString);
}

TEST(TestCellKindRoundTrip) {
  CellKindAnchor(CellKind::Floor);
  CheckRoundTrip(kCellKinds, CellKindToString, CellKindFromString);
}

TEST(TestCellOriginRoundTrip) {
  CellOriginAnchor(CellOrigin::Default);
  CheckRoundTrip(kCellOrigins, CellOriginToString, CellOriginFromString);
}

TEST(TestFSMStateTypeRoundTrip) {
  FSMStateTypeAnchor(FSMStateType::None);
  CheckRoundTrip(kFSMStateTypes, FSMStateTypeToString, FSMStateTypeFromString);
}

TEST(TestSemanticTagRoundTrip) {
  SemanticTagAnchor(SemanticTag::SynchroGoal);
  // SemanticTag has a count sentinel: the table must cover every real tag.
  ASSERT_EQ(kSemanticTags.size(), static_cast<size_t>(SemanticTag::_Count));
  CheckRoundTrip(kSemanticTags, SemanticTagToString, SemanticTagFromString);
}

// =============================================================================
// Pinned reader quirks (wire-format compatibility).
// =============================================================================

// Status strings: writer emits lowercase ("stunned"); the reader must accept
// both lowercase and capitalized spellings (regression pin for the JSON-load
// bug where capitalized-only matching silently dropped statuses), plus the
// short CSV aliases handled case-insensitively.
TEST(TestStatusTypeFromStringAcceptsBothSpellings) {
  ASSERT_TRUE(StatusTypeFromString("stunned") == StatusType::Stunned);
  ASSERT_TRUE(StatusTypeFromString("Stunned") == StatusType::Stunned);
  ASSERT_TRUE(StatusTypeFromString("STUNNED") == StatusType::Stunned);
  ASSERT_TRUE(StatusTypeFromString("stun") == StatusType::Stunned);
  ASSERT_TRUE(StatusTypeFromString("slowed") == StatusType::Slowed);
  ASSERT_TRUE(StatusTypeFromString("Slowed") == StatusType::Slowed);
  ASSERT_TRUE(StatusTypeFromString("slow") == StatusType::Slowed);
  ASSERT_TRUE(StatusTypeFromString("marked") == StatusType::Marked);
  ASSERT_TRUE(StatusTypeFromString("Marked") == StatusType::Marked);
  ASSERT_TRUE(StatusTypeFromString("mark") == StatusType::Marked);
  ASSERT_TRUE(StatusTypeFromString("") == StatusType::None);
  ASSERT_TRUE(StatusTypeFromString("garbage") == StatusType::None);
}

// Legacy v1 JSON: Synchro/Target cell kinds flatten to Floor (their semantic
// role now lives in the annotations array).
TEST(TestCellKindFromStringLegacyV1Fallback) {
  ASSERT_TRUE(CellKindFromString("Synchro") == CellKind::Floor);
  ASSERT_TRUE(CellKindFromString("Target") == CellKind::Floor);
  ASSERT_TRUE(CellKindFromString("garbage") == CellKind::Floor);
}

// =============================================================================
// CSV-facing parsers (shared by agent_config.cc and effect_config.cc).
// Case-insensitive, accept both the lowercase CSV vocabulary and the
// uppercase wire names. One documented default each (with a cerr warning on
// unknown input) - no current data/*.csv row hits the fallback, proven by
// the registry pinning tests in test_csv_utils.cc.
// =============================================================================

TEST(TestTargetFilterFromCSV) {
  struct Row { const char* input; TargetFilter expected; };
  const std::vector<Row> table = {
      // Lowercase CSV vocabulary (data/effects.csv, data/agents.csv).
      {"all", TargetFilter::All},
      {"companion", TargetFilter::Companion},
      {"enemy", TargetFilter::Enemy},
      {"neutral", TargetFilter::Neutral},
      // Capitalized enum-name vocabulary (the old agents-vs-effects
      // divergence: "Enemy" used to parse as Enemy in one loader and All in
      // the other).
      {"All", TargetFilter::All},
      {"Companion", TargetFilter::Companion},
      {"Enemy", TargetFilter::Enemy},
      {"Neutral", TargetFilter::Neutral},
      // Mixed case.
      {"ENEMY", TargetFilter::Enemy},
      {"cOmPaNiOn", TargetFilter::Companion},
  };
  for (const auto& row : table) {
    ASSERT_TRUE(TargetFilterFromCSV(row.input) == row.expected);
  }
  // Documented unknown-input default (warns on cerr).
  ASSERT_TRUE(TargetFilterFromCSV("garbage") == TargetFilter::All);
  ASSERT_TRUE(TargetFilterFromCSV("") == TargetFilter::All);
}

TEST(TestFactionFromCSV) {
  struct Row { const char* input; Faction expected; };
  const std::vector<Row> table = {
      // Lowercase CSV vocabulary (data/agents.csv faction column).
      {"companion", Faction::COMPANION},
      {"enemy", Faction::ENEMY},
      {"neutral", Faction::NEUTRAL},
      // Uppercase wire vocabulary (FactionToString output).
      {"COMPANION", Faction::COMPANION},
      {"ENEMY", Faction::ENEMY},
      {"NEUTRAL", Faction::NEUTRAL},
      // Mixed case.
      {"Companion", Faction::COMPANION},
      {"eNeMy", Faction::ENEMY},
  };
  for (const auto& row : table) {
    ASSERT_TRUE(FactionFromCSV(row.input) == row.expected);
  }
  // Documented unknown-input default (warns on cerr). ENEMY, matching the
  // old agent_config CSV parser; the strict JSON wire reader
  // FactionFromString keeps its COMPANION fallback (pinned below).
  ASSERT_TRUE(FactionFromCSV("garbage") == Faction::ENEMY);
  ASSERT_TRUE(FactionFromCSV("") == Faction::ENEMY);
}

// Unknown-input fallbacks, pinned to the pre-consolidation reader behavior.
TEST(TestFromStringFallbacks) {
  ASSERT_TRUE(DirectionFromString("garbage") == Direction::Up);
  ASSERT_TRUE(FactionFromString("garbage") == Faction::COMPANION);
  ASSERT_TRUE(ObjectTypeFromString("garbage") == ObjectType::Object);
  ASSERT_TRUE(ActorColorFromString("garbage") == ActorColor::None);
  ASSERT_TRUE(CellOriginFromString("garbage") == CellOrigin::Default);
  ASSERT_TRUE(FSMStateTypeFromString("garbage") == FSMStateType::None);
  ASSERT_TRUE(SemanticTagFromString("garbage") == SemanticTag::SynchroGoal);
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
