// Copyright 2024
// Cell system for The Companions game (data-driven approach)

#ifndef COMPANIONS_CORE_CELL_H_
#define COMPANIONS_CORE_CELL_H_

#include <string>

#include "types.h"

namespace companions {

// =============================================================================
// Cell Kinds - all cell types in the game
// =============================================================================
enum class CellKind {
  Floor,      // Normal walkable
  Wall,       // Blocked (not walkable, not pathable)
  Hazard,     // Pathable but not walkable (lava, pit)
  HealArea,   // Heals actors standing on it
  // Task-semantic roles (SynchroGoal, AggroTarget, etc.) live in
  // AnnotationStore / snapshot.annotations, not on Cell.
};

// =============================================================================
// Cell Origin - how the cell was created (display only, no game logic impact)
// =============================================================================
enum class CellOrigin {
  Default,    // Standard cell (perimeter wall or floor)
  Room,       // Floor carved as part of a room
  Corridor,   // Floor carved as part of a corridor
  Obstacle,   // Wall added as a scattered obstacle
};

// =============================================================================
// Cell Properties - defines behavior for each cell kind
// =============================================================================
struct CellProperties {
  bool walkable;        // Can actor willingly walk here?
  bool pathable;        // Can actor be pushed/dash through here?
  char ascii_char;      // Single char for simple display
  const char* display;  // 2-char display for viz (e.g., "██", " .", " S")
  int color_code;       // ANSI color code (0 = default)

  // Static lookup by CellKind
  static const CellProperties& Get(CellKind kind);
};

// =============================================================================
// Cell - single cell in the grid (value type, no inheritance)
// =============================================================================
class Cell {
 public:
  // Default constructor for container use
  Cell();

  // Standard constructor
  Cell(Position pos, CellKind kind = CellKind::Floor);

  // Copyable
  Cell(const Cell&) = default;
  Cell& operator=(const Cell&) = default;
  Cell(Cell&&) = default;
  Cell& operator=(Cell&&) = default;

  // Accessors
  Position GetPosition() const { return pos_; }
  CellKind GetKind() const { return kind_; }
  void SetKind(CellKind kind) { kind_ = kind; }
  CellOrigin GetOrigin() const { return origin_; }
  void SetOrigin(CellOrigin origin) { origin_ = origin; }

  // Property accessors (delegate to CellProperties)
  bool IsWalkable() const;
  bool IsPathable() const;
  char GetChar() const;
  const char* GetDisplay() const;
  int GetColorCode() const;

 private:
  Position pos_;
  CellKind kind_ = CellKind::Floor;
  CellOrigin origin_ = CellOrigin::Default;
};

// =============================================================================
// Utility
// =============================================================================
std::string CellKindToString(CellKind kind);
std::string CellOriginToString(CellOrigin origin);

}  // namespace companions

#endif  // COMPANIONS_CORE_CELL_H_
