// Copyright 2024
// CorridorGenerator - procedural corridor generation

#ifndef COMPANIONS_CORE_CORRIDOR_GENERATOR_H_
#define COMPANIONS_CORE_CORRIDOR_GENERATOR_H_

#include <vector>

#include "generation_context.h"
#include "room_generator.h"  // For RoomShape, SplitDirection
#include "types.h"

namespace companions {

// Forward declarations
struct Room;

// A door is a segment on room border where corridor connects
struct Door {
  std::vector<Position> in_cells;   // Inside room (floor cells)
  std::vector<Position> out_cells;  // Outside room (wall cells -> corridor)
  bool is_horizontal;               // Door orientation
};

// A corridor candidate connecting two doors
struct CorridorCandidate {
  std::vector<Position> cells;  // All corridor cells
  Door door1, door2;
  int Length() const { return static_cast<int>(cells.size()); }
};

// Corridor generator for procedural map generation
class CorridorGenerator {
 public:
  explicit CorridorGenerator(GenerationContext& ctx);

  // Generate possible door positions for a room
  std::vector<Door> GenerateDoorsForRoom(const RoomShape& shape,
                                         SplitDirection split_dir,
                                         bool is_first_room, int door_width);

  // Generate all valid corridor candidates between two sets of doors
  std::vector<CorridorCandidate> GenerateCandidates(
      const std::vector<Door>& doors1, const std::vector<Door>& doors2,
      int corridor_width);

  // Validate that a corridor doesn't touch rooms except at doors
  bool Validate(const CorridorCandidate& corridor, const RoomShape& room1,
                const RoomShape& room2) const;

  // Select a pair of non-adjacent corridors (or single if no valid pair)
  std::vector<CorridorCandidate> SelectPair(
      const std::vector<CorridorCandidate>& candidates);

  // Carve a corridor into the grid
  void Carve(const CorridorCandidate& corridor);

  // Legacy corridor generation (complexity 3+)
  void ConnectRooms(const std::vector<Room>& rooms, int corridor_width);

 private:
  void CarveCorridorH(int row, int col1, int col2, int width);
  void CarveCorridorV(int col, int row1, int row2, int width);

  GenerationContext& ctx_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_CORRIDOR_GENERATOR_H_
