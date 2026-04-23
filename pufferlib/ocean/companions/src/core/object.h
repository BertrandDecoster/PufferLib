// Copyright 2024
// Object/Actor hierarchy for The Companions game

#ifndef COMPANIONS_CORE_OBJECT_H_
#define COMPANIONS_CORE_OBJECT_H_

#include <memory>
#include <string>
#include <vector>

#include "types.h"

// Forward declare FSM types
namespace companions {
class FSMState;
struct FSMContext;
class BaseEnv;
}

namespace companions {

// Forward declaration for passkey pattern
class ObjectManager;

// =============================================================================
// Passkey for position updates - only ObjectManager can construct this
// =============================================================================
class PositionUpdateKey {
  friend class ObjectManager;
  PositionUpdateKey() = default;
};

// =============================================================================
// Object Types
// =============================================================================
enum class ObjectType {
  Object,
  Actor,
  Agent,
  AgentFSM,  // Renamed from Enemy - agent with FSM AI
  Companion,
  Player,
  NPCCompanion
};

// Fine-grained per-class archetype. Complementary to ObjectType: ObjectType
// stops at the base-class level (AgentFSM), AgentKind identifies the concrete
// class (Zombie / Goblin / Dragon / ...). Values kept numerically in sync
// with Companions_AgentKind in the C API (companions_api.h) so the API
// translation is a `static_cast`; a block of static_asserts guards drift.
enum class AgentKind : int32_t {
  Unknown     = 0,
  Companion   = 1,
  NeutralNpc  = 2,
  EnemyZombie = 10,
  EnemyGoblin = 11,
  EnemyDragon = 12,
};

// =============================================================================
// Actor Colors (logical, not terminal-specific)
// =============================================================================
enum class ActorColor {
  None,
  Red,
  Green,
  Blue
};

// =============================================================================
// Object - base class for anything in the game
// =============================================================================
class Object {
 public:
  explicit Object(ObjectId id);
  virtual ~Object() = default;

  // Copyable for cloning support
  Object(const Object&) = default;
  Object& operator=(const Object&) = default;
  Object(Object&&) = default;
  Object& operator=(Object&&) = default;

  ObjectId GetId() const { return id_; }
  virtual ObjectType GetType() const { return ObjectType::Object; }
  virtual std::string GetTypeName() const { return "Object"; }

  // Clone for deep copying
  virtual std::unique_ptr<Object> Clone() const {
    return std::make_unique<Object>(*this);
  }

 protected:
  ObjectId id_;
};

// =============================================================================
// Actor - an object instanced in the grid (occupies a cell)
// =============================================================================
class Actor : public Object {
 public:
  Actor(ObjectId id, Position pos);

  ObjectType GetType() const override { return ObjectType::Actor; }
  std::string GetTypeName() const override { return "Actor"; }

  Position GetPosition() const { return pos_; }
  void SetPosition(Position pos, PositionUpdateKey) { pos_ = pos; }

  bool IsAlive() const { return alive_; }
  void SetAlive(bool alive) { alive_ = alive; }

  virtual char GetChar() const { return 'A'; }

  // Clone for deep copying (covariant return type)
  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Actor>(*this);
  }

 protected:
  Position pos_;
  bool alive_ = true;
};

// =============================================================================
// StatusEffect - a temporary effect applied to an agent
// =============================================================================
enum class StatusType {
  None,
  Stunned,  // Cannot move (forced Stay)
  Slowed,   // Moves every other tick
  Marked    // Takes bonus damage
};

struct StatusEffect {
  StatusType type = StatusType::None;
  int duration = 0;  // Ticks remaining

  bool IsActive() const { return type != StatusType::None && duration > 0; }
  void Tick() { if (duration > 0) duration--; }
  void Clear() { type = StatusType::None; duration = 0; }
};

// =============================================================================
// Agent - an actor that can take actions
// =============================================================================
class Agent : public Actor {
 public:
  Agent(ObjectId id, Position pos);

  ObjectType GetType() const override { return ObjectType::Agent; }
  std::string GetTypeName() const override { return "Agent"; }

  // Concrete archetype for API/rendering consumers. Default picks NeutralNpc
  // when the agent is neutral and no concrete subclass has overridden this;
  // this matches the pre-refactor ToAPIAgentKind fallback exactly.
  virtual AgentKind GetAgentKind() const {
    return faction_ == Faction::NEUTRAL ? AgentKind::NeutralNpc
                                        : AgentKind::Unknown;
  }

  // The intended action for this step
  void SetIntention(DecodedAction action) { intention_ = action; }
  DecodedAction GetIntention() const { return intention_; }
  void ClearIntention() { intention_ = {MovementAction::Stay}; }

  // Original intention (captured before collision resolution)
  void CaptureOriginalIntention() { original_intention_ = intention_; }
  DecodedAction GetOriginalIntention() const { return original_intention_; }

  // Agent index for action array ordering
  void SetAgentIndex(int idx) { agent_index_ = idx; }
  int GetAgentIndex() const { return agent_index_; }

  // ==========================================================================
  // Faction
  // ==========================================================================
  Faction GetFaction() const { return faction_; }
  void SetFaction(Faction f) { faction_ = f; }

  // ==========================================================================
  // Health System
  // ==========================================================================
  int GetHealth() const { return health_; }
  int GetMaxHealth() const { return max_health_; }
  void SetMaxHealth(int hp) { max_health_ = hp; health_ = hp; }
  void TakeDamage(int amount);
  void Heal(int amount);
  bool IsDead() const { return health_ <= 0; }

  // ==========================================================================
  // Status Effects
  // ==========================================================================
  void ApplyStatus(StatusType type, int duration);
  void ClearStatus(StatusType type);
  void ClearAllStatuses();
  void TickStatuses();  // Called each step to decrement durations

  bool HasStatus(StatusType type) const;
  bool IsStunned() const { return HasStatus(StatusType::Stunned); }
  bool IsSlowed() const { return HasStatus(StatusType::Slowed); }
  bool IsMarked() const { return HasStatus(StatusType::Marked); }

  // Get active status effects for observation/rendering
  const std::vector<StatusEffect>& GetStatuses() const { return statuses_; }

  // Marked damage multiplier (configurable, default 1.5x)
  static constexpr float kMarkedDamageMultiplier = 1.5f;

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Agent>(*this);
  }

 protected:
  DecodedAction intention_;
  DecodedAction original_intention_;  // Captured before collision resolution
  int agent_index_ = -1;
  Faction faction_ = Faction::NEUTRAL;
  int health_ = 3;
  int max_health_ = 3;
  std::vector<StatusEffect> statuses_;  // Active status effects
};

// =============================================================================
// AgentFSM - agent with FSM AI support (can be enemy, neutral, or ally)
// =============================================================================
class AgentFSM : public Agent {
 public:
  AgentFSM(ObjectId id, Position pos);
  virtual ~AgentFSM();  // Defined in object.cc (needs FSMContext complete type)

  // Copy support (for Clone)
  AgentFSM(const AgentFSM& other);
  AgentFSM& operator=(const AgentFSM& other);

  ObjectType GetType() const override { return ObjectType::AgentFSM; }
  std::string GetTypeName() const override { return "AgentFSM"; }
  char GetChar() const override { return 'E'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<AgentFSM>(*this);
  }

  // ==========================================================================
  // FSM Support
  // ==========================================================================

  // Set the FSM for this agent
  void SetFSM(const FSMState* initial_state, FSMContext ctx);

  // Update FSM (called by BaseEnv::UpdateAgentFSM)
  void UpdateFSM(BaseEnv& env);

  // Check if this agent has an FSM
  bool HasFSM() const { return current_state_ != nullptr; }

  // FSM accessors
  const FSMState* GetCurrentState() const { return current_state_; }
  void SetCurrentState(const FSMState* state) { current_state_ = state; }
  FSMContext& GetFSMContext();
  const FSMContext& GetFSMContext() const;

  // ==========================================================================
  // Movement - override in subclasses for different behavior
  // ==========================================================================

  // Virtual movement method - subclasses override for different behaviors
  // Sets intention via SetIntention() after computing the move
  virtual void MoveTo(Position target, const BaseEnv& env);

  // ==========================================================================
  // Cadence Support (for slow agents like Zombies)
  // ==========================================================================

  // Set cadence pattern (e.g., {1,0} = move, skip, move, skip...)
  void SetCadence(std::vector<int> cadence) { cadence_ = std::move(cadence); }
  const std::vector<int>& GetCadence() const { return cadence_; }

  // Advance internal tick counter
  void AdvanceTick() { tick_++; }
  int GetTick() const { return tick_; }
  void SetTick(int t) { tick_ = t; }

  // Check if this agent can act this tick based on cadence
  bool CanAct() const {
    return cadence_.empty() ||
           cadence_[tick_ % cadence_.size()] != 0;
  }

 protected:
  // FSM state (Flyweight - points to singleton states)
  const FSMState* current_state_ = nullptr;

  // FSM context (per-agent runtime data) - stored as unique_ptr to avoid
  // including fsm_state.h (forward declare only)
  std::unique_ptr<FSMContext> fsm_context_;

  // Cadence pattern for movement timing
  std::vector<int> cadence_;
  int tick_ = 0;
};

// =============================================================================
// Companion - player-side agent (base class)
// =============================================================================
class Companion : public Agent {
 public:
  Companion(ObjectId id, Position pos);

  ObjectType GetType() const override { return ObjectType::Companion; }
  std::string GetTypeName() const override { return "Companion"; }
  AgentKind GetAgentKind() const override { return AgentKind::Companion; }

  virtual bool IsPlayerControlled() const { return false; }

  // Direction facing (for display)
  Direction GetDirection() const { return direction_; }
  void SetDirection(Direction dir) { direction_ = dir; }

  // Color for rendering
  ActorColor GetColor() const { return color_; }
  void SetColor(ActorColor color) { color_ = color; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Companion>(*this);
  }

 protected:
  Direction direction_ = Direction::Down;  // Default: facing down
  ActorColor color_ = ActorColor::None;
};

// =============================================================================
// Player - the human-controlled companion
// =============================================================================
class Player : public Companion {
 public:
  Player(ObjectId id, Position pos);

  ObjectType GetType() const override { return ObjectType::Player; }
  std::string GetTypeName() const override { return "Player"; }
  bool IsPlayerControlled() const override { return true; }
  char GetChar() const override { return 'P'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Player>(*this);
  }
};

// =============================================================================
// NPCCompanion - AI-controlled companion
// =============================================================================
class NPCCompanion : public Companion {
 public:
  NPCCompanion(ObjectId id, Position pos);

  ObjectType GetType() const override { return ObjectType::NPCCompanion; }
  std::string GetTypeName() const override { return "NPCCompanion"; }
  char GetChar() const override { return 'C'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<NPCCompanion>(*this);
  }
};

// =============================================================================
// Utility
// =============================================================================
std::string ObjectTypeToString(ObjectType type);

// Type checking helpers
bool IsAgent(const Object* obj);
bool IsCompanion(const Object* obj);
bool IsAgentFSM(const Object* obj);

// Legacy alias for IsAgentFSM (deprecated, use IsAgentFSM)
inline bool IsEnemy(const Object* obj) { return IsAgentFSM(obj); }

// Parse status type from string (case-insensitive)
StatusType StatusTypeFromString(const std::string& name);
std::string StatusTypeToString(StatusType type);

}  // namespace companions

#endif  // COMPANIONS_CORE_OBJECT_H_
