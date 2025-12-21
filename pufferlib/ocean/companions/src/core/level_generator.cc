// Copyright 2024
// LevelGenerator implementation

#include "level_generator.h"

#include <algorithm>
#include <stdexcept>

#include "map_generator.h"
#include "object.h"

namespace companions {

// =============================================================================
// Static entry point
// =============================================================================

Snapshot LevelGenerator::Generate(const LevelConfig& config) {
  LevelGenerator generator(config);
  return generator.GenerateInternal();
}

// =============================================================================
// Constructor
// =============================================================================

LevelGenerator::LevelGenerator(const LevelConfig& config)
    : config_(config), rng_(config.map.seed) {}

// =============================================================================
// Main generation
// =============================================================================

Snapshot LevelGenerator::GenerateInternal() {
  // Phase 1: Generate base grid using MapGenerator
  GenerateBaseGrid();

  // Phase 2: Place special cells
  if (config_.synchro_cell_count > 0) {
    PlaceSynchroCells();
  }
  if (config_.patrol_square_size > 0) {
    PlacePatrolSquare();
  }
  if (config_.has_target_cell) {
    PlaceTargetCell();
  }

  // Phase 3: Spawn agents
  SpawnCompanions();
  if (config_.num_enemies > 0) {
    SpawnEnemies();
  }

  // Convert to snapshot
  return CreateSnapshot();
}

// =============================================================================
// Generation phases
// =============================================================================

void LevelGenerator::GenerateBaseGrid() {
  grid_ = MapGenerator::Generate(config_.map);
  object_manager_ = std::make_unique<ObjectManager>(config_.map.rows, config_.map.cols);
}

void LevelGenerator::PlaceSynchroCells() {
  auto empty_cells = FindEmptyFloorCells();

  if (static_cast<int>(empty_cells.size()) < config_.synchro_cell_count) {
    throw std::runtime_error("Not enough empty cells for synchro placement");
  }

  // Shuffle and pick cells
  portable_shuffle(empty_cells.begin(), empty_cells.end(), rng_);

  for (int i = 0; i < config_.synchro_cell_count; ++i) {
    Position pos = empty_cells[i];
    grid_->SetCell(pos, CellKind::Synchro);
    synchro_positions_.push_back(pos);
  }
}

void LevelGenerator::PlacePatrolSquare() {
  // Patrol square is a square perimeter that the enemy walks
  // Size 3 = 3x3 square with 8 perimeter cells
  int size = config_.patrol_square_size;

  // Find a valid location for the patrol square (needs size x size of walkable cells)
  int rows = config_.map.rows;
  int cols = config_.map.cols;

  // Try random positions until we find one that works
  std::vector<Position> candidates;
  for (int r = 2; r < rows - size - 1; ++r) {
    for (int c = 2; c < cols - size - 1; ++c) {
      candidates.push_back({r, c});
    }
  }

  portable_shuffle(candidates.begin(), candidates.end(), rng_);

  for (const Position& top_left : candidates) {
    // Check if all cells in the square are walkable
    bool valid = true;
    for (int dr = 0; dr < size && valid; ++dr) {
      for (int dc = 0; dc < size && valid; ++dc) {
        Position p{top_left.row + dr, top_left.col + dc};
        if (!grid_->IsWalkable(p)) {
          valid = false;
        }
      }
    }

    if (valid) {
      // Generate clockwise patrol path (perimeter cells only)
      patrol_path_.clear();

      // Top edge (left to right)
      for (int c = 0; c < size; ++c) {
        patrol_path_.push_back({top_left.row, top_left.col + c});
      }
      // Right edge (top to bottom, excluding corners)
      for (int r = 1; r < size; ++r) {
        patrol_path_.push_back({top_left.row + r, top_left.col + size - 1});
      }
      // Bottom edge (right to left, excluding corners)
      for (int c = size - 2; c >= 0; --c) {
        patrol_path_.push_back({top_left.row + size - 1, top_left.col + c});
      }
      // Left edge (bottom to top, excluding corners)
      for (int r = size - 2; r > 0; --r) {
        patrol_path_.push_back({top_left.row + r, top_left.col});
      }

      // Enemy spawns at first patrol position
      enemy_spawn_position_ = patrol_path_[0];
      return;
    }
  }

  throw std::runtime_error("Could not find valid location for patrol square");
}

void LevelGenerator::PlaceTargetCell() {
  // Target cell should be placed away from patrol area
  auto empty_cells = FindEmptyFloorCells();

  // Filter out cells near patrol path
  std::vector<Position> valid_cells;
  for (const Position& pos : empty_cells) {
    bool too_close = false;
    for (const Position& patrol_pos : patrol_path_) {
      int dist = std::abs(pos.row - patrol_pos.row) +
                 std::abs(pos.col - patrol_pos.col);
      if (dist < 4) {  // Keep target at least 4 cells from patrol
        too_close = true;
        break;
      }
    }
    if (!too_close) {
      valid_cells.push_back(pos);
    }
  }

  if (valid_cells.empty()) {
    // Fallback: use any empty cell
    valid_cells = empty_cells;
  }

  if (valid_cells.empty()) {
    throw std::runtime_error("No valid position for target cell");
  }

  portable_shuffle(valid_cells.begin(), valid_cells.end(), rng_);
  target_position_ = valid_cells[0];
  grid_->SetCell(target_position_, CellKind::Target);
}

void LevelGenerator::SpawnCompanions() {
  auto empty_cells = FindEmptyFloorCells();

  if (static_cast<int>(empty_cells.size()) < config_.num_companions) {
    throw std::runtime_error("Not enough empty cells for companion spawning");
  }

  // Filter out cells near patrol path (if exists)
  std::vector<Position> valid_cells;
  for (const Position& pos : empty_cells) {
    bool too_close = false;
    for (const Position& patrol_pos : patrol_path_) {
      int dist = std::abs(pos.row - patrol_pos.row) +
                 std::abs(pos.col - patrol_pos.col);
      if (dist < 4) {  // Keep companions at least 4 cells from patrol
        too_close = true;
        break;
      }
    }
    if (!too_close) {
      valid_cells.push_back(pos);
    }
  }

  if (static_cast<int>(valid_cells.size()) < config_.num_companions) {
    // Fallback: use any empty cell
    valid_cells = empty_cells;
  }

  portable_shuffle(valid_cells.begin(), valid_cells.end(), rng_);

  // Colors for companions (cycle through available colors)
  const ActorColor colors[] = {ActorColor::Red, ActorColor::Green, ActorColor::Blue};
  const int num_colors = sizeof(colors) / sizeof(colors[0]);

  // First companion is the player, rest are NPCs
  for (int i = 0; i < config_.num_companions; ++i) {
    Position pos = valid_cells[i];
    ActorColor color = colors[i % num_colors];
    if (i == 0) {
      auto* player = object_manager_->CreateActor<Player>(pos);
      player->SetColor(color);
    } else {
      auto* npc = object_manager_->CreateActor<NPCCompanion>(pos);
      npc->SetColor(color);
    }
  }
}

void LevelGenerator::SpawnEnemies() {
  // Spawn enemies at patrol start positions
  if (!patrol_path_.empty() && config_.num_enemies > 0) {
    AgentFSM* enemy = object_manager_->CreateActor<AgentFSM>(enemy_spawn_position_);
    enemy->SetFaction(Faction::ENEMY);
    // FSM setup would be done by the environment after loading
  }
}

// =============================================================================
// Helpers
// =============================================================================

std::vector<Position> LevelGenerator::FindEmptyFloorCells() {
  std::vector<Position> result;

  for (int r = 1; r < config_.map.rows - 1; ++r) {
    for (int c = 1; c < config_.map.cols - 1; ++c) {
      Position pos{r, c};
      if (grid_->GetCellKind(pos) == CellKind::Floor &&
          !object_manager_->IsOccupied(pos)) {
        result.push_back(pos);
      }
    }
  }

  return result;
}

bool LevelGenerator::IsValidSpawnPosition(Position pos) const {
  if (!grid_->IsInBounds(pos)) return false;
  if (!grid_->IsWalkable(pos)) return false;
  if (object_manager_->IsOccupied(pos)) return false;
  return true;
}

Snapshot LevelGenerator::CreateSnapshot() const {
  Snapshot snap;

  // Grid dimensions
  snap.rows = config_.map.rows;
  snap.cols = config_.map.cols;

  // Grid cells
  auto cell_data = grid_->GetAllCellData();
  snap.cells.reserve(cell_data.size());
  for (const auto& [kind, origin] : cell_data) {
    snap.cells.push_back({kind, origin});
  }

  // Agents
  for (const Agent* agent : object_manager_->GetAllAgents()) {
    AgentSnapshot as;
    as.id = agent->GetId();
    as.type = static_cast<int>(agent->GetType());
    as.position = agent->GetPosition();
    as.prev_position = agent->GetPosition();
    as.health = agent->GetHealth();
    as.max_health = agent->GetMaxHealth();
    as.agent_index = agent->GetAgentIndex();
    as.faction = static_cast<int>(agent->GetFaction());
    as.alive = agent->IsAlive();

    // Direction for Companions
    if (const Companion* comp = dynamic_cast<const Companion*>(agent)) {
      as.direction = static_cast<int>(comp->GetDirection());
      as.color = static_cast<int>(comp->GetColor());
    }

    // FSM data for enemies - store patrol path
    if (const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      if (!patrol_path_.empty()) {
        as.has_fsm = true;
        as.fsm.patrol_path = patrol_path_;
        as.fsm.patrol_index = 0;
        as.fsm.patrol_forward = true;
      }
    }

    snap.agents.push_back(std::move(as));
  }

  // Timing
  snap.tick = 0;
  snap.horizon = config_.horizon;
  snap.d4_transform = config_.d4_transform;

  // Patrol path (if generated)
  snap.patrol_path = patrol_path_;

  // RNG state
  snap.rng_state = rng_.GetState();
  snap.rng_inc = rng_.GetInc();

  return snap;
}

}  // namespace companions
