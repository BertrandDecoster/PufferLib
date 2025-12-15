// Copyright 2024
// AggroEnv - goal is to lure the enemy away and step on the TargetCell

#ifndef COMPANIONS_ENV_AGGRO_ENV_H_
#define COMPANIONS_ENV_AGGRO_ENV_H_

#include <vector>

#include "base_env.h"
#include "../core/pcg32.h"

namespace companions {

// Enemy types for AggroEnv
enum class EnemyType { Zombie, Goblin };

// =============================================================================
// AggroEnv - companions must lure an FSM enemy away from the TargetCell
//
// The environment has:
// - 1-3 companions (controlled agents)
// - 1 NPC enemy with Patrol/Aggro/ReturnToPatrol FSM
// - A 3x3 patrol square for the enemy
// - A TargetCell that a companion must step on to win
//
// Smart spawning ensures:
// - Companions spawn outside enemy aggro range
// - TargetCell spawns outside enemy aggro range
// - TargetCell is not in a corner (at least 2 walkable adjacent cells)
// =============================================================================
class AggroEnv : public BaseEnv {
 public:
  // Constructor with grid size, num companions (1-3), enemy type, seed, and horizon
  // Minimum grid size is 6x6 to fit patrol square
  // d4_transform: 0-7, D4 symmetry transformation to apply after Reset()
  AggroEnv(int grid_size = 10, int num_companions = 1,
           EnemyType enemy_type = EnemyType::Zombie, unsigned int seed = 42,
           int d4_transform = 0, int horizon = kDefaultHorizon);

  // Copy constructor (for OpenSpiel State::Clone())
  AggroEnv(const AggroEnv& other);
  AggroEnv& operator=(const AggroEnv& other);

  // Clone (polymorphic copy)
  std::unique_ptr<BaseEnv> Clone() const override;

  // RL interface
  void Reset() override;
  void Reset(unsigned int seed) override;
  bool IsDone() const override;

  // Success check
  bool IsSuccess() const override { return success_; }

  // Accessors
  Position GetTargetPosition() const { return target_pos_; }
  Position GetEnemySpawnPosition() const { return enemy_spawn_pos_; }
  const std::vector<Position>& GetPatrolPath() const { return patrol_path_; }
  int GetNumCompanions() const { return num_companions_; }
  EnemyType GetEnemyType() const { return enemy_type_; }

  // Utility bounds
  double MinUtility() const override { return -1.0 * horizon_; }  // Time penalty only
  double MaxUtility() const override { return kWinReward; }

  // Vector Observation - adds AggroEnv-specific features:
  //   - Base features from BaseEnv (8)
  //   - Relative position to enemy (2)
  //   - Relative position to target cell (2)
  //   - Enemy FSM state one-hot (3: patrol, aggro, returning)
  //   - Distance to enemy (1)
  // Total: 8 + 2 + 2 + 3 + 1 = 16
  void VectorObservation(std::vector<float>& values, int player = 0) const override;
  int VectorObservationSize() const override;

  // Reward constants
  static constexpr double kWinReward = 1.0;
  static constexpr double kTimePenalty = -0.01;

  // FSM parameters
  static constexpr int kAggroRange = 3;
  static constexpr int kLoseTargetRange = 5;

 protected:
  void CalculateRewards(std::vector<double>& rewards) override;

 private:
  void SetupGrid();
  void PlacePatrolSquare();
  void SpawnEnemy();
  void PlaceTargetCell();
  void SpawnCompanions();
  void ValidateConfig();

  int num_companions_;
  EnemyType enemy_type_;
  unsigned int seed_;
  pcg32 rng_;

  Position target_pos_;
  Position enemy_spawn_pos_;           // Enemy's starting position
  std::vector<Position> patrol_path_;  // 8 cells (3x3 perimeter)
  bool success_ = false;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_AGGRO_ENV_H_
