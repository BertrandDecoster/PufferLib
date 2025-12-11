// Copyright 2024
// FSM State base class and context for agent AI

#ifndef COMPANIONS_CORE_FSM_FSM_STATE_H_
#define COMPANIONS_CORE_FSM_FSM_STATE_H_

#include <string>
#include <vector>

#include "../agent_config.h"
#include "../pcg32.h"
#include "../types.h"

namespace companions {

// Forward declarations
class BaseEnv;
class AgentFSM;

// =============================================================================
// AttackIntent - Stored when an attack is being prepared/executed
// =============================================================================
struct AttackIntent {
  Position target_position;     // Center of attack area
  int area_width = 1;           // Width of attack area
  int area_height = 1;          // Height of attack area
  int damage = 1;               // Damage to deal
  TargetFilter filter;          // Who gets affected
};

// =============================================================================
// FSMContext - Per-actor runtime data for FSM decisions
// =============================================================================
struct FSMContext {
  // Target tracking (for AggroState)
  ObjectId target_id = kInvalidObjectId;

  // Patrol data - waypoints must be on orthogonal paths
  std::vector<Position> patrol_path;
  int patrol_index = 0;
  bool patrol_forward = true;  // true = increasing index, false = decreasing

  // Detection/aggro configuration (set by actor type)
  int detection_range = 3;      // Manhattan distance to detect companions
  int lose_target_range = 5;    // Distance to lose target and return to patrol

  // Attack configuration (loaded from AgentConfig)
  bool has_attack = false;
  std::string attack_effect_name;  // Effect name from effects.csv (preferred)

  // Legacy attack fields (used if attack_effect_name is empty)
  int telegraph_ticks = 1;      // Ticks to telegraph before attack
  int attack_ticks = 1;         // Ticks attack is active (usually 1)
  int recovery_ticks = 1;       // Ticks to recover after attack
  int attack_damage = 1;        // Base damage
  int attack_width = 1;         // Attack area width
  int attack_height = 1;        // Attack area height
  TargetFilter attack_filter;   // Who gets affected

  // Attack runtime state
  int attack_tick_counter = 0;  // Counter for current attack phase
  AttackIntent current_attack;  // Active attack being prepared/executed

  // RNG for deterministic tie-breaking
  pcg32* rng = nullptr;
};

// =============================================================================
// FSMState - Abstract base class for FSM states (Flyweight pattern)
//
// States are singletons - they contain no per-actor data.
// All per-actor data lives in FSMContext.
// States directly call agent.MoveTo() to set movement intentions.
// =============================================================================
class FSMState {
 public:
  virtual ~FSMState() = default;

  // Lifecycle hooks (optional override)
  // OnEnter/OnExit take non-const BaseEnv& to allow side effects (spawn effects, etc.)
  virtual void OnEnter(FSMContext& /*ctx*/, AgentFSM& /*agent*/,
                       BaseEnv& /*env*/) const {}
  virtual void OnExit(FSMContext& /*ctx*/, AgentFSM& /*agent*/,
                      BaseEnv& /*env*/) const {}

  // Main update - returns next state, or nullptr to stay in current state
  virtual const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                                 const BaseEnv& env) const = 0;

  // State name for debugging
  virtual std::string GetName() const = 0;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_FSM_FSM_STATE_H_
