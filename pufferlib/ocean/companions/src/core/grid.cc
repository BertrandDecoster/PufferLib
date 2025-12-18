// Copyright 2024
// Grid implementation

#include "grid.h"

#include <cassert>
#include <sstream>

namespace companions {

// Static out-of-bounds sentinel (Wall, not walkable)
Cell Grid::out_of_bounds_cell_ = Cell({-1, -1}, CellKind::Wall);

Grid::Grid(int rows, int cols) : rows_(rows), cols_(cols) {
  cells_.resize(rows_);
  for (int r = 0; r < rows_; ++r) {
    cells_[r].reserve(cols_);
    for (int c = 0; c < cols_; ++c) {
      cells_[r].emplace_back(Position{r, c}, CellKind::Floor);
    }
  }
}

void Grid::SetCell(Position pos, CellKind kind) {
  SetCell(pos.row, pos.col, kind);
}

void Grid::SetCell(int row, int col, CellKind kind) {
  if (!IsInBounds(row, col)) return;
  cells_[row][col].SetKind(kind);
}

void Grid::SetCell(Position pos, CellKind kind, CellOrigin origin) {
  SetCell(pos.row, pos.col, kind, origin);
}

void Grid::SetCell(int row, int col, CellKind kind, CellOrigin origin) {
  if (!IsInBounds(row, col)) return;
  cells_[row][col].SetKind(kind);
  cells_[row][col].SetOrigin(origin);
}

const Cell& Grid::GetCell(Position pos) const {
  return GetCell(pos.row, pos.col);
}

const Cell& Grid::GetCell(int row, int col) const {
  if (!IsInBounds(row, col)) return out_of_bounds_cell_;
  return cells_[row][col];
}

Cell& Grid::GetMutableCell(Position pos) {
  return GetMutableCell(pos.row, pos.col);
}

Cell& Grid::GetMutableCell(int row, int col) {
  assert(IsInBounds(row, col) && "GetMutableCell called with out-of-bounds position");
  return cells_[row][col];
}

CellKind Grid::GetCellKind(Position pos) const {
  return GetCell(pos).GetKind();
}

CellKind Grid::GetCellKind(int row, int col) const {
  return GetCell(row, col).GetKind();
}

bool Grid::IsInBounds(Position pos) const {
  return IsInBounds(pos.row, pos.col);
}

bool Grid::IsInBounds(int row, int col) const {
  return row >= 0 && row < rows_ && col >= 0 && col < cols_;
}

bool Grid::IsWalkable(Position pos) const {
  return GetCell(pos).IsWalkable();
}

bool Grid::IsPathable(Position pos) const {
  return GetCell(pos).IsPathable();
}

std::vector<Position> Grid::FindCellsOfKind(CellKind kind) const {
  std::vector<Position> result;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      if (cells_[r][c].GetKind() == kind) {
        result.push_back({r, c});
      }
    }
  }
  return result;
}

std::vector<Position> Grid::FindWalkableCells() const {
  std::vector<Position> result;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      if (cells_[r][c].IsWalkable()) {
        result.push_back({r, c});
      }
    }
  }
  return result;
}

std::vector<Position> Grid::GetNeighbors(Position pos) const {
  std::vector<Position> neighbors;
  neighbors.reserve(4);

  // Order: Up, Down, Left, Right (matches MovementAction enum order)
  const Position offsets[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (const auto& offset : offsets) {
    Position neighbor = {pos.row + offset.row, pos.col + offset.col};
    if (IsInBounds(neighbor)) {
      neighbors.push_back(neighbor);
    }
  }
  return neighbors;
}

std::vector<Position> Grid::GetWalkableNeighbors(Position pos) const {
  std::vector<Position> neighbors;
  neighbors.reserve(4);

  const Position offsets[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (const auto& offset : offsets) {
    Position neighbor = {pos.row + offset.row, pos.col + offset.col};
    if (IsInBounds(neighbor) && IsWalkable(neighbor)) {
      neighbors.push_back(neighbor);
    }
  }
  return neighbors;
}

std::string Grid::ToString() const {
  std::ostringstream ss;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      ss << cells_[r][c].GetChar();
    }
    ss << '\n';
  }
  return ss.str();
}

std::vector<std::pair<CellKind, CellOrigin>> Grid::GetAllCellData() const {
  std::vector<std::pair<CellKind, CellOrigin>> data;
  data.reserve(rows_ * cols_);
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      data.emplace_back(cells_[r][c].GetKind(), cells_[r][c].GetOrigin());
    }
  }
  return data;
}

void Grid::SetAllCellData(const std::vector<std::pair<CellKind, CellOrigin>>& data) {
  if (static_cast<int>(data.size()) != rows_ * cols_) {
    return;  // Size mismatch, do nothing
  }
  int idx = 0;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      cells_[r][c].SetKind(data[idx].first);
      cells_[r][c].SetOrigin(data[idx].second);
      ++idx;
    }
  }
}

}  // namespace companions
