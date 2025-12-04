// Copyright 2024
// MapGenerator for procedural map generation with curriculum learning support
//
// =============================================================================
// OVERVIEW
// =============================================================================
// MapGenerator creates procedural grid maps for curriculum learning in RL.
// A single "complexity" knob (0-5) controls all generation parameters.
//
// Usage:
//   auto config = MapGenerator::DefaultConfig(rows, cols, complexity, seed);
//   auto grid = MapGenerator::Generate(config);
//
// =============================================================================
// COMPLEXITY LEVELS - VISUAL EXAMPLES (10x10 grid)
// =============================================================================
//
// COMPLEXITY 0: Empty Rectangle
// -----------------------------
// Backward compatible with original SynchroEnv. No RNG consumed.
//
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+
//
//   ## = Wall, (space) = Floor
//   - Wall perimeter, floor interior
//   - 64 walkable cells (8x8 interior)
//
//
// COMPLEXITY 1: Scattered Obstacles
// ----------------------------------
// Empty rectangle + random obstacles (~4% density).
// Obstacles placed one-by-one; reverted if they break connectivity.
//
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |##|  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |##|  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |##|  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |##|  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+
//
//   - ~4% obstacle density (complexity * 4)
//   - Connectivity guaranteed (flood-fill check after each obstacle)
//
//
// COMPLEXITY 2-5: Rooms and Corridors
// ------------------------------------
// Start with all walls, carve rooms, connect with L-shaped corridors.
//
// Algorithm:
//   1. Fill grid with walls
//   2. Place N non-overlapping rooms (N = complexity)
//   3. Connect rooms sequentially with L-shaped corridors
//   4. Add extra corridor connections (more at lower complexity)
//   5. Scatter obstacles in open areas
//   6. Validate connectivity; fallback to simpler generation if broken
//
// COMPLEXITY 2 (2 rooms, wide corridors, 8% obstacles):
//
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |OO|  |  |  |  |c |c |c |##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |c |c |c |##|##|  <- corridor (width 2)
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |##|##|##|##|c |c |##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |##|##|##|##|c |c |##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|c |c |##|  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|c |c |c |  |  |  |  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|c |c |c |  |  |  |OO|  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+
//
//   c = corridor cells (width 2), OO = obstacle
//   - Room 1 (top-left): L-shaped, extends down
//   - Room 2 (bottom-right): rectangular
//   - L-shaped corridor connection with width 2
//
//
// COMPLEXITY 5 (5 rooms, narrow corridors, 20% obstacles):
//
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |##|##|##|##|  |  |  |##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |##|  |##|##|##|##|  |  |  |##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|  |  |  |  |  |  |  |  |##|  |  |  |  |##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|  |##|##|  |##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|  |  |  |##|##|##|##|  |##|  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|  |##|  |##|##|##|##|  |##|  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|  |  |  |  |  |  |  |  |##|  |  |  |##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |##|##|##|##|##|##|##|##|##|##|##|##|##|##|##|##|
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//
//   - More rooms, narrower corridors (width 1)
//   - Higher obstacle density in open areas
//   - Requires more pathfinding to navigate
//
// =============================================================================
// DERIVED PARAMETERS FROM COMPLEXITY
// =============================================================================
//
//   | Complexity | Num Rooms | Corridor Width | Obstacle % | Generation    |
//   |------------|-----------|----------------|------------|---------------|
//   | 0          | 0         | N/A            | 0%         | Empty rect    |
//   | 1          | 0         | N/A            | 4%         | Obstacles     |
//   | 2          | 2         | 2              | 8%         | Rooms+corr    |
//   | 3          | 3         | 1-2            | 12%        | Rooms+corr    |
//   | 4          | 4         | 1              | 16%        | Rooms+corr    |
//   | 5          | 5         | 1              | 20%        | Rooms+corr    |
//
// Extra corridor connections: max(0, 3 - (complexity - 2))
//   - Complexity 2: +1 extra connection (more paths)
//   - Complexity 5: +0 extra connections (maze-like)
//
// =============================================================================
// ENV-OVERRIDABLE BOUNDS
// =============================================================================
// Each env can override default bounds via MapConfig:
//
//   MapConfig config = MapGenerator::DefaultConfig(12, 12, 3, seed);
//   config.num_rooms = {2, 2};        // Force exactly 2 rooms
//   config.corridor_width = {3, 3};   // Force wide corridors
//   config.obstacle_density = {0, 0}; // No obstacles
//   auto grid = MapGenerator::Generate(config);
//
// =============================================================================


#ifndef COMPANIONS_CORE_MAP_GENERATOR_H_
#define COMPANIONS_CORE_MAP_GENERATOR_H_

#include <climits>
#include <memory>
#include <random>
#include <vector>

#include "grid.h"
#include "types.h"

namespace companions {

// =============================================================================
// MapBounds - constrain a parameter within [min, max]
// INT_MAX for max means "use complexity-derived default"
// =============================================================================
struct MapBounds {
  int min = 0;
  int max = INT_MAX;

  // Clamp a value to these bounds
  int Clamp(int value) const {
    if (max == INT_MAX) return value;  // No upper bound override
    return std::max(min, std::min(max, value));
  }

  // Check if bounds override the default (i.e., max != INT_MAX)
  bool HasOverride() const { return max != INT_MAX; }
};

// =============================================================================
// MapConfig - all parameters for map generation
// =============================================================================
struct MapConfig {
  int rows = 12;
  int cols = 12;
  int complexity = 0;  // 0-5, single complexity knob
  uint64_t seed = 0;

  // Env-overridable bounds
  // If max == INT_MAX, use complexity-derived default
  MapBounds num_rooms;         // default: [0, complexity]
  MapBounds corridor_width;    // default: [max(1, 3-complexity/2), 3]
  MapBounds obstacle_density;  // default: [0, complexity*4] (as percentage 0-100)
  MapBounds dead_end_count;    // default: [0, complexity]
};

// =============================================================================
// Room - represents a rectangular room in the map
// =============================================================================
struct Room {
  int top;
  int left;
  int width;
  int height;

  Position Center() const {
    return {top + height / 2, left + width / 2};
  }

  bool Overlaps(const Room& other, int margin = 1) const {
    return !(left + width + margin <= other.left ||
             other.left + other.width + margin <= left ||
             top + height + margin <= other.top ||
             other.top + other.height + margin <= top);
  }
};

// =============================================================================
// Complexity 2: New room/corridor generation structures
// =============================================================================

enum class SplitDirection { Horizontal, Vertical };

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

// Room shape for complexity 2
struct RoomShape {
  std::vector<Position> cells;  // All floor cells in the room
  int top, left, width, height; // Bounding box
};

// =============================================================================
// MapGenerator - procedural map generation
// =============================================================================
class MapGenerator {
 public:
  // Generate a map from config
  static std::unique_ptr<Grid> Generate(const MapConfig& config);

  // Convenience: create config with defaults for given dimensions and complexity
  static MapConfig DefaultConfig(int rows, int cols, int complexity,
                                 uint64_t seed);

 private:
  MapGenerator(const MapConfig& config);

  // Main generation phases
  std::unique_ptr<Grid> GenerateInternal();
  void GenerateEmptyRectangle();
  void GenerateWithObstacles();
  void GenerateWithRooms();

  // Room generation helpers (legacy, used for complexity >= 3 if re-enabled)
  std::vector<Room> PlaceRooms(int num_rooms);
  void CarveRoom(const Room& room);
  void ConnectRooms(const std::vector<Room>& rooms);
  void CarveCorridorH(int row, int col1, int col2, int width);
  void CarveCorridorV(int col, int row1, int row2, int width);

  // Complexity 2: Two rooms with dual corridors
  struct Quadrant {
    int top, left, height, width;  // Bounds (excluding separator and border)
  };
  std::pair<Quadrant, Quadrant> DivideIntoQuadrants(SplitDirection* out_dir);
  RoomShape GenerateRoomInQuadrant(const Quadrant& quad, bool prefer_l_toward_other);
  void CarveRoomShape(const RoomShape& shape);
  std::vector<Door> GenerateDoorsForRoom(const RoomShape& shape,
                                         SplitDirection split_dir,
                                         bool is_first_room, int door_width);
  std::vector<CorridorCandidate> GenerateCorridorCandidates(
      const std::vector<Door>& doors1, const std::vector<Door>& doors2,
      int corridor_width);
  bool ValidateCorridor(const CorridorCandidate& corridor,
                        const RoomShape& room1, const RoomShape& room2) const;
  std::vector<CorridorCandidate> SelectCorridorPair(
      const std::vector<CorridorCandidate>& candidates);
  void CarveCorridor(const CorridorCandidate& corridor);

  // Obstacle generation
  void ScatterObstacles(int density_percent);

  // Connectivity validation
  bool IsConnected() const;
  int FloodFill(Position start, std::vector<std::vector<bool>>& visited) const;

  // Derived parameters from complexity
  int GetNumRooms() const;
  int GetCorridorWidth() const;
  int GetObstacleDensity() const;
  int GetDeadEndCount() const;

  MapConfig config_;
  std::unique_ptr<Grid> grid_;
  std::mt19937 rng_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_MAP_GENERATOR_H_
