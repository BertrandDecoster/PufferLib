// Copyright 2024
// BaseEnv for The Companions game

#ifndef COMPANIONS_ENV_BASE_ENV_H_
#define COMPANIONS_ENV_BASE_ENV_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/annotations.h"
#include "../core/d4_transform.h"
#include "../core/pcg32.h"
#include "../core/effect_config.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/skill_config.h"
#include "../core/snapshot.h"
#include "../core/tag_table.h"
#include "../core/types.h"
#include "task_lens.h"

namespace companions {

// Forward declaration
class EffectSystem;

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
  virtual void Reset(unsigned int seed) = 0;
  virtual StepResult Step(const std::vector<Action>& actions);
  virtual bool IsDone() const = 0;
  // Latched success flag. BaseEnv::Step sets it once the active TaskLens
  // reports IsSuccess; it stays set until ResetSuccess is called (e.g. by
  // SetTaskLens). Subclasses normally should not override.
  virtual bool IsSuccess() const { return success_; }
  virtual void ResetSuccess() { success_ = false; }

  // Task lens management
  bool SetTaskLens(std::unique_ptr<TaskLens> lens);

  // Set a lens AND hand it parameters to materialize objective cells.
  // Activates before CanOperateOn so lenses that stamp their own objective
  // cells can satisfy the readiness check.
  bool SetTaskLensWithParams(std::unique_ptr<TaskLens> lens,
                              const LensParams& params);

  TaskLens* GetTaskLens() const { return task_lens_.get(); }

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
      int count, pcg32& rng,
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

  // Patrol path accessor (override in AggroEnv)
  virtual const std::vector<Position>& GetPatrolPath() const;

  // Semantic annotations (task tags on cells and agents, independent of
  // physical CellKind). Lenses populate on Activate and clean up via
  // owner_lens_id on Deactivate; persistent annotations (owner_lens_id == -1)
  // come from the map generator or external HTN input.
  const AnnotationStore& GetAnnotations() const { return annotations_; }
  AnnotationStore& GetMutableAnnotations() { return annotations_; }

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

  // Companion casts. Off by default: a companion's InteractAction is then
  // ignored, as it always was (RL envs keep their dynamics). When on, an
  // attacking companion stays put (the movement part of its action only aims)
  // and casts the generic "companion_cast" effect on the cell it faces. What
  // the cast means is up to the host that turned it on.
  void SetCompanionCastEnabled(bool enabled) { companion_cast_enabled_ = enabled; }
  bool IsCompanionCastEnabled() const { return companion_cast_enabled_; }

  struct CompanionCast {
    ObjectId caster = kInvalidObjectId;
    Position cell;  // The faced cell the cast was spawned on
  };
  // Casts resolved by the last Step (empty when casts are off).
  const std::vector<CompanionCast>& GetLastCasts() const { return last_casts_; }

  // ==========================================================================
  // Skills and tags (data-driven; names are opaque to the env)
  // ==========================================================================
  // A companion's Skill1 uses the skill in its slot 0: the companion stays put
  // (the movement only aims) and the skill resolves after movement. A skill
  // that cannot be used (cooldown, disabled slot, empty slot) is dropped and
  // the movement applies as if no interact had been given; the exception is
  // an empty slot 0 while the legacy companion cast is enabled, which casts
  // "companion_cast" instead (see SetCompanionCastEnabled).
  const TagTable& GetTagTable() const { return tags_; }
  TagTable& GetMutableTagTable() { return tags_; }
  const SkillBook& GetSkillBook() const { return skills_; }
  SkillBook& GetMutableSkillBook() { return skills_; }

  // Put `skill` in a companion's slot (0-based; "" empties it) and reset the
  // slot's cooldown. False for an unknown skill, slot or companion.
  bool SetCompanionSkill(ObjectId companion, int slot, const std::string& skill);

  // Host primitives: land / remove a tag outside of a step. `duration` is a
  // positive step count or kPermanentTag; ApplyTagTo returns false for 0 or
  // anything below kPermanentTag. Durations tick at the START of each Step: a
  // tag applied during step t (or between steps t and t+1) with duration d is
  // present after steps t .. t+d-1.
  bool ApplyTagTo(ObjectId agent, const std::string& tag, int duration);
  bool RemoveTagFrom(ObjectId agent, const std::string& tag);

  struct SkillUse {
    ObjectId caster = kInvalidObjectId;
    std::string skill;
    Position target;  // The skill's centre: landing cell for Self skills
  };
  struct TagApplication {
    ObjectId agent = kInvalidObjectId;
    TagId tag = kInvalidTag;
    // Steps, or kPermanentTag. Ticks at the START of each Step: landed during
    // step t with duration d, the tag is present after steps t .. t+d-1.
    int duration = kPermanentTag;
    ObjectId source = kInvalidObjectId;  // Caster, or kInvalidObjectId for a zone
    std::string cause;                   // Skill name, or "zone"
    bool fresh = false;                  // The agent did not have the tag before
  };
  // What the last Step did (cleared at the start of every Step, and by
  // LoadSnapshot, hence by every Reset).
  // Skills resolve one caster at a time in agent-index order, each from its
  // current cell: an earlier push / pull can move a later caster (ResolveSkills).
  const std::vector<SkillUse>& GetLastSkillUses() const { return last_skill_uses_; }
  const std::vector<TagApplication>& GetLastTagsApplied() const { return last_tags_applied_; }

  // ==========================================================================
  // Snapshot Support - Save/Load complete world state
  // ==========================================================================

  // Save current state to a snapshot (grid, agents, effects, timing)
  virtual Snapshot SaveSnapshot() const;

  // Load state from a snapshot
  // Throws std::runtime_error if snapshot is incompatible (e.g., wrong dimensions)
  virtual void LoadSnapshot(const Snapshot& snapshot);

  // Validate that snapshot has required cell types for this environment
  // Override in subclasses to check for required cells (e.g., Synchro cells)
  // Throws std::runtime_error if validation fails
  virtual void ValidateSnapshot(const Snapshot& snapshot) const;

 protected:
  // Subclass hooks for custom step logic
  virtual void PreStep();
  virtual void PostStep() {}

  // Update all agents with FSM AI (called in PreStep)
  void UpdateAgentFSM();

  // D4 symmetry transform - call at end of Reset() in subclasses
  // Transforms grid cells and actor positions according to d4_transform_
  void ApplyD4Transform();

  // Collision resolution (the new system)
  void GatherIntentions(const std::vector<Action>& actions);
  void CaptureOriginalIntentions();  // Save intentions before collision resolution
  void ResolveCollisions();
  Position PredictPosition(const Agent* agent) const;
  bool ValidateMovement(const Agent* agent, Position target) const;

  // Movement execution
  void ExecuteValidatedMovements();

  // Interaction resolution (attacks, effects, etc.)
  // Called after movement to apply damage from AttackState agents
  void ResolveInteractions();

  // Skills (see GetSkillBook)
  // Empties the per-step reports (casts, skill uses, tags applied): their
  // ObjectIds are re-issued by a new world.
  void ClearStepReports();
  void TickTagsAndCooldowns();  // Start of Step
  // Not rooted, and not slowed on a tick where slow forbids walking. Walking
  // and caster-moving skills both need it.
  bool CanMoveItself(const Agent& agent) const;
  bool CanUseSkill(const Companion& comp, int slot) const;
  void ResolveSkills();         // After movement, in agent-index order
  // Resolves one skill (caster motion, area, tags); returns its centre.
  Position UseSkill(Companion& caster, const SkillConfig& skill);
  // `centre`, then its in-bounds orthogonal ring (up, right, down, left) for Cross.
  std::vector<Position> AreaCells(Position centre, SkillArea area) const;
  // The one definition of "affected": appends the living agents on `cells` that
  // pass the skill's filter, never the caster, each once, in cell order.
  void CollectAffected(const std::vector<Position>& cells, const SkillConfig& skill,
                       ObjectId caster, std::vector<Agent*>& affected);
  void LandTag(Agent& agent, const std::string& tag, int duration,
               ObjectId source, const std::string& cause);
  void MoveActor(Actor& actor, Position to);  // Skill motions (zone tags follow in a later task)
  // Roots `on_area` (the affected agents on the area, centre included, not the
  // dash path) before anything moves, then PushOut (the ring, away from the
  // centre) / PullIn (one ring thing, by priority, into a free centre).
  void AreaMotion(const SkillConfig& skill, Position centre, ObjectId caster,
                  const std::vector<Agent*>& on_area);

  int rows_;
  int cols_;
  std::unique_ptr<Grid> grid_;
  std::unique_ptr<ObjectManager> object_manager_;
  std::unique_ptr<EffectSystem> effect_system_;
  int tick_ = 0;
  int horizon_ = kDefaultHorizon;
  int d4_transform_ = 0;  // D4 symmetry transformation (0-7)
  std::unique_ptr<TaskLens> task_lens_;
  AnnotationStore annotations_;
  // Latched once the active lens reports IsSuccess. Reset via ResetSuccess.
  bool success_ = false;
  bool companion_cast_enabled_ = false;
  std::vector<CompanionCast> last_casts_;
  TagTable tags_;
  SkillBook skills_;
  std::vector<SkillUse> last_skill_uses_;
  std::vector<TagApplication> last_tags_applied_;
  // Pre-reserved reward buffer, reused each Step to avoid allocation on the
  // hot path. Audit F11.
  mutable std::vector<double> reward_buffer_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_BASE_ENV_H_
