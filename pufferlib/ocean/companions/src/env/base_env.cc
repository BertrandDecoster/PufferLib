// Copyright 2024
// BaseEnv implementation

#include "base_env.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "../core/agent_config.h"
#include "../core/cell.h"
#include "../core/effect_config.h"
#include "../core/fsm/enemies.h"
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"
#include "../core/game_logger.h"
#include "effect_system.h"
#include "skill_motion.h"

namespace companions {

BaseEnv::BaseEnv(int rows, int cols, int d4_transform)
    : rows_(rows),
      cols_(cols),
      grid_(std::make_unique<Grid>(rows, cols)),
      object_manager_(std::make_unique<ObjectManager>(rows, cols)),
      effect_system_(std::make_unique<EffectSystem>(object_manager_.get(), grid_.get())),
      d4_transform_(d4_transform) {
  assert(IsValidD4Transform(d4_transform) && "Invalid D4 transform (must be 0-7)");
}

BaseEnv::~BaseEnv() = default;

BaseEnv::BaseEnv(const BaseEnv& other)
    : rows_(other.rows_),
      cols_(other.cols_),
      grid_(std::make_unique<Grid>(*other.grid_)),
      object_manager_(std::make_unique<ObjectManager>(*other.object_manager_)),
      effect_system_(std::make_unique<EffectSystem>(*other.effect_system_)),
      tick_(other.tick_),
      horizon_(other.horizon_),
      d4_transform_(other.d4_transform_),
      annotations_(other.annotations_),
      success_(other.success_),
      failed_(other.failed_),
      end_reason_(other.end_reason_),
      tags_(other.tags_),
      skills_(other.skills_),
      last_skill_uses_(other.last_skill_uses_),
      last_tags_applied_(other.last_tags_applied_),
      cell_tags_(other.cell_tags_) {
  // Update EffectSystem pointers to point to our new copies
  effect_system_->UpdatePointers(object_manager_.get(), grid_.get());
}

BaseEnv& BaseEnv::operator=(const BaseEnv& other) {
  if (this != &other) {
    rows_ = other.rows_;
    cols_ = other.cols_;
    grid_ = std::make_unique<Grid>(*other.grid_);
    object_manager_ = std::make_unique<ObjectManager>(*other.object_manager_);
    effect_system_ = std::make_unique<EffectSystem>(*other.effect_system_);
    effect_system_->UpdatePointers(object_manager_.get(), grid_.get());
    tick_ = other.tick_;
    horizon_ = other.horizon_;
    d4_transform_ = other.d4_transform_;
    annotations_ = other.annotations_;
    success_ = other.success_;
    failed_ = other.failed_;
    end_reason_ = other.end_reason_;
    tags_ = other.tags_;
    skills_ = other.skills_;
    last_skill_uses_ = other.last_skill_uses_;
    last_tags_applied_ = other.last_tags_applied_;
    cell_tags_ = other.cell_tags_;
  }
  return *this;
}

StepResult BaseEnv::Step(const std::vector<Action>& actions) {
  StepResult result;

  // Validate action count
  int num_agents = NumAgents();
  if (static_cast<int>(actions.size()) != num_agents) {
    result.info = "Invalid action count";
    result.done = IsDone();
    return result;
  }

  // Timers set from here on also cover the rest of this step (see
  // Agent::BeginStep); they all tick at its end.
  ClearStepReports();
  for (Agent* agent : object_manager_->GetAllAgents()) agent->BeginStep();

  // Pre-step hook
  PreStep();

  // Gather intentions from actions
  GatherIntentions(actions);

  // Capture original intentions before collision resolution modifies them
  CaptureOriginalIntentions();

  // Resolve collisions (iterative fixed-point)
  ResolveCollisions();

  // Execute validated movements
  ExecuteValidatedMovements();

  // Update FSM after movements (so FSM sees actual positions)
  UpdateAgentFSM();

  // Zones land on whoever stands on them, before casts and skills
  ApplyZoneTags();

  // Resolve interactions (attacks, effects)
  ResolveInteractions();

  // Tick active effects (advance timers, apply damage/push)
  // Effect pushes move agents without zone tags: they get them next step if they stay.
  effect_system_->Tick();

  // Every timer (tags, statuses, cooldowns) ticks here, at the end of the step
  for (Agent* agent : object_manager_->GetAllAgents()) agent->EndStep();

  // Increment tick
  tick_++;

  // Post-step hook
  PostStep();

  // Calculate rewards via the active TaskLens (the single source of truth).
  // Envs without a lens get zeros; BaseEnv::Step never falls back to env-side
  // reward logic, since the TaskLens invariant requires rewards live with the task.
  // Reuse the pre-allocated reward_buffer_ and move it into result.rewards to
  // avoid per-step allocation on the hot path. Audit F11.
  if (static_cast<int>(reward_buffer_.size()) != num_agents) {
    reward_buffer_.assign(num_agents, 0.0);
  } else {
    std::fill(reward_buffer_.begin(), reward_buffer_.end(), 0.0);
  }
  if (task_lens_) {
    for (int i = 0; i < num_agents; ++i) {
      reward_buffer_[i] = task_lens_->ComputeReward(*this, i);
    }
    // Latch success so IsSuccess/IsDone survive even if agents subsequently
    // leave a winning configuration (matches pre-TaskLens semantics where
    // CalculateRewards set success_ once per episode). Latch a failure the
    // same way, after the rewards: the lens pays it on this step only. The
    // first outcome is final.
    if (!success_ && !failed_) {
      if (task_lens_->IsSuccess(*this)) {
        success_ = true;
      } else if (task_lens_->IsFailed(*this)) {
        failed_ = true;
      }
    }
  }
  result.rewards = reward_buffer_;

  // Check termination
  result.done = IsDone();
  LatchEndReason();

  return result;
}

EndReason BaseEnv::GetEndReason() const {
  if (!IsDone()) return EndReason::None;
  if (end_reason_ != EndReason::None) return end_reason_;
  return ComputeEndReason();
}

EndReason BaseEnv::ComputeEndReason() const {
  if (IsSuccess()) return EndReason::Success;
  // Done even without the horizon: a failure ended the episode, latched or by
  // the env's own rule. A latched failure the env's IsDone ignores did not.
  if (IsDoneWithoutHorizon()) return EndReason::TaskFailed;
  if (tick_ >= horizon_) return EndReason::Horizon;
  return EndReason::TaskFailed;  // An env's end rule it does not describe
}

void BaseEnv::LatchEndReason() {
  if (!IsDone()) {
    end_reason_ = EndReason::None;
  } else if (end_reason_ == EndReason::None) {
    end_reason_ = ComputeEndReason();
  }
}

void BaseEnv::RelatchEndReasonAfterLoad() {
  end_reason_ = EndReason::None;
  LatchEndReason();
}

void BaseEnv::PreStep() {
  // Run FSM updates BEFORE movement resolution so FSM agents set their intentions
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      // A stunned agent's FSM is frozen: no chasing, no wind-up, no strike.
      if (fsm_agent->HasFSM() && fsm_agent->IsAlive() && !fsm_agent->IsStunned()) {
        fsm_agent->UpdateFSM(*this);
      }
    }
  }
}

void BaseEnv::UpdateAgentFSM() {
  // FSM updates now happen in PreStep() before movement resolution.
  // This function is kept for potential post-movement FSM hooks.
}

void BaseEnv::ApplyD4Transform() {
  if (d4_transform_ == 0) return;  // Identity - no transformation needed

  D4Transform transform = ToD4Transform(d4_transform_);
  int old_rows = rows_;
  int old_cols = cols_;

  // Transform the grid
  grid_ = TransformGrid(*grid_, transform);

  // Update dimensions (may swap for certain transforms)
  auto [new_rows, new_cols] = GetTransformedDimensions(old_rows, old_cols, transform);
  rows_ = new_rows;
  cols_ = new_cols;

  // Transform actor positions
  object_manager_->TransformActorPositions(
      new_rows, new_cols,
      [transform](Position pos, int rows, int cols) {
        return TransformPosition(pos, rows, cols, transform);
      });

  // Transform annotation cell positions so goal tags / room tags / other
  // cell-keyed semantic data stay aligned with the rotated grid. Agent
  // annotations are keyed by ObjectId and need no transformation.
  // TransformPosition takes the pre-transform dims (matches the convention
  // used by ObjectManager::TransformActorPositions).
  annotations_.TransformCellPositions(
      [transform, old_rows, old_cols](Position pos) {
        return TransformPosition(pos, old_rows, old_cols, transform);
      });

  // Zones are cell-keyed like those annotations: same mapping (row-major in
  // the old dimensions -> row-major in the new ones).
  if (!cell_tags_.empty()) {
    std::vector<CellTag> moved(cell_tags_.size());
    for (int r = 0; r < old_rows; ++r) {
      for (int c = 0; c < old_cols; ++c) {
        Position to = TransformPosition(Position{r, c}, old_rows, old_cols, transform);
        moved[static_cast<size_t>(to.row * new_cols + to.col)] =
            cell_tags_[static_cast<size_t>(r * old_cols + c)];
      }
    }
    cell_tags_ = std::move(moved);
  }
}

std::string BaseEnv::ToString() const {
  std::ostringstream ss;
  ss << "Tick: " << tick_ << "\n";

  // Draw grid with actors
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      Position pos{r, c};
      const Actor* actor = object_manager_->GetActorAt(pos);
      if (actor && actor->IsAlive()) {
        ss << actor->GetChar();
      } else {
        ss << grid_->GetCell(pos).GetChar();
      }
    }
    ss << '\n';
  }

  // List agents
  ss << "\nAgents:\n";
  for (const Agent* agent : object_manager_->GetAllAgents()) {
    ss << "  [" << agent->GetAgentIndex() << "] "
       << agent->GetTypeName() << " #" << agent->GetId()
       << " @(" << agent->GetPosition().row << ","
       << agent->GetPosition().col << ")"
       << (agent->IsAlive() ? "" : " [DEAD]") << "\n";
  }

  return ss.str();
}

void BaseEnv::ObservationTensor(std::vector<float>& values, int player) const {
  auto shape = ObservationShape();
  int total = 1;
  for (int dim : shape) total *= dim;

  values.resize(total);
  std::fill(values.begin(), values.end(), 0.0f);

  // 5-plane observation (matching OpenSpiel format):
  // Plane 0: Floor cells (walkable, non-goal)
  // Plane 1: Wall cells (obstacles)
  // Plane 2: Goal cells (Synchro or Target depending on env)
  // Plane 3: Current player position
  // Plane 4: Other agents positions
  auto set_plane = [&](int plane, int row, int col, float value) {
    values[plane * rows_ * cols_ + row * cols_ + col] = value;
  };

  // Get current player agent for player-specific observation
  auto agents = object_manager_->GetAllAgents();
  const Agent* current_agent = nullptr;
  if (player >= 0 && player < static_cast<int>(agents.size())) {
    current_agent = agents[player];
  }

  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      Position pos{r, c};
      CellKind kind = grid_->GetCellKind(pos);

      // Plane 0: Floor (all walkable cells, including synchro)
      if (grid_->IsWalkable(pos)) {
        set_plane(0, r, c, 1.0f);
      }

      // Plane 1: Walls
      if (kind == CellKind::Wall) {
        set_plane(1, r, c, 1.0f);
      }

      // Plane 2: Goal cells (lens decides via annotations)
      if (task_lens_ && task_lens_->IsGoalCell(*this, pos)) {
        set_plane(2, r, c, 1.0f);
      }

      // Plane 3 & 4: Agent positions
      const Actor* actor = object_manager_->GetActorAt(pos);
      if (actor && actor->IsAlive()) {
        if (current_agent && actor->GetId() == current_agent->GetId()) {
          // Plane 3: Current player
          set_plane(3, r, c, 1.0f);
        } else {
          // Plane 4: Other agents
          set_plane(4, r, c, 1.0f);
        }
      }
    }
  }
}

std::vector<int> BaseEnv::ObservationShape() const {
  return {5, rows_, cols_};
}

// =============================================================================
// Vector Observation
// =============================================================================

int BaseEnv::VectorObservationSize() const {
  // Base features per player:
  // - Own position (row, col): 2
  // - Own health ratio: 1
  // - Distance to nearest goal (normalized): 1
  // - Relative positions to other companions (dx, dy per other): 2 * (max_companions - 1)
  // - Steps remaining (absolute / 100): 1
  // For simplicity, we use a fixed max of 3 companions
  constexpr int kMaxCompanions = 3;
  return 2 + 1 + 1 + 2 * (kMaxCompanions - 1) + 1;  // = 9
}

void BaseEnv::VectorObservation(std::vector<float>& values, int player) const {
  // Thin wrapper around WriteVectorObservation to avoid duplicating logic.
  values.assign(VectorObservationSize(), 0.0f);
  WriteVectorObservation(values.data(), player);
}

// =============================================================================
// Direct-Write Observation Methods (Zero-Copy for C Bindings)
// =============================================================================

void BaseEnv::WriteObservationTensor(float* buffer, int player) const {
  // Same logic as ObservationTensor() but writes directly to buffer
  int total = 5 * rows_ * cols_;
  std::memset(buffer, 0, total * sizeof(float));

  // 5-plane observation (matching OpenSpiel format):
  // Plane 0: Floor cells (walkable, non-goal)
  // Plane 1: Wall cells (obstacles)
  // Plane 2: Goal cells (Synchro or Target depending on env)
  // Plane 3: Current player position
  // Plane 4: Other agents positions
  auto set_plane = [&](int plane, int row, int col, float value) {
    buffer[plane * rows_ * cols_ + row * cols_ + col] = value;
  };

  // Get current player agent for player-specific observation
  auto agents = object_manager_->GetAllAgents();
  const Agent* current_agent = nullptr;
  if (player >= 0 && player < static_cast<int>(agents.size())) {
    current_agent = agents[player];
  }

  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      Position pos{r, c};
      CellKind kind = grid_->GetCellKind(pos);

      // Plane 0: Floor (all walkable cells, including synchro)
      if (grid_->IsWalkable(pos)) {
        set_plane(0, r, c, 1.0f);
      }

      // Plane 1: Walls
      if (kind == CellKind::Wall) {
        set_plane(1, r, c, 1.0f);
      }

      // Plane 2: Goal cells (lens decides via annotations)
      if (task_lens_ && task_lens_->IsGoalCell(*this, pos)) {
        set_plane(2, r, c, 1.0f);
      }

      // Plane 3 & 4: Agent positions
      const Actor* actor = object_manager_->GetActorAt(pos);
      if (actor && actor->IsAlive()) {
        if (current_agent && actor->GetId() == current_agent->GetId()) {
          // Plane 3: Current player
          set_plane(3, r, c, 1.0f);
        } else {
          // Plane 4: Other agents
          set_plane(4, r, c, 1.0f);
        }
      }
    }
  }
}

void BaseEnv::WriteVectorObservation(float* buffer, int player) const {
  // Same logic as VectorObservation() but writes directly to buffer
  int size = VectorObservationSize();
  std::memset(buffer, 0, size * sizeof(float));

  auto agents = object_manager_->GetAllAgents();
  if (player < 0 || player >= static_cast<int>(agents.size())) {
    return;
  }

  const Agent* current_agent = agents[player];
  Position my_pos = current_agent->GetPosition();

  // Normalization factors
  float max_dim = static_cast<float>(std::max(rows_, cols_));

  int idx = 0;

  // Feature 0-1: Own position (normalized to [0,1])
  buffer[idx++] = static_cast<float>(my_pos.row) / max_dim;
  buffer[idx++] = static_cast<float>(my_pos.col) / max_dim;

  // Feature 2: Health ratio
  int max_hp = current_agent->GetMaxHealth();
  buffer[idx++] = max_hp > 0 ? static_cast<float>(current_agent->GetHealth()) / max_hp : 1.0f;

  // Feature 3: Distance to nearest goal cell. Lens answers what counts as a
  // goal; lenses with no geometric goal (DodgeLens) return empty and the
  // feature is forced to 0.0 rather than a misleading 1.0 from an empty scan.
  if (task_lens_) {
    std::vector<Position> goals = task_lens_->GetGoalCells(*this);
    if (goals.empty()) {
      buffer[idx++] = 0.0f;
    } else {
      float min_dist = max_dim * 2.0f;  // Max possible Manhattan distance
      for (const auto& goal : goals) {
        float dist = static_cast<float>(std::abs(my_pos.row - goal.row) +
                                        std::abs(my_pos.col - goal.col));
        min_dist = std::min(min_dist, dist);
      }
      buffer[idx++] = min_dist / (max_dim * 2.0f);  // Normalize to [0,1]
    }
  } else {
    buffer[idx++] = 0.0f;
  }

  // Features 4-7: Relative positions to other companions (dx, dy per other)
  // Max 2 others (for 3 companions total)
  constexpr int kMaxOthers = 2;
  int other_count = 0;
  for (size_t i = 0; i < agents.size() && other_count < kMaxOthers; ++i) {
    if (static_cast<int>(i) == player) continue;
    const Agent* other = agents[i];
    Position other_pos = other->GetPosition();

    // Relative position normalized to [-1, 1]
    buffer[idx++] = static_cast<float>(other_pos.row - my_pos.row) / max_dim;
    buffer[idx++] = static_cast<float>(other_pos.col - my_pos.col) / max_dim;
    other_count++;
  }

  // Pad remaining slots with zeros if fewer than max others
  while (other_count < kMaxOthers) {
    buffer[idx++] = 0.0f;
    buffer[idx++] = 0.0f;
    other_count++;
  }

  // Feature 8: Steps remaining (absolute / 100)
  int steps_left = horizon_ - tick_;
  buffer[idx++] = static_cast<float>(steps_left) / 100.0f;
}

int BaseEnv::NumAgents() const {
  return object_manager_->GetNumAgents();
}

std::vector<Action> BaseEnv::LegalActions(int agent_idx) const {
  std::vector<Action> actions;

  auto agents = object_manager_->GetAllAgents();
  if (agent_idx < 0 || agent_idx >= static_cast<int>(agents.size())) {
    return actions;
  }

  const Agent* agent = agents[agent_idx];
  if (!agent->IsAlive() || agent->IsStunned() || agent->IsDowned()) {
    // The dead, the stunned and the downed (GatherIntentions forces the last
    // two to stay) can only stay
    actions.push_back(EncodeAction(MovementAction::Stay));
    return actions;
  }

  Position pos = agent->GetPosition();

  // Check each movement action
  for (int m = 0; m < kNumMovementActions; ++m) {
    MovementAction mov = static_cast<MovementAction>(m);
    Position target = ApplyMovement(pos, mov);

    // Stay is always legal; a rooted agent can only stay (GatherIntentions
    // turns its moves into Stay), though its skills below keep every aim
    if (mov == MovementAction::Stay) {
      actions.push_back(EncodeAction(mov));
      continue;
    }
    if (!CanMoveItself(*agent)) continue;

    // Check if target is walkable (don't check occupancy - that's for collision)
    if (grid_->IsInBounds(target) && grid_->IsWalkable(target)) {
      actions.push_back(EncodeAction(mov));
    }
  }

  // A companion's usable skills, slot by slot among the enabled ones (Skill1 =
  // slot 0, Skill2 = slot 1), each for every aim (the movement only aims, so
  // aiming into a wall is legal too).
  if (const auto* comp = dynamic_cast<const Companion*>(agent)) {
    for (int slot = 0; slot < kEnabledSkillSlots; ++slot) {
      if (!CanUseSkill(*comp, slot)) continue;
      const auto interact =
          static_cast<InteractAction>(static_cast<int>(InteractAction::Skill1) + slot);
      for (int m = 0; m < kNumMovementActions; ++m) {
        actions.push_back(EncodeAction(static_cast<MovementAction>(m), interact));
      }
    }
  }

  return actions;
}

void BaseEnv::GatherIntentions(const std::vector<Action>& actions) {
  auto agents = object_manager_->GetAllAgents();
  for (size_t i = 0; i < agents.size() && i < actions.size(); ++i) {
    Agent* agent = agents[i];

    // Stunned and downed agents are forced to stay
    if (agent->IsStunned() || agent->IsDowned()) {
      agent->SetIntention({MovementAction::Stay});
      continue;
    }

    // Skip agents with FSM - they set their own intentions in PreStep via UpdateFSM.
    // A rooted one still acts (its attack), it just does not move.
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      if (fsm_agent->HasFSM()) {
        if (agent->IsRooted()) {
          agent->SetIntention({MovementAction::Stay, agent->GetIntention().interact});
        }
        continue;
      }
    }

    DecodedAction decoded = DecodeAction(actions[i]);

    // A companion using a skill (its slot's, kDefaultSkill when nothing else
    // is there) stays put: the movement only aims (Stay keeps the facing). A
    // skill it cannot use is dropped and the move applies.
    if (decoded.interact != InteractAction::None) {
      if (Companion* comp = dynamic_cast<Companion*>(agent)) {
        if (CanUseSkill(*comp, SkillSlotOf(decoded.interact))) {
          if (auto dir = MovementToDirection(decoded.movement)) comp->SetDirection(*dir);
          agent->SetIntention({MovementAction::Stay, decoded.interact});
          continue;
        }
      }
      decoded.interact = InteractAction::None;
    }

    // Rooted agents cannot move by themselves
    if (!CanMoveItself(*agent)) {
      decoded.movement = MovementAction::Stay;
    }

    agent->SetIntention(decoded);

    // Auto-update direction for Companions based on intended movement
    if (auto dir = MovementToDirection(decoded.movement)) {
      if (Companion* comp = dynamic_cast<Companion*>(agent)) {
        comp->SetDirection(*dir);
      }
    }
  }
}

void BaseEnv::CaptureOriginalIntentions() {
  for (Agent* agent : object_manager_->GetAllAgents()) {
    agent->CaptureOriginalIntention();
  }
}

// =============================================================================
// Collision Resolution - Fixed-Point Iteration Algorithm
//
// Resolves simultaneous movement conflicts using iterative refinement:
// 1. Gather intended moves from all agents
// 2. Repeat until no changes:
//    a) Invalidate moves into walls/out-of-bounds
//    b) Detect and cancel swap conflicts (A→B, B→A)
//    c) Detect and cancel same-cell conflicts (A→X, B→X)
//    d) Validate chase moves (A→B only if B is moving away)
// 3. Execute remaining valid moves
//
// Guaranteed to terminate: each iteration removes at least one conflict.
// =============================================================================
void BaseEnv::ResolveCollisions() {
  // Safety limit: each iteration should remove at least one conflict,
  // so we need at most NumAgents * 4 iterations (4 directions per agent)
  bool changed = true;
  int max_iterations = NumAgents() * 4;
  int iteration = 0;

  // Hoist maps out of the loop — .clear() preserves bucket arrays, so we
  // only pay for one allocation cycle per ResolveCollisions call, not per
  // fixed-point iteration. Audit F7.
  std::unordered_map<ObjectId, Position> target_pos;
  std::unordered_map<Position, ObjectId, PositionHash> current_occupant;
  std::unordered_map<Position, std::vector<Agent*>, PositionHash> claims;

  while (changed && iteration++ < max_iterations) {
    changed = false;

    auto agents = object_manager_->GetAllAgents();

    // Build current state
    target_pos.clear();
    current_occupant.clear();
    for (const Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      target_pos[agent->GetId()] = PredictPosition(agent);
      current_occupant[agent->GetPosition()] = agent->GetId();
    }

    // Check 1: Grid walkability / bounds. Check 2: Swap detection (A→B,B→A).
    for (Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      if (agent->GetIntention().movement == MovementAction::Stay) continue;

      Position target = target_pos[agent->GetId()];

      if (!grid_->IsInBounds(target) || !grid_->IsWalkable(target)) {
        agent->SetIntention({MovementAction::Stay});
        changed = true;
        continue;
      }

      auto occ_it = current_occupant.find(target);
      if (occ_it != current_occupant.end()) {
        ObjectId other_id = occ_it->second;
        if (other_id != agent->GetId()) {
          Position other_target = target_pos[other_id];
          if (other_target == agent->GetPosition()) {
            // Swap detected! Both become noop
            agent->SetIntention({MovementAction::Stay});
            Agent* other = dynamic_cast<Agent*>(object_manager_->GetActor(other_id));
            if (other) {
              other->SetIntention({MovementAction::Stay});
            }
            changed = true;
            continue;
          }
        }
      }
    }

    // Check 3: Multiple agents claiming same target cell.
    // Rebuild target_pos after the Check-1/2 mutations.
    target_pos.clear();
    for (const Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      target_pos[agent->GetId()] = PredictPosition(agent);
    }

    claims.clear();
    for (Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      claims[target_pos[agent->GetId()]].push_back(agent);
    }

    for (auto& [pos, claiming_agents] : claims) {
      if (claiming_agents.size() > 1) {
        for (Agent* agent : claiming_agents) {
          if (agent->GetIntention().movement != MovementAction::Stay) {
            agent->SetIntention({MovementAction::Stay});
            changed = true;
          }
        }
      }
    }

    // Check 4: Chase validity - ensure cell will actually be vacated.
    target_pos.clear();
    for (const Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      target_pos[agent->GetId()] = PredictPosition(agent);
    }

    for (Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      if (agent->GetIntention().movement == MovementAction::Stay) continue;

      Position target = target_pos[agent->GetId()];
      auto occ_it = current_occupant.find(target);
      if (occ_it != current_occupant.end() && occ_it->second != agent->GetId()) {
        ObjectId occupant_id = occ_it->second;
        Agent* occupant = dynamic_cast<Agent*>(object_manager_->GetActor(occupant_id));
        if (occupant) {
          if (occupant->GetIntention().movement == MovementAction::Stay) {
            agent->SetIntention({MovementAction::Stay});
            changed = true;
          }
        }
      }
    }
  }
  assert(iteration <= max_iterations && "Collision resolution exceeded max iterations");
}

Position BaseEnv::PredictPosition(const Agent* agent) const {
  if (!agent || !agent->IsAlive()) return {-1, -1};
  return ApplyMovement(agent->GetPosition(), agent->GetIntention().movement);
}

bool BaseEnv::ValidateMovement(const Agent* agent, Position target) const {
  if (!agent) return false;
  if (!grid_->IsInBounds(target)) return false;
  if (!grid_->IsWalkable(target)) return false;
  return true;
}

void BaseEnv::ExecuteValidatedMovements() {
  // Collect all movements first (to handle simultaneous moves correctly)
  struct Movement {
    ObjectId id;
    Position from;
    Position to;
  };
  std::vector<Movement> movements;

  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    if (agent->GetIntention().movement == MovementAction::Stay) continue;

    Position from = agent->GetPosition();
    Position to = PredictPosition(agent);

    if (from != to) {
      movements.push_back({agent->GetId(), from, to});
    }
  }

  // Execute all movements
  for (const Movement& mv : movements) {
    object_manager_->UpdatePosition(mv.id, mv.to);
  }

  // Snapshot executed action (post-collision, pre-clear) so the C API can
  // read it after Step returns. GetIntention() alone would be {Stay, None}
  // immediately after the ClearIntention loop below.
  for (Agent* agent : object_manager_->GetAllAgents()) {
    agent->CaptureExecutedAction();
  }

  // Clear intentions
  for (Agent* agent : object_manager_->GetAllAgents()) {
    agent->ClearIntention();
  }
}

std::vector<Position> BaseEnv::FindEmptyCells(
    int count, pcg32& rng,
    std::optional<Rectangle> include,
    std::optional<Rectangle> exclude) {
  // Get all Floor cells
  std::vector<Position> candidates = grid_->FindCellsOfKind(CellKind::Floor);

  // Filter by occupancy and rectangle constraints
  std::vector<Position> empty;
  empty.reserve(candidates.size());
  for (const Position& pos : candidates) {
    if (object_manager_->IsOccupied(pos)) continue;
    if (include && !include->Contains(pos)) continue;
    if (exclude && exclude->Contains(pos)) continue;
    empty.push_back(pos);
  }

  // Shuffle results (portable_shuffle for cross-platform determinism)
  portable_shuffle(empty.begin(), empty.end(), rng);

  // Return all if count == -1
  if (count == -1) {
    return empty;
  }

  // Check if we have enough
  if (static_cast<int>(empty.size()) < count) {
    throw std::runtime_error(
        "Not enough empty cells: requested " + std::to_string(count) +
        ", available " + std::to_string(empty.size()));
  }

  return std::vector<Position>(empty.begin(), empty.begin() + count);
}

void BaseEnv::ResolveInteractions() {
  // FSM attack damage is handled via the Effect system (AttackState::OnEnter
  // spawns the effect); companions act through their skills.
  ResolveSkills();
}

// =============================================================================
// Skills
// =============================================================================

namespace {
bool PassesFilter(const Agent& a, TargetFilter f) {
  switch (f) {
    case TargetFilter::All: return true;
    case TargetFilter::Companion: return a.GetFaction() == Faction::COMPANION;
    case TargetFilter::Enemy: return a.GetFaction() == Faction::ENEMY;
    case TargetFilter::Neutral: return a.GetFaction() == Faction::NEUTRAL;
  }
  return false;
}
}  // namespace

void BaseEnv::ClearStepReports() {
  last_skill_uses_.clear();
  last_tags_applied_.clear();
}

bool BaseEnv::CanMoveItself(const Agent& agent) const { return !agent.IsRooted(); }

bool BaseEnv::CanUseSkill(const Companion& comp, int slot) const {
  if (!comp.IsAffectable() || slot < 0 || slot >= kEnabledSkillSlots) return false;
  if (comp.GetCooldown(slot) != 0) return false;
  const SkillConfig* skill = skills_.Find(comp.GetSkill(slot));
  if (!skill) return false;
  // A skill that moves its caster is movement: Rooted forbids it too.
  return !SkillMovesCaster(*skill) || CanMoveItself(comp);
}

bool BaseEnv::SetCompanionSkill(ObjectId id, int slot, const std::string& skill) {
  if (slot < 0 || slot >= kMaxSkillSlots || !IsValidNameLength(skill)) return false;
  auto* comp = dynamic_cast<Companion*>(object_manager_->GetActor(id));
  if (!comp) return false;
  if (!skill.empty() && !skills_.Find(skill)) return false;
  comp->SetSkill(slot, skill);  // "" puts kDefaultSkill back
  comp->SetCooldown(slot, 0);
  return true;
}

bool BaseEnv::ApplyTagTo(ObjectId id, const std::string& tag, int duration) {
  if (duration == 0 || duration < kPermanentTag) return false;
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent || tag.empty() || !IsValidNameLength(tag)) return false;
  agent->ApplyTag(tags_.Intern(tag), duration);
  return true;
}

bool BaseEnv::RemoveTagFrom(ObjectId id, const std::string& tag) {
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent) return false;
  TagId t = tags_.Find(tag);
  if (t != kInvalidTag) agent->RemoveTag(t);
  return true;
}

void BaseEnv::LandTag(Agent& agent, TagId tag, int duration, ObjectId source,
                      const std::string& cause) {
  if (duration == 0 || agent.IsDowned()) return;  // Lands nothing, so reports nothing
  bool fresh = !agent.HasTag(tag);
  agent.ApplyTag(tag, duration);
  last_tags_applied_.push_back({agent.GetId(), tag, duration, source, cause, fresh});
}

void BaseEnv::LandTag(Agent& agent, const std::string& tag, int duration,
                      ObjectId source, const std::string& cause) {
  if (duration == 0) return;  // Lands nothing, so interns nothing
  LandTag(agent, tags_.Intern(tag), duration, source, cause);
}

void BaseEnv::MoveActor(Actor& actor, Position to) {
  if (to == actor.GetPosition()) return;
  object_manager_->UpdatePosition(actor.GetId(), to);
  auto* agent = dynamic_cast<Agent*>(&actor);
  if (agent && agent->IsAffectable()) ApplyZoneTag(*agent);
}

bool BaseEnv::SetCellTag(Position cell, const std::string& tag, int duration) {
  if (!grid_->IsInBounds(cell)) return false;
  if (!tag.empty() && (duration == 0 || duration < kPermanentTag || !IsValidNameLength(tag))) {
    return false;
  }
  if (cell_tags_.empty()) {
    if (tag.empty()) return true;  // Nothing to clear
    cell_tags_.assign(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), CellTag{});
  }
  CellTag& c = cell_tags_[static_cast<size_t>(cell.row * cols_ + cell.col)];
  c = tag.empty() ? CellTag{} : CellTag{tags_.Intern(tag), duration};
  return true;
}

BaseEnv::CellTag BaseEnv::GetCellTag(Position cell) const {
  if (cell_tags_.empty() || !grid_->IsInBounds(cell)) return {};
  assert(cell_tags_.size() == static_cast<size_t>(rows_) * static_cast<size_t>(cols_));
  return cell_tags_[static_cast<size_t>(cell.row * cols_ + cell.col)];
}

void BaseEnv::ApplyZoneTag(Agent& agent) {
  CellTag c = GetCellTag(agent.GetPosition());
  if (c.tag == kInvalidTag) return;
  LandTag(agent, c.tag, c.duration, kInvalidObjectId, "zone");
}

void BaseEnv::ApplyZoneTags() {
  if (cell_tags_.empty()) return;
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (agent->IsAffectable()) ApplyZoneTag(*agent);
  }
}

std::vector<Position> BaseEnv::AreaCells(Position centre, SkillArea area) const {
  std::vector<Position> cells{centre};
  if (area == SkillArea::Cross) {
    // Priority order of the ring: up, right, down, left (vortex relies on it).
    for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
      int dr = 0, dc = 0;
      DirectionDelta(d, dr, dc);
      Position p{centre.row + dr, centre.col + dc};
      if (grid_->IsInBounds(p)) cells.push_back(p);
    }
  }
  return cells;
}

// Sequential, by design: casters resolve one by one in agent-index order
// (deterministic; earlier casters claim landings first), each from where it
// stands NOW. So an earlier caster's push / pull can move a later caster before
// it acts (its aim, range and area start from the new cell), and a caster
// rooted earlier in this pass still resolves its own skill this step, even a
// dash / teleport: whether it may use a skill was decided when intentions were
// gathered, and the root blocks from the next step.
void BaseEnv::ResolveSkills() {
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (!agent->IsAffectable()) continue;
    auto* comp = dynamic_cast<Companion*>(agent);
    if (!comp) continue;
    int slot = SkillSlotOf(agent->GetExecutedAction().interact);
    if (slot < 0 || slot >= kEnabledSkillSlots) continue;
    const SkillConfig* found = skills_.Find(comp->GetSkill(slot));
    if (!found) continue;
    // Deliberate copy: UseSkill must not observe a Define (it invalidates `found`).
    const SkillConfig skill = *found;
    Position target = UseSkill(*comp, skill);
    comp->SetCooldown(slot, skill.cooldown);
    last_skill_uses_.push_back({comp->GetId(), skill.name, target, slot});
  }
}

Position BaseEnv::UseSkill(Companion& caster, const SkillConfig& skill) {
  int dr = 0, dc = 0;
  DirectionDelta(caster.GetDirection(), dr, dc);
  const Position from = caster.GetPosition();
  std::vector<Position> path;  // Cells crossed by a dash (tag_path)

  // 1. The caster's own motion, and the skill's centre.
  switch (skill.motion) {
    case SkillMotion::Dash:
      MoveActor(caster, ResolveDash(*grid_, *object_manager_, from, dr, dc,
                                    skill.motion_distance, caster.GetId(), &path));
      break;
    case SkillMotion::Teleport:
      MoveActor(caster, ResolveTeleport(*grid_, *object_manager_, from, dr, dc,
                                        skill.motion_distance, caster.GetId()));
      break;
    default:
      break;
  }
  Position centre = from;
  switch (skill.targeting) {
    case SkillTargeting::Self:
      centre = caster.GetPosition();
      break;
    case SkillTargeting::Ground:
      centre = ResolveGroundTarget(*grid_, from, dr, dc, skill.range);
      break;
    case SkillTargeting::Projectile: {
      // Line rule, but the first living agent passing the filter stops it.
      Position cur = from;
      for (int i = 0; i < skill.range; ++i) {
        Position next{cur.row + dr, cur.col + dc};
        if (!grid_->IsInBounds(next) || !grid_->IsPathable(next)) break;
        cur = next;
        auto* hit = dynamic_cast<Agent*>(object_manager_->GetActorAt(cur));
        if (hit && hit != &caster && Affects(skill, caster, *hit)) break;
      }
      centre = cur;
      break;
    }
  }

  // 2. Affected agents: on the area, then (tag_path) on the dash path.
  std::vector<Agent*> affected;
  CollectAffected(AreaCells(centre, skill.area), skill, caster, affected);
  const std::vector<Agent*> on_area = affected;
  if (skill.tag_path) CollectAffected(path, skill, caster, affected);

  // 3. Tags land on who was there at impact (area and path).
  for (Agent* a : affected) {
    if (a == &caster && !skill.self_tags) continue;
    for (const SkillTagSpec& t : skill.tags) {
      LandTag(*a, t.tag, t.duration, caster.GetId(), skill.name);
    }
  }

  // 4. Damage, on the same agents (after the tags: an agent it kills still got
  // them). Deaths are the health system's (Agent::TakeDamage), as for effects.
  if (skill.damage > 0) {
    for (Agent* a : affected) {
      if (a == &caster && !skill.self_damage) continue;
      a->TakeDamage(skill.damage);
    }
  }

  // 5. Root (the area only) and area motions; the dead and the downed are neither.
  AreaMotion(skill, centre, caster, on_area);
  return centre;
}

bool BaseEnv::Affects(const SkillConfig& skill, const Agent& caster, const Agent& agent) const {
  if (!agent.IsAffectable() || !PassesFilter(agent, skill.filter)) return false;
  // The caster is of its own faction: without friendly fire it is spared too.
  return skill.friendly_fire || agent.GetFaction() != caster.GetFaction();
}

void BaseEnv::CollectAffected(const std::vector<Position>& cells, const SkillConfig& skill,
                              const Agent& caster, std::vector<Agent*>& affected) {
  for (const Position& p : cells) {
    auto* a = dynamic_cast<Agent*>(object_manager_->GetActorAt(p));
    if (!a || !Affects(skill, caster, *a)) continue;
    if (std::find(affected.begin(), affected.end(), a) == affected.end()) affected.push_back(a);
  }
}

void BaseEnv::AreaMotion(const SkillConfig& skill, Position centre, const Agent& caster,
                         const std::vector<Agent*>& on_area) {
  // Root first, before anything moves: no Agent* is used across a MoveActor.
  // Rooted for the next root_steps steps (a step timer, see Agent::BeginStep).
  // Push / pull never read Rooted.
  if (skill.root_steps > 0) {
    for (Agent* a : on_area) {
      if (!a->IsAffectable()) continue;  // Killed or downed by the skill's damage
      if (a == &caster && !skill.self_root) continue;
      a->ApplyStatus(StatusType::Rooted, skill.root_steps);
    }
  }

  std::vector<Position> cells = AreaCells(centre, skill.area);
  std::vector<Position> ring(cells.begin() + 1, cells.end());  // Up, right, down, left

  // A thing the motion may move: any living actor, but an agent only if the
  // skill affects it, and the caster only with self_motion.
  auto thing_at = [&](Position p) -> Actor* {
    Actor* a = object_manager_->GetActorAt(p);
    if (!a || !a->IsAlive()) return nullptr;
    if (a == &caster && !skill.self_motion) return nullptr;
    if (auto* ag = dynamic_cast<Agent*>(a); ag && !Affects(skill, caster, *ag)) return nullptr;
    return a;
  };

  if (skill.motion == SkillMotion::PushOut) {
    // Ring cells push in 4 different directions: no two pushes compete.
    for (const Position& p : ring) {
      Actor* a = thing_at(p);
      if (!a) continue;
      int dr = p.row - centre.row, dc = p.col - centre.col;
      MoveActor(*a, ResolveDash(*grid_, *object_manager_, p, dr, dc,
                                skill.motion_distance, a->GetId()));
    }
  } else if (skill.motion == SkillMotion::PullIn) {
    // Only into a free, walkable centre; one thing, by ring priority.
    if (CanLand(*grid_, *object_manager_, centre, kInvalidObjectId)) {
      for (const Position& p : ring) {
        if (Actor* a = thing_at(p)) {
          MoveActor(*a, centre);
          break;
        }
      }
    }
  }
}

// =============================================================================
// Effect System Delegation
// =============================================================================

void BaseEnv::SpawnEffect(const std::string& effect_name, EffectTarget target,
                          Direction direction, ObjectId source_id) {
  effect_system_->SpawnEffect(effect_name, target, direction, source_id);
}

const std::vector<ActiveEffect>& BaseEnv::GetActiveEffects() const {
  return effect_system_->GetActiveEffects();
}

void BaseEnv::ClearEffects() {
  effect_system_->Clear();
}

// =============================================================================
// Snapshot Support
// =============================================================================

Snapshot BaseEnv::SaveSnapshot() const {
  Snapshot snap;

  // Grid dimensions
  snap.rows = rows_;
  snap.cols = cols_;

  // Grid cells
  auto cell_data = grid_->GetAllCellData();
  snap.cells.reserve(cell_data.size());
  for (const auto& [kind, origin] : cell_data) {
    snap.cells.push_back({kind, origin});
  }

  // Agents
  for (const Agent* agent : object_manager_->GetAllAgents()) {
    AgentSnapshot as;
    as.id = agent->GetId();
    as.type = static_cast<int>(agent->GetType());
    as.position = agent->GetPosition();
    as.prev_position = agent->GetPosition();  // No prev tracking in base
    as.health = agent->GetHealth();
    as.max_health = agent->GetMaxHealth();
    as.agent_index = agent->GetAgentIndex();
    as.faction = static_cast<int>(agent->GetFaction());
    as.alive = agent->IsAlive();

    // Status effects
    for (const auto& status : agent->GetStatuses()) {
      if (status.IsActive()) {
        as.statuses.push_back({static_cast<int>(status.type), status.duration});
      }
    }

    // Tags, by name (ids are only meaningful within this env)
    for (const AgentTag& t : agent->GetTags()) {
      as.tags.push_back({tags_.Name(t.id), t.duration});
    }

    // Direction, skill slots and cooldowns for Companions
    if (const Companion* comp = dynamic_cast<const Companion*>(agent)) {
      as.direction = static_cast<int>(comp->GetDirection());
      as.color = static_cast<int>(comp->GetColor());
      for (int slot = 0; slot < kMaxSkillSlots; ++slot) {
        as.skills.push_back(comp->GetSkill(slot));
        as.cooldowns.push_back(comp->GetCooldown(slot));
      }
    }

    // FSM data for AgentFSM
    if (const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      if (fsm_agent->HasFSM()) {
        as.has_fsm = true;
        const FSMState* state = fsm_agent->GetCurrentState();
        as.fsm.state_type = state ? state->GetType() : FSMStateType::None;
        const FSMContext& ctx = fsm_agent->GetFSMContext();
        as.fsm.target_id = ctx.target_id;
        as.fsm.patrol_path = ctx.patrol_path;
        as.fsm.patrol_index = ctx.patrol_index;
        as.fsm.patrol_forward = ctx.patrol_forward;
        as.fsm.detection_range = ctx.detection_range;
        as.fsm.lose_target_range = ctx.lose_target_range;
        if (ctx.rng) {
          as.fsm.rng_state = ctx.rng->GetState();
          as.fsm.rng_inc = ctx.rng->GetInc();
        }
        // Attack runtime state
        as.fsm.attack_tick_counter = ctx.attack_tick_counter;
        as.fsm.attack_target_position = ctx.current_attack.target_position;
        as.fsm.attack_area_width = ctx.current_attack.area_width;
        as.fsm.attack_area_height = ctx.current_attack.area_height;
        as.fsm.attack_damage = ctx.current_attack.damage;
        as.fsm.attack_filter = ctx.current_attack.filter;
        // Attack configuration
        as.fsm.has_attack = ctx.has_attack;
        as.fsm.attack_effect = ctx.attack_effect_name;
        as.fsm.telegraph_ticks = ctx.telegraph_ticks;
        as.fsm.attack_ticks = ctx.attack_ticks;
        as.fsm.recovery_ticks = ctx.recovery_ticks;
      }
      if (FindEnemyKind(agent->GetTypeName())) as.kind = agent->GetTypeName();
      as.cadence = fsm_agent->GetCadence();
      as.tick = fsm_agent->GetTick();
    }

    snap.agents.push_back(std::move(as));
  }

  // Active effects
  for (const auto& effect : effect_system_->GetActiveEffects()) {
    EffectSnapshot es;
    es.effect_name = effect.config ? effect.config->name : "";
    es.target_type = static_cast<int>(effect.target.type);
    es.target_cell = effect.target.cell;
    es.target_actor_id = effect.target.actor_id;
    for (ObjectId id : effect.target.actors) {
      es.target_actors.push_back(id);
    }
    es.direction = static_cast<int>(effect.direction);
    es.ticks_remaining = effect.ticks_remaining;
    es.in_telegraph = effect.in_telegraph;
    es.loops_remaining = effect.loops_remaining;
    es.source_id = effect.source_id;
    snap.effects.push_back(std::move(es));
  }

  // Timing
  snap.tick = tick_;
  snap.horizon = horizon_;
  snap.d4_transform = d4_transform_;

  // Semantic annotations
  snap.annotations = annotations_.Serialize();

  // Every skill of the book, builtins included (a level may retune them), but
  // the fixed default one (a snapshot carrying it is rejected)
  for (const SkillConfig& skill : skills_.All()) {
    if (skill.name != kDefaultSkill) snap.skills.push_back(skill);
  }

  // Zones, by name, in current coordinates (as the cells and annotations above)
  for (int r = 0; r < rows_ && !cell_tags_.empty(); ++r) {
    for (int c = 0; c < cols_; ++c) {
      const CellTag& z = cell_tags_[static_cast<size_t>(r * cols_ + c)];
      if (z.tag != kInvalidTag) snap.cell_tags.push_back({Position{r, c}, tags_.Name(z.tag), z.duration});
    }
  }

  return snap;
}

void BaseEnv::LoadSnapshot(const Snapshot& snapshot) {
  // Validate dimensions
  if (snapshot.rows != rows_ || snapshot.cols != cols_) {
    throw std::runtime_error("Snapshot dimensions mismatch: expected " +
                             std::to_string(rows_) + "x" + std::to_string(cols_) +
                             ", got " + std::to_string(snapshot.rows) + "x" +
                             std::to_string(snapshot.cols));
  }

  // Validate for this environment type, and the v4 data (before any change)
  ValidateSnapshot(snapshot);
  snapshot.ValidateSkillsTagsZones();

  // Load grid cells
  std::vector<std::pair<CellKind, CellOrigin>> cell_data;
  cell_data.reserve(snapshot.cells.size());
  for (const auto& cell : snapshot.cells) {
    cell_data.emplace_back(cell.kind, cell.origin);
  }
  grid_->SetAllCellData(cell_data);

  // Clear existing state
  object_manager_->Clear();
  effect_system_->Clear();
  ClearStepReports();  // They name the old world's ObjectIds
  ClearCellTags();     // World state: replaced by the snapshot's zones below

  // The level's skill book: builtins, then the snapshot's skills (which may
  // retune builtins). A snapshot without skills leaves the builtins only, so
  // a level's skills never leak into the next one.
  skills_.Reset();
  for (const SkillConfig& skill : snapshot.skills) skills_.Define(skill);

  // The TagTable is deliberately NOT cleared: snapshots carry tag names and
  // re-intern them, and keeping the table keeps every id stable across loads
  // and resets for hosts that cache ids (a cleared table would hand an old id
  // to a different name). It only grows by the names the env has ever seen.

  // Load agents. They get new ids (0, 1, ... in the saved order), which may
  // differ from the saved ones (gaps left by removed objects, hand-authored
  // levels): references to agents (effect sources and targets, FSM targets
  // and agent annotations) are mapped through the saved ids below. The first
  // agent wins a duplicated saved id; an id naming no saved agent maps to
  // kInvalidObjectId (an ActorList entry or an agent annotation naming none is
  // dropped).
  std::unordered_map<int, ObjectId> saved_to_new;
  auto remap = [&saved_to_new](int saved) {
    auto it = saved_to_new.find(saved);
    return it == saved_to_new.end() ? kInvalidObjectId : it->second;
  };
  for (const auto& as : snapshot.agents) {
    ObjectType type = static_cast<ObjectType>(as.type);
    Agent* agent = nullptr;

    // Create the right agent type
    switch (type) {
      case ObjectType::Player:
        agent = object_manager_->CreateActor<Player>(as.position);
        break;
      case ObjectType::NPCCompanion:
        agent = object_manager_->CreateActor<NPCCompanion>(as.position);
        break;
      case ObjectType::Companion:
        agent = object_manager_->CreateActor<Companion>(as.position);
        break;
      case ObjectType::AgentFSM:
        // Restore the concrete class so its movement (A*, cadence, flying)
        // comes back with it (a kind ValidateSkillsTagsZones checked).
        if (const EnemyKind* kind = FindEnemyKind(as.kind)) {
          agent = kind->create(*object_manager_, as.position);
        } else {
          agent = object_manager_->CreateActor<AgentFSM>(as.position);
        }
        break;
      case ObjectType::Agent:
      default:
        agent = object_manager_->CreateActor<Agent>(as.position);
        break;
    }

    if (!agent) continue;
    saved_to_new.emplace(as.id, agent->GetId());

    // Restore basic state
    agent->SetMaxHealth(as.max_health);
    if (as.health < as.max_health) {
      agent->TakeDamage(as.max_health - as.health);
    }
    agent->SetFaction(static_cast<Faction>(as.faction));
    agent->SetAlive(as.alive);

    // Restore status effects
    for (const auto& ss : as.statuses) {
      agent->ApplyStatus(static_cast<StatusType>(ss.type), ss.duration);
    }

    // Restore tags (validated above), by name
    for (const TagSnapshot& t : as.tags) {
      agent->ApplyTag(tags_.Intern(t.tag), t.duration);
    }

    // Restore Companion-specific state
    if (Companion* comp = dynamic_cast<Companion*>(agent)) {
      comp->SetDirection(static_cast<Direction>(as.direction));
      comp->SetColor(static_cast<ActorColor>(as.color));
      // Slots as saved: ValidateSkillsTagsZones checked each names a skill of
      // the book built above. An empty ("" in older files) or missing slot is
      // kDefaultSkill (SetSkill).
      for (int slot = 0; slot < kMaxSkillSlots; ++slot) {
        const size_t i = static_cast<size_t>(slot);
        comp->SetSkill(slot, i < as.skills.size() ? as.skills[i] : std::string());
        comp->SetCooldown(slot, i < as.cooldowns.size() ? as.cooldowns[i] : 0);
      }
    }

    // Restore AgentFSM-specific state
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      fsm_agent->SetCadence(as.cadence);
      fsm_agent->SetTick(as.tick);

      if (as.has_fsm) {
        // Restore FSM state pointer via registry lookup
        const FSMState* state = GetFSMStateByType(as.fsm.state_type);
        if (state) {
          fsm_agent->SetCurrentState(state);
        }

        // Restore FSM context
        FSMContext& ctx = fsm_agent->GetFSMContext();
        ctx.target_id = as.fsm.target_id;
        ctx.patrol_path = as.fsm.patrol_path;
        ctx.patrol_index = as.fsm.patrol_index;
        ctx.patrol_forward = as.fsm.patrol_forward;
        ctx.detection_range = as.fsm.detection_range;
        ctx.lose_target_range = as.fsm.lose_target_range;

        // Restore attack runtime state
        ctx.attack_tick_counter = as.fsm.attack_tick_counter;
        ctx.current_attack.target_position = as.fsm.attack_target_position;
        ctx.current_attack.area_width = as.fsm.attack_area_width;
        ctx.current_attack.area_height = as.fsm.attack_area_height;
        ctx.current_attack.damage = as.fsm.attack_damage;
        ctx.current_attack.filter = as.fsm.attack_filter;

        // Restore attack configuration
        ctx.has_attack = as.fsm.has_attack;
        ctx.attack_effect_name = as.fsm.attack_effect;
        ctx.telegraph_ticks = as.fsm.telegraph_ticks;
        ctx.attack_ticks = as.fsm.attack_ticks;
        ctx.recovery_ticks = as.fsm.recovery_ticks;

        // Restore RNG state
        if (ctx.rng) {
          ctx.rng->SetState(as.fsm.rng_state, as.fsm.rng_inc);
        }
      }
    }
  }

  // Agents were placed in the grid alive, in the saved order: re-place them
  // now that they carry their saved alive flag, so a corpse never hides a
  // living agent standing on its cell.
  object_manager_->RebuildGrid();

  // FSM targets were loaded as saved ids (the target may come later)
  for (AgentFSM* fsm_agent : object_manager_->GetAllAgentFSMs()) {
    FSMContext& ctx = fsm_agent->GetFSMContext();
    ctx.target_id = remap(ctx.target_id);
  }

  // Load effects
  for (const auto& es : snapshot.effects) {
    const EffectConfig* config = EffectConfigRegistry::Instance().GetConfig(es.effect_name);
    if (!config) continue;

    EffectTarget target;
    target.type = static_cast<EffectTarget::Type>(es.target_type);
    target.cell = es.target_cell;
    target.actor_id = remap(es.target_actor_id);
    for (int id : es.target_actors) {
      const ObjectId actor = remap(id);
      if (actor != kInvalidObjectId) target.actors.push_back(actor);
    }

    ActiveEffect effect;
    effect.config = config;
    effect.target = target;
    effect.direction = static_cast<Direction>(es.direction);
    effect.ticks_remaining = es.ticks_remaining;
    effect.in_telegraph = es.in_telegraph;
    effect.loops_remaining = es.loops_remaining;
    effect.source_id = remap(es.source_id);

    effect_system_->AddEffect(std::move(effect));
  }

  // Restore timing
  tick_ = snapshot.tick;
  horizon_ = snapshot.horizon;
  d4_transform_ = snapshot.d4_transform;

  // A new episode: no outcome latched yet (snapshots carry none). Every
  // generated Reset goes through here too.
  ResetOutcome();

  // Restore semantic annotations (present in v2+ snapshots; empty vector in
  // v1). Agent annotations name saved ids: mapped like the effect targets.
  std::vector<AnnotationSnapshot> annotations;
  annotations.reserve(snapshot.annotations.size());
  for (AnnotationSnapshot a : snapshot.annotations) {
    if (a.target_type == static_cast<uint8_t>(AnnotationTarget::Agent)) {
      a.agent_id = remap(a.agent_id);
      if (a.agent_id == kInvalidObjectId) continue;
    }
    annotations.push_back(std::move(a));
  }
  annotations_.Deserialize(annotations);

  // Restore zones (v4+), in the snapshot's frame like the cells and cell
  // annotations: ApplyD4Transform below moves all three together.
  for (const CellTagSnapshot& z : snapshot.cell_tags) {
    const bool set = SetCellTag(z.cell, z.tag, z.duration);  // Validated above
    assert(set && "validated zone rejected");
    (void)set;
  }

  // Apply D4 symmetry transformation if specified
  // (Snapshot contains pre-transform positions, so we apply transform after loading)
  if (d4_transform_ != 0) {
    ApplyD4Transform();
  }

  // A state loaded already done (at or past the horizon) ended there: a
  // later kill keeps that reason, as after a Step.
  // The latch reads IsDone() on the state as loaded so far: a derived
  // LoadSnapshot override, or a Reset that loads a generated level, must
  // finish its own state first, then latch again (RelatchEndReasonAfterLoad).
  // AggroEnv's Reset spawns its enemy after this: under the Aggro lens its
  // level, with no living enemy yet, is done here and would keep a stale
  // TaskFailed. DodgeEnv has no override; its any_dead_ is not cleared by a
  // load (its Reset clears it).
  LatchEndReason();
}

void BaseEnv::ValidateSnapshot(const Snapshot& /*snapshot*/) const {
  // Base implementation does no validation
  // Subclasses override to check for required cell types
}

const std::vector<Position>& BaseEnv::GetPatrolPath() const {
  static const std::vector<Position> empty;
  return empty;
}

bool BaseEnv::SetTaskLens(std::unique_ptr<TaskLens> lens) {
  if (lens && !lens->CanOperateOn(*this)) {
    return false;
  }
  // Deactivate the outgoing lens so it restores any stamped cells.
  if (task_lens_) {
    task_lens_->Deactivate(*this);
  }
  task_lens_ = std::move(lens);
  // Reset the outcome so the new lens evaluates its task from scratch
  ResetOutcome();
  LatchEndReason();  // A lens change can end the episode
  return true;
}

bool BaseEnv::SetTaskLensWithParams(std::unique_ptr<TaskLens> lens,
                                     const LensParams& params) {
  // Rollback-safe swap (audit F12). Save the outgoing lens; only release it
  // once we've confirmed the new one can operate. If Activate + CanOperateOn
  // fail, un-stamp the new lens and restore the old one instead of leaving
  // the env lens-less.
  std::unique_ptr<TaskLens> previous = std::move(task_lens_);
  if (previous) {
    previous->Deactivate(*this);
  }

  if (lens) {
    // Activate BEFORE CanOperateOn so lenses that materialize their own
    // objective cells (SynchroLens, TagApplyLens) can satisfy the check.
    lens->Activate(*this, params);
    if (!lens->CanOperateOn(*this)) {
      lens->Deactivate(*this);
      // Restore previous lens so the env stays in a usable state.
      if (previous) {
        // Re-stamp the previous lens's cells via Activate. Activate takes
        // LensParams, but the previous lens was originally activated from
        // its own params — we don't store them, so callers of the failed
        // SetTaskLensWithParams that need re-stamping must hand those
        // params back. For our current lenses (Synchro/Aggro/Dodge),
        // plain SetTaskLens is enough to restore the unparameterized state.
        task_lens_ = std::move(previous);
      }
      return false;
    }
  }
  task_lens_ = std::move(lens);
  ResetOutcome();
  LatchEndReason();  // A lens change can end the episode
  return true;
}

}  // namespace companions
