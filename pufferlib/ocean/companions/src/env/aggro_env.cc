// Copyright 2024
// AggroEnv implementation

#include "aggro_env.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <random>
#include <stdexcept>

#include "../core/annotations.h"
#include "../core/cell.h"
#include "../core/d4_transform.h"
#include "../core/fsm/enemies.h"
#include "../core/fsm/fsm_states.h"
#include "../core/level_builder.h"
#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "aggro_lens.h"
#include "effect_system.h"

namespace companions {

AggroEnv::AggroEnv(int grid_size, int num_companions, EnemyType enemy_type,
                   unsigned int seed, int d4_transform, int horizon)
    : BaseEnv(grid_size, grid_size, d4_transform),
      num_companions_(num_companions),
      enemy_type_(enemy_type),
      seed_(seed),
      rng_(seed) {
  horizon_ = horizon;
  ValidateConfig();
  Reset();
  SetTaskLens(std::make_unique<AggroLens>());
}

AggroEnv::AggroEnv(const AggroEnv& other)
    : BaseEnv(other),
      num_companions_(other.num_companions_),
      enemy_type_(other.enemy_type_),
      seed_(other.seed_),
      rng_(other.rng_),
      target_pos_(other.target_pos_),
      enemy_spawn_pos_(other.enemy_spawn_pos_),
      patrol_path_(other.patrol_path_) {
  if (other.GetTaskLens()) {
    SetTaskLens(std::make_unique<AggroLens>());
  }
}

AggroEnv& AggroEnv::operator=(const AggroEnv& other) {
  if (this != &other) {
    BaseEnv::operator=(other);
    num_companions_ = other.num_companions_;
    enemy_type_ = other.enemy_type_;
    seed_ = other.seed_;
    rng_ = other.rng_;
    target_pos_ = other.target_pos_;
    enemy_spawn_pos_ = other.enemy_spawn_pos_;
    patrol_path_ = other.patrol_path_;
  }
  return *this;
}

std::unique_ptr<BaseEnv> AggroEnv::Clone() const {
  return std::make_unique<AggroEnv>(*this);
}

void AggroEnv::ValidateConfig() {
  if (num_companions_ < 1 || num_companions_ > 3) {
    throw std::invalid_argument("num_companions must be 1, 2, or 3");
  }
  // Minimum 6x6: walls + 3x3 patrol square needs at least 5x5 interior
  if (rows_ < 6 || cols_ < 6) {
    throw std::invalid_argument("grid_size must be >= 6");
  }
}

void AggroEnv::Reset() {
  // Reset state
  success_ = false;

  // Create level config for aggro env WITHOUT enemies or companions
  // AggroEnv has special spawn requirements:
  // - Enemy type (Zombie/Goblin) determines behavior - can't use generic AgentFSM
  // - Companions must spawn outside enemy's aggro range
  // So we spawn both after loading the grid/cells
  LevelConfig config = LevelConfig::ForAggro(
      rows_, num_companions_, 3 /* patrol size */,
      static_cast<unsigned int>(rng_()), d4_transform_, horizon_);
  config.num_enemies = 0;     // Don't let LevelGenerator create enemies
  config.num_companions = 0;  // Don't let LevelGenerator create companions

  // Generate snapshot using LevelGenerator
  Snapshot snapshot = LevelGenerator::Generate(config);

  // Extract positions from snapshot BEFORE loading (pre-transform order)
  // These will be transformed after LoadSnapshot applies D4
  int snap_cols = snapshot.cols;

  // Extract target cell position. Target lives in the annotation layer now
  // (Floor cell + AggroTarget tag).
  (void)snap_cols;
  target_pos_ = {0, 0};
  for (const AnnotationSnapshot& a : snapshot.annotations) {
    if (a.tag == SemanticTag::AggroTarget && a.target_type == 0) {
      target_pos_ = a.pos;
      break;
    }
  }

  // Extract patrol path from snapshot
  patrol_path_ = snapshot.patrol_path;

  // Load the snapshot (handles grid, timing, D4 transform)
  // Note: no agents in snapshot since num_enemies=0 and num_companions=0
  LoadSnapshot(snapshot);

  // Apply D4 transform to extracted positions
  if (d4_transform_ != 0) {
    D4Transform transform = ToD4Transform(d4_transform_);
    target_pos_ = TransformPosition(target_pos_, snapshot.rows, snapshot.cols, transform);
    patrol_path_ = TransformPositions(patrol_path_, snapshot.rows, snapshot.cols, transform);
  }

  // Dual-path semantic annotation for the target cell. owner_lens_id = -1
  // keeps it persistent across lens swaps (it's intrinsic level data).
  {
    AnnotationStore& annotations = GetMutableAnnotations();
    for (const Position& old_pos :
         annotations.FindCellsWithTag(SemanticTag::AggroTarget)) {
      annotations.RemoveByKey(
          AnnotationKey{AnnotationTarget::Cell, old_pos, kInvalidObjectId},
          SemanticTag::AggroTarget);
    }
    annotations.Add(
        AnnotationKey{AnnotationTarget::Cell, target_pos_, kInvalidObjectId},
        Annotation{SemanticTag::AggroTarget, {}, -1});
  }

  // Spawn the enemy using the correct type (Zombie/Goblin)
  // Uses patrol_path_ which is now in the transformed coordinate system
  SpawnEnemy();

  // Now spawn companions outside the enemy's aggro range
  SpawnCompanions();
}

void AggroEnv::Reset(unsigned int seed) {
  rng_.seed(seed);
  seed_ = seed;
  Reset();
}

void AggroEnv::SetupGrid() {
  // Create fresh grid with current dimensions
  grid_ = std::make_unique<Grid>(rows_, cols_);
  object_manager_ = std::make_unique<ObjectManager>(rows_, cols_);

  // Update EffectSystem pointers since grid_ and object_manager_ were recreated
  effect_system_->UpdatePointers(object_manager_.get(), grid_.get());

  // Use LevelBuilder to construct the level
  LevelBuilder builder(*grid_);

  // Fill with floor
  builder.Fill(CellKind::Floor);

  // Add walls around perimeter using Rectangle (fill=false means border only)
  // Rectangle params: (kind, top, left, width, height) where width=cols, height=rows
  builder.Rectangle(CellKind::Wall, 0, 0, cols_, rows_, false);
}

void AggroEnv::PlacePatrolSquare() {
  // Find valid top-left position for 3x3 square
  // Must be at least 1 cell from walls (row >= 1, col >= 1)
  // Must fit: top + 3 <= rows - 1, left + 3 <= cols - 1

  int max_top = rows_ - 4;   // Leave room for walls
  int max_left = cols_ - 4;

  int top = portable_uniform_int(rng_, 1, max_top);
  int left = portable_uniform_int(rng_, 1, max_left);

  // Generate clockwise patrol path (8 cells)
  // +--+--+--+
  // |0 |1 |2 |
  // +--+--+--+
  // |7 |  |3 |
  // +--+--+--+
  // |6 |5 |4 |
  // +--+--+--+
  patrol_path_ = {
      {top, left},          // 0: top-left
      {top, left + 1},      // 1: top-middle
      {top, left + 2},      // 2: top-right
      {top + 1, left + 2},  // 3: right-middle
      {top + 2, left + 2},  // 4: bottom-right
      {top + 2, left + 1},  // 5: bottom-middle
      {top + 2, left},      // 6: bottom-left
      {top + 1, left},      // 7: left-middle
  };
}

void AggroEnv::SpawnEnemy() {
  // Filter patrol path to exclude target cell
  std::vector<Position> valid_patrol;
  for (const Position& pos : patrol_path_) {
    if (pos != target_pos_) {
      valid_patrol.push_back(pos);
    }
  }

  // Pick random from valid patrol cells
  int idx = portable_uniform_int(
      rng_, 0, static_cast<int>(valid_patrol.size()) - 1);
  Position start_pos = valid_patrol[idx];
  enemy_spawn_pos_ = start_pos;  // Store for companion spawning

  // Find index in original patrol_path_ for FSM context
  int start_idx = 0;
  for (size_t i = 0; i < patrol_path_.size(); ++i) {
    if (patrol_path_[i] == start_pos) {
      start_idx = static_cast<int>(i);
      break;
    }
  }

  // Random initial direction (forward or backward on patrol)
  bool forward = portable_uniform_int(rng_, 0, 1) == 1;

  // Create agent based on type
  AgentFSM* fsm_agent = nullptr;
  switch (enemy_type_) {
    case EnemyType::Zombie:
      // Zombie: moves every 2 ticks (cadence {1,0})
      fsm_agent = CreateZombie(*object_manager_, start_pos, patrol_path_, rng_);
      break;
    case EnemyType::Goblin:
      // Goblin: moves every tick (no cadence)
      fsm_agent = CreateGoblin(*object_manager_, start_pos, patrol_path_, rng_);
      break;
  }

  // Set initial patrol index and direction
  FSMContext& ctx = fsm_agent->GetFSMContext();
  ctx.patrol_index = start_idx;
  ctx.patrol_forward = forward;
  ctx.detection_range = kAggroRange;
  ctx.lose_target_range = kLoseTargetRange;
}

void AggroEnv::PlaceTargetCell() {
  // Interior rectangle not touching walls: {2, 2, cols-4, rows-4}
  Rectangle interior{2, 2, cols_ - 4, rows_ - 4};
  auto candidates = FindEmptyCells(1, rng_, interior, std::nullopt);
  target_pos_ = candidates[0];
  // Target cell is a semantic annotation; the grid cell stays Floor.
  GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Cell, target_pos_, kInvalidObjectId},
      Annotation{SemanticTag::AggroTarget, {}, -1});
}

void AggroEnv::SpawnCompanions() {
  // Define interior rectangle (exclude walls)
  Rectangle interior{1, 1, cols_ - 2, rows_ - 2};

  // Define aggro exclusion zone around enemy spawn position
  Rectangle aggro_zone{
      enemy_spawn_pos_.row - kAggroRange,
      enemy_spawn_pos_.col - kAggroRange,
      2 * kAggroRange + 1,
      2 * kAggroRange + 1};

  // Try spawning outside aggro range first
  std::vector<Position> valid_positions;
  try {
    valid_positions = FindEmptyCells(num_companions_, rng_, interior, aggro_zone);
  } catch (const std::runtime_error&) {
    // Fallback: spawn anywhere in interior (small grid behavior)
    valid_positions = FindEmptyCells(num_companions_, rng_, interior, std::nullopt);
  }

  // Colors for companions
  const ActorColor colors[] = {ActorColor::Red, ActorColor::Green,
                               ActorColor::Blue};

  // Spawn companions: first one is Player, rest are NPCCompanions
  for (int i = 0; i < num_companions_; ++i) {
    ActorColor color = colors[i % 3];
    Position pos = valid_positions[i];

    if (i == 0) {
      auto* player = object_manager_->CreateActor<Player>(pos);
      player->SetColor(color);
    } else {
      auto* npc = object_manager_->CreateActor<NPCCompanion>(pos);
      npc->SetColor(color);
    }
  }
}


bool AggroEnv::IsDone() const {
  return success_ || tick_ >= horizon_;
}

// Vector observation tail comes from AggroLens::AppendVectorObs (8 features),
// wired in by BaseEnv::WriteVectorObservation. The former env-side override
// was deleted in favor of the lens semantics (per-axis normalization,
// FSMStateType-based one-hot, max-distance default with no enemy).

void AggroEnv::ValidateSnapshot(const Snapshot& snapshot) const {
  // AggroEnv requires a target cell
  if (!snapshot.HasTargetCell()) {
    throw std::runtime_error(
        "AggroEnv requires a target cell, but snapshot has none");
  }

  // AggroEnv requires a patrol path
  if (!snapshot.HasPatrolPath()) {
    throw std::runtime_error(
        "AggroEnv requires a patrol path, but snapshot has none");
  }
}

void AggroEnv::LoadSnapshot(const Snapshot& snapshot) {
  // Call base implementation first
  BaseEnv::LoadSnapshot(snapshot);

  // Extract target_pos_ from the annotation layer.
  target_pos_ = {0, 0};
  for (const AnnotationSnapshot& a : snapshot.annotations) {
    if (a.tag == SemanticTag::AggroTarget && a.target_type == 0) {
      target_pos_ = a.pos;
      break;
    }
  }

  // Extract patrol_path_ from snapshot
  patrol_path_ = snapshot.patrol_path;

  // Reset success flag
  success_ = false;
}

}  // namespace companions
