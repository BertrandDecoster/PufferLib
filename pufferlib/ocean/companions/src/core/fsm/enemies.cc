// Copyright 2024
// AgentFSM subclasses implementation

#include "enemies.h"

#include <cassert>
#include <cmath>

#include "../../env/base_env.h"
#include "../agent_config.h"
#include "../effect_config.h"
#include "../pathfinder.h"
#include "fsm_states.h"

namespace companions {

// =============================================================================
// Helper function: Convert position delta to MovementAction
// =============================================================================
namespace {

MovementAction PositionToMovement(Position from, Position to) {
  if (to.row < from.row) return MovementAction::Up;
  if (to.row > from.row) return MovementAction::Down;
  if (to.col < from.col) return MovementAction::Left;
  if (to.col > from.col) return MovementAction::Right;
  return MovementAction::Stay;
}

// Helper to load FSM configuration from registry with fallback defaults
void LoadFSMConfig(FSMContext& ctx, const std::string& type_name,
                   const std::vector<Position>& patrol_path, pcg32& rng,
                   int default_detection, int default_lose_target) {
  ctx.patrol_path = patrol_path;
  ctx.patrol_index = 0;
  ctx.patrol_forward = true;
  ctx.rng = &rng;

  const AgentConfig* config =
      AgentConfigRegistry::Instance().GetConfig(type_name);

  if (config) {
    ctx.detection_range = config->detection_range;
    ctx.lose_target_range = config->lose_target_range;
    ctx.has_attack = config->has_attack;

    if (config->has_attack) {
      // Always use agent's timing config (from agents.csv)
      ctx.telegraph_ticks = config->attack.telegraph_ticks;
      ctx.attack_ticks = config->attack.attack_ticks;
      ctx.recovery_ticks = config->attack.recovery_ticks;
      ctx.attack_damage = config->attack.damage;
      ctx.attack_width = config->attack.area.width;
      ctx.attack_height = config->attack.area.height;
      ctx.attack_filter = config->attack.filter;

      // Use effect name for spawning the visual/damage effect
      if (!config->attack_effect.empty()) {
        ctx.attack_effect_name = config->attack_effect;
      }
    }
  } else {
    // Fallback defaults if config not loaded
    ctx.detection_range = default_detection;
    ctx.lose_target_range = default_lose_target;
    ctx.has_attack = true;
    ctx.telegraph_ticks = 1;
    ctx.attack_ticks = 1;
    ctx.recovery_ticks = 1;
    ctx.attack_damage = 1;
    ctx.attack_width = 1;
    ctx.attack_height = 1;
    ctx.attack_filter = TargetFilter::Companion;
  }
}

}  // namespace

// =============================================================================
// Zombie
// =============================================================================
Zombie::Zombie(ObjectId id, Position pos) : AgentFSM(id, pos) {
  SetCadence({1, 0});  // Move every other turn
}

void Zombie::MoveTo(Position target, const BaseEnv& env) {
  // Check cadence - skip if not our turn to move
  if (!CanAct()) {
    SetIntention({MovementAction::Stay});
    return;
  }

  Position current = GetPosition();
  if (current == target) {
    SetIntention({MovementAction::Stay});
    return;
  }

  // Use A* pathfinding with optional RNG for tie-breaking on diagonals
  Pathfinder pathfinder(env.GetGrid());
  if (HasFSM()) {
    pathfinder.SetRng(GetFSMContext().rng);
  }
  auto path = pathfinder.FindPath(current, target);

  if (path.size() > 1) {
    // Path[0] is current position, path[1] is next step
    Position next = path[1];
    SetIntention({PositionToMovement(current, next)});
  } else {
    // No path found or already at target
    SetIntention({MovementAction::Stay});
  }
}

// =============================================================================
// Goblin
// =============================================================================
Goblin::Goblin(ObjectId id, Position pos) : AgentFSM(id, pos) {
  // No cadence - moves every turn (default empty cadence)
}

void Goblin::MoveTo(Position target, const BaseEnv& env) {
  Position current = GetPosition();
  if (current == target) {
    SetIntention({MovementAction::Stay});
    return;
  }

  // Use A* pathfinding with optional RNG for tie-breaking on diagonals
  Pathfinder pathfinder(env.GetGrid());
  if (HasFSM()) {
    pathfinder.SetRng(GetFSMContext().rng);
  }
  auto path = pathfinder.FindPath(current, target);

  if (path.size() > 1) {
    Position next = path[1];
    SetIntention({PositionToMovement(current, next)});
  } else {
    SetIntention({MovementAction::Stay});
  }
}

// =============================================================================
// Dragon
// =============================================================================
Dragon::Dragon(ObjectId id, Position pos) : AgentFSM(id, pos) {
  // No cadence - moves every turn
}

void Dragon::MoveTo(Position target, const BaseEnv& env) {
  Position current = GetPosition();
  if (current == target) {
    SetIntention({MovementAction::Stay});
    return;
  }

  // Flying - move directly toward target, ignoring walls
  int dr = target.row - current.row;
  int dc = target.col - current.col;

  // Prefer axis with larger delta
  MovementAction action = MovementAction::Stay;
  if (std::abs(dr) >= std::abs(dc)) {
    action = (dr > 0) ? MovementAction::Down : MovementAction::Up;
  } else {
    action = (dc > 0) ? MovementAction::Right : MovementAction::Left;
  }

  // Verify target cell is in bounds (even flying enemies can't leave the grid)
  Position next = ApplyMovement(current, action);
  if (!env.GetGrid().IsInBounds(next)) {
    // Try the other axis
    if (std::abs(dr) >= std::abs(dc)) {
      action = (dc > 0) ? MovementAction::Right
                        : (dc < 0) ? MovementAction::Left
                                   : MovementAction::Stay;
    } else {
      action = (dr > 0) ? MovementAction::Down
                        : (dr < 0) ? MovementAction::Up
                                   : MovementAction::Stay;
    }
  }

  SetIntention({action});
}

// =============================================================================
// Factory Functions
// =============================================================================
Zombie* CreateZombie(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng) {
  assert(ValidatePatrolPath(patrol_path) && "Patrol path must be orthogonal");

  auto* zombie = mgr.CreateActor<Zombie>(pos);
  FSMContext ctx;
  LoadFSMConfig(ctx, "zombie", patrol_path, rng, /*default_detection=*/3,
                /*default_lose_target=*/5);
  zombie->SetFSM(&PatrolState::Instance(), std::move(ctx));
  return zombie;
}

Goblin* CreateGoblin(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng) {
  assert(ValidatePatrolPath(patrol_path) && "Patrol path must be orthogonal");

  auto* goblin = mgr.CreateActor<Goblin>(pos);
  FSMContext ctx;
  LoadFSMConfig(ctx, "goblin", patrol_path, rng, /*default_detection=*/4,
                /*default_lose_target=*/6);
  goblin->SetFSM(&PatrolState::Instance(), std::move(ctx));
  return goblin;
}

Dragon* CreateDragon(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng) {
  assert(ValidatePatrolPath(patrol_path) && "Patrol path must be orthogonal");

  auto* dragon = mgr.CreateActor<Dragon>(pos);
  FSMContext ctx;
  LoadFSMConfig(ctx, "dragon", patrol_path, rng, /*default_detection=*/5,
                /*default_lose_target=*/8);
  dragon->SetFSM(&PatrolState::Instance(), std::move(ctx));
  return dragon;
}

// =============================================================================
// Patrol Path Validation
// =============================================================================
bool ValidatePatrolPath(const std::vector<Position>& path) {
  // Empty path or single point is valid
  if (path.size() <= 1) return true;

  // Check each segment for orthogonality
  for (size_t i = 1; i < path.size(); ++i) {
    int dr = std::abs(path[i].row - path[i - 1].row);
    int dc = std::abs(path[i].col - path[i - 1].col);

    // Segment is diagonal if both dr and dc are non-zero
    if (dr != 0 && dc != 0) {
      return false;
    }
  }

  return true;
}

}  // namespace companions
