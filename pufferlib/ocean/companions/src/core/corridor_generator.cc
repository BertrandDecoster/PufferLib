// Copyright 2024
// CorridorGenerator implementation

#include "corridor_generator.h"

#include <algorithm>
#include <random>
#include <set>

#include "map_generator.h"  // For MapConfig, Room

// Debug flag - set to 1 to enable verbose output
#define CORRIDOR_GENERATOR_DEBUG 0

#if CORRIDOR_GENERATOR_DEBUG
#include <iostream>
#define CORRIDOR_DEBUG(x) std::cerr << "[CorridorGen] " << x << std::endl
#else
#define CORRIDOR_DEBUG(x)
#endif

namespace companions {

CorridorGenerator::CorridorGenerator(GenerationContext& ctx) : ctx_(ctx) {}

std::vector<Door> CorridorGenerator::GenerateDoorsForRoom(
    const RoomShape& shape, SplitDirection split_dir, bool is_first_room,
    int door_width) {
  std::vector<Door> doors;

  // Create a set of room cells for quick lookup
  std::set<std::pair<int, int>> room_cells;
  for (const Position& p : shape.cells) {
    room_cells.insert({p.row, p.col});
  }

  // Find border cells and their outward neighbors
  std::vector<std::pair<Position, Position>> edge_pairs;  // (in_cell, out_cell)

  for (const Position& p : shape.cells) {
    Position neighbors[4] = {{p.row - 1, p.col},   // Up
                             {p.row + 1, p.col},   // Down
                             {p.row, p.col - 1},   // Left
                             {p.row, p.col + 1}};  // Right

    for (int i = 0; i < 4; ++i) {
      Position neighbor = neighbors[i];

      // Skip if neighbor is in room
      if (room_cells.count({neighbor.row, neighbor.col})) continue;

      // Skip if out of bounds (not in interior)
      if (neighbor.row < 1 || neighbor.row >= ctx_.config.rows - 1 ||
          neighbor.col < 1 || neighbor.col >= ctx_.config.cols - 1)
        continue;

      // Check if this edge faces the correct direction
      bool valid_edge = false;
      if (split_dir == SplitDirection::Horizontal) {
        if (is_first_room && i == 1)
          valid_edge = true;  // Down
        else if (!is_first_room && i == 0)
          valid_edge = true;  // Up
      } else {
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

  // Sort edges by position
  if (split_dir == SplitDirection::Horizontal) {
    std::sort(edge_pairs.begin(), edge_pairs.end(),
              [](const auto& a, const auto& b) {
                if (a.first.row != b.first.row) return a.first.row < b.first.row;
                return a.first.col < b.first.col;
              });
  } else {
    std::sort(edge_pairs.begin(), edge_pairs.end(),
              [](const auto& a, const auto& b) {
                if (a.first.col != b.first.col) return a.first.col < b.first.col;
                return a.first.row < b.first.row;
              });
  }

  // Find consecutive segments and create doors
  for (size_t start = 0; start + door_width <= edge_pairs.size(); ++start) {
    bool consecutive = true;
    for (int j = 1; j < door_width; ++j) {
      const auto& prev = edge_pairs[start + j - 1];
      const auto& curr = edge_pairs[start + j];

      if (split_dir == SplitDirection::Horizontal) {
        if (curr.first.row != prev.first.row ||
            curr.first.col != prev.first.col + 1) {
          consecutive = false;
          break;
        }
      } else {
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

std::vector<CorridorCandidate> CorridorGenerator::GenerateCandidates(
    const std::vector<Door>& doors1, const std::vector<Door>& doors2,
    int corridor_width) {
  std::vector<CorridorCandidate> candidates;

  CORRIDOR_DEBUG("GenerateCandidates: " << doors1.size() << " x " << doors2.size()
                 << " door pairs, width=" << corridor_width);

  for (const Door& d1 : doors1) {
    for (const Door& d2 : doors2) {
      CorridorCandidate candidate;
      candidate.door1 = d1;
      candidate.door2 = d2;

      Position from = d1.out_cells[d1.out_cells.size() / 2];
      Position to = d2.out_cells[d2.out_cells.size() / 2];

      std::set<std::pair<int, int>> added_cells;

      auto add_cell = [&](int row, int col) {
        if (added_cells.find({row, col}) == added_cells.end()) {
          added_cells.insert({row, col});
          candidate.cells.push_back({row, col});
        }
      };

      auto draw_vertical = [&](int r1, int r2, int col) {
        int rmin = std::min(r1, r2);
        int rmax = std::max(r1, r2);
        for (int r = rmin; r <= rmax; ++r) {
          add_cell(r, col);
        }
      };

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

      // Generate L-shaped corridor
      if (d1.is_horizontal && d2.is_horizontal) {
        if (from.col == to.col) {
          draw_vertical(from.row, to.row, from.col);
        } else {
          int mid_row = (from.row + to.row) / 2;
          draw_vertical(from.row, mid_row, from.col);
          draw_horizontal(mid_row, from.col, to.col);
          draw_vertical(mid_row, to.row, to.col);
        }
      } else {
        if (from.row == to.row) {
          draw_horizontal(from.row, from.col, to.col);
        } else {
          int mid_col = (from.col + to.col) / 2;
          draw_horizontal(from.row, from.col, mid_col);
          draw_vertical(from.row, to.row, mid_col);
          draw_horizontal(to.row, mid_col, to.col);
        }
      }

      CORRIDOR_DEBUG("    Generated " << candidate.cells.size() << " corridor cells");
      candidates.push_back(candidate);
    }
  }

  return candidates;
}

bool CorridorGenerator::Validate(const CorridorCandidate& corridor,
                                  const RoomShape& room1,
                                  const RoomShape& room2) const {
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

  for (const Position& p : corridor.cells) {
    if (p.row < 1 || p.row >= ctx_.config.rows - 1 ||
        p.col < 1 || p.col >= ctx_.config.cols - 1) {
      CORRIDOR_DEBUG("    REJECT: cell (" << p.row << "," << p.col << ") out of bounds");
      return false;
    }

    Position neighbors[4] = {{p.row - 1, p.col}, {p.row + 1, p.col},
                             {p.row, p.col - 1}, {p.row, p.col + 1}};

    for (const Position& n : neighbors) {
      if (corridor_cells.count({n.row, n.col})) continue;
      if (n.row < 0 || n.row >= ctx_.config.rows ||
          n.col < 0 || n.col >= ctx_.config.cols)
        continue;

      bool is_room1 = room1_cells.count({n.row, n.col}) > 0;
      bool is_room2 = room2_cells.count({n.row, n.col}) > 0;

      if (is_room1 || is_room2) {
        bool is_door1 = door1_in.count({n.row, n.col}) > 0;
        bool is_door2 = door2_in.count({n.row, n.col}) > 0;

        if (!is_door1 && !is_door2) {
          CORRIDOR_DEBUG("    REJECT: corridor cell (" << p.row << "," << p.col
                         << ") touches room cell (" << n.row << "," << n.col
                         << ") which is not a door");
          return false;
        }
      }
    }
  }

  return true;
}

std::vector<CorridorCandidate> CorridorGenerator::SelectPair(
    const std::vector<CorridorCandidate>& candidates) {
  if (candidates.empty()) return {};
  if (candidates.size() == 1) return {candidates[0]};

  std::vector<CorridorCandidate> sorted = candidates;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.Length() < b.Length(); });

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
      if (set2.count({r - 1, c}) || set2.count({r + 1, c}) ||
          set2.count({r, c - 1}) || set2.count({r, c + 1})) {
        return true;
      }
    }
    return false;
  };

  std::vector<double> weights;
  for (const auto& c : sorted) {
    weights.push_back(1.0 / std::max(1, c.Length()));
  }

  double total_weight = 0;
  for (double w : weights) total_weight += w;
  for (double& w : weights) w /= total_weight;

  for (int attempt = 0; attempt < 100; ++attempt) {
    double r1 = std::uniform_real_distribution<double>(0, 1)(ctx_.rng);
    size_t idx1 = 0;
    double cumulative = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
      cumulative += weights[i];
      if (r1 < cumulative) {
        idx1 = i;
        break;
      }
    }

    double r2 = std::uniform_real_distribution<double>(0, 1)(ctx_.rng);
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

  return {sorted[0]};
}

void CorridorGenerator::Carve(const CorridorCandidate& corridor) {
  for (const Position& p : corridor.cells) {
    if (ctx_.grid.IsInBounds(p) && p.row > 0 && p.row < ctx_.config.rows - 1 &&
        p.col > 0 && p.col < ctx_.config.cols - 1) {
      if (ctx_.grid.GetCell(p).GetOrigin() != CellOrigin::Room) {
        ctx_.grid.SetCell(p, CellKind::Floor, CellOrigin::Corridor);
      }
    }
  }
}

void CorridorGenerator::ConnectRooms(const std::vector<Room>& rooms,
                                      int corridor_width) {
  if (rooms.empty()) return;

  for (size_t i = 0; i < rooms.size() - 1; ++i) {
    Position from = rooms[i].Center();
    Position to = rooms[i + 1].Center();

    bool horizontal_first = ctx_.rng() % 2 == 0;

    if (horizontal_first) {
      CarveCorridorH(from.row, from.col, to.col, corridor_width);
      CarveCorridorV(to.col, from.row, to.row, corridor_width);
    } else {
      CarveCorridorV(from.col, from.row, to.row, corridor_width);
      CarveCorridorH(to.row, from.col, to.col, corridor_width);
    }
  }

  // Extra connections for lower complexity
  int extra_connections = std::max(0, 3 - (ctx_.config.complexity - 2));
  for (int i = 0; i < extra_connections && rooms.size() >= 2; ++i) {
    std::uniform_int_distribution<size_t> room_dist(0, rooms.size() - 1);
    size_t r1 = room_dist(ctx_.rng);
    size_t r2 = room_dist(ctx_.rng);
    if (r1 == r2) continue;

    Position from = rooms[r1].Center();
    Position to = rooms[r2].Center();

    if (ctx_.rng() % 2 == 0) {
      CarveCorridorH(from.row, from.col, to.col, corridor_width);
      CarveCorridorV(to.col, from.row, to.row, corridor_width);
    } else {
      CarveCorridorV(from.col, from.row, to.row, corridor_width);
      CarveCorridorH(to.row, from.col, to.col, corridor_width);
    }
  }
}

void CorridorGenerator::CarveCorridorH(int row, int col1, int col2, int width) {
  if (col1 > col2) std::swap(col1, col2);

  int half_width = width / 2;
  for (int c = col1; c <= col2; ++c) {
    for (int r = row - half_width; r <= row + half_width; ++r) {
      if (ctx_.grid.IsInBounds(r, c) && r > 0 && r < ctx_.config.rows - 1 &&
          c > 0 && c < ctx_.config.cols - 1) {
        if (ctx_.grid.GetCell(r, c).GetOrigin() != CellOrigin::Room) {
          ctx_.grid.SetCell(r, c, CellKind::Floor, CellOrigin::Corridor);
        } else {
          ctx_.grid.SetCell(r, c, CellKind::Floor);
        }
      }
    }
  }
}

void CorridorGenerator::CarveCorridorV(int col, int row1, int row2, int width) {
  if (row1 > row2) std::swap(row1, row2);

  int half_width = width / 2;
  for (int r = row1; r <= row2; ++r) {
    for (int c = col - half_width; c <= col + half_width; ++c) {
      if (ctx_.grid.IsInBounds(r, c) && r > 0 && r < ctx_.config.rows - 1 &&
          c > 0 && c < ctx_.config.cols - 1) {
        if (ctx_.grid.GetCell(r, c).GetOrigin() != CellOrigin::Room) {
          ctx_.grid.SetCell(r, c, CellKind::Floor, CellOrigin::Corridor);
        } else {
          ctx_.grid.SetCell(r, c, CellKind::Floor);
        }
      }
    }
  }
}

}  // namespace companions
