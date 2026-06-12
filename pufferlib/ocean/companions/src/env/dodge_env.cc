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
#include "dodge_lens.h"
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
  SetTaskLens(std::make_unique<DodgeLens>());
}

DodgeEnv::DodgeEnv(const DodgeEnv& other)
    : BaseEnv(other),
      num_companions_(other.num_companions_),
      hazard_interval_(other.hazard_interval_),
      seed_(other.seed_),
      rng_(other.rng_),
      any_dead_(other.any_dead_),
      hazard_effects_(other.hazard_effects_) {}

DodgeEnv& DodgeEnv::operator=(const DodgeEnv& other) {
  if (this != &other) {
    BaseEnv::operator=(other);
    num_companions_ = other.num_companions_;
    hazard_interval_ = other.hazard_interval_;
    seed_ = other.seed_;
    rng_ = other.rng_;
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

  // Wind: 5-cell cross (centre + 4 cardinals) with radial push.
  // Each cardinal cell pushes its occupant outward; the centre pushes in
  // the direction chosen at spawn (random, see DodgeEnv::SpawnHazard).
  // Active phase is telegraph_ticks=1 + active_ticks=1. The push_dx/dy
  // below are only used as the centre's fallback vector; radial_push=true
  // tells the effect system to derive each cardinal's direction from its
  // offset to the centre.
  EffectConfig wind;
  wind.name = "dodge_wind";
  wind.telegraph_ticks = 1;  // 1-turn windup
  wind.active_ticks = 2;     // blows for 2 turns
  wind.recovery_ticks = 0;
  wind.loop = 0;
  // 3x3 with corners masked off:  .#.  (centre + 4 cardinals)
  //                               ###
  //                               .#.
  wind.area = {0, 1, 0,
               1, 1, 1,
               0, 1, 0};
  wind.filter = TargetFilter::Companion;
  wind.damage = 0;
  wind.push_dx = 0;
  wind.push_dy = 1;              // centre fallback; actual random dir set per spawn
  wind.push_distance = 2;
  wind.radial_push = true;       // cardinals push outward
  wind.apply_every_tick = true;  // re-pushes anyone still in the cross each active tick
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
  std::vector<Position> floor_cells = grid_->FindCellsOfKind(CellKind::Floor);
  if (floor_cells.empty()) return;

  // Pick a random hazard type.
  size_t hazard_idx = portable_uniform_int<size_t>(
      rng_, 0, hazard_effects_.size() - 1);
  const std::string& effect_name = hazard_effects_[hazard_idx];

  // Filter out cells currently occupied by a live companion for *damaging*
  // hazards. A 3x3 fire centered on the player is unavoidable: the telegraph
  // grants only one move tick before damage lands, and the player can't
  // escape a 3x3 in one step. Wind centered on the player is fine — it just
  // pushes them — so we only restrict damaging effects.
  const EffectConfig* cfg =
      EffectConfigRegistry::Instance().GetConfig(effect_name);
  bool is_damage = cfg && cfg->damage > 0;

  std::vector<Position> candidates = floor_cells;
  if (is_damage) {
    std::vector<Position> player_cells;
    for (const Companion* c : object_manager_->GetAllCompanions()) {
      if (c->IsAlive()) player_cells.push_back(c->GetPosition());
    }
    std::vector<Position> filtered;
    filtered.reserve(candidates.size());
    for (const Position& p : candidates) {
      bool on_player = false;
      for (const Position& pc : player_cells) {
        if (p == pc) {
          on_player = true;
          break;
        }
      }
      if (!on_player) filtered.push_back(p);
    }
    // Only downgrade to the unfiltered list if nothing else is available
    // (small grids packed with companions). Otherwise use the filtered one.
    if (!filtered.empty()) candidates = std::move(filtered);
  }

  size_t pos_idx = portable_uniform_int<size_t>(rng_, 0, candidates.size() - 1);
  Position spawn_pos = candidates[pos_idx];

  Direction dir = static_cast<Direction>(portable_uniform_int(rng_, 0, 3));

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
  // Hazard spawning moved to PostStep — we need the player's post-movement
  // position when picking a centre cell, otherwise a damaging area can land
  // dead-centre on whatever cell the player just walked into.
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

  // Spawn hazards at regular intervals, AFTER movement has resolved so the
  // companion-cell exclusion inside SpawnHazard sees their new position.
  // tick_ was incremented by BaseEnv::Step() just before PostStep runs, so
  // it holds the index of the step that just completed.
  if (tick_ > 0 && tick_ % hazard_interval_ == 0) {
    SpawnHazard();
  }
}

// Observation logic lives in BaseEnv + DodgeLens now:
// - Hazard tensor planes 5/6 moved to BaseEnv::WriteObservationTensor.
// - The 10 dodge-specific vector features moved to DodgeLens::WriteVectorObs.

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
