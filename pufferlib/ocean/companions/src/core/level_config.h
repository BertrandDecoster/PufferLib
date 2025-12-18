// Copyright 2024
// LevelConfig - Complete level generation configuration
//
// LevelConfig extends MapConfig with env-specific cell placements.
// Used by the unified map generation system to create full-info levels
// that can be loaded into any environment type.

#ifndef COMPANIONS_CORE_LEVEL_CONFIG_H_
#define COMPANIONS_CORE_LEVEL_CONFIG_H_

#include "map_generator.h"
#include "types.h"

namespace companions {

// =============================================================================
// LevelConfig - Full level generation configuration
// =============================================================================
struct LevelConfig {
  // Base map configuration (grid structure, walls, rooms)
  MapConfig map;

  // Synchro cells (for SynchroEnv)
  int synchro_cell_count = 0;  // Number of synchro cells to place (0 = none)

  // Patrol square (for AggroEnv)
  int patrol_square_size = 0;  // Size of patrol square perimeter (0 = none)
  // Note: patrol_square_size=3 means a 3x3 square with 8 perimeter cells

  // Target cell (for AggroEnv)
  bool has_target_cell = false;

  // Agent configuration
  int num_companions = 3;  // Number of companion agents
  int num_enemies = 0;     // Number of FSM enemies

  // Episode configuration
  int horizon = kDefaultHorizon;

  // D4 symmetry transform (0-7)
  int d4_transform = 0;

  // ==========================================================================
  // Factory methods for common configurations
  // ==========================================================================

  // Create config for SynchroEnv
  static LevelConfig ForSynchro(int rows, int cols, int num_companions,
                                int num_synchro, int map_complexity,
                                unsigned int seed, int d4_transform = 0,
                                int horizon = kDefaultHorizon) {
    LevelConfig config;
    config.map = MapGenerator::DefaultConfig(rows, cols, map_complexity, seed);
    config.synchro_cell_count = num_synchro;
    config.num_companions = num_companions;
    config.d4_transform = d4_transform;
    config.horizon = horizon;
    return config;
  }

  // Create config for AggroEnv
  static LevelConfig ForAggro(int grid_size, int num_companions,
                              int patrol_size, unsigned int seed,
                              int d4_transform = 0,
                              int horizon = kDefaultHorizon) {
    LevelConfig config;
    // AggroEnv uses simpler maps (no procedural rooms)
    config.map = MapGenerator::DefaultConfig(grid_size, grid_size, 0, seed);
    config.patrol_square_size = patrol_size;
    config.has_target_cell = true;
    config.num_companions = num_companions;
    config.num_enemies = 1;
    config.d4_transform = d4_transform;
    config.horizon = horizon;
    return config;
  }

  // Create config for DodgeEnv
  static LevelConfig ForDodge(int grid_size, int num_companions,
                              unsigned int seed, int d4_transform = 0,
                              int horizon = kDefaultHorizon) {
    LevelConfig config;
    config.map = MapGenerator::DefaultConfig(grid_size, grid_size, 0, seed);
    config.num_companions = num_companions;
    config.d4_transform = d4_transform;
    config.horizon = horizon;
    return config;
  }

  // Create a full-info config with all cell types (for plan checkpoints)
  static LevelConfig FullInfo(int rows, int cols, int map_complexity,
                              int num_companions, int num_synchro,
                              int patrol_size, bool target,
                              unsigned int seed, int d4_transform = 0,
                              int horizon = kDefaultHorizon) {
    LevelConfig config;
    config.map = MapGenerator::DefaultConfig(rows, cols, map_complexity, seed);
    config.synchro_cell_count = num_synchro;
    config.patrol_square_size = patrol_size;
    config.has_target_cell = target;
    config.num_companions = num_companions;
    config.num_enemies = (patrol_size > 0) ? 1 : 0;
    config.d4_transform = d4_transform;
    config.horizon = horizon;
    return config;
  }
};

}  // namespace companions

#endif  // COMPANIONS_CORE_LEVEL_CONFIG_H_
