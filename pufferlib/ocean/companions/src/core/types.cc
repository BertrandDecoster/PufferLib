// Copyright 2024
// Core types implementation

#include "types.h"

namespace companions {

std::string MovementActionToString(MovementAction action) {
  switch (action) {
    case MovementAction::Stay:
      return "Stay";
    case MovementAction::Up:
      return "Up";
    case MovementAction::Down:
      return "Down";
    case MovementAction::Left:
      return "Left";
    case MovementAction::Right:
      return "Right";
    default:
      return "Unknown";
  }
}

Position ApplyMovement(const Position& pos, MovementAction action) {
  Position result = pos;
  switch (action) {
    case MovementAction::Up:
      result.row--;
      break;
    case MovementAction::Down:
      result.row++;
      break;
    case MovementAction::Left:
      result.col--;
      break;
    case MovementAction::Right:
      result.col++;
      break;
    case MovementAction::Stay:
    default:
      break;
  }
  return result;
}

std::optional<Direction> MovementToDirection(MovementAction action) {
  switch (action) {
    case MovementAction::Up:
      return Direction::Up;
    case MovementAction::Down:
      return Direction::Down;
    case MovementAction::Left:
      return Direction::Left;
    case MovementAction::Right:
      return Direction::Right;
    case MovementAction::Stay:
    default:
      return std::nullopt;
  }
}

MovementAction DirectionToMovement(Direction dir) {
  switch (dir) {
    case Direction::Up:
      return MovementAction::Up;
    case Direction::Down:
      return MovementAction::Down;
    case Direction::Left:
      return MovementAction::Left;
    case Direction::Right:
      return MovementAction::Right;
    default:
      return MovementAction::Stay;
  }
}

std::string ActionToString(Action action) {
  DecodedAction decoded = DecodeAction(action);
  std::string result = MovementActionToString(decoded.movement);
  if (decoded.interact != InteractAction::None) {
    result += "+" + InteractActionToString(decoded.interact);
  }
  return result;
}

std::optional<Action> StringToAction(const std::string& str) {
  if (str == "Stay" || str == "stay") return EncodeAction(MovementAction::Stay);
  if (str == "Up" || str == "up") return EncodeAction(MovementAction::Up);
  if (str == "Down" || str == "down") return EncodeAction(MovementAction::Down);
  if (str == "Left" || str == "left") return EncodeAction(MovementAction::Left);
  if (str == "Right" || str == "right") return EncodeAction(MovementAction::Right);
  return std::nullopt;
}

std::string InteractActionToString(InteractAction action) {
  switch (action) {
    case InteractAction::None:
      return "None";
    case InteractAction::Attack:
      return "Attack";
    default:
      return "Unknown";
  }
}

}  // namespace companions
