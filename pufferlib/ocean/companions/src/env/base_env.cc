// Copyright 2024
// BaseEnv implementation

#include "base_env.h"

#include <algorithm>
#include <cassert>
#include <cmath>
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
  effect_system_->SetHealthSink(&effect_health_);
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
      // The source's exact lens, not activated: its stamped cells and
      // annotations are copied with the grid
      task_lens_(other.task_lens_ ? other.task_lens_->Clone() : nullptr),
      annotations_(other.annotations_),
      success_(other.success_),
      end_reason_(other.end_reason_),
      interrupted_(other.interrupted_),
      downs_seen_(other.downs_seen_),
      down_cost_(other.down_cost_),
      tags_(other.tags_),
      skills_(other.skills_),
      last_skill_uses_(other.last_skill_uses_),
      last_tags_applied_(other.last_tags_applied_),
      last_downs_(other.last_downs_),
      last_revives_(other.last_revives_),
      cell_tags_(other.cell_tags_),
      zone_defs_(other.zone_defs_),
      in_step_(other.in_step_),
      pending_zones_(other.pending_zones_),
      intended_skills_(other.intended_skills_),
      max_downs_(other.max_downs_),
      context_skills_(other.context_skills_),
      reactions_(other.reactions_),
      resolved_reactions_(other.resolved_reactions_),
      tag_statuses_(other.tag_statuses_),
      tag_status_ids_(other.tag_status_ids_),
      last_reactions_(other.last_reactions_),
      last_defeats_(other.last_defeats_),
      last_turn_health_(other.last_turn_health_) {
  // Update EffectSystem pointers to point to our new copies
  effect_system_->UpdatePointers(object_manager_.get(), grid_.get());
  effect_system_->SetHealthSink(&effect_health_);
}

BaseEnv& BaseEnv::operator=(const BaseEnv& other) {
  if (this != &other) {
    rows_ = other.rows_;
    cols_ = other.cols_;
    grid_ = std::make_unique<Grid>(*other.grid_);
    object_manager_ = std::make_unique<ObjectManager>(*other.object_manager_);
    effect_system_ = std::make_unique<EffectSystem>(*other.effect_system_);
    effect_system_->UpdatePointers(object_manager_.get(), grid_.get());
    effect_system_->SetHealthSink(&effect_health_);
    tick_ = other.tick_;
    horizon_ = other.horizon_;
    d4_transform_ = other.d4_transform_;
    // Replaces this env's lens (no Deactivate: the grid and annotations are
    // replaced too)
    task_lens_ = other.task_lens_ ? other.task_lens_->Clone() : nullptr;
    annotations_ = other.annotations_;
    success_ = other.success_;
    end_reason_ = other.end_reason_;
    interrupted_ = other.interrupted_;
    downs_seen_ = other.downs_seen_;
    down_cost_ = other.down_cost_;
    tags_ = other.tags_;
    skills_ = other.skills_;
    last_skill_uses_ = other.last_skill_uses_;
    last_tags_applied_ = other.last_tags_applied_;
    last_downs_ = other.last_downs_;
    last_revives_ = other.last_revives_;
    cell_tags_ = other.cell_tags_;
    zone_defs_ = other.zone_defs_;
    in_step_ = other.in_step_;
    pending_zones_ = other.pending_zones_;
    // Scratch: it would point at the other env's agents
    zone_landings_.clear();
    reaction_hits_.clear();
    reaction_affected_.clear();
    hit_agents_.clear();
    turn_.Clear();
    intended_skills_ = other.intended_skills_;
    max_downs_ = other.max_downs_;
    context_skills_ = other.context_skills_;
    reactions_ = other.reactions_;
    resolved_reactions_ = other.resolved_reactions_;
    tag_statuses_ = other.tag_statuses_;
    tag_status_ids_ = other.tag_status_ids_;
    last_reactions_ = other.last_reactions_;
    last_defeats_ = other.last_defeats_;
    last_turn_health_ = other.last_turn_health_;
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

  // Paused for this whole step (IsInterrupted), read before the latch below
  // can end the pause: downs while paused pay nothing, whether they came
  // during the step or from the host between steps. The step that clears the
  // pause is still paused.
  const bool paused = interrupted_;

  // The verdict as the step starts, before it changes anything: a team the
  // host downed between steps is lost (TeamDown latched, so this step latches
  // no success and pays no win reward: SuccessCounts); an episode the host
  // made not done again (max_downs raised after a team down) is open again
  // (None: a success reached this step counts).
  LatchEndReason();

  // Timers set from here on also cover the rest of this step (see
  // Agent::BeginStep); they all tick at its end.
  ClearStepReports();
  for (Agent* agent : object_manager_->GetAllAgents()) agent->BeginStep();
  in_step_ = true;
  // A step that throws before its end (TickZones) leaves the env between two
  // steps again (AbortStep), not stuck inside one: a later SaveSnapshot would
  // refuse, and every timer set later would be kept one step too long.
  struct StepScope {
    BaseEnv& env;
    ~StepScope() {
      if (env.in_step_) env.AbortStep();
    }
  } step_scope{*this};
  // Nothing changes HP, alive or down during the turn: its ledger, applied
  // at its end (ApplyTurnOutcomes)
  BeginTurn();

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

  // Zones land on whoever stands on them, before casts and skills. From here
  // to the end of the step the zone map is read-only: a reaction's
  // zone_becomes waits in pending_zones_ (CommitPendingZones, below).
  ApplyZoneTags();

  // Resolve interactions (attacks, effects)
  ResolveInteractions();

  // Tick active effects (advance timers, apply damage/push)
  // Effect pushes move agents without zone tags: they get them next step if they stay.
  effect_system_->Tick();

  // The zones the step's reactions set: the map changes here, once (they
  // first land next step)
  CommitPendingZones();

  // The turn's end: its health totals, downs, deaths and revives, at once
  ApplyTurnOutcomes();

  // Every timer (tags, statuses, cooldowns, then the zones') ticks here, at
  // the end of the step
  for (Agent* agent : object_manager_->GetAllAgents()) agent->EndStep();
  TickZones();

  // Downs since the last report (this step's, and any between steps)
  for (Companion* c : object_manager_->GetAllCompanions()) {
    for (int n = c->TakeUnreportedDowns(); n > 0; --n) last_downs_.push_back(c->GetId());
  }

  // Increment tick
  tick_++;

  // Post-step hook
  PostStep();

  // The team's new downs (this step's, and any the host caused between
  // steps), read from the state
  const int downs = GetDowns();
  const int new_downs = std::max(0, downs - downs_seen_);

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
  if (task_lens_ && !paused) {
    // The lens's reward, plus the down cost once per new down
    const double down_cost = down_cost_ * new_downs;
    for (int i = 0; i < num_agents; ++i) {
      reward_buffer_[i] = task_lens_->ComputeReward(*this, i) + down_cost;
    }
    // Latch success so IsSuccess/IsDone survive even if agents subsequently
    // leave a winning configuration (matches pre-TaskLens semantics where
    // CalculateRewards set success_ once per episode). The only outcome a
    // lens latches: a task fails only by the team down or the horizon (IsDone).
    // Never once the episode ended (SuccessCounts): past the horizon, the
    // level is lost.
    if (!success_ && SuccessCounts() && task_lens_->IsSuccess(*this)) success_ = true;
    // A new down interrupts the task while the episode goes on: not on a step
    // that ends it (a success, this step's included; the horizon; the team
    // down), nor after it ended (interrupted_ is false here: IsDone() is
    // the final verdict alone)
    if (new_downs > 0 && task_lens_->IsInterruptible() && !IsDone()) interrupted_ = true;
  }
  // Seen once the lens had its say (a lens that throws leaves them new)
  downs_seen_ = downs;
  // Nobody down: the lens rewards and decides again from the next step
  if (interrupted_ && !AnyCompanionDowned()) interrupted_ = false;
  result.rewards = reward_buffer_;

  // Check termination
  result.done = IsDone();
  LatchEndReason();

  return result;
}

int BaseEnv::GetDowns() const {
  int downs = 0;
  for (const Companion* c : object_manager_->GetAllCompanions()) downs += c->GetTimesDowned();
  return downs;
}

bool BaseEnv::SetMaxDowns(int max_downs) {
  if (max_downs < 1) return false;
  max_downs_ = max_downs;
  return true;
}

bool BaseEnv::IsTeamDown() const {
  // One pass (IsDone reads it several times a step): the downs, and whether
  // anyone still stands (a dead companion does not)
  const auto companions = object_manager_->GetAllCompanions();
  if (companions.empty()) return false;
  int downs = 0;
  bool anyone_standing = false;
  for (const Companion* c : companions) {
    downs += c->GetTimesDowned();
    if (c->IsAffectable()) anyone_standing = true;
  }
  return downs >= max_downs_ || !anyone_standing;
}

bool BaseEnv::AnyCompanionDowned() const {
  for (const Companion* c : object_manager_->GetAllCompanions()) {
    if (c->IsAlive() && c->IsDowned()) return true;
  }
  return false;
}

bool BaseEnv::SetDownCost(double cost) {
  if (!std::isfinite(cost) || cost > 0.0) return false;
  down_cost_ = cost;
  return true;
}

double BaseEnv::WorstDownCost() const {
  const int companions = static_cast<int>(object_manager_->GetAllCompanions().size());
  if (companions == 0) return 0.0;
  return down_cost_ * (max_downs_ - 1 + companions);
}

EndReason BaseEnv::GetEndReason() const {
  if (!IsDone()) return EndReason::None;
  // A fixed reason; Interrupted is provisional (a later reason upgrades it)
  if (end_reason_ != EndReason::None && end_reason_ != EndReason::Interrupted) return end_reason_;
  return ComputeEndReason();
}

EndReason BaseEnv::ComputeEndReason() const {
  // IsDone's terms (success_, not the virtual IsSuccess, as IsDone reads it),
  // by priority: the success, the team down, the horizon; on a done env, what
  // is left is the interruption
  if (success_) return EndReason::Success;
  if (IsTeamDown()) return EndReason::TeamDown;
  if (tick_ >= horizon_) return EndReason::Horizon;
  assert(interrupted_ && "ComputeEndReason on an env that is not done");
  return EndReason::Interrupted;
}

void BaseEnv::LatchEndReason() {
  if (!IsDone()) {
    end_reason_ = EndReason::None;
  } else if (end_reason_ == EndReason::None || end_reason_ == EndReason::Interrupted) {
    end_reason_ = ComputeEndReason();
    // A final verdict reached while paused ends the pause
    if (end_reason_ != EndReason::Interrupted) interrupted_ = false;
  }
}

void BaseEnv::RelatchEndReasonAfterLoad() {
  end_reason_ = EndReason::None;
  downs_seen_ = GetDowns();  // The state as it now is: its downs are not new
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
  // Indexed like `agents`: ResolveSkills reads them by the same index, so
  // nothing may add or remove agents between GatherIntentions and
  // ResolveSkills (it falls back to the caster's id if something did).
  intended_skills_.assign(agents.size(), IntendedSkill{});
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

    // A companion using a skill (its slot's effective one: a context rule's,
    // else the equipped one, kDefaultSkill when nothing else is there) stays
    // put: the movement only aims (Stay keeps the facing). A skill it cannot
    // use is dropped and the move applies. The skill is fixed here, for
    // ResolveSkills: what happens later in the step does not change it.
    if (decoded.interact != InteractAction::None) {
      if (Companion* comp = dynamic_cast<Companion*>(agent)) {
        const int slot = SkillSlotOf(decoded.interact);
        const ContextSkillRule* rule = nullptr;
        if (CanUseSkill(*comp, slot, rule)) {
          intended_skills_[i] = {comp->GetId(), slot,
                                 rule ? static_cast<int>(rule - context_skills_.data()) : -1};
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
  last_reactions_.clear();
  last_defeats_.clear();
  last_downs_.clear();
  last_revives_.clear();
  last_turn_health_.clear();
}

bool BaseEnv::CanMoveItself(const Agent& agent) const { return !agent.IsRooted(); }

bool BaseEnv::CanUseSkill(const Companion& comp, int slot) const {
  const ContextSkillRule* rule = nullptr;
  return CanUseSkill(comp, slot, rule);
}

bool BaseEnv::CanUseSkill(const Companion& comp, int slot, const ContextSkillRule*& rule) const {
  rule = nullptr;
  if (!comp.IsAffectable() || slot < 0 || slot >= kEnabledSkillSlots) return false;
  rule = ActiveContextRule(comp, slot);
  // The cooldown belongs to the equipped skill: a rule's skill does not read
  // it, even when the rule gives the very skill the slot holds (the origin
  // decides, not the name)
  if (!rule && comp.GetCooldown(slot) != 0) return false;
  const SkillConfig* skill = skills_.Find(rule ? rule->skill : comp.GetSkill(slot));
  if (!skill) return false;
  // A skill that moves its caster is movement: Rooted forbids it too.
  return !SkillMovesCaster(*skill) || CanMoveItself(comp);
}

bool BaseEnv::ContextHolds(ContextCondition condition, const Companion& comp) const {
  switch (condition) {
    case ContextCondition::AdjacentDownedAlly: {
      const Position at = comp.GetPosition();
      for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
        int dr = 0, dc = 0;
        DirectionDelta(d, dr, dc);
        const Position p{at.row + dr, at.col + dc};
        if (!grid_->IsInBounds(p)) continue;
        // GetActorAt gives the living actor on a cell (a downed one is alive)
        const auto* other = dynamic_cast<const Agent*>(object_manager_->GetActorAt(p));
        if (other && other->IsAlive() && other->IsDowned() &&
            other->GetFaction() == comp.GetFaction()) {
          return true;
        }
      }
      return false;
    }
  }
  return false;
}

const ContextSkillRule* BaseEnv::ActiveContextRule(const Companion& comp, int slot) const {
  // A companion that cannot act (downed, dead) has no context: a downed one
  // lying next to a downed ally shows its equipped skill
  if (!comp.IsAffectable()) return nullptr;
  for (const ContextSkillRule& rule : context_skills_) {
    // A rule the book no longer allows (its skill gone or given a cooldown
    // behind the rules' back: a Define, a generated Reset reloading the
    // builtins) is skipped: it never disables a slot, and a context skill
    // never has a cooldown. The (cheap) condition first: the book is only
    // searched when it holds.
    if (rule.slot == slot && ContextHolds(rule.condition, comp) && IsUsableWith(rule, skills_)) {
      return &rule;
    }
  }
  return nullptr;
}

const std::string& BaseEnv::EffectiveSkill(const Companion& comp, int slot) const {
  static const std::string kNoSkill;
  if (slot < 0 || slot >= kMaxSkillSlots) return kNoSkill;
  const ContextSkillRule* rule = ActiveContextRule(comp, slot);
  return rule ? rule->skill : comp.GetSkill(slot);
}

bool BaseEnv::IsContextSkill(const Companion& comp, int slot) const {
  return ActiveContextRule(comp, slot) != nullptr;
}

bool BaseEnv::SetContextSkills(std::vector<ContextSkillRule> rules, std::string* error) {
  try {
    ValidateContextSkills(rules, skills_);
  } catch (const std::runtime_error& e) {
    if (error) *error = e.what();
    return false;
  }
  context_skills_ = std::move(rules);
  return true;
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
  if (!IsValidTimer(duration)) return false;
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent || tag.empty() || !IsValidNameLength(tag)) return false;
  if (!agent->IsAffectable()) return false;  // Downed or dead: lands and interns nothing
  // A landing like any other (immunity, status, weakness, reaction), reported
  return LandTag(*agent, tag, duration, kInvalidObjectId, "host", TagSource::Host);
}

bool BaseEnv::RemoveTagFrom(ObjectId id, const std::string& tag) {
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent) return false;
  TagId t = tags_.Find(tag);
  if (t != kInvalidTag) agent->RemoveTag(t);
  return true;
}

bool BaseEnv::LandTag(Agent& agent, TagId tag, int duration, ObjectId source,
                      const std::string& cause, TagSource kind, int reaction) {
  // 1-2. Immunity, the tag and its status
  if (!PutTag(agent, tag, duration, source, cause, kind, reaction)) return false;
  // 3. Weakness: during a step a defeated agent stays in play until the end
  //    of the turn (reaction outcomes, zone damage); between two steps it is
  //    defeated at once and gets nothing more.
  ResolveWeakness(agent, tag, source, cause, kind, reaction);
  // 4. Reaction: results never trigger one. A defeated trigger still starts
  //    it (its tags stay): a spread reaches the others and changes the zone.
  if (kind != TagSource::Reaction) ResolveReaction(agent, tag, source, cause, kind);
  return true;
}

bool BaseEnv::PutTag(Agent& agent, TagId tag, int duration, ObjectId source,
                     const std::string& cause, TagSource kind, int reaction) {
  if (duration == 0 || !agent.IsAffectable()) return false;  // Lands nothing, so reports nothing
  // 1. Immunity: an immune agent gets nothing (no report, no zone damage).
  if (agent.IsImmuneTo(tag)) return false;
  // 2. The tag, and the status bound to it (a step timer, like the tag's).
  TagApplication landed(kind);
  landed.agent = agent.GetId();
  landed.tag = tag;
  landed.duration = duration;
  landed.source = source;
  landed.cause = cause;
  landed.fresh = !agent.HasTag(tag);  // A result: ResolveReaction corrects it
  landed.reaction = reaction;
  agent.ApplyTag(tag, duration);
  last_tags_applied_.push_back(std::move(landed));
  for (size_t i = 0; i < tag_status_ids_.size(); ++i) {
    if (tag_status_ids_[i] == tag) {
      agent.ApplyStatus(tag_statuses_[i].status, tag_statuses_[i].steps);
      break;
    }
  }
  return true;
}

bool BaseEnv::LandTag(Agent& agent, const std::string& tag, int duration, ObjectId source,
                      const std::string& cause, TagSource kind) {
  if (duration == 0 || !agent.IsAffectable()) return false;  // Lands nothing, so interns nothing
  // An immune agent: SetImmunities interned the tag, so this interns nothing new
  return LandTag(agent, tags_.Intern(tag), duration, source, cause, kind);
}

void BaseEnv::ResolveWeakness(Agent& agent, TagId tag, ObjectId source, const std::string& cause,
                              TagSource kind, int reaction) {
  const std::vector<Agent::WeakTo>& weak_to = agent.GetWeakTo();
  if (weak_to.empty()) return;
  // P is asked of the map (the zone it stands on, as the step began: a step
  // changes the map only at its end), never of its tags
  const TagId here = GetCellTag(agent.GetPosition()).tag;
  if (here == kInvalidTag) return;
  for (const Agent::WeakTo& w : weak_to) {
    if (w.tag != tag || w.zone != here) continue;
    DefeatBy(agent, w, source, cause, kind, reaction);
    return;
  }
}

void BaseEnv::DefeatBy(Agent& agent, const Agent::WeakTo& weakness, ObjectId source,
                       const std::string& cause, TagSource kind, int reaction) {
  if (in_step_) {
    // In play until the end of the turn, which leaves it at 0
    LedgerEntry& e = LedgerOf(agent);
    if (e.defeated) return;  // Once per turn
    e.defeated = e.touched = true;
  } else {
    agent.Defeat();
  }
  DefeatReport d(kind);
  d.agent = agent.GetId();
  d.zone = weakness.zone;
  d.tag = weakness.tag;
  d.source = source;
  d.cause = cause;
  d.reaction = reaction;
  last_defeats_.push_back(std::move(d));
}

std::vector<char> BaseEnv::ZoneRegion(Position start) const {
  const TagId zone = GetCellTag(start).tag;
  if (zone == kInvalidTag) return {};
  auto index = [this](Position p) { return static_cast<size_t>(p.row * cols_ + p.col); };
  std::vector<char> region(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), 0);
  std::vector<Position> open{start};
  region[index(start)] = 1;
  while (!open.empty()) {
    const Position p = open.back();
    open.pop_back();
    for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
      int dr = 0, dc = 0;
      DirectionDelta(d, dr, dc);
      const Position n{p.row + dr, p.col + dc};
      if (!grid_->IsInBounds(n) || region[index(n)] || cell_tags_[index(n)].tag != zone) continue;
      region[index(n)] = 1;
      open.push_back(n);
    }
  }
  return region;
}

void BaseEnv::ResolveReaction(Agent& agent, TagId tag, ObjectId source, const std::string& cause,
                              TagSource kind) {
  const int rule = FindReaction(agent, tag);
  if (rule >= 0) FireReaction(agent, tag, rule, source, cause, kind);
}

int BaseEnv::FindReaction(const Agent& agent, TagId tag) const {
  // The first rule, in level order, pairing `tag` with a tag the agent carries
  for (size_t i = 0; i < resolved_reactions_.size(); ++i) {
    const ResolvedReaction& r = resolved_reactions_[i];
    if ((r.a == tag && agent.HasTag(r.b)) || (r.b == tag && agent.HasTag(r.a))) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void BaseEnv::FireReaction(Agent& agent, TagId tag, int rule, ObjectId source,
                           const std::string& cause, TagSource kind) {
  std::vector<Agent*> affected;
  const int index = StartReaction(agent, tag, rule, source, cause, kind, affected);
  if (index < 0) return;
  const ReactionRule& spec = reactions_[static_cast<size_t>(rule)];
  const ResolvedReaction& r = resolved_reactions_[static_cast<size_t>(rule)];

  // The outcome, agent by agent (each read on the map as the step began: the
  // region changes at the end of the step, or last between two steps)
  for (Agent* a : affected) {
    if (!a->IsAffectable()) continue;  // Nothing reaches it any more
    ReactionOutcome outcome;
    outcome.agent = a->GetId();
    const bool carried = a->HasTag(r.result);  // Before the originals go
    if (!r.keep_a) a->RemoveTag(r.a);
    if (!r.keep_b) a->RemoveTag(r.b);
    const size_t defeats = last_defeats_.size();
    const size_t landing = last_tags_applied_.size();  // The result's report entry
    outcome.result_landed =
        LandTag(*a, r.result, kPermanentTag, source, cause, TagSource::Reaction, index);
    if (outcome.result_landed) last_tags_applied_[landing].fresh = !carried;
    outcome.defeated = last_defeats_.size() > defeats;
    if (spec.damage > 0 && a->IsAffectable()) {
      outcome.damage = spec.damage;
      HurtInStep(*a, spec.damage);
    }
    last_reactions_[static_cast<size_t>(index)].affected.push_back(outcome);
  }
  ApplyZoneBecomes(static_cast<size_t>(index));
}

int BaseEnv::StartReaction(Agent& agent, TagId tag, int rule, ObjectId source,
                           const std::string& cause, TagSource kind,
                           std::vector<Agent*>& affected) {
  const ReactionRule& spec = reactions_[static_cast<size_t>(rule)];
  const ResolvedReaction& r = resolved_reactions_[static_cast<size_t>(rule)];
  // Who: the region of the zone it stands on, when that zone provides a or b
  // (asked of the map, as the step began), else the agent alone
  const TagId here = GetCellTag(agent.GetPosition()).tag;
  const bool spread = spec.spread && here != kInvalidTag && (here == r.a || here == r.b);
  // A trigger defeated at once (between two steps) alone: nobody left to
  // affect, nothing fires (during a step it is still in play)
  if (!spread && !agent.IsAffectable()) return -1;
  affected.clear();
  std::vector<char> region;
  if (spread) {
    region = ZoneRegion(agent.GetPosition());
    for (Agent* a : object_manager_->GetAllAgents()) {  // Agent-index order
      const Position p = a->GetPosition();
      if (a->IsAffectable() && region[static_cast<size_t>(p.row * cols_ + p.col)]) {
        affected.push_back(a);
      }
    }
  } else {
    affected.push_back(&agent);
  }
  const int index = static_cast<int>(last_reactions_.size());
  ReactionReport fired(kind);
  fired.rule = rule;
  fired.trigger = agent.GetId();
  fired.tag = tag;
  fired.source = source;
  fired.cause = cause;
  fired.spread = spread;
  if (spread && !spec.zone_becomes.empty()) {  // The cells it will (re)set, row-major
    for (size_t i = 0; i < region.size(); ++i) {
      if (region[i]) fired.cells.push_back({static_cast<int>(i) / cols_, static_cast<int>(i) % cols_});
    }
  }
  last_reactions_.push_back(std::move(fired));
  return index;
}

void BaseEnv::ApplyZoneBecomes(size_t index) {
  const ReactionReport& fired = last_reactions_[index];
  if (fired.cells.empty()) return;
  // A zone by name: the table's fields; a step timer, so set during a step it
  // is kept as n + 1. During a step the map is read-only: the change waits for
  // the end of the step (CommitPendingZones) and first lands next step.
  // Between two steps (a host landing) there is no phase: it applies at once.
  // Its fields are the zone table's NOW (read by name: DefineZone may change
  // them between steps); its tag was interned by SetReactions
  const size_t rule = static_cast<size_t>(fired.rule);
  const CellTag becomes = ResolveZone(resolved_reactions_[rule].becomes,
                                      GetZoneDef(reactions_[rule].zone_becomes));
  for (const Position& p : fired.cells) {
    const size_t i = static_cast<size_t>(p.row * cols_ + p.col);
    if (in_step_) {
      pending_zones_.push_back({i, becomes});
    } else {
      cell_tags_[i] = becomes;
    }
  }
}

void BaseEnv::ApplyReactionHits() {
  auto rule_of = [this](const ReactionHit& h) -> size_t {
    return static_cast<size_t>(last_reactions_[h.firing].rule);
  };
  // 1. Every firing's removals (the originals not kept)
  for (const ReactionHit& h : reaction_hits_) {
    const ResolvedReaction& r = resolved_reactions_[rule_of(h)];
    if (!r.keep_a) h.agent->RemoveTag(r.a);
    if (!r.keep_b) h.agent->RemoveTag(r.b);
  }
  // 2. Every result (immunity, the tag, its status), in firing order: each
  //    firing's outcome entry, in its affected order. Weaknesses wait for 3.
  hit_agents_.clear();
  for (ReactionHit& h : reaction_hits_) {
    const ResolvedReaction& r = resolved_reactions_[rule_of(h)];
    ReactionReport& fired = last_reactions_[h.firing];
    ReactionOutcome outcome;
    outcome.agent = h.agent->GetId();
    const size_t landing = last_tags_applied_.size();  // The result's report entry
    outcome.result_landed = PutTag(*h.agent, r.result, kPermanentTag, fired.source, fired.cause,
                                   TagSource::Reaction, static_cast<int>(h.firing));
    // Fresh: carried by the agent neither before the removals nor from an
    // earlier result of this phase (PutTag read the latter)
    if (outcome.result_landed && h.carried) last_tags_applied_[landing].fresh = false;
    h.outcome = fired.affected.size();
    fired.affected.push_back(outcome);
    if (std::find(hit_agents_.begin(), hit_agents_.end(), h.agent) == hit_agents_.end()) {
      hit_agents_.push_back(h.agent);
    }
  }
  // 3. One weakness check per agent over the results that landed on it: its
  //    first (P, S) (its own order) with P under it and S among them
  for (Agent* x : hit_agents_) {
    if (!x->IsAffectable() || x->GetWeakTo().empty() || IsDefeatedThisTurn(*x)) continue;
    const TagId here = GetCellTag(x->GetPosition()).tag;
    if (here == kInvalidTag) continue;
    for (const Agent::WeakTo& w : x->GetWeakTo()) {
      if (w.zone != here) continue;
      const ReactionHit* by = nullptr;  // The first result S that landed on x
      for (const ReactionHit& h : reaction_hits_) {
        if (h.agent == x && last_reactions_[h.firing].affected[h.outcome].result_landed &&
            resolved_reactions_[rule_of(h)].result == w.tag) {
          by = &h;
          break;
        }
      }
      if (!by) continue;
      const ReactionReport& fired = last_reactions_[by->firing];
      DefeatBy(*x, w, fired.source, fired.cause, TagSource::Reaction,
               static_cast<int>(by->firing));
      for (const ReactionHit& h : reaction_hits_) {  // Every result S on x defeated it
        ReactionOutcome& o = last_reactions_[h.firing].affected[h.outcome];
        if (h.agent == x && o.result_landed && resolved_reactions_[rule_of(h)].result == w.tag) {
          o.defeated = true;
        }
      }
      break;
    }
  }
  // 4. The damage: each firing's, in firing order, into the turn's ledger
  //    (a defeated agent included: it stays in play until the end of the
  //    turn); each firing reports its raw share. The ledger sums them, so
  //    the final state does not depend on the order.
  for (const ReactionHit& h : reaction_hits_) {
    const int damage = reactions_[rule_of(h)].damage;
    if (damage <= 0 || !h.agent->IsAffectable()) continue;
    last_reactions_[h.firing].affected[h.outcome].damage = damage;
    HurtInStep(*h.agent, damage);
  }
}

bool BaseEnv::SetReactions(std::vector<ReactionRule> rules, std::string* error) {
  try {
    ValidateReactions(rules);
  } catch (const std::runtime_error& e) {
    if (error) *error = e.what();
    return false;
  }
  std::vector<ResolvedReaction> resolved;
  resolved.reserve(rules.size());
  for (const ReactionRule& rule : rules) {
    ResolvedReaction r;
    r.a = tags_.Intern(rule.a);
    r.b = tags_.Intern(rule.b);
    r.result = tags_.Intern(rule.result);
    for (const std::string& k : rule.keep) {
      if (k == rule.a) r.keep_a = true;
      if (k == rule.b) r.keep_b = true;
    }
    if (!rule.zone_becomes.empty()) r.becomes = tags_.Intern(rule.zone_becomes);
    resolved.push_back(r);
  }
  reactions_ = std::move(rules);
  resolved_reactions_ = std::move(resolved);
  return true;
}

bool BaseEnv::SetTagStatuses(std::vector<TagStatusRule> rules, std::string* error) {
  try {
    ValidateTagStatuses(rules);
  } catch (const std::runtime_error& e) {
    if (error) *error = e.what();
    return false;
  }
  std::vector<TagId> ids;
  ids.reserve(rules.size());
  for (const TagStatusRule& rule : rules) ids.push_back(tags_.Intern(rule.tag));
  tag_statuses_ = std::move(rules);
  tag_status_ids_ = std::move(ids);
  return true;
}

bool BaseEnv::SetWeaknesses(ObjectId id, const std::vector<TagWeakness>& weak_to,
                            std::string* error) {
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent) {
    if (error) *error = "no agent " + std::to_string(id);
    return false;
  }
  try {
    ValidateWeaknesses(weak_to);
  } catch (const std::runtime_error& e) {
    if (error) *error = e.what();
    return false;
  }
  std::vector<Agent::WeakTo> ids;
  ids.reserve(weak_to.size());
  for (const TagWeakness& w : weak_to) ids.push_back({tags_.Intern(w.zone), tags_.Intern(w.tag)});
  agent->SetWeakTo(std::move(ids));
  return true;
}

std::vector<TagWeakness> BaseEnv::GetWeaknesses(ObjectId id) const {
  const auto* agent = dynamic_cast<const Agent*>(object_manager_->GetActor(id));
  std::vector<TagWeakness> out;
  if (!agent) return out;
  for (const Agent::WeakTo& w : agent->GetWeakTo()) {
    out.push_back({tags_.Name(w.zone), tags_.Name(w.tag)});
  }
  return out;
}

bool BaseEnv::SetImmunities(ObjectId id, const std::vector<std::string>& immune,
                            std::string* error) {
  auto* agent = dynamic_cast<Agent*>(object_manager_->GetActor(id));
  if (!agent) {
    if (error) *error = "no agent " + std::to_string(id);
    return false;
  }
  try {
    ValidateImmunities(immune);
  } catch (const std::runtime_error& e) {
    if (error) *error = e.what();
    return false;
  }
  std::vector<TagId> ids;
  ids.reserve(immune.size());
  for (const std::string& tag : immune) ids.push_back(tags_.Intern(tag));
  agent->SetImmune(std::move(ids));
  return true;
}

std::vector<std::string> BaseEnv::GetImmunities(ObjectId id) const {
  const auto* agent = dynamic_cast<const Agent*>(object_manager_->GetActor(id));
  std::vector<std::string> out;
  if (!agent) return out;
  for (TagId t : agent->GetImmune()) out.push_back(tags_.Name(t));
  return out;
}

void BaseEnv::MoveActor(Actor& actor, Position to) {
  if (to == actor.GetPosition()) return;
  object_manager_->UpdatePosition(actor.GetId(), to);
  auto* agent = dynamic_cast<Agent*>(&actor);
  if (agent && agent->IsAffectable()) ApplyZoneTag(*agent);
}

bool BaseEnv::DefineZone(const std::string& tag, const ZoneDef& zone) {
  if (tag.empty() || !IsValidNameLength(tag) || !IsValidZoneDef(zone)) return false;
  zone_defs_[tag] = zone;
  return true;
}

ZoneDef BaseEnv::GetZoneDef(const std::string& tag) const {
  auto it = zone_defs_.find(tag);
  return it == zone_defs_.end() ? ZoneDef{} : it->second;
}

BaseEnv::CellTag BaseEnv::ResolveZone(TagId tag, const ZoneDef& def) {
  CellTag c;
  c.tag = tag;
  c.duration = def.duration;
  // A step timer (see CellTag): set during a step, it also covers the rest of it
  c.steps = in_step_ && def.steps > 0 ? def.steps + 1 : def.steps;
  c.then = def.then.empty() ? kInvalidTag : tags_.Intern(def.then);
  c.damage = def.damage;
  return c;
}

bool BaseEnv::SetCellTag(Position cell, const std::string& tag) {
  return SetCellTag(cell, tag, GetZoneDef(tag));
}

bool BaseEnv::SetCellTag(Position cell, const std::string& tag, int duration) {
  ZoneDef zone = GetZoneDef(tag);
  zone.duration = duration;
  return SetCellTag(cell, tag, zone);
}

bool BaseEnv::SetCellTag(Position cell, const std::string& tag, const ZoneDef& zone) {
  if (!grid_->IsInBounds(cell)) return false;
  if (!tag.empty() && (!IsValidNameLength(tag) || !IsValidZoneDef(zone))) return false;
  if (cell_tags_.empty()) {
    if (tag.empty()) return true;  // Nothing to clear
    cell_tags_.assign(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), CellTag{});
  }
  CellTag& c = cell_tags_[static_cast<size_t>(cell.row * cols_ + cell.col)];
  c = tag.empty() ? CellTag{} : ResolveZone(tags_.Intern(tag), zone);
  return true;
}

BaseEnv::CellTag BaseEnv::GetCellTag(Position cell) const {
  if (cell_tags_.empty() || !grid_->IsInBounds(cell)) return {};
  assert(cell_tags_.size() == static_cast<size_t>(rows_) * static_cast<size_t>(cols_));
  return cell_tags_[static_cast<size_t>(cell.row * cols_ + cell.col)];
}

namespace {
const std::string kZoneCause = "zone";  // A zone landing's cause
}  // namespace

void BaseEnv::ApplyZoneTag(Agent& agent) {
  assert(zone_landings_.empty() && "a zone phase inside a zone phase");
  CollectZoneLanding(agent);
  ResolveZoneLandings();
}

void BaseEnv::ApplyZoneTags() {
  if (cell_tags_.empty()) return;
  assert(zone_landings_.empty() && "a zone phase inside a zone phase");
  // a. Every affectable agent on a zone gets its tag (agent-index order: the
  //    order of the reports, never of an outcome)
  for (Agent* agent : object_manager_->GetAllAgents()) CollectZoneLanding(*agent);
  ResolveZoneLandings();
}

void BaseEnv::CollectZoneLanding(Agent& agent) {
  if (!agent.IsAffectable()) return;
  // A copy: the zone that lands (its damage is dealt in sub-phase d)
  const CellTag zone = GetCellTag(agent.GetPosition());
  if (zone.tag == kInvalidTag) return;
  const size_t report = last_tags_applied_.size();  // Its report entry
  if (!PutTag(agent, zone.tag, zone.duration, kInvalidObjectId, kZoneCause, TagSource::Zone, -1)) {
    return;  // Immune: no landing, no damage
  }
  ZoneLanding landing;
  landing.agent = &agent;
  landing.zone = zone;
  landing.report = report;
  zone_landings_.push_back(landing);
}

void BaseEnv::ResolveZoneLandings() {
  // b. The weaknesses (P read on the map as the step began)
  for (const ZoneLanding& l : zone_landings_) {
    if (l.agent->IsAffectable()) {
      ResolveWeakness(*l.agent, l.zone.tag, kInvalidObjectId, kZoneCause, TagSource::Zone, -1);
    }
  }
  // c. The reactions, gathered then applied. c1: every trigger (after a and
  //    b: a defeated agent keeps its tags, so it still triggers a spread).
  for (ZoneLanding& l : zone_landings_) l.rule = FindReaction(*l.agent, l.zone.tag);
  //    c2: every firing, its affected agents and report, nothing applied
  reaction_hits_.clear();
  const size_t first_firing = last_reactions_.size();
  for (const ZoneLanding& l : zone_landings_) {
    if (l.rule < 0) continue;
    const int index = StartReaction(*l.agent, l.zone.tag, l.rule, kInvalidObjectId, kZoneCause,
                                    TagSource::Zone, reaction_affected_);
    if (index < 0) continue;
    const TagId result = resolved_reactions_[static_cast<size_t>(l.rule)].result;
    for (Agent* a : reaction_affected_) {
      ReactionHit hit;
      hit.agent = a;
      hit.firing = static_cast<size_t>(index);
      hit.carried = a->HasTag(result);
      reaction_hits_.push_back(hit);
    }
  }
  //    c3: the outcomes, per agent; then the zones, in firing order (pending)
  if (!reaction_hits_.empty()) ApplyReactionHits();
  for (size_t i = first_firing; i < last_reactions_.size(); ++i) ApplyZoneBecomes(i);
  // d. The zone's own damage, last, into the turn's ledger (a defeated agent
  //    included: it stays in play until the end of the turn)
  for (const ZoneLanding& l : zone_landings_) {
    if (l.zone.damage > 0 && l.agent->IsAffectable()) {
      last_tags_applied_[l.report].damage = l.zone.damage;
      HurtInStep(*l.agent, l.zone.damage);
    }
  }
  // No Agent* outlives the phase (a LoadSnapshot re-creates the agents)
  zone_landings_.clear();
  reaction_hits_.clear();
  reaction_affected_.clear();
  hit_agents_.clear();
}

void BaseEnv::CommitPendingZones() {
  if (pending_zones_.empty()) return;
  if (cell_tags_.empty()) {  // Cleared during the step (a hook): a fresh map
    cell_tags_.assign(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), CellTag{});
  }
  // In the order recorded: a later write to the same cell wins
  for (const PendingZone& z : pending_zones_) cell_tags_[z.cell] = z.zone;
  pending_zones_.clear();
}

void BaseEnv::RepointFsmRng(const pcg32* from, pcg32* to) {
  for (Agent* agent : object_manager_->GetAllAgents()) {
    auto* fsm = dynamic_cast<AgentFSM*>(agent);
    if (fsm && fsm->GetFSMContext().rng == from) fsm->GetFSMContext().rng = to;
  }
}

void BaseEnv::AbortStep() {
  // What the step did before the throw stays, its pending zones included,
  // without allocating (this runs while a throw unwinds, ~StepScope): a map
  // cleared during the step has no cells to write them to, so they are dropped
  if (cell_tags_.empty()) {
    pending_zones_.clear();
  } else {
    CommitPendingZones();
  }
  // A phase the throw interrupted: its scratch points at agents that may go
  zone_landings_.clear();
  reaction_hits_.clear();
  reaction_affected_.clear();
  hit_agents_.clear();
  // The turn's ledger: what the step did to health, downs, deaths and
  // revives, agent-local (no report, no allocation)
  ApplyTurnLedger(false);
  turn_.Clear();
  in_step_ = false;
  for (Agent* agent : object_manager_->GetAllAgents()) agent->AbortStep();
}

void BaseEnv::TickZones() {
  // The step is over: a successor created below lasts its n next steps
  in_step_ = false;
  for (CellTag& z : cell_tags_) {
    if (z.tag == kInvalidTag || z.steps == kPermanentTag) continue;
    if (--z.steps > 0) continue;
    // Expired: its successor, a zone by name (the table's fields), or nothing
    // (each cell is visited once, so a cycle advances one zone per tick)
    z = z.then == kInvalidTag ? CellTag{} : ResolveZone(z.then, GetZoneDef(tags_.Name(z.then)));
  }
}

// =============================================================================
// The turn's health
// =============================================================================

void BaseEnv::BeginTurn() {
  turn_.Clear();
  for (Agent* agent : object_manager_->GetAllAgents()) {
    LedgerEntry e;
    e.agent = agent;
    e.marked = agent->IsMarked();
    turn_.ledger.push_back(e);
  }
}

BaseEnv::LedgerEntry& BaseEnv::LedgerOf(Agent& agent) {
  std::vector<LedgerEntry>& ledger = turn_.ledger;
  const int index = agent.GetAgentIndex();  // Its entry when the indices are dense
  if (index >= 0 && static_cast<size_t>(index) < ledger.size() &&
      ledger[static_cast<size_t>(index)].agent == &agent) {
    return ledger[static_cast<size_t>(index)];
  }
  for (LedgerEntry& e : ledger) {
    if (e.agent == &agent) return e;
  }
  LedgerEntry e;  // An agent the turn began without: not Marked as it began
  e.agent = &agent;
  ledger.push_back(e);
  return ledger.back();
}

const BaseEnv::LedgerEntry* BaseEnv::FindLedger(const Agent& agent) const {
  for (const LedgerEntry& e : turn_.ledger) {
    if (e.agent == &agent) return &e;
  }
  return nullptr;
}

bool BaseEnv::IsDefeatedThisTurn(const Agent& agent) const {
  if (!in_step_) return false;
  const LedgerEntry* e = FindLedger(agent);
  return e && e->defeated;
}

bool BaseEnv::HurtInStep(Agent& agent, int amount) {
  if (amount <= 0) return false;
  if (!in_step_) {
    agent.TakeDamage(amount);
    return false;
  }
  if (!agent.IsAffectable()) return false;  // As TakeDamage
  LedgerEntry& e = LedgerOf(agent);
  e.damage += amount;
  e.touched = true;
  return true;
}

bool BaseEnv::HealInStep(Agent& agent, int amount) {
  if (amount <= 0) return false;
  if (!in_step_) {
    agent.Heal(amount);
    return false;
  }
  if (!agent.IsAffectable()) return false;  // As Heal (reviving is not healing)
  LedgerEntry& e = LedgerOf(agent);
  e.heal += amount;
  e.touched = true;
  return true;
}

bool BaseEnv::PlanRevive(Companion& ally, int health, const Companion& caster) {
  LedgerEntry& e = LedgerOf(ally);
  assert(!e.defeated && "a defeat and a revive on one agent in one turn");
  // As Companion::Revive will clamp it
  health = std::max(1, std::min(health, ally.GetMaxHealth()));
  e.touched = true;
  // Casters resolve in agent-index order: an earlier one giving as much keeps it
  if (e.reviver != kInvalidObjectId && health <= e.revive_health) return false;
  if (e.reviver != kInvalidObjectId) {
    // The earlier use keeps the ally in its affected, without Revive
    for (SkillUse& use : last_skill_uses_) {
      if (use.caster != e.reviver) continue;
      for (AffectedAgent& a : use.affected) {
        if (a.id == ally.GetId()) a.effects &= ~kSkillEffectRevive;
      }
    }
  }
  e.revive_health = health;
  e.reviver = caster.GetId();
  return true;
}

void BaseEnv::ApplyTurnLedger(bool report) {
  for (LedgerEntry& e : turn_.ledger) {
    if (!e.touched || !e.agent) continue;
    Agent& agent = *e.agent;
    assert(!(e.defeated && e.reviver != kInvalidObjectId) &&
           "a defeat and a revive on one agent in one turn");
    TurnHealth t;
    t.agent = agent.GetId();
    t.damage = e.damage;
    t.heal = e.heal;
    const int before = agent.GetHealth();
    ObjectId reviver = kInvalidObjectId;
    if (e.reviver != kInvalidObjectId) {
      auto* comp = dynamic_cast<Companion*>(&agent);
      if (comp && comp->Revive(e.revive_health)) {
        t.outcome = TurnOutcome::Revived;
        reviver = e.reviver;
      }
    } else if (e.defeated) {
      agent.Defeat();  // 0 whatever the heals
      t.outcome = TurnOutcome::Defeated;
    } else {
      // Marked on the total, rounded down once, only if Marked as the turn
      // began; then the heals
      if (e.marked) {
        t.marked_bonus = static_cast<int>(e.damage * Agent::kMarkedDamageMultiplier) - e.damage;
      }
      const bool was_affectable = agent.IsAffectable();
      agent.ApplyTurnHealth(before - e.damage - t.marked_bonus + e.heal, e.damage > 0);
      if (was_affectable && !agent.IsAffectable()) {
        t.outcome = dynamic_cast<Companion*>(&agent) ? TurnOutcome::Downed : TurnOutcome::Died;
      }
    }
    t.health = agent.GetHealth();
    t.change = t.health - before;
    // Emptied before the reports allocate: a throw there (AbortStep) never
    // applies it twice
    Agent* const kept = e.agent;
    const bool marked = e.marked;
    e = LedgerEntry{};
    e.agent = kept;
    e.marked = marked;
    if (!report) continue;
    if (reviver != kInvalidObjectId) last_revives_.push_back({reviver, t.agent, t.health});
    last_turn_health_.push_back(t);
  }
}

void BaseEnv::ApplyTurnOutcomes() {
  ApplyTurnLedger(true);
  turn_.Clear();
  // The turn's dead: their strikes still winding up never land
  effect_system_->CancelDeadSources();
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
  const std::vector<Agent*> agents = object_manager_->GetAllAgents();
  for (size_t i = 0; i < agents.size(); ++i) {
    Agent* agent = agents[i];
    if (!agent->IsAffectable()) continue;
    auto* comp = dynamic_cast<Companion*>(agent);
    if (!comp) continue;
    int slot = SkillSlotOf(agent->GetExecutedAction().interact);
    if (slot < 0 || slot >= kEnabledSkillSlots) continue;
    // The skill GatherIntentions fixed (whoever an earlier caster revived or
    // downed since, the context read then holds): at the agent's index, else
    // (the agents changed mid-step, which they must not) by the caster's id
    auto matches = [&](const IntendedSkill& r) {
      return r.caster == comp->GetId() && r.slot == slot &&
             r.rule < static_cast<int>(context_skills_.size());
    };
    const IntendedSkill* intended =
        i < intended_skills_.size() && matches(intended_skills_[i]) ? &intended_skills_[i] : nullptr;
    if (!intended) {
      for (const IntendedSkill& r : intended_skills_) {
        if (matches(r)) {
          intended = &r;
          break;
        }
      }
    }
    assert(intended && "a skill use GatherIntentions did not record");
    if (!intended) continue;
    ResolveSkillUse(*comp, slot, intended->rule);
  }
}

void BaseEnv::ResolveSkillUse(Companion& comp, int slot, int rule) {
  const std::string& name = rule >= 0 ? context_skills_[static_cast<size_t>(rule)].skill
                                      : comp.GetSkill(slot);
  const SkillConfig* found = skills_.Find(name);
  if (!found) return;
  // Deliberate copy: UseSkill must not observe a Define (it invalidates `found`).
  const SkillConfig skill = *found;
  SkillTargets targets = UseSkill(comp, skill);
  // Cooldowns belong to the equipped skill: a context skill does not spend it
  if (rule < 0) comp.SetCooldown(slot, skill.cooldown);
  last_skill_uses_.push_back(
      {comp.GetId(), skill.name, targets.centre, slot, std::move(targets.affected)});
}

BaseEnv::SkillOutcome BaseEnv::PreviewSkillOutcome(ObjectId caster, int slot,
                                                   Direction aim) const {
  SkillOutcome outcome;
  if (!dynamic_cast<const Companion*>(object_manager_->GetActor(caster))) return outcome;
  std::unique_ptr<BaseEnv> world = Clone();
  // The use's reports only: not the last step's (nor the host's since), not
  // the downs between steps the next step reports
  world->ClearStepReports();
  for (Companion* c : world->object_manager_->GetAllCompanions()) c->TakeUnreportedDowns();
  auto* comp = dynamic_cast<Companion*>(world->object_manager_->GetActor(caster));
  assert(comp && "a clone keeps the ids");
  // As the step, around its skills: timers set from here on are step timers
  for (Agent* agent : world->object_manager_->GetAllAgents()) agent->BeginStep();
  world->in_step_ = true;
  world->BeginTurn();
  // As GatherIntentions decides: the stunned (and the downed) stay, then a
  // use CanUseSkill allows, with the context read now
  const ContextSkillRule* rule = nullptr;
  outcome.usable = !comp->IsStunned() && world->CanUseSkill(*comp, slot, rule);
  if (outcome.usable) {
    comp->SetDirection(aim);  // GatherIntentions: the movement aims
    world->ResolveSkillUse(
        *comp, slot, rule ? static_cast<int>(rule - world->context_skills_.data()) : -1);
  }
  // The zones the use sets at the end of the step (its reactions' cells),
  // committed as the step's end would, then the turn's end (health, downs,
  // deaths, revives), the timers not ticked
  world->CommitPendingZones();
  world->ApplyTurnOutcomes();
  for (Companion* c : world->object_manager_->GetAllCompanions()) {
    for (int n = c->TakeUnreportedDowns(); n > 0; --n) world->last_downs_.push_back(c->GetId());
  }
  outcome.world = std::move(world);
  return outcome;
}

BaseEnv::SkillPreview BaseEnv::PreviewSkill(const Companion& caster, int slot,
                                            Direction aim) const {
  SkillPreview preview;
  preview.centre = preview.caster_landing = caster.GetPosition();
  preview.skill = EffectiveSkill(caster, slot);  // "" out of range
  // As the step decides: GatherIntentions makes the stunned stay, then keeps
  // a use CanUseSkill allows
  preview.usable = !caster.IsStunned() && CanUseSkill(caster, slot);
  const SkillConfig* skill = preview.skill.empty() ? nullptr : skills_.Find(preview.skill);
  if (!skill) return preview;
  SkillTargets targets = ResolveSkillTargets(caster, *skill, aim);
  preview.centre = targets.centre;
  preview.caster_landing = targets.landing;
  preview.affected = std::move(targets.affected);
  return preview;
}

const Actor* BaseEnv::ActorAfterMotion(Position p, const Agent& caster, Position landing) const {
  const Position from = caster.GetPosition();
  if (landing != from) {
    // Landing needs a cell free of living actors (CanLand), and a corpse
    // never takes a cell from the living: once there, the caster is what
    // GetActorAt gives. The cell it left then holds nobody (a corpse under
    // it was taken off the grid when it walked over it).
    if (p == landing) return &caster;
    if (p == from) return nullptr;
  }
  return object_manager_->GetActorAt(p);
}

const Agent* BaseEnv::AgentAfterMotion(Position p, const Agent& caster, Position landing) const {
  return dynamic_cast<const Agent*>(ActorAfterMotion(p, caster, landing));
}

const Actor* BaseEnv::MotionThingAt(Position p, const SkillConfig& skill, const Agent& caster,
                                    Position landing) const {
  const Actor* a = ActorAfterMotion(p, caster, landing);
  if (!a || !a->IsAlive()) return nullptr;
  if (a == &caster && !skill.self_motion) return nullptr;
  if (auto* ag = dynamic_cast<const Agent*>(a); ag && !Affects(skill, caster, *ag)) return nullptr;
  return a;
}

Position BaseEnv::PushLanding(const SkillConfig& skill, Position p, Position centre,
                              ObjectId mover, const Agent& caster, Position landing) const {
  // CanLand, the caster on its landing cell
  auto can_land = [&](Position q) {
    if (!grid_->IsInBounds(q) || !grid_->IsWalkable(q)) return false;
    const Actor* a = ActorAfterMotion(q, caster, landing);
    return !(a && a->IsAlive() && a->GetId() != mover);
  };
  return ResolveDashWith(*grid_, p, p.row - centre.row, p.col - centre.col,
                         skill.motion_distance, can_land);
}

std::optional<Position> BaseEnv::PullFrom(const SkillConfig& skill, Position centre,
                                          const Agent& caster, Position landing) const {
  // CanLand with nobody excepted, the caster on its landing cell
  if (!grid_->IsInBounds(centre) || !grid_->IsWalkable(centre)) return std::nullopt;
  const Actor* on_centre = ActorAfterMotion(centre, caster, landing);
  if (on_centre && on_centre->IsAlive()) return std::nullopt;
  const std::vector<Position> cells = AreaCells(centre, skill.area);
  for (size_t i = 1; i < cells.size(); ++i) {  // The ring, by priority
    if (MotionThingAt(cells[i], skill, caster, landing)) return cells[i];
  }
  return std::nullopt;
}

BaseEnv::SkillTargets BaseEnv::ResolveSkillTargets(const Companion& caster,
                                                   const SkillConfig& skill,
                                                   Direction aim) const {
  int dr = 0, dc = 0;
  DirectionDelta(aim, dr, dc);
  const Position from = caster.GetPosition();
  std::vector<Position> path;  // Cells crossed by a dash (tag_path)
  SkillTargets t;

  // 1. The caster's own motion, and the skill's centre.
  switch (skill.motion) {
    case SkillMotion::Dash:
      t.landing = ResolveDash(*grid_, *object_manager_, from, dr, dc, skill.motion_distance,
                              caster.GetId(), &path);
      break;
    case SkillMotion::Teleport:
      t.landing = ResolveTeleport(*grid_, *object_manager_, from, dr, dc,
                                  skill.motion_distance, caster.GetId());
      break;
    default:
      t.landing = from;
      break;
  }
  t.centre = from;
  switch (skill.targeting) {
    case SkillTargeting::Self:
      t.centre = t.landing;
      break;
    case SkillTargeting::Ground:
      t.centre = ResolveGroundTarget(*grid_, from, dr, dc, skill.range);
      break;
    case SkillTargeting::Projectile: {
      // Line rule, but the first agent the skill affects stops it (a downed
      // one is passed over, and a standing one by an affects_downed skill).
      Position cur = from;
      for (int i = 0; i < skill.range; ++i) {
        Position next{cur.row + dr, cur.col + dc};
        if (!grid_->IsInBounds(next) || !grid_->IsPathable(next)) break;
        cur = next;
        const Agent* hit = AgentAfterMotion(cur, caster, t.landing);
        if (hit && hit != &caster && Affects(skill, caster, *hit)) break;
      }
      t.centre = cur;
      break;
    }
  }

  // 2. Affected agents: on the area, then (tag_path) on the dash path.
  std::vector<Position> found_on;  // Parallel to t.affected
  CollectAffected(AreaCells(t.centre, skill.area), skill, caster, t.landing, t.affected,
                  found_on);
  const size_t on_area = t.affected.size();
  if (skill.tag_path) CollectAffected(path, skill, caster, t.landing, t.affected, found_on);

  // 3. What the use does to each (the caster only as its self_* flags allow).
  std::optional<Position> pulled_from;
  if (skill.motion == SkillMotion::PullIn) pulled_from = PullFrom(skill, t.centre, caster, t.landing);
  for (size_t i = 0; i < t.affected.size(); ++i) {
    const Agent* a = AgentAfterMotion(found_on[i], caster, t.landing);
    const bool self = a == &caster;
    const bool area = i < on_area;
    unsigned e = 0;
    // Tags: at least one of them lands (an agent immune to all gets none)
    const bool lands_a_tag =
        std::any_of(skill.tags.begin(), skill.tags.end(), [&](const SkillTagSpec& tag) {
          if (a->GetImmune().empty()) return true;
          const TagId id = tags_.Find(tag.tag);  // Never interned: nobody is immune to it
          return id == kInvalidTag || !a->IsImmuneTo(id);
        });
    if (lands_a_tag && (!self || skill.self_tags)) e |= kSkillEffectTags;
    if (skill.damage > 0 && (!self || skill.self_damage)) e |= kSkillEffectDamage;
    // Only a downed companion gets up (an affects_downed skill reaches the downed only)
    if (skill.revive_percent > 0 && a->IsDowned() && dynamic_cast<const Companion*>(a)) {
      e |= kSkillEffectRevive;
    }
    if (area && skill.root_steps > 0 && (!self || skill.self_root)) e |= kSkillEffectRoot;
    if (area && found_on[i] != t.centre) {  // The ring
      // Pushed only if the push would move it (a wall right behind it stops
      // it), read as AreaMotion reads it: the caster on its landing cell
      const Position p = found_on[i];
      const bool pushed = skill.motion == SkillMotion::PushOut &&
                          MotionThingAt(p, skill, caster, t.landing) == a &&
                          PushLanding(skill, p, t.centre, a->GetId(), caster, t.landing) != p;
      const bool pulled = pulled_from && *pulled_from == found_on[i];
      if (pushed || pulled) e |= kSkillEffectMotion;
    }
    t.affected[i].effects = e;
  }
  return t;
}

BaseEnv::SkillTargets BaseEnv::UseSkill(Companion& caster, const SkillConfig& skill) {
  // 1-2. The caster's landing, the centre and the affected agents, as
  // PreviewSkill sees them (the one implementation of targeting), then the
  // caster's own motion (it lands its cell's zone tag, which no targeting
  // reads).
  SkillTargets targets = ResolveSkillTargets(caster, skill, caster.GetDirection());
  MoveActor(caster, targets.landing);
  const Position centre = targets.centre;
  // The agents, parallel to targets.affected, whose effects become what the
  // use DID (the decision above is what it does unless its own damage downs
  // or kills an agent first, or a motion moves nothing).
  std::vector<Agent*> agents;
  agents.reserve(targets.affected.size());
  for (const AffectedAgent& t : targets.affected) {
    agents.push_back(dynamic_cast<Agent*>(object_manager_->GetActor(t.id)));
    assert(agents.back() && "ResolveSkillTargets gives agents");
  }
  auto has = [&](size_t i, unsigned e) { return agents[i] && (targets.affected[i].effects & e); };
  auto drop = [&](size_t i, unsigned e) { targets.affected[i].effects &= ~e; };

  // 3. Tags land on who was there at impact (area and path). Nobody becomes
  // unaffectable during a step (downs and deaths wait for the end of the
  // turn: a caster its landing zone takes to 0 still gets its own use); an
  // agent that is not (a host call in a hook) gets nothing, and the use
  // reports nothing on it.
  for (size_t i = 0; i < agents.size(); ++i) {
    if (!has(i, kSkillEffectTags)) continue;
    if (!agents[i]->IsAffectable()) {
      drop(i, kSkillEffectTags);
      continue;
    }
    bool landed = false;  // An agent immune to all of them gets none
    for (const SkillTagSpec& t : skill.tags) {
      landed |= LandTag(*agents[i], t.tag, t.duration, caster.GetId(), skill.name,
                        TagSource::Skill);
    }
    if (!landed) drop(i, kSkillEffectTags);
  }

  // 4. Damage, on the same agents (after the tags), into the turn's ledger:
  // an agent it takes to 0 goes down or dies at the end of the turn, so it is
  // still rooted and moved below.
  for (size_t i = 0; i < agents.size(); ++i) {
    if (!has(i, kSkillEffectDamage)) continue;
    if (agents[i]->IsAffectable()) {
      HurtInStep(*agents[i], skill.damage);
    } else {
      drop(i, kSkillEffectDamage);
    }
  }

  // 5. Revive: the downed it affects get up where they lie at the end of the
  // turn (an affects_downed skill has no tags, damage, root or motion). Two
  // revivers on one ally: the Revive effect stays on the use credited
  // (PlanRevive).
  for (size_t i = 0; i < agents.size(); ++i) {
    if (!has(i, kSkillEffectRevive)) continue;
    auto* comp = dynamic_cast<Companion*>(agents[i]);
    const int health = comp ? (comp->GetMaxHealth() * skill.revive_percent + 99) / 100 : 0;
    bool revived = false;
    if (comp && comp->IsAlive() && comp->IsDowned()) {
      if (in_step_) {
        revived = PlanRevive(*comp, health, caster);
      } else if (comp->Revive(health)) {
        revived = true;
        last_revives_.push_back({caster.GetId(), comp->GetId(), comp->GetHealth()});
      }
    }
    if (!revived) drop(i, kSkillEffectRevive);
  }

  // 6. Root, before anything moves (Rooted for the next root_steps steps, a
  // step timer, see Agent::BeginStep). Then the area motion; Motion is what
  // it really moved.
  for (size_t i = 0; i < agents.size(); ++i) {
    if (!has(i, kSkillEffectRoot)) continue;
    if (agents[i]->IsAffectable()) {
      agents[i]->ApplyStatus(StatusType::Rooted, skill.root_steps);
    } else {
      drop(i, kSkillEffectRoot);
    }
  }
  const std::vector<ObjectId> moved = AreaMotion(skill, centre, caster);
  for (AffectedAgent& t : targets.affected) {
    t.effects &= ~kSkillEffectMotion;
    if (std::find(moved.begin(), moved.end(), t.id) != moved.end()) t.effects |= kSkillEffectMotion;
  }
  return targets;
}

bool BaseEnv::Affects(const SkillConfig& skill, const Agent& caster, const Agent& agent) const {
  const bool reachable =
      skill.affects_downed ? agent.IsAlive() && agent.IsDowned() : agent.IsAffectable();
  if (!reachable || !PassesFilter(agent, skill.filter)) return false;
  // The caster is of its own faction: without friendly fire it is spared too.
  return skill.friendly_fire || agent.GetFaction() != caster.GetFaction();
}

void BaseEnv::CollectAffected(const std::vector<Position>& cells, const SkillConfig& skill,
                              const Agent& caster, Position landing,
                              std::vector<AffectedAgent>& affected,
                              std::vector<Position>& found_on) const {
  for (const Position& p : cells) {
    const Agent* a = AgentAfterMotion(p, caster, landing);
    if (!a || !Affects(skill, caster, *a)) continue;
    const ObjectId id = a->GetId();
    if (std::find_if(affected.begin(), affected.end(),
                     [id](const AffectedAgent& x) { return x.id == id; }) == affected.end()) {
      affected.push_back({id, 0});
      found_on.push_back(p);
    }
  }
}

std::vector<ObjectId> BaseEnv::AreaMotion(const SkillConfig& skill, Position centre,
                                          const Agent& caster) {
  std::vector<ObjectId> moved;
  // The caster already stands on its landing cell: the world as it is now.
  const Position here = caster.GetPosition();
  auto move = [&](const Actor* thing, Position to) {
    if (to == thing->GetPosition()) return;  // Stopped right away: moved nothing
    moved.push_back(thing->GetId());
    MoveActor(*object_manager_->GetActor(thing->GetId()), to);
  };
  if (skill.motion == SkillMotion::PushOut) {
    // Ring cells push in 4 different directions: no two pushes compete.
    const std::vector<Position> cells = AreaCells(centre, skill.area);
    for (size_t i = 1; i < cells.size(); ++i) {
      const Position p = cells[i];
      const Actor* thing = MotionThingAt(p, skill, caster, here);
      if (!thing) continue;
      move(thing, PushLanding(skill, p, centre, thing->GetId(), caster, here));
    }
  } else if (skill.motion == SkillMotion::PullIn) {
    if (std::optional<Position> from = PullFrom(skill, centre, caster, here)) {
      move(MotionThingAt(*from, skill, caster, here), centre);
    }
  }
  return moved;
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
  // Between two steps: timers set during one are kept as n + 1, so a state
  // saved inside a step would load with them one step too long (a Step that
  // threw is not inside one: it aborts, see AbortStep)
  if (in_step_) throw std::logic_error("SaveSnapshot during a step");
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
    // Its weaknesses and immunities (any agent), by name
    for (const Agent::WeakTo& w : agent->GetWeakTo()) {
      as.weak_to.push_back({tags_.Name(w.zone), tags_.Name(w.tag)});
    }
    for (TagId t : agent->GetImmune()) as.immune.push_back(tags_.Name(t));

    // Direction, skill slots, cooldowns and downs for Companions
    if (const Companion* comp = dynamic_cast<const Companion*>(agent)) {
      as.direction = static_cast<int>(comp->GetDirection());
      as.color = static_cast<int>(comp->GetColor());
      as.downed = comp->IsDowned();
      as.times_downed = comp->GetTimesDowned();
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
  snap.max_downs = max_downs_;
  // Always explicit. Only the rules usable with this book (the others are
  // inert, skipped at run time): the snapshot's own loader would reject them.
  snap.context_skills.emplace();
  for (const ContextSkillRule& rule : context_skills_) {
    if (IsUsableWith(rule, skills_)) snap.context_skills->push_back(rule);
  }

  // Semantic annotations
  snap.annotations = annotations_.Serialize();

  // Every skill of the book, builtins included (a level may retune them), but
  // the fixed default one (a snapshot carrying it is rejected)
  for (const SkillConfig& skill : skills_.All()) {
    if (skill.name != kDefaultSkill) snap.skills.push_back(skill);
  }

  // Zones, by name, in current coordinates (as the cells and annotations
  // above), each with every field as the cell holds it (its own resolved
  // copy: an override, or the table as it was when the zone was created),
  // its remaining steps included (read between two steps: the steps to come)
  for (int r = 0; r < rows_ && !cell_tags_.empty(); ++r) {
    for (int c = 0; c < cols_; ++c) {
      const CellTag& z = cell_tags_[static_cast<size_t>(r * cols_ + c)];
      if (z.tag == kInvalidTag) continue;
      CellTagSnapshot zone;
      zone.cell = Position{r, c};
      zone.tag = tags_.Name(z.tag);
      zone.duration = z.duration;
      zone.steps = z.steps;
      zone.then = z.then == kInvalidTag ? std::string() : tags_.Name(z.then);
      zone.damage = z.damage;
      snap.cell_tags.push_back(std::move(zone));
    }
  }

  // The level's combo rules: the zone table, the reactions, the tag statuses
  snap.zones = zone_defs_;
  snap.reactions = reactions_;
  snap.tag_statuses = tag_statuses_;

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
  in_step_ = false;    // Between two steps, even after a Step that threw
  pending_zones_.clear();  // The old world's: the snapshot's zones replace them
  turn_.Clear();           // It points at the old world's agents

  // The level's combo rules (validated above; none in a snapshot before v7).
  // The zone table first: the zone cells below resolve against it.
  ClearZoneDefs();
  for (const auto& [tag, zone] : snapshot.zones) {
    const bool defined = DefineZone(tag, zone);
    assert(defined && "validated zone definition rejected");
    (void)defined;
  }
  const bool rules_set = SetReactions(snapshot.reactions) && SetTagStatuses(snapshot.tag_statuses);
  assert(rules_set && "validated reactions or tag statuses rejected");
  (void)rules_set;

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

    // Restore basic state. The health as saved, without the consequences of
    // damage: TakeDamage would down a companion saved at 0 HP (a phantom
    // down) or kill it. Deaths and downs come from their own flags.
    agent->SetMaxHealth(as.max_health);
    agent->RestoreHealth(as.health);
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
    // Its weaknesses and immunities (validated above)
    const bool agent_rules_set = SetWeaknesses(agent->GetId(), as.weak_to) &&
                                 SetImmunities(agent->GetId(), as.immune);
    assert(agent_rules_set && "validated weaknesses or immunities rejected");
    (void)agent_rules_set;

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
      // Downs last: a downed agent accepts no new tags (it keeps those it
      // had), so the tags restored above must land before it goes down. A
      // downed companion has no statuses (validated). Loaded as already
      // reported (the step that downed it did).
      comp->RestoreDowns(as.downed, as.times_downed);
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

  // The level's max downs (validated >= 1 above), before the latch below
  // reads IsDone()
  const bool max_downs_set = SetMaxDowns(snapshot.max_downs);
  assert(max_downs_set && "validated max_downs rejected");
  (void)max_downs_set;
  // The level's context skills (absent: the default ones), validated above
  // against the book just built
  context_skills_ = snapshot.context_skills ? *snapshot.context_skills : DefaultContextSkills();

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
  // annotations: ApplyD4Transform below moves all three together. Each with
  // its own fields, the table's for those it lacks (CellZone: v7 files saved
  // by SaveSnapshot have them all; older ones the duration only, and no
  // table: the defaults). Set between two steps (in_step_ is false), so its
  // steps are stored as given: the steps to come.
  for (const CellTagSnapshot& z : snapshot.cell_tags) {
    const bool set = SetCellTag(z.cell, z.tag, snapshot.CellZone(z));  // Validated above
    assert(set && "validated zone rejected");
    (void)set;
  }

  // Apply D4 symmetry transformation if specified
  // (Snapshot contains pre-transform positions, so we apply transform after loading)
  if (d4_transform_ != 0) {
    ApplyD4Transform();
  }

  // A state loaded already done (at or past the horizon, or the team down)
  // ended there: a later down keeps that reason, as after a Step.
  // The latch reads IsDone() on the state as loaded so far: a derived
  // LoadSnapshot override, or a Reset that loads a generated level, must
  // finish its own state first, then latch again (RelatchEndReasonAfterLoad:
  // AggroEnv's Reset spawns its enemy and companions after this).
  LatchEndReason();
}

void BaseEnv::LoadGeneratedLevel(Snapshot snapshot) {
  // Level data kept across Reset: a generated level has the default.
  snapshot.max_downs = max_downs_;
  // The context skills too, but not through the snapshot: checked against the
  // generated book (the builtins), a rule naming an earlier level's skill
  // would fail the Reset. Kept as they are, such a rule is skipped
  // (ActiveContextRule). No rule to check meanwhile.
  // Restored whatever happens (a derived LoadSnapshot may throw after the
  // base load set the snapshot's).
  std::vector<ContextSkillRule> rules = context_skills_;
  snapshot.context_skills = std::vector<ContextSkillRule>{};
  // The zone table, the reactions and the tag statuses as well (level data;
  // a generated level has none, and LoadSnapshot would set its none). Their
  // resolved tag ids stay valid: the TagTable is kept. Per-agent data
  // (weaknesses, immunities) belongs to the agents: the generated ones have
  // none.
  std::map<std::string, ZoneDef> zone_defs = zone_defs_;
  std::vector<ReactionRule> reactions = reactions_;
  std::vector<ResolvedReaction> resolved_reactions = resolved_reactions_;
  std::vector<TagStatusRule> tag_statuses = tag_statuses_;
  std::vector<TagId> tag_status_ids = tag_status_ids_;
  auto restore = [&]() {
    context_skills_ = std::move(rules);
    zone_defs_ = std::move(zone_defs);
    reactions_ = std::move(reactions);
    resolved_reactions_ = std::move(resolved_reactions);
    tag_statuses_ = std::move(tag_statuses);
    tag_status_ids_ = std::move(tag_status_ids);
  };
  try {
    LoadSnapshot(snapshot);
  } catch (...) {
    restore();
    throw;
  }
  restore();
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
  // A lens change starts a new episode: done again at once only at or past
  // the horizon or with the team down (a success is latched by a Step)
  LatchEndReason();
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
  LatchEndReason();  // As SetTaskLens
  return true;
}

}  // namespace companions
