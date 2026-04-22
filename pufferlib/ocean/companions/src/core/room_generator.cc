// Copyright 2024
// RoomGenerator implementation

#include "room_generator.h"

#include <algorithm>
#include <random>

#include "level_builder.h"
#include "map_generator.h"  // For MapConfig, Room

namespace companions {

RoomGenerator::RoomGenerator(GenerationContext& ctx) : ctx_(ctx) {}

std::pair<Quadrant, Quadrant> RoomGenerator::DivideIntoQuadrants(
    SplitDirection* out_dir) {
  // Interior dimensions (excluding 1-cell border)
  int interior_rows = ctx_.config.rows - 2;
  int interior_cols = ctx_.config.cols - 2;

  // Choose split direction based on aspect ratio
  SplitDirection dir;
  if (interior_cols > interior_rows) {
    dir = SplitDirection::Vertical;  // Split vertically (left/right quadrants)
  } else if (interior_rows > interior_cols) {
    dir = SplitDirection::Horizontal;  // Split horizontally (top/bottom)
  } else {
    // Square: random choice
    dir = (ctx_.rng() % 2 == 0) ? SplitDirection::Horizontal
                                 : SplitDirection::Vertical;
  }
  *out_dir = dir;

  Quadrant q1, q2;

  if (dir == SplitDirection::Horizontal) {
    // Horizontal split: top/bottom quadrants
    int separator_row = 1 + (interior_rows + 1) / 2;

    q1.top = 1;
    q1.left = 1;
    q1.height = separator_row - 1;
    q1.width = interior_cols;

    q2.top = separator_row + 1;
    q2.left = 1;
    q2.height = ctx_.config.rows - 2 - separator_row;
    q2.width = interior_cols;
  } else {
    // Vertical split: left/right quadrants
    int separator_col = 1 + (interior_cols + 1) / 2;

    q1.top = 1;
    q1.left = 1;
    q1.height = interior_rows;
    q1.width = separator_col - 1;

    q2.top = 1;
    q2.left = separator_col + 1;
    q2.height = interior_rows;
    q2.width = ctx_.config.cols - 2 - separator_col;
  }

  return {q1, q2};
}

RoomShape RoomGenerator::GenerateInQuadrant(const Quadrant& quad,
                                            bool prefer_l_toward_other) {
  RoomShape shape;

  // Room size: 60-100% of quadrant dimensions
  int min_width = std::max(2, quad.width * 60 / 100);
  int min_height = std::max(2, quad.height * 60 / 100);
  int max_width = quad.width;
  int max_height = quad.height;

  int room_width = portable_uniform_int(ctx_.rng, min_width, max_width);
  int room_height = portable_uniform_int(ctx_.rng, min_height, max_height);

  // Random position within quadrant
  int max_offset_col = quad.width - room_width;
  int max_offset_row = quad.height - room_height;

  int room_left = quad.left;
  int room_top = quad.top;
  if (max_offset_col > 0) {
    room_left += portable_uniform_int(ctx_.rng, 0, max_offset_col);
  }
  if (max_offset_row > 0) {
    room_top += portable_uniform_int(ctx_.rng, 0, max_offset_row);
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
  bool make_l_shape = (room_width >= 4 && room_height >= 4 && ctx_.rng() % 2 == 0);

  if (make_l_shape) {
    // Carve size: keep at least 2 cells in each arm
    int carve_width = std::min(room_width - 2,
                               std::max(1, room_width * 40 / 100));
    int carve_height = std::min(room_height - 2,
                                std::max(1, room_height * 40 / 100));

    // Choose which corner to carve (0=TL, 1=TR, 2=BL, 3=BR)
    int corner;
    if (prefer_l_toward_other) {
      // First room: carve corner facing away from other room
      corner = (ctx_.rng() % 2 == 0) ? 2 : 3;  // Bottom corners
    } else {
      // Second room: carve top corners
      corner = (ctx_.rng() % 2 == 0) ? 0 : 1;  // Top corners
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

void RoomGenerator::Carve(const RoomShape& shape) {
  for (const Position& p : shape.cells) {
    ctx_.grid.SetCell(p, CellKind::Floor, CellOrigin::Room);
  }
}

std::vector<Room> RoomGenerator::PlaceRooms(int num_rooms) {
  std::vector<Room> rooms;
  if (num_rooms <= 0) return rooms;

  // Quadrant-based placement
  int interior_rows = ctx_.config.rows - 2;
  int interior_cols = ctx_.config.cols - 2;

  // Calculate grid divisions based on num_rooms
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

  int sector_height = interior_rows / grid_rows;
  int sector_width = interior_cols / grid_cols;
  int min_room_size = 3;

  int room_count = 0;
  for (int sr = 0; sr < grid_rows && room_count < num_rooms; ++sr) {
    for (int sc = 0; sc < grid_cols && room_count < num_rooms; ++sc) {
      int sector_top = 1 + sr * sector_height;
      int sector_left = 1 + sc * sector_width;

      int max_room_width = std::max(min_room_size, sector_width - 1);
      int max_room_height = std::max(min_room_size, sector_height - 1);

      int room_width = portable_uniform_int(ctx_.rng, min_room_size, max_room_width);
      int room_height = portable_uniform_int(ctx_.rng, min_room_size, max_room_height);

      int max_offset_col = std::max(0, sector_width - room_width);
      int max_offset_row = std::max(0, sector_height - room_height);

      int room_left = sector_left;
      int room_top = sector_top;
      if (max_offset_col > 0) {
        room_left += portable_uniform_int(ctx_.rng, 0, max_offset_col);
      }
      if (max_offset_row > 0) {
        room_top += portable_uniform_int(ctx_.rng, 0, max_offset_row);
      }

      room_left = std::min(room_left, ctx_.config.cols - room_width - 1);
      room_top = std::min(room_top, ctx_.config.rows - room_height - 1);

      rooms.push_back({room_top, room_left, room_width, room_height});
      room_count++;
    }
  }

  return rooms;
}

void RoomGenerator::CarveRoom(const Room& room) {
  LevelBuilder builder(ctx_.grid);
  builder.Fill(CellKind::Floor, CellOrigin::Room, room.top, room.left,
               room.width, room.height);
}

}  // namespace companions
