// Copyright 2024
// GameEnv - game-serving shell environment with no episode semantics

#ifndef COMPANIONS_ENV_GAME_ENV_H_
#define COMPANIONS_ENV_GAME_ENV_H_

#include <memory>

#include "base_env.h"
#include "../core/pcg32.h"

namespace companions {

// =============================================================================
// GameEnv - the BaseEnv used when serving the game (DLL / Unreal), as opposed
// to training. In-game the world never "ends": IsDone() always returns false,
// there is no horizon termination and no auto-anything. The plan executor
// polls IsSuccess() (lens-driven, latched by BaseEnv::Step) plus game-level
// facts to decide when to swap the active TaskLens to the next task.
//
// Reset() still produces a playable synchro-task level from the configured
// parameters via the exact level-generation path SynchroEnv::Reset uses
// (LevelConfig::ForSynchro -> LevelGenerator::Generate -> LoadSnapshot), and
// the constructor activates a SynchroLens, so companions_create(config) keeps
// producing the level consumers expect today. The only difference is episode
// semantics. In-game, levels normally arrive via LoadSnapshot instead.
// =============================================================================
class GameEnv : public BaseEnv {
 public:
  // Same parameter set as SynchroEnv's full constructor so the C API config
  // maps 1:1 and same seed => identical level.
  GameEnv(int rows, int cols, int num_companions, int num_synchro,
          int map_complexity, unsigned int seed, int d4_transform = 0,
          int horizon = kDefaultHorizon);

  // Copies preserve the active lens KIND (lenses are stateless) and the
  // latched success_ flag, unlike SynchroEnv's copy which always re-creates
  // a SynchroLens and resets success.
  GameEnv(const GameEnv& other);
  GameEnv& operator=(const GameEnv& other);

  std::unique_ptr<BaseEnv> Clone() const override;

  // RL interface
  void Reset() override;
  void Reset(unsigned int seed) override;

  // The in-game world never terminates. Success is polled separately via
  // IsSuccess(); horizon_ is advisory only (feeds the steps-remaining
  // observation feature).
  bool IsDone() const override { return false; }

  // Utility bounds are not meaningful for a never-terminating env; these
  // conservative per-step bounds exist only to satisfy the BaseEnv interface
  // (MCTS-style planners should not run on GameEnv).
  double MinUtility() const override { return -1.0; }
  double MaxUtility() const override { return 1.0; }

  // Configuration accessors
  int GetNumCompanions() const { return num_companions_; }
  int GetNumSynchro() const { return num_synchro_; }
  int GetMapComplexity() const { return map_complexity_; }

  // No ValidateSnapshot override: the game shell must accept ANY level
  // snapshot (synchro, aggro, dodge, editor-made). Lens compatibility is
  // checked separately by SetTaskLens via CanOperateOn.

 private:
  void ValidateConfig();
  void CopyLensFrom(const GameEnv& other);

  int num_companions_;
  int num_synchro_;
  int map_complexity_ = 0;
  pcg32 rng_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_GAME_ENV_H_
