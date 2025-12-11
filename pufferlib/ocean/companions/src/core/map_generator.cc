// Copyright 2024
// MapGenerator implementation

#include "map_generator.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>

#include "level_builder.h"

// Debug flag - set to 1 to enable verbose output
#define MAP_GENERATOR_DEBUG 0

#if MAP_GENERATOR_DEBUG
#define MAP_DEBUG(x) std::cerr << "[MapGen] " << x << std::endl
#else
#define MAP_DEBUG(x)
#endif

namespace companions {

// =============================================================================
// Static methods
// =============================================================================

std::unique_ptr<Grid> MapGenerator::Generate(const MapConfig& config) {
  MapGenerator generator(config);
  return generator.GenerateInternal();
}

MapConfig MapGenerator::DefaultConfig(int rows, int cols, int complexity,
                                      uint64_t seed) {
  MapConfig config;
  config.rows = rows;
  config.cols = cols;
  config.complexity = std::max(0, std::min(5, complexity));  // Clamp to 0-5
  config.seed = seed;
  // Leave bounds at defaults (max = INT_MAX means use complexity-derived)
  return config;
}

// =============================================================================
// Constructor
// =============================================================================

MapGenerator::MapGenerator(const MapConfig& config)
    : config_(config), rng_(config.seed) {}

// =============================================================================
// Main generation
// =============================================================================

std::unique_ptr<Grid> MapGenerator::GenerateInternal() {
  grid_ = std::make_unique<Grid>(config_.rows, config_.cols);

  if (config_.complexity == 0) {
    // Complexity 0: empty rectangle (current SynchroEnv behavior)
    GenerateEmptyRectangle();
  } else if (config_.complexity == 1) {
    // Complexity 1: empty rectangle with scattered obstacles
    GenerateWithObstacles();
  } else if (config_.complexity == 2) {
    // Complexity 2: two rooms with dual corridors
    GenerateWithRooms();
  } else {
    // Complexity 3-5: disabled for now
    throw std::runtime_error(
        "MapGenerator: complexity >= 3 is not supported. Use complexity 0, 1, or 2.");
  }

  return std::move(grid_);
}

void MapGenerator::GenerateEmptyRectangle() {
  LevelBuilder builder(*grid_);

  // Fill with floor (top, left, width, height)
  builder.Fill(CellKind::Floor, 0, 0, config_.cols, config_.rows);

  // Add walls around perimeter (top, left, width, height)
  builder.Border(CellKind::Wall, 0, 0, config_.cols, config_.rows);
}

void MapGenerator::GenerateWithObstacles() {
  // Start with empty rectangle
  GenerateEmptyRectangle();

  // Scatter obstacles based on complexity
  int density = GetObstacleDensity();
  if (density > 0) {
    ScatterObstacles(density);
  }
}

void MapGenerator::GenerateWithRooms() {
  // Complexity 2: Two rooms with guaranteed separation and dual corridors
  MAP_DEBUG("=== GenerateWithRooms() START ===");
  MAP_DEBUG("Grid size: " << config_.rows << "x" << config_.cols);

  LevelBuilder builder(*grid_);

  // Start with all walls
  builder.Fill(CellKind::Wall);

  // Check minimum grid size (7x7)
  if (config_.rows < 7 || config_.cols < 7) {
    MAP_DEBUG("Grid too small, falling back to obstacles");
    // Fallback to simple generation for too-small grids
    GenerateWithObstacles();
    return;
  }

  // Step 1: Divide grid into two quadrants
  SplitDirection split_dir;
  auto [quad1, quad2] = DivideIntoQuadrants(&split_dir);

  MAP_DEBUG("Split direction: " << (split_dir == SplitDirection::Horizontal ? "HORIZONTAL" : "VERTICAL"));
  MAP_DEBUG("Quadrant1: top=" << quad1.top << " left=" << quad1.left
            << " h=" << quad1.height << " w=" << quad1.width);
  MAP_DEBUG("Quadrant2: top=" << quad2.top << " left=" << quad2.left
            << " h=" << quad2.height << " w=" << quad2.width);

  // Step 2: Generate rooms in each quadrant
  // First room: L-shape carved toward other room (if possible)
  // Second room: L-shape carved toward other room (opposite direction)
  RoomShape room1 = GenerateRoomInQuadrant(quad1, true);
  RoomShape room2 = GenerateRoomInQuadrant(quad2, false);

  MAP_DEBUG("Room1: " << room1.cells.size() << " cells, bounds top=" << room1.top
            << " left=" << room1.left << " h=" << room1.height << " w=" << room1.width);
  MAP_DEBUG("Room2: " << room2.cells.size() << " cells, bounds top=" << room2.top
            << " left=" << room2.left << " h=" << room2.height << " w=" << room2.width);

  // Step 3: Carve rooms
  CarveRoomShape(room1);
  CarveRoomShape(room2);

  // Step 4: Generate doors for each room
  int corridor_width = GetCorridorWidth();
  MAP_DEBUG("Corridor width: " << corridor_width);

  std::vector<Door> doors1 =
      GenerateDoorsForRoom(room1, split_dir, true, corridor_width);
  std::vector<Door> doors2 =
      GenerateDoorsForRoom(room2, split_dir, false, corridor_width);

  MAP_DEBUG("Room1 has " << doors1.size() << " possible doors");
  MAP_DEBUG("Room2 has " << doors2.size() << " possible doors");

  for (size_t i = 0; i < doors1.size() && i < 3; ++i) {
    MAP_DEBUG("  Door1[" << i << "] in=(" << doors1[i].in_cells[0].row << ","
              << doors1[i].in_cells[0].col << ") out=(" << doors1[i].out_cells[0].row
              << "," << doors1[i].out_cells[0].col << ")");
  }
  for (size_t i = 0; i < doors2.size() && i < 3; ++i) {
    MAP_DEBUG("  Door2[" << i << "] in=(" << doors2[i].in_cells[0].row << ","
              << doors2[i].in_cells[0].col << ") out=(" << doors2[i].out_cells[0].row
              << "," << doors2[i].out_cells[0].col << ")");
  }

  // Step 5: Generate all valid corridor candidates
  std::vector<CorridorCandidate> candidates =
      GenerateCorridorCandidates(doors1, doors2, corridor_width);

  MAP_DEBUG("Generated " << candidates.size() << " corridor candidates");

  // Filter to valid corridors only
  std::vector<CorridorCandidate> valid_candidates;
  for (const auto& c : candidates) {
    if (ValidateCorridor(c, room1, room2)) {
      valid_candidates.push_back(c);
    }
  }

  MAP_DEBUG("Valid corridors: " << valid_candidates.size() << " (rejected "
            << (candidates.size() - valid_candidates.size()) << ")");

  // Step 6: Select corridor pair (or single if no valid pair)
  std::vector<CorridorCandidate> selected;
  if (!valid_candidates.empty()) {
    selected = SelectCorridorPair(valid_candidates);
  }

  MAP_DEBUG("Selected " << selected.size() << " corridors");
  for (size_t i = 0; i < selected.size(); ++i) {
    MAP_DEBUG("  Corridor " << i << ": " << selected[i].cells.size() << " cells");
  }

  // Step 7: Carve corridors
  for (const auto& corridor : selected) {
    CarveCorridor(corridor);
  }

  // Final connectivity check - fallback if broken
  if (!IsConnected()) {
    MAP_DEBUG("Map not connected, falling back to obstacles!");
    GenerateWithObstacles();
  } else {
    MAP_DEBUG("=== GenerateWithRooms() SUCCESS ===");
  }
}

// =============================================================================
// Complexity 2: Helper functions
// =============================================================================

std::pair<MapGenerator::Quadrant, MapGenerator::Quadrant>
MapGenerator::DivideIntoQuadrants(SplitDirection* out_dir) {
  // Interior dimensions (excluding 1-cell border)
  int interior_rows = config_.rows - 2;
  int interior_cols = config_.cols - 2;

  // Choose split direction based on aspect ratio
  SplitDirection dir;
  if (interior_cols > interior_rows) {
    dir = SplitDirection::Vertical;  // Split vertically (left/right quadrants)
  } else if (interior_rows > interior_cols) {
    dir = SplitDirection::Horizontal;  // Split horizontally (top/bottom)
  } else {
    // Square: random choice
    dir = (rng_() % 2 == 0) ? SplitDirection::Horizontal
                            : SplitDirection::Vertical;
  }
  *out_dir = dir;

  Quadrant q1, q2;

  if (dir == SplitDirection::Horizontal) {
    // Horizontal split: top/bottom quadrants
    // Separator row: (interior_rows + 1) / 2 from interior start
    int separator_row = 1 + (interior_rows + 1) / 2;  // In grid coordinates

    // Quadrant 1 (top): rows 1 to separator_row - 1
    q1.top = 1;
    q1.left = 1;
    q1.height = separator_row - 1;
    q1.width = interior_cols;

    // Quadrant 2 (bottom): rows separator_row + 1 to rows - 2
    q2.top = separator_row + 1;
    q2.left = 1;
    q2.height = config_.rows - 2 - separator_row;
    q2.width = interior_cols;
  } else {
    // Vertical split: left/right quadrants
    int separator_col = 1 + (interior_cols + 1) / 2;  // In grid coordinates

    // Quadrant 1 (left): cols 1 to separator_col - 1
    q1.top = 1;
    q1.left = 1;
    q1.height = interior_rows;
    q1.width = separator_col - 1;

    // Quadrant 2 (right): cols separator_col + 1 to cols - 2
    q2.top = 1;
    q2.left = separator_col + 1;
    q2.height = interior_rows;
    q2.width = config_.cols - 2 - separator_col;
  }

  return {q1, q2};
}

RoomShape MapGenerator::GenerateRoomInQuadrant(const Quadrant& quad,
                                                bool prefer_l_toward_other) {
  RoomShape shape;

  // Room size: 60-100% of quadrant dimensions
  int min_width = std::max(2, quad.width * 60 / 100);
  int min_height = std::max(2, quad.height * 60 / 100);
  int max_width = quad.width;
  int max_height = quad.height;

  std::uniform_int_distribution<int> width_dist(min_width, max_width);
  std::uniform_int_distribution<int> height_dist(min_height, max_height);
  int room_width = width_dist(rng_);
  int room_height = height_dist(rng_);

  // Random position within quadrant
  int max_offset_col = quad.width - room_width;
  int max_offset_row = quad.height - room_height;

  int room_left = quad.left;
  int room_top = quad.top;
  if (max_offset_col > 0) {
    std::uniform_int_distribution<int> col_dist(0, max_offset_col);
    room_left += col_dist(rng_);
  }
  if (max_offset_row > 0) {
    std::uniform_int_distribution<int> row_dist(0, max_offset_row);
    room_top += row_dist(rng_);
  }

  shape.top = room_top;
  shape.left = room_left;
  shape.width = room_width;
  shape.height = room_height;

  // Start with full rectangle
  for (int r = room_top; r < room_top + room_height; ++r) {
    for (int c = room_left; c < room_left + room_width; ++c) {
      shape.cells.push_back({r, c});
    }
  }

  // 50% chance to make L-shaped if room is at least 4x4
  bool make_l_shape = (room_width >= 4 && room_height >= 4 && rng_() % 2 == 0);

  if (make_l_shape) {
    // Choose corner to carve based on prefer_l_toward_other
    // prefer_l_toward_other = true means we're the first quadrant
    // For horizontal split: first is top, carve bottom corner
    // For vertical split: first is left, carve right corner

    // Carve size: keep at least 2 cells in each arm
    int carve_width = std::min(room_width - 2,
                               std::max(1, room_width * 40 / 100));
    int carve_height = std::min(room_height - 2,
                                std::max(1, room_height * 40 / 100));

    // Choose which corner to carve (0=TL, 1=TR, 2=BL, 3=BR)
    int corner;
    if (prefer_l_toward_other) {
      // First room: carve corner facing away from other room
      // This creates an L that opens toward the separator
      corner = (rng_() % 2 == 0) ? 2 : 3;  // Bottom corners
    } else {
      // Second room: carve top corners
      corner = (rng_() % 2 == 0) ? 0 : 1;  // Top corners
    }

    // Remove cells from chosen corner
    int carve_top, carve_left;
    switch (corner) {
      case 0:  // Top-left
        carve_top = room_top;
        carve_left = room_left;
        break;
      case 1:  // Top-right
        carve_top = room_top;
        carve_left = room_left + room_width - carve_width;
        break;
      case 2:  // Bottom-left
        carve_top = room_top + room_height - carve_height;
        carve_left = room_left;
        break;
      case 3:  // Bottom-right
      default:
        carve_top = room_top + room_height - carve_height;
        carve_left = room_left + room_width - carve_width;
        break;
    }

    // Remove carved cells
    shape.cells.erase(
        std::remove_if(shape.cells.begin(), shape.cells.end(),
                       [=](const Position& p) {
                         return p.row >= carve_top &&
                                p.row < carve_top + carve_height &&
                                p.col >= carve_left &&
                                p.col < carve_left + carve_width;
                       }),
        shape.cells.end());
  }

  return shape;
}

void MapGenerator::CarveRoomShape(const RoomShape& shape) {
  for (const Position& p : shape.cells) {
    grid_->SetCell(p, CellKind::Floor, CellOrigin::Room);
  }
}

std::vector<Door> MapGenerator::GenerateDoorsForRoom(const RoomShape& shape,
                                                      SplitDirection split_dir,
                                                      bool is_first_room,
                                                      int door_width) {
  std::vector<Door> doors;

  // Create a set of room cells for quick lookup
  std::set<std::pair<int, int>> room_cells;
  for (const Position& p : shape.cells) {
    room_cells.insert({p.row, p.col});
  }

  // Find border cells and their outward neighbors
  // For first room with horizontal split: look at bottom edge
  // For first room with vertical split: look at right edge
  // For second room: opposite edges

  // Determine which direction doors should face
  // is_first_room = true: doors face toward second quadrant
  // is_first_room = false: doors face toward first quadrant

  std::vector<std::pair<Position, Position>> edge_pairs;  // (in_cell, out_cell)

  for (const Position& p : shape.cells) {
    // Check each orthogonal neighbor
    Position neighbors[4] = {{p.row - 1, p.col},   // Up
                             {p.row + 1, p.col},   // Down
                             {p.row, p.col - 1},   // Left
                             {p.row, p.col + 1}};  // Right

    for (int i = 0; i < 4; ++i) {
      Position neighbor = neighbors[i];

      // Skip if neighbor is in room
      if (room_cells.count({neighbor.row, neighbor.col})) continue;

      // Skip if out of bounds (not in interior)
      if (neighbor.row < 1 || neighbor.row >= config_.rows - 1 ||
          neighbor.col < 1 || neighbor.col >= config_.cols - 1)
        continue;

      // Check if this edge faces the correct direction
      bool valid_edge = false;
      if (split_dir == SplitDirection::Horizontal) {
        // Horizontal split: first room doors face down, second face up
        if (is_first_room && i == 1)
          valid_edge = true;  // Down
        else if (!is_first_room && i == 0)
          valid_edge = true;  // Up
      } else {
        // Vertical split: first room doors face right, second face left
        if (is_first_room && i == 3)
          valid_edge = true;  // Right
        else if (!is_first_room && i == 2)
          valid_edge = true;  // Left
      }

      if (valid_edge) {
        edge_pairs.push_back({p, neighbor});
      }
    }
  }

  // Group consecutive edge pairs into door segments
  // For simplicity, generate all possible doors of width door_width

  // Sort edges by position
  if (split_dir == SplitDirection::Horizontal) {
    // Sort by column for horizontal split (doors are horizontal segments)
    std::sort(edge_pairs.begin(), edge_pairs.end(),
              [](const auto& a, const auto& b) {
                if (a.first.row != b.first.row) return a.first.row < b.first.row;
                return a.first.col < b.first.col;
              });
  } else {
    // Sort by row for vertical split (doors are vertical segments)
    std::sort(edge_pairs.begin(), edge_pairs.end(),
              [](const auto& a, const auto& b) {
                if (a.first.col != b.first.col) return a.first.col < b.first.col;
                return a.first.row < b.first.row;
              });
  }

  // Find consecutive segments and create doors
  for (size_t start = 0; start + door_width <= edge_pairs.size(); ++start) {
    // Check if next door_width edges are consecutive
    bool consecutive = true;
    for (int j = 1; j < door_width; ++j) {
      const auto& prev = edge_pairs[start + j - 1];
      const auto& curr = edge_pairs[start + j];

      // Check if they're adjacent
      if (split_dir == SplitDirection::Horizontal) {
        // Should be on same row, adjacent columns
        if (curr.first.row != prev.first.row ||
            curr.first.col != prev.first.col + 1) {
          consecutive = false;
          break;
        }
      } else {
        // Should be on same column, adjacent rows
        if (curr.first.col != prev.first.col ||
            curr.first.row != prev.first.row + 1) {
          consecutive = false;
          break;
        }
      }
    }

    if (consecutive) {
      Door door;
      door.is_horizontal = (split_dir == SplitDirection::Horizontal);
      for (int j = 0; j < door_width; ++j) {
        door.in_cells.push_back(edge_pairs[start + j].first);
        door.out_cells.push_back(edge_pairs[start + j].second);
      }
      doors.push_back(door);
    }
  }

  return doors;
}

std::vector<CorridorCandidate> MapGenerator::GenerateCorridorCandidates(
    const std::vector<Door>& doors1, const std::vector<Door>& doors2,
    int corridor_width) {
  std::vector<CorridorCandidate> candidates;

  MAP_DEBUG("GenerateCorridorCandidates: " << doors1.size() << " x " << doors2.size()
            << " door pairs, width=" << corridor_width);

  for (const Door& d1 : doors1) {
    for (const Door& d2 : doors2) {
      // Generate corridor path from d1.out_cells to d2.out_cells
      CorridorCandidate candidate;
      candidate.door1 = d1;
      candidate.door2 = d2;

      // Get center positions of doors
      Position from = d1.out_cells[d1.out_cells.size() / 2];
      Position to = d2.out_cells[d2.out_cells.size() / 2];
      MAP_DEBUG("  Corridor from (" << from.row << "," << from.col << ") to ("
                << to.row << "," << to.col << ")");

      // Use a set to track added cells and avoid duplicates
      std::set<std::pair<int, int>> added_cells;

      // Helper lambda to add a cell if not already added
      auto add_cell = [&](int row, int col) {
        if (added_cells.find({row, col}) == added_cells.end()) {
          added_cells.insert({row, col});
          candidate.cells.push_back({row, col});
        }
      };

      // Helper lambda to draw a vertical segment (single-cell width for now)
      auto draw_vertical = [&](int r1, int r2, int col) {
        int rmin = std::min(r1, r2);
        int rmax = std::max(r1, r2);
        for (int r = rmin; r <= rmax; ++r) {
          add_cell(r, col);
        }
      };

      // Helper lambda to draw a horizontal segment (single-cell width for now)
      auto draw_horizontal = [&](int row, int c1, int c2) {
        int cmin = std::min(c1, c2);
        int cmax = std::max(c1, c2);
        for (int c = cmin; c <= cmax; ++c) {
          add_cell(row, c);
        }
      };

      // Add door out_cells first
      for (const Position& p : d1.out_cells) {
        add_cell(p.row, p.col);
      }
      for (const Position& p : d2.out_cells) {
        add_cell(p.row, p.col);
      }

      // Generate proper L-shaped corridor
      // Both doors have same orientation (both horizontal or both vertical)
      if (d1.is_horizontal && d2.is_horizontal) {
        // Doors are horizontal (on top/bottom edges)
        // L-shape: vertical at from.col -> horizontal at mid_row -> vertical at to.col

        if (from.col == to.col) {
          // Straight vertical corridor
          draw_vertical(from.row, to.row, from.col);
        } else {
          // L-shape corridor
          int mid_row = (from.row + to.row) / 2;

          // Segment 1: Vertical from door1 to mid_row at from.col
          draw_vertical(from.row, mid_row, from.col);

          // Segment 2: Horizontal from from.col to to.col at mid_row
          draw_horizontal(mid_row, from.col, to.col);

          // Segment 3: Vertical from mid_row to door2 at to.col
          draw_vertical(mid_row, to.row, to.col);
        }
      } else {
        // Doors are vertical (on left/right edges)
        // L-shape: horizontal at from.row -> vertical at mid_col -> horizontal at to.row

        if (from.row == to.row) {
          // Straight horizontal corridor
          draw_horizontal(from.row, from.col, to.col);
        } else {
          // L-shape corridor
          int mid_col = (from.col + to.col) / 2;

          // Segment 1: Horizontal from door1 to mid_col at from.row
          draw_horizontal(from.row, from.col, mid_col);

          // Segment 2: Vertical from from.row to to.row at mid_col
          draw_vertical(from.row, to.row, mid_col);

          // Segment 3: Horizontal from mid_col to door2 at to.row
          draw_horizontal(to.row, mid_col, to.col);
        }
      }

      MAP_DEBUG("    Generated " << candidate.cells.size() << " corridor cells");
      candidates.push_back(candidate);
    }
  }

  return candidates;
}

bool MapGenerator::ValidateCorridor(const CorridorCandidate& corridor,
                                    const RoomShape& room1,
                                    const RoomShape& room2) const {
  // Create sets for quick lookup
  std::set<std::pair<int, int>> room1_cells, room2_cells;
  for (const Position& p : room1.cells) {
    room1_cells.insert({p.row, p.col});
  }
  for (const Position& p : room2.cells) {
    room2_cells.insert({p.row, p.col});
  }

  std::set<std::pair<int, int>> door1_in, door2_in;
  for (const Position& p : corridor.door1.in_cells) {
    door1_in.insert({p.row, p.col});
  }
  for (const Position& p : corridor.door2.in_cells) {
    door2_in.insert({p.row, p.col});
  }

  std::set<std::pair<int, int>> corridor_cells;
  for (const Position& p : corridor.cells) {
    corridor_cells.insert({p.row, p.col});
  }

  // Check each corridor cell
  for (const Position& p : corridor.cells) {
    // Check bounds
    if (p.row < 1 || p.row >= config_.rows - 1 ||
        p.col < 1 || p.col >= config_.cols - 1) {
      MAP_DEBUG("    REJECT: cell (" << p.row << "," << p.col << ") out of bounds");
      return false;
    }

    // Check orthogonal neighbors (morphological dilation)
    Position neighbors[4] = {{p.row - 1, p.col}, {p.row + 1, p.col},
                             {p.row, p.col - 1}, {p.row, p.col + 1}};

    for (const Position& n : neighbors) {
      // Skip if neighbor is part of corridor
      if (corridor_cells.count({n.row, n.col})) continue;

      // Skip if out of bounds
      if (n.row < 0 || n.row >= config_.rows ||
          n.col < 0 || n.col >= config_.cols)
        continue;

      // Check if neighbor is a floor cell (room cell)
      bool is_room1 = room1_cells.count({n.row, n.col}) > 0;
      bool is_room2 = room2_cells.count({n.row, n.col}) > 0;

      if (is_room1 || is_room2) {
        // It's a room cell - must be a door cell
        bool is_door1 = door1_in.count({n.row, n.col}) > 0;
        bool is_door2 = door2_in.count({n.row, n.col}) > 0;

        if (!is_door1 && !is_door2) {
          MAP_DEBUG("    REJECT: corridor cell (" << p.row << "," << p.col
                    << ") touches room cell (" << n.row << "," << n.col
                    << ") which is not a door");
          return false;  // Corridor touches room outside of doors
        }
      }
    }
  }

  return true;
}

std::vector<CorridorCandidate> MapGenerator::SelectCorridorPair(
    const std::vector<CorridorCandidate>& candidates) {
  if (candidates.empty()) return {};
  if (candidates.size() == 1) return {candidates[0]};

  // Sort by length (ascending)
  std::vector<CorridorCandidate> sorted = candidates;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.Length() < b.Length(); });

  // Create corridor cell sets for adjacency checking
  auto get_corridor_set = [](const CorridorCandidate& c) {
    std::set<std::pair<int, int>> cells;
    for (const Position& p : c.cells) {
      cells.insert({p.row, p.col});
    }
    return cells;
  };

  auto are_adjacent = [](const std::set<std::pair<int, int>>& set1,
                         const std::set<std::pair<int, int>>& set2) {
    for (const auto& [r, c] : set1) {
      // Check orthogonal neighbors
      if (set2.count({r - 1, c}) || set2.count({r + 1, c}) ||
          set2.count({r, c - 1}) || set2.count({r, c + 1})) {
        return true;
      }
    }
    return false;
  };

  // Try to find a non-adjacent pair
  // Weight selection toward shorter corridors
  std::vector<double> weights;
  for (const auto& c : sorted) {
    weights.push_back(1.0 / std::max(1, c.Length()));
  }

  // Normalize weights
  double total_weight = 0;
  for (double w : weights) total_weight += w;
  for (double& w : weights) w /= total_weight;

  // Try 100 times to find a valid pair
  for (int attempt = 0; attempt < 100; ++attempt) {
    // Weighted random selection for first corridor
    double r1 = std::uniform_real_distribution<double>(0, 1)(rng_);
    size_t idx1 = 0;
    double cumulative = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
      cumulative += weights[i];
      if (r1 < cumulative) {
        idx1 = i;
        break;
      }
    }

    // Weighted random selection for second corridor
    double r2 = std::uniform_real_distribution<double>(0, 1)(rng_);
    size_t idx2 = 0;
    cumulative = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
      cumulative += weights[i];
      if (r2 < cumulative) {
        idx2 = i;
        break;
      }
    }

    if (idx1 == idx2) continue;

    auto set1 = get_corridor_set(sorted[idx1]);
    auto set2 = get_corridor_set(sorted[idx2]);

    if (!are_adjacent(set1, set2)) {
      return {sorted[idx1], sorted[idx2]};
    }
  }

  // Fallback: return single shortest corridor
  return {sorted[0]};
}

void MapGenerator::CarveCorridor(const CorridorCandidate& corridor) {
  for (const Position& p : corridor.cells) {
    if (grid_->IsInBounds(p) && p.row > 0 && p.row < config_.rows - 1 &&
        p.col > 0 && p.col < config_.cols - 1) {
      // Only set corridor origin if not already a room
      if (grid_->GetCell(p).GetOrigin() != CellOrigin::Room) {
        grid_->SetCell(p, CellKind::Floor, CellOrigin::Corridor);
      }
    }
  }
}

// =============================================================================
// Room generation
// =============================================================================

std::vector<Room> MapGenerator::PlaceRooms(int num_rooms) {
  std::vector<Room> rooms;
  if (num_rooms <= 0) return rooms;

  // Quadrant-based placement: divide grid into sectors and place one room per sector
  // This ensures rooms are spread across the grid instead of clustering

  // Available interior space (excluding 1-cell border)
  int interior_rows = config_.rows - 2;
  int interior_cols = config_.cols - 2;

  // Calculate grid divisions based on num_rooms
  // For 2 rooms: 1x2 or 2x1 (based on aspect ratio)
  // For 3 rooms: 1x3 or 3x1
  // For 4 rooms: 2x2
  // For 5 rooms: 2x3 or 3x2 (with one empty)
  int grid_rows, grid_cols;
  if (num_rooms <= 2) {
    if (interior_cols >= interior_rows) {
      grid_rows = 1;
      grid_cols = num_rooms;
    } else {
      grid_rows = num_rooms;
      grid_cols = 1;
    }
  } else if (num_rooms == 3) {
    if (interior_cols >= interior_rows) {
      grid_rows = 1;
      grid_cols = 3;
    } else {
      grid_rows = 3;
      grid_cols = 1;
    }
  } else if (num_rooms == 4) {
    grid_rows = 2;
    grid_cols = 2;
  } else {  // 5+
    grid_rows = 2;
    grid_cols = 3;
  }

  // Size of each sector
  int sector_height = interior_rows / grid_rows;
  int sector_width = interior_cols / grid_cols;

  // Minimum room size
  int min_room_size = 3;

  // Place one room in each sector
  int room_count = 0;
  for (int sr = 0; sr < grid_rows && room_count < num_rooms; ++sr) {
    for (int sc = 0; sc < grid_cols && room_count < num_rooms; ++sc) {
      // Sector bounds (in interior coordinates, starting at row/col 1)
      int sector_top = 1 + sr * sector_height;
      int sector_left = 1 + sc * sector_width;

      // Room size: random within sector constraints
      // Room should fit within sector with some margin
      int max_room_width = std::max(min_room_size, sector_width - 1);
      int max_room_height = std::max(min_room_size, sector_height - 1);

      std::uniform_int_distribution<int> width_dist(min_room_size, max_room_width);
      std::uniform_int_distribution<int> height_dist(min_room_size, max_room_height);
      int room_width = width_dist(rng_);
      int room_height = height_dist(rng_);

      // Random position within sector
      int max_offset_col = std::max(0, sector_width - room_width);
      int max_offset_row = std::max(0, sector_height - room_height);

      int room_left = sector_left;
      int room_top = sector_top;
      if (max_offset_col > 0) {
        std::uniform_int_distribution<int> col_dist(0, max_offset_col);
        room_left += col_dist(rng_);
      }
      if (max_offset_row > 0) {
        std::uniform_int_distribution<int> row_dist(0, max_offset_row);
        room_top += row_dist(rng_);
      }

      // Ensure room doesn't go out of bounds
      room_left = std::min(room_left, config_.cols - room_width - 1);
      room_top = std::min(room_top, config_.rows - room_height - 1);

      rooms.push_back({room_top, room_left, room_width, room_height});
      room_count++;
    }
  }

  return rooms;
}

void MapGenerator::CarveRoom(const Room& room) {
  LevelBuilder builder(*grid_);
  builder.Fill(CellKind::Floor, CellOrigin::Room, room.top, room.left,
               room.width, room.height);
}

void MapGenerator::ConnectRooms(const std::vector<Room>& rooms) {
  if (rooms.empty()) return;

  // Simple approach: connect each room to the next one
  // This creates a spanning tree (all rooms connected)
  int corridor_width = GetCorridorWidth();

  for (size_t i = 0; i < rooms.size() - 1; ++i) {
    Position from = rooms[i].Center();
    Position to = rooms[i + 1].Center();

    // L-shaped corridor: horizontal then vertical (or vice versa)
    bool horizontal_first = rng_() % 2 == 0;

    if (horizontal_first) {
      // Horizontal corridor from 'from' to (from.row, to.col)
      CarveCorridorH(from.row, from.col, to.col, corridor_width);
      // Vertical corridor from (from.row, to.col) to 'to'
      CarveCorridorV(to.col, from.row, to.row, corridor_width);
    } else {
      // Vertical first
      CarveCorridorV(from.col, from.row, to.row, corridor_width);
      CarveCorridorH(to.row, from.col, to.col, corridor_width);
    }
  }

  // Add extra connections for higher complexity (lower complexity = more connectivity)
  // At complexity 2, add 1-2 extra connections; at complexity 5, add 0
  int extra_connections = std::max(0, 3 - (config_.complexity - 2));
  for (int i = 0; i < extra_connections && rooms.size() >= 2; ++i) {
    std::uniform_int_distribution<size_t> room_dist(0, rooms.size() - 1);
    size_t r1 = room_dist(rng_);
    size_t r2 = room_dist(rng_);
    if (r1 == r2) continue;

    Position from = rooms[r1].Center();
    Position to = rooms[r2].Center();

    if (rng_() % 2 == 0) {
      CarveCorridorH(from.row, from.col, to.col, corridor_width);
      CarveCorridorV(to.col, from.row, to.row, corridor_width);
    } else {
      CarveCorridorV(from.col, from.row, to.row, corridor_width);
      CarveCorridorH(to.row, from.col, to.col, corridor_width);
    }
  }
}

void MapGenerator::CarveCorridorH(int row, int col1, int col2, int width) {
  if (col1 > col2) std::swap(col1, col2);

  int half_width = width / 2;
  for (int c = col1; c <= col2; ++c) {
    for (int r = row - half_width; r <= row + half_width; ++r) {
      if (grid_->IsInBounds(r, c) && r > 0 && r < config_.rows - 1 &&
          c > 0 && c < config_.cols - 1) {
        // Only set corridor origin if not already a room (rooms take precedence)
        if (grid_->GetCell(r, c).GetOrigin() != CellOrigin::Room) {
          grid_->SetCell(r, c, CellKind::Floor, CellOrigin::Corridor);
        } else {
          grid_->SetCell(r, c, CellKind::Floor);
        }
      }
    }
  }
}

void MapGenerator::CarveCorridorV(int col, int row1, int row2, int width) {
  if (row1 > row2) std::swap(row1, row2);

  int half_width = width / 2;
  for (int r = row1; r <= row2; ++r) {
    for (int c = col - half_width; c <= col + half_width; ++c) {
      if (grid_->IsInBounds(r, c) && r > 0 && r < config_.rows - 1 &&
          c > 0 && c < config_.cols - 1) {
        // Only set corridor origin if not already a room (rooms take precedence)
        if (grid_->GetCell(r, c).GetOrigin() != CellOrigin::Room) {
          grid_->SetCell(r, c, CellKind::Floor, CellOrigin::Corridor);
        } else {
          grid_->SetCell(r, c, CellKind::Floor);
        }
      }
    }
  }
}

// =============================================================================
// Obstacle generation
// =============================================================================

void MapGenerator::ScatterObstacles(int density_percent) {
  // Get all floor cells (excluding perimeter)
  std::vector<Position> floor_cells;
  for (int r = 2; r < config_.rows - 2; ++r) {
    for (int c = 2; c < config_.cols - 2; ++c) {
      if (grid_->GetCellKind(r, c) == CellKind::Floor) {
        floor_cells.push_back({r, c});
      }
    }
  }

  if (floor_cells.empty()) return;

  // Calculate number of obstacles to place
  int num_obstacles = static_cast<int>(floor_cells.size()) * density_percent / 100;

  // Shuffle and pick cells for obstacles
  std::shuffle(floor_cells.begin(), floor_cells.end(), rng_);

  int placed = 0;
  for (const Position& pos : floor_cells) {
    if (placed >= num_obstacles) break;

    // Remember original origin to restore if we need to undo
    CellOrigin original_origin = grid_->GetCell(pos).GetOrigin();

    // Place wall with obstacle origin
    grid_->SetCell(pos, CellKind::Wall, CellOrigin::Obstacle);
    placed++;

    // Check connectivity after each placement
    if (!IsConnected()) {
      // Undo this placement - it broke connectivity
      grid_->SetCell(pos, CellKind::Floor, original_origin);
      placed--;
    }
  }
}

// =============================================================================
// Connectivity validation
// =============================================================================

bool MapGenerator::IsConnected() const {
  // Find first walkable cell
  Position start{-1, -1};
  int total_walkable = 0;

  for (int r = 0; r < config_.rows; ++r) {
    for (int c = 0; c < config_.cols; ++c) {
      if (grid_->IsWalkable({r, c})) {
        total_walkable++;
        if (!start.IsValid()) {
          start = {r, c};
        }
      }
    }
  }

  if (total_walkable == 0) return true;  // No walkable cells is technically connected

  // Flood fill from start
  std::vector<std::vector<bool>> visited(config_.rows,
                                         std::vector<bool>(config_.cols, false));
  int reached = FloodFill(start, visited);

  return reached == total_walkable;
}

int MapGenerator::FloodFill(Position start,
                            std::vector<std::vector<bool>>& visited) const {
  if (!start.IsValid() || !grid_->IsWalkable(start)) return 0;

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
      if (grid_->IsInBounds(next) && grid_->IsWalkable(next) &&
          !visited[next.row][next.col]) {
        visited[next.row][next.col] = true;
        queue.push(next);
        count++;
      }
    }
  }

  return count;
}

// =============================================================================
// Derived parameters
// =============================================================================

int MapGenerator::GetNumRooms() const {
  // Default: complexity rooms (0 at complexity 0, up to 5 at complexity 5)
  // But for complexity < 2, we don't use rooms anyway
  int default_val = config_.complexity;

  if (config_.num_rooms.HasOverride()) {
    return config_.num_rooms.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetCorridorWidth() const {
  // Default: narrower corridors at higher complexity
  // complexity 2: width 2, complexity 3-4: width 1-2, complexity 5: width 1
  int default_val = std::max(1, 3 - config_.complexity / 2);

  if (config_.corridor_width.HasOverride()) {
    return config_.corridor_width.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetObstacleDensity() const {
  // Default: complexity * 4 percent (0% at complexity 0, up to 20% at complexity 5)
  int default_val = config_.complexity * 4;

  if (config_.obstacle_density.HasOverride()) {
    return config_.obstacle_density.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetDeadEndCount() const {
  // Default: complexity dead ends
  int default_val = config_.complexity;

  if (config_.dead_end_count.HasOverride()) {
    return config_.dead_end_count.Clamp(default_val);
  }
  return default_val;
}

}  // namespace companions
