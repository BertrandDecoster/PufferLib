// Copyright 2024
// MapGenerator implementation - orchestrates modular generators

#include "map_generator.h"

#include <algorithm>
#include <stdexcept>

#include "connectivity.h"
#include "corridor_generator.h"
#include "generation_context.h"
#include "level_builder.h"
#include "room_generator.h"

// Debug flag - set to 1 to enable verbose output
#define MAP_GENERATOR_DEBUG 0

#if MAP_GENERATOR_DEBUG
#include <iostream>
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
    GenerationContext ctx(*grid_, rng_, config_);
    ScatterObstacles(ctx, density);
  }
}

void MapGenerator::GenerateWithRooms() {
  // Complexity 2: Two rooms with guaranteed separation and dual corridors
  MAP_DEBUG("=== GenerateWithRooms() START ===");
  MAP_DEBUG("Grid size: " << config_.rows << "x" << config_.cols);

  LevelBuilder builder(*grid_);
  GenerationContext ctx(*grid_, rng_, config_);

  // Start with all walls
  builder.Fill(CellKind::Wall);

  // Check minimum grid size (7x7)
  if (config_.rows < 7 || config_.cols < 7) {
    MAP_DEBUG("Grid too small, falling back to obstacles");
    GenerateWithObstacles();
    return;
  }

  // Create generators
  RoomGenerator room_gen(ctx);
  CorridorGenerator corridor_gen(ctx);

  // Step 1: Divide grid into two quadrants
  SplitDirection split_dir;
  auto [quad1, quad2] = room_gen.DivideIntoQuadrants(&split_dir);

  MAP_DEBUG("Split direction: " << (split_dir == SplitDirection::Horizontal ? "HORIZONTAL" : "VERTICAL"));
  MAP_DEBUG("Quadrant1: top=" << quad1.top << " left=" << quad1.left
            << " h=" << quad1.height << " w=" << quad1.width);
  MAP_DEBUG("Quadrant2: top=" << quad2.top << " left=" << quad2.left
            << " h=" << quad2.height << " w=" << quad2.width);

  // Step 2: Generate rooms in each quadrant
  RoomShape room1 = room_gen.GenerateInQuadrant(quad1, true);
  RoomShape room2 = room_gen.GenerateInQuadrant(quad2, false);

  MAP_DEBUG("Room1: " << room1.cells.size() << " cells, bounds top=" << room1.top
            << " left=" << room1.left << " h=" << room1.height << " w=" << room1.width);
  MAP_DEBUG("Room2: " << room2.cells.size() << " cells, bounds top=" << room2.top
            << " left=" << room2.left << " h=" << room2.height << " w=" << room2.width);

  // Step 3: Carve rooms
  room_gen.Carve(room1);
  room_gen.Carve(room2);

  // Step 4: Generate doors for each room
  int corridor_width = GetCorridorWidth();
  MAP_DEBUG("Corridor width: " << corridor_width);

  std::vector<Door> doors1 =
      corridor_gen.GenerateDoorsForRoom(room1, split_dir, true, corridor_width);
  std::vector<Door> doors2 =
      corridor_gen.GenerateDoorsForRoom(room2, split_dir, false, corridor_width);

  MAP_DEBUG("Room1 has " << doors1.size() << " possible doors");
  MAP_DEBUG("Room2 has " << doors2.size() << " possible doors");

  // Step 5: Generate all valid corridor candidates
  std::vector<CorridorCandidate> candidates =
      corridor_gen.GenerateCandidates(doors1, doors2, corridor_width);

  MAP_DEBUG("Generated " << candidates.size() << " corridor candidates");

  // Filter to valid corridors only
  std::vector<CorridorCandidate> valid_candidates;
  for (const auto& c : candidates) {
    if (corridor_gen.Validate(c, room1, room2)) {
      valid_candidates.push_back(c);
    }
  }

  MAP_DEBUG("Valid corridors: " << valid_candidates.size() << " (rejected "
            << (candidates.size() - valid_candidates.size()) << ")");

  // Step 6: Select corridor pair (or single if no valid pair)
  std::vector<CorridorCandidate> selected;
  if (!valid_candidates.empty()) {
    selected = corridor_gen.SelectPair(valid_candidates);
  }

  MAP_DEBUG("Selected " << selected.size() << " corridors");

  // Step 7: Carve corridors
  for (const auto& corridor : selected) {
    corridor_gen.Carve(corridor);
  }

  // Final connectivity check - fallback if broken
  if (!IsConnected(*grid_, config_.rows, config_.cols)) {
    MAP_DEBUG("Map not connected, falling back to obstacles!");
    GenerateWithObstacles();
  } else {
    MAP_DEBUG("=== GenerateWithRooms() SUCCESS ===");
  }
}

// =============================================================================
// Derived parameters
// =============================================================================

int MapGenerator::GetNumRooms() const {
  int default_val = config_.complexity;

  if (config_.num_rooms.HasOverride()) {
    return config_.num_rooms.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetCorridorWidth() const {
  int default_val = std::max(1, 3 - config_.complexity / 2);

  if (config_.corridor_width.HasOverride()) {
    return config_.corridor_width.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetObstacleDensity() const {
  int default_val = config_.complexity * 4;

  if (config_.obstacle_density.HasOverride()) {
    return config_.obstacle_density.Clamp(default_val);
  }
  return default_val;
}

int MapGenerator::GetDeadEndCount() const {
  int default_val = config_.complexity;

  if (config_.dead_end_count.HasOverride()) {
    return config_.dead_end_count.Clamp(default_val);
  }
  return default_val;
}

}  // namespace companions
