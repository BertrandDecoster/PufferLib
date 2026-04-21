// Copyright 2024
// Cell system implementation

#include "cell.h"

#include <stdexcept>
#include <unordered_map>

namespace companions {

// =============================================================================
// Cell Properties Map
// =============================================================================
namespace {

// Static property map: CellKind -> CellProperties
const std::unordered_map<CellKind, CellProperties>
    kCellPropertiesMap = {
        //                    walkable, pathable, char, display, color
        {CellKind::Floor,    {true,     true,     '.', " .",    0}},
        {CellKind::Wall,     {false,    false,    '#', "##",    90}},  // Gray
        {CellKind::Hazard,   {false,    true,     '~', "~~",    91}},  // Red
        {CellKind::HealArea, {true,     true,     '+', " +",    92}},  // Green
};

// Default properties for unknown cell kinds
const CellProperties kDefaultProperties = {false, false, '?', "??", 0};

}  // namespace

// =============================================================================
// CellProperties
// =============================================================================
const CellProperties& CellProperties::Get(CellKind kind) {
  auto it = kCellPropertiesMap.find(kind);
  if (it != kCellPropertiesMap.end()) {
    return it->second;
  }
  return kDefaultProperties;
}

// =============================================================================
// Cell
// =============================================================================
Cell::Cell() : pos_{-1, -1}, kind_(CellKind::Floor) {}

Cell::Cell(Position pos, CellKind kind) : pos_(pos), kind_(kind) {}

bool Cell::IsWalkable() const {
  return CellProperties::Get(kind_).walkable;
}

bool Cell::IsPathable() const {
  return CellProperties::Get(kind_).pathable;
}

char Cell::GetChar() const {
  return CellProperties::Get(kind_).ascii_char;
}

const char* Cell::GetDisplay() const {
  // For Floor cells, distinguish between Room and Corridor origins
  if (kind_ == CellKind::Floor) {
    switch (origin_) {
      case CellOrigin::Room:
        return "  ";  // Room floors: empty space
      case CellOrigin::Corridor:
        return " .";  // Corridor floors: dot
      default:
        return " .";  // Default floors: dot
    }
  }
  return CellProperties::Get(kind_).display;
}

int Cell::GetColorCode() const {
  return CellProperties::Get(kind_).color_code;
}

// =============================================================================
// Utility
// =============================================================================
std::string CellKindToString(CellKind kind) {
  switch (kind) {
    case CellKind::Floor:
      return "Floor";
    case CellKind::Wall:
      return "Wall";
    case CellKind::Hazard:
      return "Hazard";
    case CellKind::HealArea:
      return "HealArea";
    default:
      return "Unknown";
  }
}

std::string CellOriginToString(CellOrigin origin) {
  switch (origin) {
    case CellOrigin::Default:
      return "Default";
    case CellOrigin::Room:
      return "Room";
    case CellOrigin::Corridor:
      return "Corridor";
    case CellOrigin::Obstacle:
      return "Obstacle";
    default:
      return "Unknown";
  }
}

}  // namespace companions
