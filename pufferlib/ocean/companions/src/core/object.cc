// Copyright 2024
// Object/Actor hierarchy implementation

#include "object.h"

#include <algorithm>
#include <cctype>

#include "fsm/fsm_state.h"
#include "game_logger.h"

namespace companions {

// =============================================================================
// Object
// =============================================================================
Object::Object(ObjectId id) : id_(id) {}

// =============================================================================
// Actor
// =============================================================================
Actor::Actor(ObjectId id, Position pos) : Object(id), pos_(pos) {}

// =============================================================================
// Agent
// =============================================================================
Agent::Agent(ObjectId id, Position pos) : Actor(id, pos) {}

void Agent::TakeDamage(int amount) {
  // Marked targets take bonus damage
  if (HasStatus(StatusType::Marked)) {
    amount = static_cast<int>(amount * kMarkedDamageMultiplier);
  }
  health_ -= amount;
  if (health_ < 0) health_ = 0;
  if (health_ <= 0) {
    SetAlive(false);
  }
}

void Agent::Heal(int amount) {
  health_ += amount;
  if (health_ > max_health_) health_ = max_health_;
}

void Agent::ApplyStatus(StatusType type, int duration) {
  if (type == StatusType::None || duration <= 0) return;

  // Check if we already have this status - refresh duration if so
  for (auto& status : statuses_) {
    if (status.type == type) {
      // Refresh to max of current and new duration
      status.duration = std::max(status.duration, duration);
      return;
    }
  }

  // Add new status
  statuses_.push_back({type, duration});
}

void Agent::ClearStatus(StatusType type) {
  statuses_.erase(
      std::remove_if(statuses_.begin(), statuses_.end(),
                     [type](const StatusEffect& s) { return s.type == type; }),
      statuses_.end());
}

void Agent::ClearAllStatuses() { statuses_.clear(); }

void Agent::TickStatuses() {
  for (auto& status : statuses_) {
    status.Tick();
  }

  // Remove expired statuses
  statuses_.erase(
      std::remove_if(statuses_.begin(), statuses_.end(),
                     [](const StatusEffect& s) { return !s.IsActive(); }),
      statuses_.end());
}

bool Agent::HasStatus(StatusType type) const {
  for (const auto& status : statuses_) {
    if (status.type == type && status.IsActive()) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// AgentFSM
// =============================================================================
AgentFSM::AgentFSM(ObjectId id, Position pos) : Agent(id, pos) {
  // Default faction is ENEMY for backward compatibility
  // Can be changed via SetFaction() for neutral/ally FSM agents
  faction_ = Faction::ENEMY;
}

// Destructor defined here (not defaulted in header) because unique_ptr<FSMContext>
// requires FSMContext to be a complete type, which it is here after #include fsm_state.h
AgentFSM::~AgentFSM() = default;

AgentFSM::AgentFSM(const AgentFSM& other)
    : Agent(other),
      current_state_(other.current_state_),
      fsm_context_(other.fsm_context_
                       ? std::make_unique<FSMContext>(*other.fsm_context_)
                       : nullptr),
      cadence_(other.cadence_),
      tick_(other.tick_) {}

AgentFSM& AgentFSM::operator=(const AgentFSM& other) {
  if (this != &other) {
    Agent::operator=(other);
    current_state_ = other.current_state_;
    fsm_context_ = other.fsm_context_
                       ? std::make_unique<FSMContext>(*other.fsm_context_)
                       : nullptr;
    cadence_ = other.cadence_;
    tick_ = other.tick_;
  }
  return *this;
}

void AgentFSM::SetFSM(const FSMState* initial_state, FSMContext ctx) {
  current_state_ = initial_state;
  fsm_context_ = std::make_unique<FSMContext>(std::move(ctx));
}

void AgentFSM::UpdateFSM(BaseEnv& env) {
  if (!current_state_ || !fsm_context_) return;

  // Log current state at start of update
  LOG_FSM("Agent " << GetId() << " (" << GetTypeName() << ") in state: "
          << current_state_->GetName());

  // Loop to handle transitions - new state runs immediately in the same tick
  // This follows the standard game AI FSM pattern where transitions advance
  // to the next state in the same frame (Unity, Unreal, etc.)
  const int kMaxTransitions = 10;  // Safety limit to prevent infinite loops
  for (int i = 0; i < kMaxTransitions; ++i) {
    const FSMState* next_state = current_state_->Update(*fsm_context_, *this, env);

    if (!next_state || next_state == current_state_) {
      // No transition, state executed its action - done
      break;
    }

    // Log state transition
    std::string old_state_name = current_state_->GetName();

    // Transition occurred - switch and loop to run new state immediately
    current_state_->OnExit(*fsm_context_, *this, env);
    current_state_ = next_state;
    current_state_->OnEnter(*fsm_context_, *this, env);

    LOG_FSM("Agent " << GetId() << " (" << GetTypeName() << "): "
            << old_state_name << " -> " << current_state_->GetName());
  }

  // Advance tick counter AFTER state update completes.
  // This means CanAct() checks the tick value from the START of this update,
  // ensuring cadence patterns like [1,0] work correctly:
  // tick=0 (CanAct=true, move), tick=1 (CanAct=false, skip), tick=2 (CanAct=true, move)...
  AdvanceTick();
}

FSMContext& AgentFSM::GetFSMContext() {
  if (!fsm_context_) {
    fsm_context_ = std::make_unique<FSMContext>();
  }
  return *fsm_context_;
}

const FSMContext& AgentFSM::GetFSMContext() const {
  static FSMContext empty_context;
  return fsm_context_ ? *fsm_context_ : empty_context;
}

void AgentFSM::MoveTo(Position target, const BaseEnv& /*env*/) {
  // Base implementation: simple direct movement toward target
  // Subclasses override for pathfinding, flying, etc.
  Position current = GetPosition();

  if (current == target) {
    SetIntention({MovementAction::Stay});
    return;
  }

  // Move toward target (prefer axis with larger delta)
  int dr = target.row - current.row;
  int dc = target.col - current.col;

  MovementAction action = MovementAction::Stay;
  if (std::abs(dr) >= std::abs(dc)) {
    action = (dr > 0) ? MovementAction::Down : MovementAction::Up;
  } else {
    action = (dc > 0) ? MovementAction::Right : MovementAction::Left;
  }

  SetIntention({action});
}

// =============================================================================
// Companion
// =============================================================================
Companion::Companion(ObjectId id, Position pos) : Agent(id, pos) {
  faction_ = Faction::COMPANION;
}

// =============================================================================
// Player
// =============================================================================
Player::Player(ObjectId id, Position pos) : Companion(id, pos) {}

// =============================================================================
// NPCCompanion
// =============================================================================
NPCCompanion::NPCCompanion(ObjectId id, Position pos) : Companion(id, pos) {}

// =============================================================================
// Utility
// =============================================================================
std::string ObjectTypeToString(ObjectType type) {
  switch (type) {
    case ObjectType::Object:
      return "Object";
    case ObjectType::Actor:
      return "Actor";
    case ObjectType::Agent:
      return "Agent";
    case ObjectType::AgentFSM:
      return "AgentFSM";
    case ObjectType::Companion:
      return "Companion";
    case ObjectType::Player:
      return "Player";
    case ObjectType::NPCCompanion:
      return "NPCCompanion";
    default:
      return "Unknown";
  }
}

bool IsAgent(const Object* obj) {
  if (!obj) return false;
  ObjectType type = obj->GetType();
  return type == ObjectType::Agent ||
         type == ObjectType::AgentFSM ||
         type == ObjectType::Companion ||
         type == ObjectType::Player ||
         type == ObjectType::NPCCompanion;
}

bool IsCompanion(const Object* obj) {
  if (!obj) return false;
  ObjectType type = obj->GetType();
  return type == ObjectType::Companion ||
         type == ObjectType::Player ||
         type == ObjectType::NPCCompanion;
}

bool IsAgentFSM(const Object* obj) {
  if (!obj) return false;
  return obj->GetType() == ObjectType::AgentFSM;
}

// =============================================================================
// Status Effect Utilities
// =============================================================================
StatusType StatusTypeFromString(const std::string& name) {
  // Case-insensitive comparison
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "stunned" || lower == "stun") return StatusType::Stunned;
  if (lower == "slowed" || lower == "slow") return StatusType::Slowed;
  if (lower == "marked" || lower == "mark") return StatusType::Marked;
  return StatusType::None;
}

std::string StatusTypeToString(StatusType type) {
  switch (type) {
    case StatusType::Stunned:
      return "stunned";
    case StatusType::Slowed:
      return "slowed";
    case StatusType::Marked:
      return "marked";
    case StatusType::None:
    default:
      return "none";
  }
}

}  // namespace companions
