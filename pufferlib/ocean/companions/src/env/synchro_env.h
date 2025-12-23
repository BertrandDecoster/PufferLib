// Copyright 2024
// SynchroEnv - goal is to get all agents on SynchroCells

#ifndef COMPANIONS_ENV_SYNCHRO_ENV_H_
#define COMPANIONS_ENV_SYNCHRO_ENV_H_

#include <vector>

#include "base_env.h"
#include "../core/pcg32.h"

namespace companions {

// =============================================================================
// SynchroEnv - agents must each stand on a SynchroCell
// =============================================================================
class SynchroEnv : public BaseEnv {
 public:
  // Default constructor uses random seed, default 12x12 grid, 3 companions, 3 synchro cells
  SynchroEnv();

  // Full constructor with all parameters (uses random seed)
  // num_companions >= 1, num_synchro >= 1 && num_synchro <= num_companions
  SynchroEnv(int rows, int cols, int num_companions = 3, int num_synchro = 3,
             int horizon = kDefaultHorizon);

  // Constructor with explicit seed for reproducibility
  explicit SynchroEnv(unsigned int seed);

  // Full constructor with seed and map complexity (for curriculum learning)
  // map_complexity: 0-5, where 0 = empty rectangle (current behavior)
  // d4_transform: 0-7, D4 symmetry transformation to apply after Reset()
  SynchroEnv(int rows, int cols, int num_companions, int num_synchro,
             int map_complexity, unsigned int seed, int d4_transform = 0,
             int horizon = kDefaultHorizon);

  // Copy constructor (for OpenSpiel State::Clone())
  SynchroEnv(const SynchroEnv& other);
  SynchroEnv& operator=(const SynchroEnv& other);

  // Clone (polymorphic copy)
  std::unique_ptr<BaseEnv> Clone() const override;

  // RL interface
  void Reset() override;
  void Reset(unsigned int seed) override;  // Reset with specific seed
  bool IsDone() const override;

  // Success check
  bool IsSuccess() const override;
  void ResetSuccess() override { success_ = false; }

  // Get synchro cell positions
  const std::vector<Position>& GetSynchroPositions() const {
    return synchro_positions_;
  }

  // How many agents are on synchro cells?
  int NumAgentsOnSynchroCells() const;

  // Configuration accessors
  int GetNumCompanions() const { return num_companions_; }
  int GetNumSynchro() const { return num_synchro_; }
  int GetMapComplexity() const { return map_complexity_; }

  // Utility bounds (environment-specific reward structure)
  double MinUtility() const override;
  double MaxUtility() const override;

  // Reward constants for SynchroEnv (scaled to fit pufferl's [-1,1] clipping)
  static constexpr double kWinReward = 1.0;
  static constexpr double kProgressReward = 0.01;
  // Time penalty is computed as: -num_agents * kProgressReward
  // This ensures max progress per step is 0 when not winning

  // Observation masking - Synchro cells are goals, Target cells hidden
  CellKind GetMaskedCellKind(CellKind kind) const override {
    if (kind == CellKind::Target) return CellKind::Floor;
    return kind;  // Synchro cells remain visible
  }

  // Snapshot validation - SynchroEnv requires synchro cells
  void ValidateSnapshot(const Snapshot& snapshot) const override;

 protected:
  void CalculateRewards(std::vector<double>& rewards) override;

 private:
  void SetupGrid();
  void PlaceSynchroCells();
  void SpawnAgents();
  void ValidateConfig();

  int num_companions_;
  int num_synchro_;
  int map_complexity_ = 0;  // 0-5, curriculum learning parameter
  pcg32 rng_;
  std::vector<Position> synchro_positions_;
  bool success_ = false;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_SYNCHRO_ENV_H_
