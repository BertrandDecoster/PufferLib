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
constexpr ObjectId kInvalidObjectId = -1;

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
// Interact Actions (combat, abilities, etc.)
// =============================================================================
enum class InteractAction {
  None = 0,
  Attack = 1,
  // Future: Spell, Ability, Use, etc.
};

constexpr int kNumInteractActions = 2;  // None, Attack (expand as needed)

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
