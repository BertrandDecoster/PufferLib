// Copyright 2024
// Centralized enum <-> string converters (see enum_strings.h).
//
// IMPORTANT: the ToString outputs and the FromString fallbacks are part of
// the JSON wire format (tests/data/golden_snapshot_v2.json). Do not change
// existing strings; new enum values get new strings.

#include "enum_strings.h"

#include <algorithm>
#include <cctype>

namespace companions {

// =============================================================================
// Direction
// =============================================================================
std::string DirectionToString(Direction dir) {
  switch (dir) {
    case Direction::Up: return "Up";
    case Direction::Down: return "Down";
    case Direction::Left: return "Left";
    case Direction::Right: return "Right";
    default: return "Unknown";
  }
}

Direction DirectionFromString(const std::string& str) {
  if (str == "Down") return Direction::Down;
  if (str == "Left") return Direction::Left;
  if (str == "Right") return Direction::Right;
  return Direction::Up;
}

// =============================================================================
// Faction
// =============================================================================
std::string FactionToString(Faction faction) {
  switch (faction) {
    case Faction::COMPANION: return "COMPANION";
    case Faction::ENEMY: return "ENEMY";
    case Faction::NEUTRAL: return "NEUTRAL";
    default: return "Unknown";
  }
}

Faction FactionFromString(const std::string& str) {
  if (str == "ENEMY") return Faction::ENEMY;
  if (str == "NEUTRAL") return Faction::NEUTRAL;
  return Faction::COMPANION;
}

// =============================================================================
// ObjectType
// =============================================================================
std::string ObjectTypeToString(ObjectType type) {
  switch (type) {
    case ObjectType::Object: return "Object";
    case ObjectType::Actor: return "Actor";
    case ObjectType::Agent: return "Agent";
    case ObjectType::AgentFSM: return "AgentFSM";
    case ObjectType::Companion: return "Companion";
    case ObjectType::Player: return "Player";
    case ObjectType::NPCCompanion: return "NPCCompanion";
    default: return "Unknown";
  }
}

ObjectType ObjectTypeFromString(const std::string& str) {
  if (str == "Actor") return ObjectType::Actor;
  if (str == "Agent") return ObjectType::Agent;
  if (str == "AgentFSM") return ObjectType::AgentFSM;
  if (str == "Companion") return ObjectType::Companion;
  if (str == "Player") return ObjectType::Player;
  if (str == "NPCCompanion") return ObjectType::NPCCompanion;
  return ObjectType::Object;
}

// =============================================================================
// StatusType
// =============================================================================
std::string StatusTypeToString(StatusType type) {
  // Lowercase on purpose: this is the JSON wire spelling and the CSV
  // status_applied vocabulary.
  switch (type) {
    case StatusType::Stunned: return "stunned";
    case StatusType::Slowed: return "slowed";
    case StatusType::Marked: return "marked";
    case StatusType::None:
    default:
      return "none";
  }
}

StatusType StatusTypeFromString(const std::string& name) {
  // Case-insensitive: the writer emits lowercase names, but older snapshots
  // briefly contained capitalized forms ("Stunned") and a capitalized-only
  // reader silently dropped statuses on JSON load (caught by the
  // golden-fixture round-trip test). Accept both, plus the short CSV
  // aliases.
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "stunned" || lower == "stun") return StatusType::Stunned;
  if (lower == "slowed" || lower == "slow") return StatusType::Slowed;
  if (lower == "marked" || lower == "mark") return StatusType::Marked;
  return StatusType::None;
}

// =============================================================================
// ActorColor
// =============================================================================
std::string ActorColorToString(ActorColor color) {
  switch (color) {
    case ActorColor::None: return "None";
    case ActorColor::Red: return "Red";
    case ActorColor::Green: return "Green";
    case ActorColor::Blue: return "Blue";
    default: return "None";
  }
}

ActorColor ActorColorFromString(const std::string& str) {
  if (str == "Red") return ActorColor::Red;
  if (str == "Green") return ActorColor::Green;
  if (str == "Blue") return ActorColor::Blue;
  return ActorColor::None;
}

// =============================================================================
// CellKind
// =============================================================================
std::string CellKindToString(CellKind kind) {
  switch (kind) {
    case CellKind::Floor: return "Floor";
    case CellKind::Wall: return "Wall";
    case CellKind::Hazard: return "Hazard";
    case CellKind::HealArea: return "HealArea";
    default: return "Unknown";
  }
}

CellKind CellKindFromString(const std::string& str) {
  if (str == "Wall") return CellKind::Wall;
  if (str == "Hazard") return CellKind::Hazard;
  if (str == "HealArea") return CellKind::HealArea;
  // Legacy v1 JSON: Synchro/Target cells now flatten to Floor; their
  // task-semantic role lives in the annotations array.
  return CellKind::Floor;
}

// =============================================================================
// CellOrigin
// =============================================================================
std::string CellOriginToString(CellOrigin origin) {
  switch (origin) {
    case CellOrigin::Default: return "Default";
    case CellOrigin::Room: return "Room";
    case CellOrigin::Corridor: return "Corridor";
    case CellOrigin::Obstacle: return "Obstacle";
    default: return "Unknown";
  }
}

CellOrigin CellOriginFromString(const std::string& str) {
  if (str == "Room") return CellOrigin::Room;
  if (str == "Corridor") return CellOrigin::Corridor;
  if (str == "Obstacle") return CellOrigin::Obstacle;
  return CellOrigin::Default;
}

// =============================================================================
// FSMStateType
// =============================================================================
std::string FSMStateTypeToString(FSMStateType type) {
  switch (type) {
    case FSMStateType::Patrol: return "Patrol";
    case FSMStateType::Aggro: return "Aggro";
    case FSMStateType::ReturnToPatrol: return "ReturnToPatrol";
    case FSMStateType::Telegraph: return "Telegraph";
    case FSMStateType::Attack: return "Attack";
    case FSMStateType::Recovery: return "Recovery";
    default: return "None";
  }
}

FSMStateType FSMStateTypeFromString(const std::string& str) {
  if (str == "Patrol") return FSMStateType::Patrol;
  if (str == "Aggro") return FSMStateType::Aggro;
  if (str == "ReturnToPatrol") return FSMStateType::ReturnToPatrol;
  if (str == "Telegraph") return FSMStateType::Telegraph;
  if (str == "Attack") return FSMStateType::Attack;
  if (str == "Recovery") return FSMStateType::Recovery;
  return FSMStateType::None;
}

// =============================================================================
// SemanticTag - keep in sync with htn_bridge.py SEMANTIC_TAG_NAMES.
// =============================================================================
std::string SemanticTagToString(SemanticTag tag) {
  switch (tag) {
    case SemanticTag::SynchroGoal: return "SynchroGoal";
    case SemanticTag::AggroTarget: return "AggroTarget";
    case SemanticTag::QuestPickup: return "QuestPickup";
    case SemanticTag::SafeZone:    return "SafeZone";
    case SemanticTag::TargetMob:   return "TargetMob";
    case SemanticTag::SkillGiver:  return "SkillGiver";
    case SemanticTag::Escort:      return "Escort";
    case SemanticTag::HtnName:     return "HtnName";
    case SemanticTag::Room:        return "Room";
    case SemanticTag::_Count:      return "Unknown";
  }
  return "Unknown";
}

SemanticTag SemanticTagFromString(const std::string& str) {
  if (str == "SynchroGoal") return SemanticTag::SynchroGoal;
  if (str == "AggroTarget") return SemanticTag::AggroTarget;
  if (str == "QuestPickup") return SemanticTag::QuestPickup;
  if (str == "SafeZone")    return SemanticTag::SafeZone;
  if (str == "TargetMob")   return SemanticTag::TargetMob;
  if (str == "SkillGiver")  return SemanticTag::SkillGiver;
  if (str == "Escort")      return SemanticTag::Escort;
  if (str == "HtnName")     return SemanticTag::HtnName;
  if (str == "Room")        return SemanticTag::Room;
  return SemanticTag::SynchroGoal;  // Fallback
}

}  // namespace companions
