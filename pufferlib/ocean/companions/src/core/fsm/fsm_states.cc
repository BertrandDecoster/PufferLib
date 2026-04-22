// Copyright 2024
// FSM States implementation

#include "fsm_states.h"

#include <algorithm>
#include <limits>
#include <random>

#include "../../env/base_env.h"
#include "../effect_config.h"
#include "../game_logger.h"
#include "../object.h"

namespace companions {

// =============================================================================
// Helper functions
// =============================================================================
namespace {

// Find closest companion to agent position within detection range
// Returns kInvalidObjectId if none found
// On ties: keeps existing target if still in range, otherwise uses RNG
ObjectId FindClosestCompanion(const Position& agent_pos, int detection_range,
                              ObjectId current_target,
                              const BaseEnv& env, pcg32* rng) {
  const auto& mgr = env.GetObjectManager();
  int closest_dist = detection_range + 1;
  std::vector<ObjectId> tied_ids;

  for (const Companion* comp : mgr.GetAllCompanions()) {
    if (!comp->IsAlive()) continue;

    int dist = ManhattanDistance(agent_pos, comp->GetPosition());
    if (dist <= detection_range) {
      if (dist < closest_dist) {
        closest_dist = dist;
        tied_ids.clear();
        tied_ids.push_back(comp->GetId());
      } else if (dist == closest_dist) {
        tied_ids.push_back(comp->GetId());
      }
    }
  }

  if (tied_ids.empty()) {
    return kInvalidObjectId;
  }

  // Tie-breaking: prefer current target if still in range
  if (std::find(tied_ids.begin(), tied_ids.end(), current_target) != tied_ids.end()) {
    return current_target;
  }

  // Otherwise use RNG for deterministic selection
  if (rng && tied_ids.size() > 1) {
    size_t idx = portable_uniform_int<size_t>(*rng, 0, tied_ids.size() - 1);
    return tied_ids[idx];
  }

  return tied_ids[0];
}

// Check if position is on patrol path
bool IsOnPatrolPath(const Position& pos, const std::vector<Position>& path) {
  return std::find(path.begin(), path.end(), pos) != path.end();
}

// Find nearest patrol waypoint index
int FindNearestPatrolIndex(const Position& pos,
                           const std::vector<Position>& path) {
  int best_idx = 0;
  int best_dist = std::numeric_limits<int>::max();

  for (size_t i = 0; i < path.size(); ++i) {
    int dist = ManhattanDistance(pos, path[i]);
    if (dist < best_dist) {
      best_dist = dist;
      best_idx = static_cast<int>(i);
    }
  }

  return best_idx;
}

// Returns next state after losing target based on patrol path position
// Used when transitioning out of AggroState or RecoveryState
const FSMState* TransitionAfterLostTarget(FSMContext& ctx, Position agent_pos) {
  if (ctx.patrol_path.empty()) {
    return &PatrolState::Instance();
  }

  if (IsOnPatrolPath(agent_pos, ctx.patrol_path)) {
    ctx.patrol_index = FindNearestPatrolIndex(agent_pos, ctx.patrol_path);
    return &PatrolState::Instance();
  } else {
    ctx.patrol_index = FindNearestPatrolIndex(agent_pos, ctx.patrol_path);
    return &ReturnToPatrolState::Instance();
  }
}

}  // namespace

// =============================================================================
// PatrolState
// =============================================================================
const PatrolState& PatrolState::Instance() {
  static PatrolState instance;
  return instance;
}

const FSMState* PatrolState::Update(FSMContext& ctx, AgentFSM& agent,
                                    const BaseEnv& env) const {
  Position agent_pos = agent.GetPosition();

  // Check for companions in detection range
  ObjectId closest = FindClosestCompanion(agent_pos, ctx.detection_range,
                                          ctx.target_id, env, ctx.rng);
  if (closest != kInvalidObjectId) {
    ctx.target_id = closest;
    return &AggroState::Instance();  // AggroState will execute immediately
  }

  // No target - continue patrol
  if (ctx.patrol_path.empty()) {
    // No patrol path, just stay
    agent.MoveTo(agent_pos, env);
    return nullptr;
  }

  // Get current waypoint
  Position waypoint = ctx.patrol_path[ctx.patrol_index];

  // Check if we've reached the waypoint BEFORE calling MoveTo
  // This prevents "wasting" a tick at each waypoint
  if (agent_pos == waypoint) {
    // Advance to next waypoint (loop pattern)
    ctx.patrol_index = (ctx.patrol_index + 1) %
                       static_cast<int>(ctx.patrol_path.size());
    waypoint = ctx.patrol_path[ctx.patrol_index];
  }

  // Move toward current/next waypoint
  agent.MoveTo(waypoint, env);

  return nullptr;  // Stay in PatrolState
}

// =============================================================================
// AggroState
// =============================================================================
const AggroState& AggroState::Instance() {
  static AggroState instance;
  return instance;
}

const FSMState* AggroState::Update(FSMContext& ctx, AgentFSM& agent,
                                   const BaseEnv& env) const {
  Position agent_pos = agent.GetPosition();
  const auto& mgr = env.GetObjectManager();

  // Get current target
  // Note: dynamic_cast is used here because Player, NPCCompanion inherit from
  // Companion but have distinct ObjectType values. The RTTI overhead is
  // acceptable given ~1-2μs per FSM update and pathfinding costs ~10-15μs.
  const Actor* target = mgr.GetActor(ctx.target_id);
  const Companion* target_comp = dynamic_cast<const Companion*>(target);

  // Check if target is still valid and in range
  bool target_valid = target_comp && target_comp->IsAlive();
  int target_dist = target_valid
                        ? ManhattanDistance(agent_pos, target_comp->GetPosition())
                        : ctx.lose_target_range + 1;

  if (!target_valid || target_dist > ctx.lose_target_range) {
    // Lost target - transition based on patrol path position
    ctx.target_id = kInvalidObjectId;
    return TransitionAfterLostTarget(ctx, agent_pos);
  }

  // Check for closer companions (may switch target)
  ObjectId new_closest = FindClosestCompanion(agent_pos, ctx.detection_range,
                                              ctx.target_id, env, ctx.rng);
  if (new_closest != kInvalidObjectId) {
    ctx.target_id = new_closest;
    // Update target reference if changed
    target = mgr.GetActor(ctx.target_id);
    target_comp = dynamic_cast<const Companion*>(target);
  }

  // Check if we can attack (adjacent to target and has attack ability)
  if (ctx.has_attack && target_comp && target_comp->IsAlive()) {
    Position target_pos = target_comp->GetPosition();
    int current_dist = ManhattanDistance(agent_pos, target_pos);

    if (current_dist <= 1) {
      // Adjacent - start attack sequence
      return &TelegraphState::Instance();
    }

    // Not adjacent - move toward target
    agent.MoveTo(target_pos, env);
    return nullptr;  // Stay in AggroState
  }

  // Follow target using A* pathfinding (Euclidean heuristic naturally
  // prioritizes the axis with larger distance)
  if (target_comp && target_comp->IsAlive()) {
    agent.MoveTo(target_comp->GetPosition(), env);
  } else {
    agent.MoveTo(agent_pos, env);  // Stay if no valid target
  }

  return nullptr;  // Stay in AggroState
}

// =============================================================================
// ReturnToPatrolState
// =============================================================================
const ReturnToPatrolState& ReturnToPatrolState::Instance() {
  static ReturnToPatrolState instance;
  return instance;
}

const FSMState* ReturnToPatrolState::Update(FSMContext& ctx, AgentFSM& agent,
                                            const BaseEnv& env) const {
  Position agent_pos = agent.GetPosition();

  // Check for companions during return (can re-aggro)
  ObjectId closest = FindClosestCompanion(agent_pos, ctx.detection_range,
                                          ctx.target_id, env, ctx.rng);
  if (closest != kInvalidObjectId) {
    ctx.target_id = closest;
    return &AggroState::Instance();  // AggroState will execute immediately
  }

  // No target - continue returning to patrol
  if (ctx.patrol_path.empty()) {
    // No patrol path, go to PatrolState
    return &PatrolState::Instance();
  }

  Position target_waypoint = ctx.patrol_path[ctx.patrol_index];

  // Check if we've reached the patrol point
  if (agent_pos == target_waypoint) {
    // Reached patrol path - resume patrol
    return &PatrolState::Instance();
  }

  // Move toward nearest patrol point
  agent.MoveTo(target_waypoint, env);

  return nullptr;  // Stay in ReturnToPatrolState
}

// =============================================================================
// TelegraphState
// =============================================================================
const TelegraphState& TelegraphState::Instance() {
  static TelegraphState instance;
  return instance;
}

void TelegraphState::OnEnter(FSMContext& ctx, AgentFSM& agent,
                             BaseEnv& env) const {
  // Reset tick counter for telegraph phase
  ctx.attack_tick_counter = 0;

  // Lock in target position for the attack
  const auto& mgr = env.GetObjectManager();
  const Actor* target = mgr.GetActor(ctx.target_id);

  if (target && target->IsAlive()) {
    ctx.current_attack.target_position = target->GetPosition();
  } else {
    // If no valid target, target our own position (will likely miss)
    ctx.current_attack.target_position = agent.GetPosition();
  }

  // Copy attack config to current attack
  ctx.current_attack.area_width = ctx.attack_width;
  ctx.current_attack.area_height = ctx.attack_height;
  ctx.current_attack.damage = ctx.attack_damage;
  ctx.current_attack.filter = ctx.attack_filter;
}

const FSMState* TelegraphState::Update(FSMContext& ctx, AgentFSM& agent,
                                       const BaseEnv& env) const {
  // Agent stays in place during telegraph
  agent.MoveTo(agent.GetPosition(), env);

  // Check BEFORE incrementing so 1-tick means "stay for 1 update, transition next"
  if (ctx.attack_tick_counter >= ctx.telegraph_ticks) {
    return &AttackState::Instance();
  }

  // Log timer tick
  LOG_FSM("Agent " << agent.GetId() << ": Telegraph tick "
          << ctx.attack_tick_counter << "/" << ctx.telegraph_ticks);

  ctx.attack_tick_counter++;

  return nullptr;  // Stay in TelegraphState
}

// =============================================================================
// AttackState
// =============================================================================
const AttackState& AttackState::Instance() {
  static AttackState instance;
  return instance;
}

void AttackState::OnEnter(FSMContext& ctx, AgentFSM& agent,
                          BaseEnv& env) const {
  // Reset tick counter for attack phase
  ctx.attack_tick_counter = 0;

  // Spawn damage effect at locked target position
  // The effect system handles damage application independently of FSM state
  if (!ctx.attack_effect_name.empty()) {
    env.SpawnEffect(
        ctx.attack_effect_name,
        EffectTarget::AtCell(ctx.current_attack.target_position),
        Direction::Up,  // Direction doesn't matter for 1x1 effects
        agent.GetId()   // Source ID for self-damage immunity
    );
  }
}

const FSMState* AttackState::Update(FSMContext& ctx, AgentFSM& agent,
                                    const BaseEnv& env) const {
  // Agent stays in place during attack
  agent.MoveTo(agent.GetPosition(), env);

  // Check BEFORE incrementing so 1-tick means "stay for 1 update, transition next"
  if (ctx.attack_tick_counter >= ctx.attack_ticks) {
    return &RecoveryState::Instance();
  }

  // Log timer tick
  LOG_FSM("Agent " << agent.GetId() << ": Attack tick "
          << ctx.attack_tick_counter << "/" << ctx.attack_ticks);

  ctx.attack_tick_counter++;

  return nullptr;  // Stay in AttackState
}

// =============================================================================
// RecoveryState
// =============================================================================
const RecoveryState& RecoveryState::Instance() {
  static RecoveryState instance;
  return instance;
}

void RecoveryState::OnEnter(FSMContext& ctx, AgentFSM& /*agent*/,
                            BaseEnv& /*env*/) const {
  // Reset tick counter for recovery phase
  ctx.attack_tick_counter = 0;
}

const FSMState* RecoveryState::Update(FSMContext& ctx, AgentFSM& agent,
                                      const BaseEnv& env) const {
  // Agent stays in place during recovery
  agent.MoveTo(agent.GetPosition(), env);

  // Check BEFORE incrementing so 1-tick means "stay for 1 update, transition next"
  if (ctx.attack_tick_counter >= ctx.recovery_ticks) {
    // Recovery complete - check if target still in range
    Position agent_pos = agent.GetPosition();
    ObjectId closest = FindClosestCompanion(agent_pos, ctx.detection_range,
                                            ctx.target_id, env, ctx.rng);
    if (closest != kInvalidObjectId) {
      ctx.target_id = closest;
      return &AggroState::Instance();
    }

    // No target in range - transition based on patrol path position
    return TransitionAfterLostTarget(ctx, agent_pos);
  }

  // Log timer tick
  LOG_FSM("Agent " << agent.GetId() << ": Recovery tick "
          << ctx.attack_tick_counter << "/" << ctx.recovery_ticks);

  ctx.attack_tick_counter++;

  return nullptr;  // Stay in RecoveryState
}

}  // namespace companions
