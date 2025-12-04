// Copyright 2024
// D4 symmetry transformations implementation

#include "d4_transform.h"

#include <cassert>

#include "grid.h"

namespace companions {

// =============================================================================
// Validation
// =============================================================================

bool IsValidD4Transform(int transform_id) {
  return transform_id >= 0 && transform_id < kNumD4Transforms;
}

D4Transform ToD4Transform(int transform_id) {
  assert(IsValidD4Transform(transform_id));
  return static_cast<D4Transform>(transform_id);
}

// =============================================================================
// Dimension queries
// =============================================================================

bool SwapsDimensions(D4Transform transform) {
  switch (transform) {
    case D4Transform::Rot90:
    case D4Transform::Rot270:
    case D4Transform::FlipD:
    case D4Transform::FlipA:
      return true;
    default:
      return false;
  }
}

std::pair<int, int> GetTransformedDimensions(int rows, int cols,
                                             D4Transform transform) {
  if (SwapsDimensions(transform)) {
    return {cols, rows};
  }
  return {rows, cols};
}

// =============================================================================
// Position transformations
// =============================================================================

Position TransformPosition(Position pos, int rows, int cols,
                           D4Transform transform) {
  int r = pos.row;
  int c = pos.col;

  switch (transform) {
    case D4Transform::Identity:
      return {r, c};

    case D4Transform::Rot90:
      // (r, c) -> (c, rows-1-r)
      return {c, rows - 1 - r};

    case D4Transform::Rot180:
      // (r, c) -> (rows-1-r, cols-1-c)
      return {rows - 1 - r, cols - 1 - c};

    case D4Transform::Rot270:
      // (r, c) -> (cols-1-c, r)
      return {cols - 1 - c, r};

    case D4Transform::FlipH:
      // Horizontal flip: (r, c) -> (r, cols-1-c)
      return {r, cols - 1 - c};

    case D4Transform::FlipV:
      // Vertical flip: (r, c) -> (rows-1-r, c)
      return {rows - 1 - r, c};

    case D4Transform::FlipD:
      // Main diagonal (transpose): (r, c) -> (c, r)
      return {c, r};

    case D4Transform::FlipA:
      // Anti-diagonal: (r, c) -> (cols-1-c, rows-1-r)
      return {cols - 1 - c, rows - 1 - r};
  }

  // Should never reach here
  return pos;
}

std::vector<Position> TransformPositions(const std::vector<Position>& positions,
                                         int rows, int cols,
                                         D4Transform transform) {
  std::vector<Position> result;
  result.reserve(positions.size());

  for (const auto& pos : positions) {
    result.push_back(TransformPosition(pos, rows, cols, transform));
  }

  return result;
}

// =============================================================================
// Grid transformation
// =============================================================================

std::unique_ptr<Grid> TransformGrid(const Grid& grid, D4Transform transform) {
  if (transform == D4Transform::Identity) {
    // Optimization: just copy the grid
    return std::make_unique<Grid>(grid);
  }

  int src_rows = grid.GetRows();
  int src_cols = grid.GetCols();
  auto [dst_rows, dst_cols] =
      GetTransformedDimensions(src_rows, src_cols, transform);

  auto result = std::make_unique<Grid>(dst_rows, dst_cols);

  // Transform each cell
  for (int r = 0; r < src_rows; ++r) {
    for (int c = 0; c < src_cols; ++c) {
      Position src_pos{r, c};
      Position dst_pos = TransformPosition(src_pos, src_rows, src_cols, transform);

      const Cell& cell = grid.GetCell(src_pos);
      result->SetCell(dst_pos, cell.GetKind(), cell.GetOrigin());
    }
  }

  return result;
}

// =============================================================================
// Utility
// =============================================================================

const char* D4TransformToString(D4Transform transform) {
  switch (transform) {
    case D4Transform::Identity:
      return "Identity";
    case D4Transform::Rot90:
      return "Rot90";
    case D4Transform::Rot180:
      return "Rot180";
    case D4Transform::Rot270:
      return "Rot270";
    case D4Transform::FlipH:
      return "FlipH";
    case D4Transform::FlipV:
      return "FlipV";
    case D4Transform::FlipD:
      return "FlipD";
    case D4Transform::FlipA:
      return "FlipA";
  }
  return "Unknown";
}

}  // namespace companions
