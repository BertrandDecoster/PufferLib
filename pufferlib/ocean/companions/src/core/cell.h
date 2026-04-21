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
  Synchro,    // Goal cell for SynchroEnv
  HealArea,   // Heals actors standing on it
  Target,     // Goal cell for AggroEnv
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

  // Base kind: the "physical" terrain the cell represents. Remains stable
  // when a lens stamps an objective kind (Synchro, Target) over the cell.
  // Stamp saves the current kind into base_kind (if not already stamped),
  // then sets kind. Unstamp restores kind from base_kind.
  CellKind GetBaseKind() const { return base_kind_; }
  void SetBaseKind(CellKind kind) { base_kind_ = kind; }

  // Stamp an overlay kind onto this cell. First stamp preserves current kind
  // in base_kind; subsequent stamps while already-stamped just swap overlays.
  void StampOverlay(CellKind overlay_kind, bool is_stamp_overlay) {
    if (is_stamp_overlay && !IsStamped()) {
      base_kind_ = kind_;
      stamped_ = true;
    }
    kind_ = overlay_kind;
  }

  // Remove any overlay, restoring base_kind as the current kind.
  void RemoveOverlay() {
    if (stamped_) {
      kind_ = base_kind_;
      stamped_ = false;
    }
  }

  bool IsStamped() const { return stamped_; }

  // Property accessors (delegate to CellProperties)
  bool IsWalkable() const;
  bool IsPathable() const;
  char GetChar() const;
  const char* GetDisplay() const;
  int GetColorCode() const;

 private:
  Position pos_;
  CellKind kind_ = CellKind::Floor;
  CellKind base_kind_ = CellKind::Floor;   // Physical terrain (restored on unstamp)
  CellOrigin origin_ = CellOrigin::Default;
  bool stamped_ = false;                    // True while an overlay is active
};

// =============================================================================
// Utility
// =============================================================================
std::string CellKindToString(CellKind kind);
std::string CellOriginToString(CellOrigin origin);

}  // namespace companions

#endif  // COMPANIONS_CORE_CELL_H_
