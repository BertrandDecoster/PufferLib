// Copyright 2024
// DodgeEnv implementation

#include "dodge_env.h"

#include <algorithm>
#include <cassert>
#include <random>
#include <stdexcept>

#include "../core/agent_config.h"  // For TargetFilter
#include "../core/cell.h"
#include "../core/effect_config.h"
#include "../core/level_builder.h"
#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "effect_system.h"

namespace companions {

DodgeEnv::DodgeEnv(int grid_size, int num_companions, int hazard_interval,
                   int horizon, unsigned int seed, int d4_transform)
    : BaseEnv(grid_size, grid_size, d4_transform),
      num_companions_(num_companions),
      hazard_interval_(hazard_interval),
      seed_(seed),
      rng_(seed) {
  horizon_ = horizon;
  ValidateConfig();

  // Register default effects if not already done
  RegisterDefaultEffects();

  // Set up hazard effect names
  hazard_effects_ = {"dodge_fire", "dodge_wind"};

  Reset();
}

DodgeEnv::DodgeEnv(const DodgeEnv& other)
    : BaseEnv(other),
      num_companions_(other.num_companions_),
      hazard_interval_(other.hazard_interval_),
      seed_(other.seed_),
      rng_(other.rng_),
      success_(other.success_),
      any_dead_(other.any_dead_),
      hazard_effects_(other.hazard_effects_) {}

DodgeEnv& DodgeEnv::operator=(const DodgeEnv& other) {
  if (this != &other) {
    BaseEnv::operator=(other);
    num_companions_ = other.num_companions_;
    hazard_interval_ = other.hazard_interval_;
    seed_ = other.seed_;
    rng_ = other.rng_;
    success_ = other.success_;
    any_dead_ = other.any_dead_;
    hazard_effects_ = other.hazard_effects_;
  }
  return *this;
}

std::unique_ptr<BaseEnv> DodgeEnv::Clone() const {
  return std::make_unique<DodgeEnv>(*this);
}

void DodgeEnv::ValidateConfig() {
  if (num_companions_ < 1 || num_companions_ > 3) {
    throw std::invalid_argument("num_companions must be 1, 2, or 3");
  }
  if (rows_ < 5 || cols_ < 5) {
    throw std::invalid_argument("grid_size must be >= 5");
  }
  if (hazard_interval_ < 1) {
    throw std::invalid_argument("hazard_interval must be >= 1");
  }
  if (horizon_ < 1) {
    throw std::invalid_argument("horizon must be >= 1");
  }
}

void DodgeEnv::RegisterDefaultEffects() {
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  // Check if already registered
  if (registry.GetConfig("dodge_fire") != nullptr) {
    return;
  }

  // Fire burst: 3x3 damage area
  EffectConfig fire;
  fire.name = "dodge_fire";
  fire.telegraph_ticks = 2;
  fire.active_ticks = 1;
  fire.recovery_ticks = 0;
  fire.loop = 0;
  fire.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // 3x3 full
  fire.filter = TargetFilter::Companion;
  fire.damage = 3;  // Lethal
  fire.telegraph_visible = true;
  registry.RegisterConfig(fire);

  // Wind push: 1x1 push effect
  EffectConfig wind;
  wind.name = "dodge_wind";
  wind.telegraph_ticks = 1;
  wind.active_ticks = 1;
  wind.recovery_ticks = 0;
  wind.loop = 0;
  wind.area = {1};  // 1x1
  wind.filter = TargetFilter::Companion;
  wind.damage = 0;
  wind.push_dx = 0;
  wind.push_dy = 1;  // Push down
  wind.push_distance = 2;
  wind.telegraph_visible = true;
  registry.RegisterConfig(wind);
}

void DodgeEnv::Reset() {
  // Reset state
  success_ = false;
  any_dead_ = false;

  // Clear effects
  ClearEffects();

  // Create level config for dodge env
  LevelConfig config = LevelConfig::ForDodge(
      rows_, num_companions_, static_cast<unsigned int>(rng_()),
      d4_transform_, horizon_);

  // Generate snapshot using LevelGenerator
  Snapshot snapshot = LevelGenerator::Generate(config);

  // Load the snapshot (handles grid, companions, timing, D4 transform)
  LoadSnapshot(snapshot);
}

void DodgeEnv::Reset(unsigned int seed) {
  rng_.seed(seed);
  seed_ = seed;
  Reset();
}

void DodgeEnv::SetupGrid() {
  // Create fresh grid
  grid_ = std::make_unique<Grid>(rows_, cols_);
  object_manager_ = std::make_unique<ObjectManager>(rows_, cols_);

  // Update EffectSystem pointers since grid_ and object_manager_ were recreated
  effect_system_->UpdatePointers(object_manager_.get(), grid_.get());

  // Use LevelBuilder
  LevelBuilder builder(*grid_);

  // Fill with floor
  builder.Fill(CellKind::Floor);

  // Add walls around perimeter
  builder.Border(CellKind::Wall);
}

void DodgeEnv::SpawnCompanions() {
  // Find empty cells in the interior
  std::vector<Position> empty = FindEmptyCells(num_companions_, rng_);

  for (int i = 0; i < num_companions_; ++i) {
    Player* player = object_manager_->CreateActor<Player>(empty[i]);
    player->SetMaxHealth(3);
    player->SetFaction(Faction::COMPANION);
  }
}

void DodgeEnv::SpawnHazard() {
  // Find valid spawn positions (any floor cell)
  std::vector<Position> floor_cells = grid_->FindCellsOfKind(CellKind::Floor);

  if (floor_cells.empty()) return;

  // Pick a random position
  std::uniform_int_distribution<size_t> pos_dist(0, floor_cells.size() - 1);
  Position spawn_pos = floor_cells[pos_dist(rng_)];

  // Pick a random hazard type
  std::uniform_int_distribution<size_t> hazard_dist(0, hazard_effects_.size() - 1);
  const std::string& effect_name = hazard_effects_[hazard_dist(rng_)];

  // Pick a random direction for push effects
  std::uniform_int_distribution<int> dir_dist(0, 3);
  Direction dir = static_cast<Direction>(dir_dist(rng_));

  // Spawn the effect
  SpawnEffect(effect_name, EffectTarget::AtCell(spawn_pos), dir);
}

bool DodgeEnv::IsDone() const {
  // Done if survived all ticks
  if (tick_ >= horizon_) {
    return true;
  }

  // Done if any companion died
  if (any_dead_) {
    return true;
  }

  return false;
}

void DodgeEnv::PreStep() {
  // Spawn hazards at regular intervals
  if (tick_ > 0 && tick_ % hazard_interval_ == 0) {
    SpawnHazard();
  }
}

void DodgeEnv::PostStep() {
  // Check for deaths
  for (const Agent* agent : object_manager_->GetAllAgents()) {
    if (agent->IsDead()) {
      any_dead_ = true;
      break;
    }
  }

  // Check for success
  if (tick_ >= horizon_ && !any_dead_) {
    success_ = true;
  }
}

void DodgeEnv::CalculateRewards(std::vector<double>& rewards) {
  // Base survival bonus
  for (size_t i = 0; i < rewards.size(); ++i) {
    if (!any_dead_) {
      rewards[i] += kSurvivalBonus;
    }
  }

  // Win reward
  if (success_) {
    for (size_t i = 0; i < rewards.size(); ++i) {
      rewards[i] += kWinReward;
    }
  }

  // Death penalty
  if (any_dead_) {
    for (size_t i = 0; i < rewards.size(); ++i) {
      rewards[i] += kDeathPenalty;
    }
  }
}

void DodgeEnv::ObservationTensor(std::vector<float>& values, int player) const {
  auto shape = ObservationShape();
  int total = 1;
  for (int dim : shape) total *= dim;

  values.resize(total);
  std::fill(values.begin(), values.end(), 0.0f);

  // 7-plane observation:
  // Plane 0: Floor cells
  // Plane 1: Wall cells
  // Plane 2: Synchro cells (unused in DodgeEnv)
  // Plane 3: Current player position
  // Plane 4: Other agents positions
  // Plane 5: Telegraph zones (danger zones showing where effects will hit)
  // Plane 6: Active effect zones (currently damaging areas)
  auto set_plane = [&](int plane, int row, int col, float value) {
    values[plane * rows_ * cols_ + row * cols_ + col] = value;
  };

  // Get current player agent
  auto agents = object_manager_->GetAllAgents();
  const Agent* current_agent = nullptr;
  if (player >= 0 && player < static_cast<int>(agents.size())) {
    current_agent = agents[player];
  }

  // Planes 0-4: Same as base implementation
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      Position pos{r, c};
      CellKind kind = grid_->GetCellKind(pos);

      if (grid_->IsWalkable(pos)) {
        set_plane(0, r, c, 1.0f);
      }
      if (kind == CellKind::Wall) {
        set_plane(1, r, c, 1.0f);
      }
      if (kind == CellKind::Synchro) {
        set_plane(2, r, c, 1.0f);
      }

      const Actor* actor = object_manager_->GetActorAt(pos);
      if (actor && actor->IsAlive()) {
        if (current_agent && actor->GetId() == current_agent->GetId()) {
          set_plane(3, r, c, 1.0f);
        } else {
          set_plane(4, r, c, 1.0f);
        }
      }
    }
  }

  // Planes 5-6: Effect zones
  for (const auto& effect : GetActiveEffects()) {
    if (!effect.config || !effect.config->telegraph_visible) continue;

    Position center = effect.GetCenter(*object_manager_);
    int area_size = effect.config->GetAreaSize();
    int half = area_size / 2;

    // Mark affected cells
    for (int dr = -half; dr <= half; ++dr) {
      for (int dc = -half; dc <= half; ++dc) {
        if (!effect.config->IsPositionAffected(dr, dc, effect.direction)) {
          continue;
        }

        int r = center.row + dr;
        int c = center.col + dc;
        if (r < 0 || r >= rows_ || c < 0 || c >= cols_) continue;

        if (effect.in_telegraph) {
          set_plane(5, r, c, 1.0f);  // Telegraph zone
        } else {
          set_plane(6, r, c, 1.0f);  // Active zone
        }
      }
    }
  }
}

std::vector<int> DodgeEnv::ObservationShape() const {
  return {7, rows_, cols_};  // 7 planes instead of 5
}

// =============================================================================
// Vector Observation
// =============================================================================

int DodgeEnv::VectorObservationSize() const {
  // Base features (8) + DodgeEnv-specific (10)
  return BaseEnv::VectorObservationSize() + 10;
}

void DodgeEnv::VectorObservation(std::vector<float>& values, int player) const {
  // Start with base features
  BaseEnv::VectorObservation(values, player);

  // Resize to include our additional features
  int base_size = BaseEnv::VectorObservationSize();
  values.resize(VectorObservationSize(), 0.0f);

  auto agents = object_manager_->GetAllAgents();
  if (player < 0 || player >= static_cast<int>(agents.size())) {
    return;
  }

  const Agent* current_agent = agents[player];
  Position my_pos = current_agent->GetPosition();
  float max_dim = static_cast<float>(std::max(rows_, cols_));

  int idx = base_size;

  // Feature: Survival progress (1)
  int ticks_remaining = horizon_ - tick_;
  values[idx++] = static_cast<float>(ticks_remaining) / horizon_;

  // Feature: Number of active effects normalized (1)
  constexpr int kMaxEffects = 10;  // Normalization cap
  values[idx++] = std::min(1.0f, static_cast<float>(GetActiveEffects().size()) / kMaxEffects);

  // Features: Danger in each direction (4) - how close is nearest ACTIVE hazard
  // Direction order: Up, Down, Left, Right
  float danger_up = 1.0f, danger_down = 1.0f, danger_left = 1.0f, danger_right = 1.0f;

  // Features: Telegraph danger in each direction (4)
  float telegraph_up = 1.0f, telegraph_down = 1.0f, telegraph_left = 1.0f, telegraph_right = 1.0f;

  for (const auto& effect : GetActiveEffects()) {
    if (!effect.config) continue;

    Position center = effect.target.cell;

    // Check all cells affected by this effect
    int area_size = effect.config->GetAreaSize();
    int half = area_size / 2;

    for (int dr = -half; dr <= half; ++dr) {
      for (int dc = -half; dc <= half; ++dc) {
        int area_idx = (dr + half) * area_size + (dc + half);
        if (area_idx < 0 || area_idx >= static_cast<int>(effect.config->area.size())) continue;
        if (effect.config->area[area_idx] == 0) continue;

        Position affected = {center.row + dr, center.col + dc};

        // Calculate relative position and distance
        int delta_row = affected.row - my_pos.row;
        int delta_col = affected.col - my_pos.col;
        float dist = static_cast<float>(std::abs(delta_row) + std::abs(delta_col));
        float norm_dist = dist / max_dim;

        // Determine primary direction and update danger values
        if (delta_row < 0 && std::abs(delta_row) >= std::abs(delta_col)) {
          // Effect is above
          if (effect.in_telegraph) {
            telegraph_up = std::min(telegraph_up, norm_dist);
          } else {
            danger_up = std::min(danger_up, norm_dist);
          }
        } else if (delta_row > 0 && std::abs(delta_row) >= std::abs(delta_col)) {
          // Effect is below
          if (effect.in_telegraph) {
            telegraph_down = std::min(telegraph_down, norm_dist);
          } else {
            danger_down = std::min(danger_down, norm_dist);
          }
        } else if (delta_col < 0) {
          // Effect is left
          if (effect.in_telegraph) {
            telegraph_left = std::min(telegraph_left, norm_dist);
          } else {
            danger_left = std::min(danger_left, norm_dist);
          }
        } else if (delta_col > 0) {
          // Effect is right
          if (effect.in_telegraph) {
            telegraph_right = std::min(telegraph_right, norm_dist);
          } else {
            danger_right = std::min(danger_right, norm_dist);
          }
        }
        // If delta_row == 0 && delta_col == 0, player is on the effect - very dangerous!
        // This would set all directions to 0 distance
      }
    }
  }

  // Add danger features (inverted: 0 = far/safe, 1 = close/dangerous)
  values[idx++] = 1.0f - danger_up;
  values[idx++] = 1.0f - danger_down;
  values[idx++] = 1.0f - danger_left;
  values[idx++] = 1.0f - danger_right;

  // Add telegraph features
  values[idx++] = 1.0f - telegraph_up;
  values[idx++] = 1.0f - telegraph_down;
  values[idx++] = 1.0f - telegraph_left;
  values[idx++] = 1.0f - telegraph_right;
}

void DodgeEnv::ValidateSnapshot(const Snapshot& snapshot) const {
  // DodgeEnv has minimal requirements - just needs floor cells for movement
  int floor_count = snapshot.CountCells(CellKind::Floor);
  if (floor_count < num_companions_) {
    throw std::runtime_error(
        "DodgeEnv requires at least " + std::to_string(num_companions_) +
        " floor cells for companions, but snapshot has " +
        std::to_string(floor_count));
  }
}

}  // namespace companions
