// Copyright 2024
// LevelBuilder for The Companions game

#ifndef COMPANIONS_CORE_LEVEL_BUILDER_H_
#define COMPANIONS_CORE_LEVEL_BUILDER_H_

#include "cell.h"
#include "grid.h"
#include "types.h"

namespace companions {

// =============================================================================
// LevelBuilder - utilities for constructing levels
// =============================================================================
class LevelBuilder {
 public:
  explicit LevelBuilder(Grid& grid);

  // Rectangle: outline or filled
  void Rectangle(CellKind kind, int top, int left, int width, int height,
                 bool fill = false);

  // Line (horizontal or vertical)
  void Line(CellKind kind, Position start, Position end);

  // Single cell
  void Set(CellKind kind, Position pos);
  void Set(CellKind kind, int row, int col);

  // Fill a rectangular region (or entire grid if no bounds specified)
  void Fill(CellKind kind, int top, int left, int width, int height);
  void Fill(CellKind kind);
  void Fill(CellKind kind, CellOrigin origin, int top, int left, int width, int height);

  // Border around a rectangular region (or entire grid if no bounds specified)
  void Border(CellKind kind, int top, int left, int width, int height);
  void Border(CellKind kind);

 private:
  Grid& grid_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_LEVEL_BUILDER_H_
