// Copyright 2024
// Core types for The Companions game

#ifndef COMPANIONS_CORE_TYPES_H_
#define COMPANIONS_CORE_TYPES_H_

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>

namespace companions {

// =============================================================================
// Type aliases
// =============================================================================
using Action = int64_t;
using ObjectId = int;

// =============================================================================
// Constants
// =============================================================================
constexpr int kDefaultGridSize = 12;
constexpr int kDefaultHorizon = 100;
constexpr ObjectId kInvalidObjectId = -1;
// The team's downs that lose a level (BaseEnv::SetMaxDowns; level data, and
// what a snapshot without max_downs loads)
constexpr int kDefaultMaxDowns = 3;

using TagId = int;
constexpr TagId kInvalidTag = -1;
constexpr int kPermanentTag = -1;  // Tag duration: never expires
// Longest skill or tag name, in bytes. Longer names are refused where they
// enter the env (ValidateSkillConfig, snapshot validation, the host
// primitives), so the C API's fixed-size name buffers never truncate one.
constexpr int kMaxNameLength = 31;
inline bool IsValidNameLength(const std::string& name) {
  return name.size() <= static_cast<size_t>(kMaxNameLength);
}
// Longest step timer level data may set (tag and zone durations, zone steps,
// tag status steps, a skill's root_steps and cooldown, host tag landings): a
// timer set during a step is kept as n + 1 (Agent::TimerSteps,
// BaseEnv::ResolveZone), so the ceiling keeps that far from INT_MAX.
constexpr int kMaxTimerSteps = 1000000;
// A timer of steps: 1..kMaxTimerSteps, or kPermanentTag (never expires)
inline bool IsValidTimer(int steps) {
  return steps == kPermanentTag || (steps > 0 && steps <= kMaxTimerSteps);
}

// What a zone of some tag is: level data keyed by tag (BaseEnv::DefineZone,
// the level's zone table), or a per-cell override (BaseEnv::SetCellTag). The
// defaults are those of a tag the table does not define: permanent, landing
// a permanent tag, harmless, without successor.
struct ZoneDef {
  int duration = kPermanentTag;  // The tag's duration as it lands: steps, or kPermanentTag
  // The zone's own lifetime, a step timer (see BaseEnv::CellTag): steps, or
  // kPermanentTag (never expires)
  int steps = kPermanentTag;
  // The tag the cell gets when the zone expires ("" = the cell loses its
  // zone); that zone takes its fields from the table
  std::string then;
  int damage = 0;  // Health each landing takes (the turn's ledger, see BaseEnv::GetLastTurnHealth), >= 0

  bool operator==(const ZoneDef& o) const {
    return duration == o.duration && steps == o.steps && then == o.then && damage == o.damage;
  }
  bool operator!=(const ZoneDef& o) const { return !(*this == o); }
};

// =============================================================================
// Position
// =============================================================================
struct Position {
  int row = -1;
  int col = -1;

  bool operator==(const Position& other) const {
    return row == other.row && col == other.col;
  }

  bool operator!=(const Position& other) const {
    return !(*this == other);
  }

  bool IsValid() const {
    return row >= 0 && col >= 0;
  }
};

// Hash function for Position (needed for unordered_map)
struct PositionHash {
  std::size_t operator()(const Position& pos) const {
    return std::hash<int>()(pos.row) ^ (std::hash<int>()(pos.col) << 16);
  }
};

// =============================================================================
// Direction (for actor facing)
// =============================================================================
enum class Direction { Up, Down, Left, Right };

// =============================================================================
// Faction (for agent allegiance)
// =============================================================================
enum class Faction {
  COMPANION,  // Player-controlled agents
  ENEMY,      // Hostile to companions
  NEUTRAL,    // Non-hostile (NPCs, allies, etc.)
};

// =============================================================================
// Movement Actions
// =============================================================================
enum class MovementAction {
  Stay = 0,
  Up = 1,
  Down = 2,
  Left = 3,
  Right = 4
};

constexpr int kNumMovementActions = 5;

// =============================================================================
// Interact Actions: use the skill in a companion's slot
// =============================================================================
enum class InteractAction {
  None = 0,
  Skill1 = 1,
  Skill2 = 2,
  Attack = Skill1,  // Historical name of slot 1
};

constexpr int kMaxSkillSlots = 2;

// The fixed default skill: a melee strike on the faced cell (projectile,
// range 1, single, damage 1, no friendly fire, cooldown 0; defined in
// SkillBook's builtins). Every companion skill slot always holds a skill, and
// a slot with nothing else in it holds this one ("" is never a slot's value:
// clearing a slot puts it back). A level cannot redefine it (SkillBook::Define
// throws, a snapshot carrying it is rejected, SaveSnapshot never writes it;
// see RejectDefaultSkillName in skill_config.h).
inline constexpr char kDefaultSkill[] = "attack";
// Slots the flat action space exposes. Slot 2 exists everywhere else (data,
// snapshots, C API) but is not trainable yet: set to 2 to enable it (the
// action space grows from 10 to 15).
constexpr int kEnabledSkillSlots = 1;
constexpr int kNumInteractActions = 1 + kEnabledSkillSlots;

// Slot index used by an interact action (-1 for None).
inline int SkillSlotOf(InteractAction action) {
  return static_cast<int>(action) - 1;
}

// =============================================================================
// Action encoding/decoding
// Action space = movement * kNumInteractActions + interact
// =============================================================================
struct DecodedAction {
  MovementAction movement = MovementAction::Stay;
  InteractAction interact = InteractAction::None;
};

// Total action space size
constexpr int kNumActions = kNumMovementActions * kNumInteractActions;

inline Action EncodeAction(MovementAction mov, InteractAction interact = InteractAction::None) {
  return static_cast<Action>(mov) * kNumInteractActions + static_cast<Action>(interact);
}

inline DecodedAction DecodeAction(Action action) {
  DecodedAction result;
  result.movement = static_cast<MovementAction>((action / kNumInteractActions) % kNumMovementActions);
  result.interact = static_cast<InteractAction>(action % kNumInteractActions);
  return result;
}

// =============================================================================
// Position arithmetic operators
// =============================================================================
inline Position operator+(const Position& a, const Position& b) {
  return {a.row + b.row, a.col + b.col};
}

inline Position operator-(const Position& a, const Position& b) {
  return {a.row - b.row, a.col - b.col};
}

// =============================================================================
// Distance functions
// =============================================================================
// Manhattan distance (L1 norm) - exact distance for 4-connected grid movement
inline int ManhattanDistance(const Position& a, const Position& b) {
  return std::abs(a.row - b.row) + std::abs(a.col - b.col);
}

// Chebyshev distance (L∞ norm) - for 8-connected grids (future use)
inline int ChebyshevDistance(const Position& a, const Position& b) {
  return std::max(std::abs(a.row - b.row), std::abs(a.col - b.col));
}

// =============================================================================
// Utility functions
// =============================================================================
std::string MovementActionToString(MovementAction action);
std::string DirectionToString(Direction dir);

// Get the position resulting from applying a movement action
Position ApplyMovement(const Position& pos, MovementAction action);

// Convert MovementAction to Direction (Stay returns nullopt)
std::optional<Direction> MovementToDirection(MovementAction action);

// Convert Direction to MovementAction
MovementAction DirectionToMovement(Direction dir);

// Action string conversions
std::string ActionToString(Action action);
std::optional<Action> StringToAction(const std::string& str);

// Faction string conversion
std::string FactionToString(Faction faction);

// InteractAction string conversion
std::string InteractActionToString(InteractAction action);

}  // namespace companions

#endif  // COMPANIONS_CORE_TYPES_H_
