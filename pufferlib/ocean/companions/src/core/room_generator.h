// Copyright 2024
// RoomGenerator - procedural room generation

#ifndef COMPANIONS_CORE_ROOM_GENERATOR_H_
#define COMPANIONS_CORE_ROOM_GENERATOR_H_

#include <utility>
#include <vector>

#include "generation_context.h"
#include "types.h"

namespace companions {

// Forward declarations
struct MapConfig;
struct Room;

// Direction for splitting grid into quadrants
enum class SplitDirection { Horizontal, Vertical };

// Quadrant bounds for room placement
struct Quadrant {
  int top, left, height, width;
};

// Room shape for complexity 2+ (can be L-shaped)
struct RoomShape {
  std::vector<Position> cells;  // All floor cells in the room
  int top, left, width, height; // Bounding box
};

// Room generator for procedural map generation
class RoomGenerator {
 public:
  explicit RoomGenerator(GenerationContext& ctx);

  // Complexity 2: Divide grid into two quadrants
  std::pair<Quadrant, Quadrant> DivideIntoQuadrants(SplitDirection* out_dir);

  // Generate a room shape within a quadrant (may be L-shaped)
  RoomShape GenerateInQuadrant(const Quadrant& quad, bool prefer_l_toward_other);

  // Carve a room shape into the grid (set cells to floor)
  void Carve(const RoomShape& shape);

  // Legacy room placement (complexity 3+)
  std::vector<Room> PlaceRooms(int num_rooms);
  void CarveRoom(const Room& room);

 private:
  GenerationContext& ctx_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_ROOM_GENERATOR_H_
