// Copyright 2024
// LevelGenerator - Full level generation with all cell types
//
// LevelGenerator builds on MapGenerator to create complete levels
// with all cell types (synchro, target, patrol) and spawned agents.
// Returns a Snapshot that can be loaded into any compatible environment.

#ifndef COMPANIONS_CORE_LEVEL_GENERATOR_H_
#define COMPANIONS_CORE_LEVEL_GENERATOR_H_

#include <memory>
#include <vector>

#include "grid.h"
#include "level_config.h"
#include "object_manager.h"
#include "pcg32.h"
#include "snapshot.h"
#include "types.h"

namespace companions {

// =============================================================================
// LevelGenerator - Generate complete levels with all features
// =============================================================================
class LevelGenerator {
 public:
  // Generate a complete level from config, returning a Snapshot
  static Snapshot Generate(const LevelConfig& config);

 private:
  explicit LevelGenerator(const LevelConfig& config);

  Snapshot GenerateInternal();

  // Generation phases
  void GenerateBaseGrid();
  void PlaceSynchroCells();
  void PlacePatrolSquare();
  void PlaceTargetCell();
  void SpawnCompanions();
  void SpawnEnemies();

  // Helper: find empty floor cells
  std::vector<Position> FindEmptyFloorCells();

  // Helper: check if position is valid for placement
  bool IsValidSpawnPosition(Position pos) const;

  // Convert current state to Snapshot
  Snapshot CreateSnapshot() const;

  LevelConfig config_;
  std::unique_ptr<Grid> grid_;
  std::unique_ptr<ObjectManager> object_manager_;
  pcg32 rng_;

  // Stored positions for snapshot
  std::vector<Position> synchro_positions_;
  std::vector<Position> patrol_path_;
  Position target_position_;
  Position enemy_spawn_position_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_LEVEL_GENERATOR_H_
