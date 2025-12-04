// Copyright 2024
// D4 symmetry transformations for grid environments

#ifndef COMPANIONS_CORE_D4_TRANSFORM_H_
#define COMPANIONS_CORE_D4_TRANSFORM_H_

#include <memory>
#include <utility>
#include <vector>

#include "types.h"

namespace companions {

class Grid;

// =============================================================================
// D4 Transform - the 8 symmetries of a square (dihedral group D4)
// =============================================================================
enum class D4Transform : int {
  Identity = 0,  // No change
  Rot90 = 1,     // Rotate 90° clockwise
  Rot180 = 2,    // Rotate 180°
  Rot270 = 3,    // Rotate 270° clockwise (= 90° counter-clockwise)
  FlipH = 4,     // Reflect horizontal (flip left-right)
  FlipV = 5,     // Reflect vertical (flip top-bottom)
  FlipD = 6,     // Reflect along main diagonal (transpose)
  FlipA = 7,     // Reflect along anti-diagonal
};

constexpr int kNumD4Transforms = 8;

// =============================================================================
// Validation
// =============================================================================

// Check if transform ID is valid (0-7)
bool IsValidD4Transform(int transform_id);

// Convert int to D4Transform (asserts valid)
D4Transform ToD4Transform(int transform_id);

// =============================================================================
// Dimension queries
// =============================================================================

// Does this transform swap rows and cols?
// True for: Rot90, Rot270, FlipD, FlipA
bool SwapsDimensions(D4Transform transform);

// Get dimensions after transformation
// For dimension-swapping transforms on rectangular grids, rows and cols swap
std::pair<int, int> GetTransformedDimensions(int rows, int cols,
                                             D4Transform transform);

// =============================================================================
// Position transformations
// =============================================================================

// Transform a single position
// rows/cols are the ORIGINAL grid dimensions (before transform)
Position TransformPosition(Position pos, int rows, int cols,
                           D4Transform transform);

// Transform a vector of positions
std::vector<Position> TransformPositions(const std::vector<Position>& positions,
                                         int rows, int cols,
                                         D4Transform transform);

// =============================================================================
// Grid transformation
// =============================================================================

// Transform a grid - returns a new grid with transformed cells
// Cell kinds and origins are preserved, only positions change
std::unique_ptr<Grid> TransformGrid(const Grid& grid, D4Transform transform);

// =============================================================================
// Utility
// =============================================================================

// Get string name for transform
const char* D4TransformToString(D4Transform transform);

}  // namespace companions

#endif  // COMPANIONS_CORE_D4_TRANSFORM_H_
