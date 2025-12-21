// Copyright 2024
// SynchroEnv implementation

#include "synchro_env.h"

#include <algorithm>
#include <cassert>
#include <random>
#include <stdexcept>

#include "../core/cell.h"
#include "synchro_lens.h"
#include "../core/d4_transform.h"
#include "../core/level_builder.h"
#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "../core/map_generator.h"
#include "effect_system.h"

namespace companions {

SynchroEnv::SynchroEnv()
    : SynchroEnv(kDefaultGridSize, kDefaultGridSize, 3, 3, 0, std::random_device{}()) {}

SynchroEnv::SynchroEnv(int rows, int cols, int num_companions, int num_synchro,
                       int horizon)
    : SynchroEnv(rows, cols, num_companions, num_synchro, 0, std::random_device{}(), 0, horizon) {}

SynchroEnv::SynchroEnv(unsigned int seed)
    : SynchroEnv(kDefaultGridSize, kDefaultGridSize, 3, 3, 0, seed) {}

SynchroEnv::SynchroEnv(int rows, int cols, int num_companions, int num_synchro,
                       int map_complexity, unsigned int seed, int d4_transform,
                       int horizon)
    : BaseEnv(rows, cols, d4_transform),
      num_companions_(num_companions),
      num_synchro_(num_synchro),
      map_complexity_(std::max(0, std::min(5, map_complexity))),
      rng_(seed) {
  horizon_ = horizon;
  ValidateConfig();
  Reset();
  SetTaskLens(std::make_unique<SynchroLens>());
}

SynchroEnv::SynchroEnv(const SynchroEnv& other)
    : BaseEnv(other),
      num_companions_(other.num_companions_),
      num_synchro_(other.num_synchro_),
      map_complexity_(other.map_complexity_),
      rng_(other.rng_),
      synchro_positions_(other.synchro_positions_),
      success_(other.success_) {
  if (other.GetTaskLens()) {
    SetTaskLens(std::make_unique<SynchroLens>());
  }
}

SynchroEnv& SynchroEnv::operator=(const SynchroEnv& other) {
  if (this != &other) {
    BaseEnv::operator=(other);
    num_companions_ = other.num_companions_;
    num_synchro_ = other.num_synchro_;
    map_complexity_ = other.map_complexity_;
    rng_ = other.rng_;
    synchro_positions_ = other.synchro_positions_;
    success_ = other.success_;
  }
  return *this;
}

std::unique_ptr<BaseEnv> SynchroEnv::Clone() const {
  return std::make_unique<SynchroEnv>(*this);
}

void SynchroEnv::ValidateConfig() {
  if (num_companions_ < 1) {
    throw std::invalid_argument("num_companions must be >= 1");
  }
  if (num_synchro_ < 1) {
    throw std::invalid_argument("num_synchro must be >= 1");
  }
  if (num_synchro_ > num_companions_) {
    throw std::invalid_argument("num_synchro must be <= num_companions");
  }
  // Check grid capacity
  int interior = (rows_ - 2) * (cols_ - 2);
  if (num_companions_ + num_synchro_ > interior) {
    throw std::invalid_argument(
        "Not enough interior cells (" + std::to_string(interior) +
        ") for " + std::to_string(num_companions_) + " companions + " +
        std::to_string(num_synchro_) + " synchro cells");
  }
}

void SynchroEnv::Reset() {
  // Reset state
  success_ = false;

  // Create level config for synchro env
  LevelConfig config = LevelConfig::ForSynchro(
      rows_, cols_, num_companions_, num_synchro_,
      map_complexity_, static_cast<unsigned int>(rng_()), d4_transform_, horizon_);

  // Generate snapshot using LevelGenerator
  Snapshot snapshot = LevelGenerator::Generate(config);

  // Extract synchro positions from snapshot BEFORE loading (pre-transform order)
  synchro_positions_.clear();
  int snap_cols = snapshot.cols;
  for (size_t i = 0; i < snapshot.cells.size(); ++i) {
    if (snapshot.cells[i].kind == CellKind::Synchro) {
      int r = static_cast<int>(i) / snap_cols;
      int c = static_cast<int>(i) % snap_cols;
      synchro_positions_.push_back({r, c});
    }
  }

  // Load the snapshot (handles grid, agents, timing, D4 transform)
  LoadSnapshot(snapshot);

  // Transform synchro positions using same D4 transform that was applied
  if (d4_transform_ != 0) {
    D4Transform transform = ToD4Transform(d4_transform_);
    synchro_positions_ = TransformPositions(synchro_positions_,
                                            snapshot.rows, snapshot.cols, transform);
  }
}

void SynchroEnv::Reset(unsigned int seed) {
  rng_.seed(seed);
  Reset();
}

void SynchroEnv::SetupGrid() {
  // Use MapGenerator to create grid with appropriate complexity
  // Only consume RNG for complexity > 0 (where randomness matters)
  // This preserves backward compatibility for complexity 0 (empty rectangle)
  uint64_t map_seed = 0;
  if (map_complexity_ > 0) {
    map_seed = rng_();
  }
  auto config = MapGenerator::DefaultConfig(rows_, cols_, map_complexity_, map_seed);
  grid_ = MapGenerator::Generate(config);

  // Create fresh object manager
  object_manager_ = std::make_unique<ObjectManager>(rows_, cols_);

  // Update EffectSystem pointers since grid_ and object_manager_ were recreated
  effect_system_->UpdatePointers(object_manager_.get(), grid_.get());
}

void SynchroEnv::PlaceSynchroCells() {
  // Find random empty cells for synchro positions
  std::vector<Position> positions = FindEmptyCells(num_synchro_, rng_);

  // Set them as Synchro cells
  for (const Position& pos : positions) {
    grid_->SetCell(pos, CellKind::Synchro);
    synchro_positions_.push_back(pos);
  }
}

void SynchroEnv::SpawnAgents() {
  // Find random empty cells for agent positions
  std::vector<Position> spawn_positions = FindEmptyCells(num_companions_, rng_);

  // Colors for companions (cycle through available colors)
  const ActorColor colors[] = {ActorColor::Red, ActorColor::Green, ActorColor::Blue};
  const int num_colors = sizeof(colors) / sizeof(colors[0]);

  // Spawn companions: first one is Player, rest are NPCCompanions
  for (int i = 0; i < num_companions_; ++i) {
    ActorColor color = colors[i % num_colors];

    if (i == 0) {
      auto* player = object_manager_->CreateActor<Player>(spawn_positions[i]);
      player->SetColor(color);
    } else {
      auto* npc = object_manager_->CreateActor<NPCCompanion>(spawn_positions[i]);
      npc->SetColor(color);
    }
  }
}

bool SynchroEnv::IsDone() const {
  return success_ || tick_ >= horizon_;
}

bool SynchroEnv::IsSuccess() const {
  return success_;
}

int SynchroEnv::NumAgentsOnSynchroCells() const {
  int count = 0;
  for (const Agent* agent : object_manager_->GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    Position pos = agent->GetPosition();
    if (grid_->GetCellKind(pos) == CellKind::Synchro) {
      count++;
    }
  }
  return count;
}

void SynchroEnv::CalculateRewards(std::vector<double>& rewards) {
  int on_synchro = NumAgentsOnSynchroCells();
  int num_agents = NumAgents();

  // Time penalty is -num_agents * kProgressReward
  // This ensures max progress per step is 0 when all agents are on synchro but not winning
  double time_penalty = -static_cast<double>(num_agents) * kProgressReward;
  double reward = kProgressReward * on_synchro + time_penalty;

  if (on_synchro >= num_synchro_) {
    success_ = true;
    reward += kWinReward;
  }

  for (int i = 0; i < num_agents; ++i) {
    rewards[i] = reward;
  }
}

double SynchroEnv::MinUtility() const {
  // Worst case: time penalty every step, no agents on synchro cells
  // time_penalty = -num_agents * kProgressReward
  int num_agents = NumAgents();
  double time_penalty = -static_cast<double>(num_agents) * kProgressReward;
  return horizon_ * time_penalty;
}

double SynchroEnv::MaxUtility() const {
  // Best case: win immediately
  // With our reward structure, intermediate steps contribute at most 0
  // (when all agents on synchro but not yet winning)
  // The only positive contribution is the win reward
  return kWinReward;
}

void SynchroEnv::ValidateSnapshot(const Snapshot& snapshot) const {
  // SynchroEnv requires synchro cells to function
  int synchro_count = snapshot.CountCells(CellKind::Synchro);
  if (synchro_count < num_synchro_) {
    throw std::runtime_error(
        "SynchroEnv requires at least " + std::to_string(num_synchro_) +
        " synchro cells, but snapshot has " + std::to_string(synchro_count));
  }
}

}  // namespace companions
