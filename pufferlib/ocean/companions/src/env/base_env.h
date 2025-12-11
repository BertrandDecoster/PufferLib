// Copyright 2024
// BaseEnv for The Companions game

#ifndef COMPANIONS_ENV_BASE_ENV_H_
#define COMPANIONS_ENV_BASE_ENV_H_

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "../core/d4_transform.h"
#include "../core/effect_config.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/types.h"

namespace companions {

// Forward declaration
class EffectSystem;

constexpr int kDefaultHorizon = 100;

// =============================================================================
// Rectangle - axis-aligned bounding box for spatial filtering
// =============================================================================
struct Rectangle {
  int top;
  int left;
  int width;
  int height;

  bool Contains(Position pos) const {
    return pos.row >= top && pos.row < top + height &&
           pos.col >= left && pos.col < left + width;
  }
};

// =============================================================================
// StepResult - returned by Step()
// =============================================================================
struct StepResult {
  bool done = false;
  std::vector<double> rewards;
  std::string info;
};

// =============================================================================
// BaseEnv - RL-style environment base class
// =============================================================================
class BaseEnv {
 public:
  explicit BaseEnv(int rows = kDefaultGridSize, int cols = kDefaultGridSize,
                   int d4_transform = 0);
  virtual ~BaseEnv();

  // Copyable (for OpenSpiel State::Clone())
  BaseEnv(const BaseEnv& other);
  BaseEnv& operator=(const BaseEnv& other);

  // RL interface
  virtual void Reset() = 0;
  virtual StepResult Step(const std::vector<Action>& actions);
  virtual bool IsDone() const = 0;
  virtual bool IsSuccess() const { return false; }  // Override in subclasses

  // Clone this environment (virtual for polymorphic copy in OpenSpiel)
  virtual std::unique_ptr<BaseEnv> Clone() const = 0;

  // Observation
  // Returns a 5-plane observation tensor [5 × rows × cols]:
  //   Plane 0: Floor cells (1.0 if walkable, including synchro cells)
  //   Plane 1: Wall cells (1.0 if wall)
  //   Plane 2: Synchro cells (1.0 if synchro/goal cell)
  //   Plane 3: Current player position (1.0 at player's location)
  //   Plane 4: Other agents positions (1.0 at other agent locations)
  virtual std::string ToString() const;
  virtual void ObservationTensor(std::vector<float>& values, int player = 0) const;
  virtual std::vector<int> ObservationShape() const;
  std::vector<int> ObservationTensorShape() const { return ObservationShape(); }

  // Vector Observation - flat feature vector alternative to tensor
  // Returns a 1D vector with hand-crafted features suitable for MLP-based RL.
  // Base features (per player view):
  //   - Own position (row, col) normalized to [0,1]
  //   - Own health / max_health
  //   - Distance to nearest goal cell (normalized)
  //   - Relative positions of other companions (dx, dy per companion)
  // Subclasses may extend with environment-specific features.
  virtual void VectorObservation(std::vector<float>& values, int player = 0) const;
  virtual int VectorObservationSize() const;

  // Direct-write observation methods (zero-copy for C bindings)
  // These write directly to a pre-allocated buffer, avoiding std::vector allocation
  void WriteObservationTensor(float* buffer, int player = 0) const;
  void WriteVectorObservation(float* buffer, int player = 0) const;

  // Utility bounds (for MCTS and planning algorithms)
  // Pure virtual - each environment defines its own reward structure
  virtual double MinUtility() const = 0;
  virtual double MaxUtility() const = 0;
  int GetHorizon() const { return horizon_; }

  // Action space
  int NumAgents() const;
  int NumActions() const { return kNumMovementActions; }
  std::vector<Action> LegalActions(int agent_idx) const;

  // Find empty cells (Floor cells with no actor)
  // - count: number of cells to return (-1 = return all matching)
  // - include: if set, cells must be inside this rectangle
  // - exclude: if set, cells must NOT be inside this rectangle
  // Returns shuffled cells, throws if count > 0 and not enough available
  std::vector<Position> FindEmptyCells(
      int count, std::mt19937& rng,
      std::optional<Rectangle> include = std::nullopt,
      std::optional<Rectangle> exclude = std::nullopt);

  // Accessors
  const Grid& GetGrid() const { return *grid_; }
  Grid& GetMutableGrid() { return *grid_; }
  const ObjectManager& GetObjectManager() const { return *object_manager_; }
  ObjectManager& GetMutableObjectManager() { return *object_manager_; }
  int GetTick() const { return tick_; }
  int GetRows() const { return rows_; }
  int GetCols() const { return cols_; }
  int GetD4Transform() const { return d4_transform_; }

  // Effect system access (delegates to EffectSystem)
  const std::vector<ActiveEffect>& GetActiveEffects() const;
  void ClearEffects();
  EffectSystem& GetEffectSystem() { return *effect_system_; }
  const EffectSystem& GetEffectSystem() const { return *effect_system_; }

  // Spawn a new effect at a target location (public for testing)
  // Delegates to EffectSystem::SpawnEffect
  void SpawnEffect(const std::string& effect_name, EffectTarget target,
                   Direction direction = Direction::Up,
                   ObjectId source_id = kInvalidObjectId);

 protected:
  // Subclass hooks for custom step logic
  virtual void PreStep();
  virtual void PostStep() {}
  virtual void CalculateRewards(std::vector<double>& /*rewards*/) {}

  // Update all agents with FSM AI (called in PreStep)
  void UpdateAgentFSM();

  // D4 symmetry transform - call at end of Reset() in subclasses
  // Transforms grid cells and actor positions according to d4_transform_
  void ApplyD4Transform();

  // Collision resolution (the new system)
  void GatherIntentions(const std::vector<Action>& actions);
  void ResolveCollisions();
  Position PredictPosition(const Agent* agent) const;
  bool ValidateMovement(const Agent* agent, Position target) const;

  // Movement execution
  void ExecuteValidatedMovements();

  // Interaction resolution (attacks, effects, etc.)
  // Called after movement to apply damage from AttackState agents
  void ResolveInteractions();

  int rows_;
  int cols_;
  std::unique_ptr<Grid> grid_;
  std::unique_ptr<ObjectManager> object_manager_;
  std::unique_ptr<EffectSystem> effect_system_;
  int tick_ = 0;
  int horizon_ = kDefaultHorizon;
  int d4_transform_ = 0;  // D4 symmetry transformation (0-7)
};

}  // namespace companions

#endif  // COMPANIONS_ENV_BASE_ENV_H_
