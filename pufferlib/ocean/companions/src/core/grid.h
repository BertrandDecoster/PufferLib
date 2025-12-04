// Copyright 2024
// Grid class for The Companions game

#ifndef COMPANIONS_CORE_GRID_H_
#define COMPANIONS_CORE_GRID_H_

#include <string>
#include <vector>

#include "cell.h"
#include "types.h"

namespace companions {

// =============================================================================
// Grid - owns all cells as value types (cache-friendly)
// =============================================================================
class Grid {
 public:
  explicit Grid(int rows = kDefaultGridSize, int cols = kDefaultGridSize);
  ~Grid() = default;

  // Copyable
  Grid(const Grid&) = default;
  Grid& operator=(const Grid&) = default;
  Grid(Grid&&) = default;
  Grid& operator=(Grid&&) = default;

  // Cell access
  void SetCell(Position pos, CellKind kind);
  void SetCell(int row, int col, CellKind kind);
  void SetCell(Position pos, CellKind kind, CellOrigin origin);
  void SetCell(int row, int col, CellKind kind, CellOrigin origin);
  const Cell& GetCell(Position pos) const;
  const Cell& GetCell(int row, int col) const;
  Cell& GetMutableCell(Position pos);
  Cell& GetMutableCell(int row, int col);
  CellKind GetCellKind(Position pos) const;
  CellKind GetCellKind(int row, int col) const;

  // Queries
  bool IsInBounds(Position pos) const;
  bool IsInBounds(int row, int col) const;
  bool IsWalkable(Position pos) const;
  bool IsPathable(Position pos) const;

  // Size
  int GetRows() const { return rows_; }
  int GetCols() const { return cols_; }

  // Find cells
  std::vector<Position> FindCellsOfKind(CellKind kind) const;
  std::vector<Position> FindWalkableCells() const;

  // Neighbor queries
  std::vector<Position> GetNeighbors(Position pos) const;          // All 4 adjacent
  std::vector<Position> GetWalkableNeighbors(Position pos) const;  // Only walkable

  // String representation
  std::string ToString() const;

 private:
  int rows_;
  int cols_;
  std::vector<std::vector<Cell>> cells_;

  // Out-of-bounds sentinel cell
  static Cell out_of_bounds_cell_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_GRID_H_
