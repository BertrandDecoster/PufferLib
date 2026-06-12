// Copyright 2024
// DodgeEnv - companions must dodge timed area effects to survive

#ifndef COMPANIONS_ENV_DODGE_ENV_H_
#define COMPANIONS_ENV_DODGE_ENV_H_

#include <string>
#include <vector>

#include "base_env.h"
#include "../core/pcg32.h"

namespace companions {

// =============================================================================
// DodgeEnv - companions must survive a barrage of timed area effects
//
// The environment has:
// - 1-3 companions (controlled agents)
// - Timed hazard effects spawning at regular intervals
// - Goal is to survive for a set number of ticks
//
// Hazard types:
// - "fire_burst": 3x3 damage area, 2 tick telegraph, 1 tick active
// - "wind_push": Push effect, 1 tick telegraph, pushes 2 cells
// - Custom effects can be loaded via EffectConfigRegistry
//
// Parameters:
// - Grid size (minimum 5x5)
// - Number of companions (1-3)
// - Hazard spawn interval (ticks between spawns)
// - Survival horizon (ticks to survive to win)
// =============================================================================
class DodgeEnv : public BaseEnv {
 public:
  // Constructor
  // d4_transform: 0-7, D4 symmetry transformation to apply after Reset()
  DodgeEnv(int grid_size = 7, int num_companions = 1,
           int hazard_interval = 3, int horizon = 50,
           unsigned int seed = 42, int d4_transform = 0);

  // Copy constructor (for OpenSpiel State::Clone())
  DodgeEnv(const DodgeEnv& other);
  DodgeEnv& operator=(const DodgeEnv& other);

  // Clone (polymorphic copy)
  std::unique_ptr<BaseEnv> Clone() const override;

  // RL interface
  void Reset() override;
  void Reset(unsigned int seed) override;
  bool IsDone() const override;

  // Observations come from BaseEnv (universal 7-plane tensor + base vector
  // features) plus DodgeLens::WriteVectorObs (10 dodge-specific features).

  // Accessors
  int GetNumCompanions() const { return num_companions_; }
  int GetHazardInterval() const { return hazard_interval_; }

  // Utility bounds
  double MinUtility() const override { return -10.0; }  // Death penalty
  double MaxUtility() const override { return kWinReward; }

  // Reward constants
  static constexpr double kWinReward = 10.0;
  static constexpr double kDeathPenalty = -10.0;
  static constexpr double kSurvivalBonus = 0.1;  // Per tick bonus

  // Register default hazard effects (call once at startup)
  static void RegisterDefaultEffects();

  // Snapshot validation - DodgeEnv has minimal requirements (just floor cells)
  void ValidateSnapshot(const Snapshot& snapshot) const override;

 protected:
  void PreStep() override;
  void PostStep() override;

 private:
  void SetupGrid();
  void SpawnCompanions();
  void SpawnHazard();
  void ValidateConfig();

  int num_companions_;
  int hazard_interval_;
  unsigned int seed_;
  pcg32 rng_;

  bool any_dead_ = false;

  // Effect names to randomly spawn
  std::vector<std::string> hazard_effects_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_DODGE_ENV_H_
