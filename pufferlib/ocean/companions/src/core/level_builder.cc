// Copyright 2024
// LevelBuilder implementation

#include "level_builder.h"

#include <algorithm>
#include <stdexcept>

namespace companions {

LevelBuilder::LevelBuilder(Grid& grid) : grid_(grid) {}

void LevelBuilder::Rectangle(CellKind kind, int top, int left, int width,
                              int height, bool fill) {
  if (fill) {
    Fill(kind, top, left, width, height);
  } else {
    Border(kind, top, left, width, height);
  }
}

void LevelBuilder::Line(CellKind kind, Position start, Position end) {
  // Horizontal line
  if (start.row == end.row) {
    int min_col = std::min(start.col, end.col);
    int max_col = std::max(start.col, end.col);
    for (int c = min_col; c <= max_col; ++c) {
      grid_.SetCell(start.row, c, kind);
    }
  }
  // Vertical line
  else if (start.col == end.col) {
    int min_row = std::min(start.row, end.row);
    int max_row = std::max(start.row, end.row);
    for (int r = min_row; r <= max_row; ++r) {
      grid_.SetCell(r, start.col, kind);
    }
  }
  // Diagonal or other - error
  else {
    throw std::invalid_argument("Points are not in a straight line");
  }
}

void LevelBuilder::Set(CellKind kind, Position pos) {
  grid_.SetCell(pos, kind);
}

void LevelBuilder::Set(CellKind kind, int row, int col) {
  grid_.SetCell(row, col, kind);
}

void LevelBuilder::Fill(CellKind kind, int top, int left, int width,
                         int height) {
  for (int r = top; r < top + height; ++r) {
    for (int c = left; c < left + width; ++c) {
      grid_.SetCell(r, c, kind);
    }
  }
}

void LevelBuilder::Fill(CellKind kind) {
  Fill(kind, 0, 0, grid_.GetRows(), grid_.GetCols());
}

void LevelBuilder::Fill(CellKind kind, CellOrigin origin, int top, int left,
                         int width, int height) {
  for (int r = top; r < top + height; ++r) {
    for (int c = left; c < left + width; ++c) {
      grid_.SetCell(r, c, kind, origin);
    }
  }
}

void LevelBuilder::Border(CellKind kind, int top, int left, int width,
                           int height) {
  // Top and bottom edges
  for (int c = left; c < left + width; ++c) {
    grid_.SetCell(top, c, kind);
    grid_.SetCell(top + height - 1, c, kind);
  }
  // Left and right edges
  for (int r = top; r < top + height; ++r) {
    grid_.SetCell(r, left, kind);
    grid_.SetCell(r, left + width - 1, kind);
  }
}

void LevelBuilder::Border(CellKind kind) {
  Border(kind, 0, 0, grid_.GetRows(), grid_.GetCols());
}

}  // namespace companions
