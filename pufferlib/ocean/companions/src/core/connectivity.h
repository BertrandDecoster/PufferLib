// Copyright 2024
// Connectivity algorithms for map generation

#ifndef COMPANIONS_CORE_CONNECTIVITY_H_
#define COMPANIONS_CORE_CONNECTIVITY_H_

#include <vector>

#include "generation_context.h"
#include "types.h"

namespace companions {

// Check if all walkable cells in the grid are connected.
// Uses flood-fill from the first walkable cell.
bool IsConnected(const Grid& grid, int rows, int cols);

// Flood fill from start position, marking visited cells.
// Returns the number of cells reached.
int FloodFill(const Grid& grid, Position start, int rows, int cols,
              std::vector<std::vector<bool>>& visited);

// Scatter obstacles on floor cells while maintaining connectivity.
// Places walls at random floor positions, reverting if placement breaks connectivity.
void ScatterObstacles(GenerationContext& ctx, int density_percent);

}  // namespace companions

#endif  // COMPANIONS_CORE_CONNECTIVITY_H_
