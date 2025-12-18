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
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"
#include "../core/game_logger.h"
#include "effect_system.h"

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
      d4_transform_(other.d4_transform_) {
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

  // Resolve interactions (attacks, effects)
  ResolveInteractions();

  // Tick active effects (advance timers, apply damage/push)
  effect_system_->Tick();

  // Tick agent status effects (decrement durations, remove expired)
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (agent->IsAlive()) {
      agent->TickStatuses();
    }
  }

  // Increment tick
  tick_++;

  // Post-step hook
  PostStep();

  // Calculate rewards
  result.rewards.resize(num_agents, 0.0);
  CalculateRewards(result.rewards);

  // Check termination
  result.done = IsDone();

  return result;
}

void BaseEnv::PreStep() {
  // FSM updates are now done after movements in Step()
}

void BaseEnv::UpdateAgentFSM() {
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      if (fsm_agent->HasFSM() && fsm_agent->IsAlive()) {
        fsm_agent->UpdateFSM(*this);
      }
    }
  }
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
      CellKind raw_kind = grid_->GetCellKind(pos);
      CellKind kind = GetMaskedCellKind(raw_kind);  // Apply env-specific masking

      // Plane 0: Floor (all walkable cells, including synchro)
      if (grid_->IsWalkable(pos)) {
        set_plane(0, r, c, 1.0f);
      }

      // Plane 1: Walls
      if (kind == CellKind::Wall) {
        set_plane(1, r, c, 1.0f);
      }

      // Plane 2: Goal cells (Synchro or Target depending on env)
      if (kind == CellKind::Synchro || kind == CellKind::Target) {
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
  values.clear();
  values.resize(VectorObservationSize(), 0.0f);

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
  values[idx++] = static_cast<float>(my_pos.row) / max_dim;
  values[idx++] = static_cast<float>(my_pos.col) / max_dim;

  // Feature 2: Health ratio
  int max_hp = current_agent->GetMaxHealth();
  values[idx++] = max_hp > 0 ? static_cast<float>(current_agent->GetHealth()) / max_hp : 1.0f;

  // Feature 3: Distance to nearest goal cell (Synchro cell)
  std::vector<Position> goals = grid_->FindCellsOfKind(CellKind::Synchro);
  float min_dist = max_dim * 2.0f;  // Max possible Manhattan distance
  for (const auto& goal : goals) {
    float dist = static_cast<float>(std::abs(my_pos.row - goal.row) +
                                    std::abs(my_pos.col - goal.col));
    min_dist = std::min(min_dist, dist);
  }
  values[idx++] = min_dist / (max_dim * 2.0f);  // Normalize

  // Features 4-7: Relative positions to other companions (dx, dy per other)
  // Max 2 others (for 3 companions total)
  constexpr int kMaxOthers = 2;
  int other_count = 0;
  for (size_t i = 0; i < agents.size() && other_count < kMaxOthers; ++i) {
    if (static_cast<int>(i) == player) continue;
    const Agent* other = agents[i];
    Position other_pos = other->GetPosition();

    // Relative position normalized to [-1, 1]
    values[idx++] = static_cast<float>(other_pos.row - my_pos.row) / max_dim;
    values[idx++] = static_cast<float>(other_pos.col - my_pos.col) / max_dim;
    other_count++;
  }

  // Pad remaining slots with zeros if fewer than max others
  while (other_count < kMaxOthers) {
    values[idx++] = 0.0f;
    values[idx++] = 0.0f;
    other_count++;
  }

  // Feature 8: Steps remaining (absolute / 100)
  // Use absolute steps (not ratio) so policy can learn reward prediction
  int steps_left = horizon_ - tick_;
  values[idx++] = static_cast<float>(steps_left) / 100.0f;
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
      CellKind raw_kind = grid_->GetCellKind(pos);
      CellKind kind = GetMaskedCellKind(raw_kind);  // Apply env-specific masking

      // Plane 0: Floor (all walkable cells, including synchro)
      if (grid_->IsWalkable(pos)) {
        set_plane(0, r, c, 1.0f);
      }

      // Plane 1: Walls
      if (kind == CellKind::Wall) {
        set_plane(1, r, c, 1.0f);
      }

      // Plane 2: Goal cells (Synchro or Target depending on env)
      if (kind == CellKind::Synchro || kind == CellKind::Target) {
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

  // Feature 3: Distance to nearest goal cell (Synchro cell)
  std::vector<Position> goals = grid_->FindCellsOfKind(CellKind::Synchro);
  float min_dist = max_dim * 2.0f;  // Max possible Manhattan distance
  for (const auto& goal : goals) {
    float dist = static_cast<float>(std::abs(my_pos.row - goal.row) +
                                    std::abs(my_pos.col - goal.col));
    min_dist = std::min(min_dist, dist);
  }
  buffer[idx++] = min_dist / (max_dim * 2.0f);  // Normalize

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
  if (!agent->IsAlive()) {
    // Dead agents can only stay
    actions.push_back(EncodeAction(MovementAction::Stay));
    return actions;
  }

  Position pos = agent->GetPosition();

  // Check each movement action
  for (int m = 0; m < kNumMovementActions; ++m) {
    MovementAction mov = static_cast<MovementAction>(m);
    Position target = ApplyMovement(pos, mov);

    // Stay is always legal
    if (mov == MovementAction::Stay) {
      actions.push_back(EncodeAction(mov));
      continue;
    }

    // Check if target is walkable (don't check occupancy - that's for collision)
    if (grid_->IsInBounds(target) && grid_->IsWalkable(target)) {
      actions.push_back(EncodeAction(mov));
    }
  }

  return actions;
}

void BaseEnv::GatherIntentions(const std::vector<Action>& actions) {
  auto agents = object_manager_->GetAllAgents();
  for (size_t i = 0; i < agents.size() && i < actions.size(); ++i) {
    Agent* agent = agents[i];

    // Stunned agents are forced to stay
    if (agent->IsStunned()) {
      agent->SetIntention({MovementAction::Stay});
      continue;
    }

    // Skip agents with FSM - they set their own intentions in PreStep
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      if (fsm_agent->HasFSM()) {
        continue;
      }
    }

    DecodedAction decoded = DecodeAction(actions[i]);

    // Slowed agents can only move on even ticks
    if (agent->IsSlowed() && (tick_ % 2) == 1) {
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
  while (changed && iteration++ < max_iterations) {
    changed = false;

    auto agents = object_manager_->GetAllAgents();

    // Build current state
    std::unordered_map<ObjectId, Position> target_pos;
    std::unordered_map<Position, ObjectId, PositionHash> current_occupant;

    for (const Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      target_pos[agent->GetId()] = PredictPosition(agent);
      current_occupant[agent->GetPosition()] = agent->GetId();
    }

    // Check for invalid moves
    for (Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      if (agent->GetIntention().movement == MovementAction::Stay) continue;

      Position target = target_pos[agent->GetId()];

      // Check 1: Grid walkability / bounds
      if (!grid_->IsInBounds(target) || !grid_->IsWalkable(target)) {
        agent->SetIntention({MovementAction::Stay});
        changed = true;
        continue;
      }

      // Check 2: Swap detection (A→B and B→A)
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

    // Check 3: Multiple agents claiming same target cell
    // Rebuild target_pos after modifications
    target_pos.clear();
    for (const Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      target_pos[agent->GetId()] = PredictPosition(agent);
    }

    std::unordered_map<Position, std::vector<Agent*>, PositionHash> claims;
    for (Agent* agent : agents) {
      if (!agent->IsAlive()) continue;
      claims[target_pos[agent->GetId()]].push_back(agent);
    }

    for (auto& [pos, claiming_agents] : claims) {
      if (claiming_agents.size() > 1) {
        // Conflict! All agents moving to this cell become noop
        for (Agent* agent : claiming_agents) {
          if (agent->GetIntention().movement != MovementAction::Stay) {
            agent->SetIntention({MovementAction::Stay});
            changed = true;
          }
        }
      }
    }

    // Check 4: Chase validity - ensure cell will actually be vacated
    // Rebuild target_pos after modifications
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
          // Occupant must be moving away for chase to work
          if (occupant->GetIntention().movement == MovementAction::Stay) {
            // Occupant staying, so we can't enter
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
  // FSM attack damage is now handled via Effect system (AttackState::OnEnter spawns effect)
  // TODO: Handle companion interact actions when implemented
  // For now, companions always have interact = None, so nothing to do
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

    // Direction for Companions
    if (const Companion* comp = dynamic_cast<const Companion*>(agent)) {
      as.direction = static_cast<int>(comp->GetDirection());
      as.color = static_cast<int>(comp->GetColor());
    }

    // FSM data for AgentFSM
    if (const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(agent)) {
      if (fsm_agent->HasFSM()) {
        as.has_fsm = true;
        const FSMState* state = fsm_agent->GetCurrentState();
        as.fsm.state_name = state ? state->GetName() : "";
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
      }
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

  // Validate for this environment type
  ValidateSnapshot(snapshot);

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

  // Load agents
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
        agent = object_manager_->CreateActor<AgentFSM>(as.position);
        break;
      case ObjectType::Agent:
      default:
        agent = object_manager_->CreateActor<Agent>(as.position);
        break;
    }

    if (!agent) continue;

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

    // Restore Companion-specific state
    if (Companion* comp = dynamic_cast<Companion*>(agent)) {
      comp->SetDirection(static_cast<Direction>(as.direction));
      comp->SetColor(static_cast<ActorColor>(as.color));
    }

    // Restore AgentFSM-specific state
    if (AgentFSM* fsm_agent = dynamic_cast<AgentFSM*>(agent)) {
      fsm_agent->SetCadence(as.cadence);
      // Note: tick and FSM state restoration requires more complex handling
      // For now, FSM agents will start fresh when loaded
      // Full FSM restoration would require FSMState registry lookup
    }
  }

  // Load effects
  for (const auto& es : snapshot.effects) {
    const EffectConfig* config = EffectConfigRegistry::Instance().GetConfig(es.effect_name);
    if (!config) continue;

    EffectTarget target;
    target.type = static_cast<EffectTarget::Type>(es.target_type);
    target.cell = es.target_cell;
    target.actor_id = es.target_actor_id;
    for (int id : es.target_actors) {
      target.actors.push_back(id);
    }

    ActiveEffect effect;
    effect.config = config;
    effect.target = target;
    effect.direction = static_cast<Direction>(es.direction);
    effect.ticks_remaining = es.ticks_remaining;
    effect.in_telegraph = es.in_telegraph;
    effect.loops_remaining = es.loops_remaining;
    effect.source_id = es.source_id;

    effect_system_->AddEffect(std::move(effect));
  }

  // Restore timing
  tick_ = snapshot.tick;
  horizon_ = snapshot.horizon;
  d4_transform_ = snapshot.d4_transform;

  // Apply D4 symmetry transformation if specified
  // (Snapshot contains pre-transform positions, so we apply transform after loading)
  if (d4_transform_ != 0) {
    ApplyD4Transform();
  }
}

void BaseEnv::ValidateSnapshot(const Snapshot& /*snapshot*/) const {
  // Base implementation does no validation
  // Subclasses override to check for required cell types
}

}  // namespace companions
