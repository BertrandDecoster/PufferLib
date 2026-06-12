// Copyright 2024
// GameEnv implementation

#include "game_env.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "../core/snapshot.h"
#include "aggro_lens.h"
#include "dodge_lens.h"
#include "synchro_lens.h"

namespace companions {

namespace {

// Recreate a (stateless) lens of the same kind, for polymorphic copy.
std::unique_ptr<TaskLens> MakeLensOfKind(TaskLens::Kind kind) {
  switch (kind) {
    case TaskLens::kSynchro:
      return std::make_unique<SynchroLens>();
    case TaskLens::kAggro:
      return std::make_unique<AggroLens>();
    case TaskLens::kDodge:
      return std::make_unique<DodgeLens>();
    default:
      return nullptr;
  }
}

}  // namespace

GameEnv::GameEnv(int rows, int cols, int num_companions, int num_synchro,
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

void GameEnv::CopyLensFrom(const GameEnv& other) {
  // Assign task_lens_ directly instead of going through SetTaskLens: the
  // copy must preserve the latched success_ flag (copied by BaseEnv's copy),
  // and SetTaskLens would call ResetSuccess. Lens-owned annotations were
  // copied with annotations_, so no Activate/Deactivate is needed either.
  if (other.GetTaskLens()) {
    task_lens_ = MakeLensOfKind(other.GetTaskLens()->GetKind());
  } else {
    task_lens_.reset();
  }
}

GameEnv::GameEnv(const GameEnv& other)
    : BaseEnv(other),
      num_companions_(other.num_companions_),
      num_synchro_(other.num_synchro_),
      map_complexity_(other.map_complexity_),
      rng_(other.rng_) {
  CopyLensFrom(other);
}

GameEnv& GameEnv::operator=(const GameEnv& other) {
  if (this != &other) {
    BaseEnv::operator=(other);
    num_companions_ = other.num_companions_;
    num_synchro_ = other.num_synchro_;
    map_complexity_ = other.map_complexity_;
    rng_ = other.rng_;
    CopyLensFrom(other);
  }
  return *this;
}

std::unique_ptr<BaseEnv> GameEnv::Clone() const {
  return std::make_unique<GameEnv>(*this);
}

void GameEnv::ValidateConfig() {
  // Same constraints as SynchroEnv so companions_create rejects the same
  // configs it always rejected.
  if (num_companions_ < 1) {
    throw std::invalid_argument("num_companions must be >= 1");
  }
  if (num_synchro_ < 1) {
    throw std::invalid_argument("num_synchro must be >= 1");
  }
  if (num_synchro_ > num_companions_) {
    throw std::invalid_argument("num_synchro must be <= num_companions");
  }
  int interior = (rows_ - 2) * (cols_ - 2);
  if (num_companions_ + num_synchro_ > interior) {
    throw std::invalid_argument(
        "Not enough interior cells (" + std::to_string(interior) + ") for " +
        std::to_string(num_companions_) + " companions + " +
        std::to_string(num_synchro_) + " synchro cells");
  }
}

void GameEnv::Reset() {
  // Same generation path as SynchroEnv::Reset: one rng_() draw seeds the
  // level config, so GameEnv(seed) produces the identical level layout that
  // SynchroEnv(seed) used to produce for C API consumers.
  LevelConfig config = LevelConfig::ForSynchro(
      rows_, cols_, num_companions_, num_synchro_, map_complexity_,
      static_cast<unsigned int>(rng_()), d4_transform_, horizon_);

  Snapshot snapshot = LevelGenerator::Generate(config);

  // LoadSnapshot handles grid, agents, effects, timing, the D4 transform and
  // the annotation layer (persistent SynchroGoal tags travel inside the
  // snapshot's annotations, so SynchroLens finds its goals without any
  // env-side re-stamping).
  LoadSnapshot(snapshot);

  // A fresh level invalidates any success latched on the previous level.
  ResetSuccess();
}

void GameEnv::Reset(unsigned int seed) {
  rng_.seed(seed);
  Reset();
}

}  // namespace companions
