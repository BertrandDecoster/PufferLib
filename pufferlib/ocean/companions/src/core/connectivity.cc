// Copyright 2024
// Connectivity algorithms implementation

#include "connectivity.h"

#include <queue>

#include "grid.h"
#include "map_generator.h"  // For MapConfig
#include "pcg32.h"

namespace companions {

bool IsConnected(const Grid& grid, int rows, int cols) {
  // Find first walkable cell
  Position start{-1, -1};
  int total_walkable = 0;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (grid.IsWalkable({r, c})) {
        total_walkable++;
        if (!start.IsValid()) {
          start = {r, c};
        }
      }
    }
  }

  if (total_walkable == 0) return true;  // No walkable cells is technically connected

  // Flood fill from start
  std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
  int reached = FloodFill(grid, start, rows, cols, visited);

  return reached == total_walkable;
}

int FloodFill(const Grid& grid, Position start, int rows, int cols,
              std::vector<std::vector<bool>>& visited) {
  if (!start.IsValid() || !grid.IsWalkable(start)) return 0;

  std::queue<Position> queue;
  queue.push(start);
  visited[start.row][start.col] = true;
  int count = 1;

  const int dr[] = {-1, 1, 0, 0};
  const int dc[] = {0, 0, -1, 1};

  while (!queue.empty()) {
    Position curr = queue.front();
    queue.pop();

    for (int i = 0; i < 4; ++i) {
      Position next{curr.row + dr[i], curr.col + dc[i]};
      if (grid.IsInBounds(next) && grid.IsWalkable(next) &&
          !visited[next.row][next.col]) {
        visited[next.row][next.col] = true;
        queue.push(next);
        count++;
      }
    }
  }

  return count;
}

void ScatterObstacles(GenerationContext& ctx, int density_percent) {
  // Get all floor cells (excluding perimeter)
  std::vector<Position> floor_cells;
  for (int r = 2; r < ctx.config.rows - 2; ++r) {
    for (int c = 2; c < ctx.config.cols - 2; ++c) {
      if (ctx.grid.GetCellKind(r, c) == CellKind::Floor) {
        floor_cells.push_back({r, c});
      }
    }
  }

  if (floor_cells.empty()) return;

  // Calculate number of obstacles to place
  int num_obstacles = static_cast<int>(floor_cells.size()) * density_percent / 100;

  // Shuffle and pick cells for obstacles (portable_shuffle for cross-platform determinism)
  portable_shuffle(floor_cells.begin(), floor_cells.end(), ctx.rng);

  int placed = 0;
  for (const Position& pos : floor_cells) {
    if (placed >= num_obstacles) break;

    // Remember original origin to restore if we need to undo
    CellOrigin original_origin = ctx.grid.GetCell(pos).GetOrigin();

    // Place wall with obstacle origin
    ctx.grid.SetCell(pos, CellKind::Wall, CellOrigin::Obstacle);
    placed++;

    // Check connectivity after each placement
    if (!IsConnected(ctx.grid, ctx.config.rows, ctx.config.cols)) {
      // Undo this placement - it broke connectivity
      ctx.grid.SetCell(pos, CellKind::Floor, original_origin);
      placed--;
    }
  }
}

}  // namespace companions
