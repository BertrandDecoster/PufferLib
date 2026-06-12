// Copyright 2024
// EffectSystem implementation

#include "effect_system.h"

#include <iostream>
#include <sstream>

#include "../core/agent_config.h"
#include "../core/enum_strings.h"
#include "../core/game_logger.h"

namespace companions {

EffectSystem::EffectSystem(ObjectManager* object_manager, Grid* grid)
    : object_manager_(object_manager), grid_(grid) {}

EffectSystem::EffectSystem(const EffectSystem& other)
    : object_manager_(other.object_manager_),
      grid_(other.grid_),
      active_effects_(other.active_effects_) {}

EffectSystem& EffectSystem::operator=(const EffectSystem& other) {
  if (this != &other) {
    object_manager_ = other.object_manager_;
    grid_ = other.grid_;
    active_effects_ = other.active_effects_;
  }
  return *this;
}

void EffectSystem::UpdatePointers(ObjectManager* object_manager, Grid* grid) {
  object_manager_ = object_manager;
  grid_ = grid;
}

void EffectSystem::SpawnEffect(const std::string& effect_name,
                                EffectTarget target, Direction direction,
                                ObjectId source_id) {
  const EffectConfig* config =
      EffectConfigRegistry::Instance().GetConfig(effect_name);
  if (!config) {
    std::cerr << "WARNING: Unknown effect '" << effect_name << "' - effect not spawned\n";
    return;
  }

  ActiveEffect effect;
  effect.config = config;
  effect.target = target;
  effect.direction = direction;
  effect.source_id = source_id;

  // Log effect creation
  if (LOG_ENABLED(Effects)) {
    Position pos = target.cell;
    if (target.type == EffectTarget::Type::Actor) {
      const Actor* actor = object_manager_->GetActor(target.actor_id);
      if (actor) pos = actor->GetPosition();
    }
    std::string source_str;
    if (source_id != kInvalidObjectId) {
      source_str = " by Agent " + std::to_string(source_id);
    }
    LOG_EFFECT("Spawned '" << effect_name << "' at (" << pos.row << ","
                           << pos.col << ") dir=" << DirectionToString(direction)
                           << source_str);
  }

  // Start in telegraph phase if there are telegraph ticks
  if (config->telegraph_ticks > 0) {
    effect.in_telegraph = true;
    effect.ticks_remaining = config->telegraph_ticks;
  } else {
    // Skip to active phase and apply immediately
    effect.in_telegraph = false;
    effect.ticks_remaining = config->active_ticks;
    effect.loops_remaining = config->loop;
    // Apply the effect on spawn since there's no telegraph. One-shot
    // spawn-time application doesn't participate in cascade counting.
    active_effects_.push_back(effect);
    ApplyEffectModifiers(active_effects_.back(), /*apply_count=*/nullptr);
    return;
  }

  // Set up loop counter
  effect.loops_remaining = config->loop;

  active_effects_.push_back(effect);
}

void EffectSystem::Tick() {
  // Process effects and collect finished ones for removal.
  // apply_count tracks the cascade depth per agent across all effects
  // resolving within this single Tick(). When an agent has already been
  // touched kCascadeDepthLimit times, later effects this tick skip them.
  std::vector<size_t> to_remove;
  std::unordered_map<ObjectId, int> apply_count;

  for (size_t i = 0; i < active_effects_.size(); ++i) {
    ActiveEffect& effect = active_effects_[i];

    if (effect.ticks_remaining > 0) {
      effect.ticks_remaining--;
    }

    // Continuous effects (apply_every_tick) fire their modifiers on every
    // tick they spend in the active phase, not just the transition. We do
    // this after the decrement: if we're still in active and haven't hit
    // the end-of-phase branch below, apply again. The transition tick also
    // applies (via the ticks_remaining == 0 branch), so the total number of
    // applications equals `active_ticks` for continuous effects.
    if (effect.ticks_remaining > 0 && !effect.in_telegraph &&
        effect.config && effect.config->apply_every_tick) {
      ApplyEffectModifiers(effect, &apply_count);
    }

    // Check for phase transitions
    if (effect.ticks_remaining == 0) {
      if (effect.in_telegraph) {
        // Transition from telegraph to active phase
        effect.in_telegraph = false;
        effect.ticks_remaining = effect.config->active_ticks;

        // Log phase transition
        if (LOG_ENABLED(Effects)) {
          Position pos = effect.target.cell;
          if (effect.target.type == EffectTarget::Type::Actor) {
            const Actor* actor =
                object_manager_->GetActor(effect.target.actor_id);
            if (actor) pos = actor->GetPosition();
          }
          LOG_EFFECT("'" << effect.config->name << "' at (" << pos.row << ","
                         << pos.col << "): Telegraph -> Active");
        }

        // Apply effect modifiers when entering active phase
        ApplyEffectModifiers(effect, &apply_count);
      } else {
        // Active phase complete - check for looping
        if (effect.loops_remaining > 0) {
          // Decrement loop counter and restart
          effect.loops_remaining--;

          LOG_EFFECT("'" << effect.config->name << "': Loop restart ("
                         << effect.loops_remaining << " remaining)");

          if (effect.config->telegraph_ticks > 0) {
            effect.in_telegraph = true;
            effect.ticks_remaining = effect.config->telegraph_ticks;
          } else {
            effect.ticks_remaining = effect.config->active_ticks;
            ApplyEffectModifiers(effect, &apply_count);
          }
        } else if (effect.config->loop == -1) {
          // Infinite loop - restart
          if (effect.config->telegraph_ticks > 0) {
            effect.in_telegraph = true;
            effect.ticks_remaining = effect.config->telegraph_ticks;
          } else {
            effect.ticks_remaining = effect.config->active_ticks;
            ApplyEffectModifiers(effect, &apply_count);
          }
        } else {
          // Effect is finished
          LOG_EFFECT("'" << effect.config->name << "': Completed");
          to_remove.push_back(i);
        }
      }
    }
  }

  // Remove finished effects (iterate backwards to maintain indices)
  for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
    active_effects_.erase(active_effects_.begin() +
                          static_cast<std::ptrdiff_t>(*it));
  }
}

void EffectSystem::ApplyEffectModifiers(
    const ActiveEffect& effect,
    std::unordered_map<ObjectId, int>* apply_count) {
  if (!effect.config) return;

  const EffectConfig& cfg = *effect.config;
  Position center = effect.target.cell;

  // For actor-targeted effects, resolve center position
  if (effect.target.type == EffectTarget::Type::Actor) {
    const Actor* target_actor =
        object_manager_->GetActor(effect.target.actor_id);
    if (target_actor && target_actor->IsAlive()) {
      center = target_actor->GetPosition();
    }
  }

  (void)cfg.GetAreaSize();  // Area size already used by IsPositionAffected

  // Default push direction from the effect's facing. `radial_push` effects
  // override this per-agent below (each cardinal cell of the area shoves
  // outward; the center uses the facing-derived direction).
  int default_push_dx = 0;
  int default_push_dy = 0;
  cfg.GetRotatedPush(effect.direction, default_push_dx, default_push_dy);

  // Find all agents affected
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (!agent->IsAlive()) continue;

    // Skip source agent if present
    if (effect.source_id != kInvalidObjectId &&
        agent->GetId() == effect.source_id) {
      continue;
    }

    Position agent_pos = agent->GetPosition();

    // Calculate relative position from center
    int rel_row = agent_pos.row - center.row;
    int rel_col = agent_pos.col - center.col;

    // Check if position is affected by the area pattern
    if (!cfg.IsPositionAffected(rel_row, rel_col, effect.direction)) {
      continue;
    }

    // Resolve the push vector for THIS agent.
    // - Non-radial: the effect's facing-derived vector (GetRotatedPush).
    // - Radial + cardinal: sign of the offset to the centre (pushes outward).
    // - Radial + centre: map the effect's Direction straight to a cardinal
    //   push so a Direction::Up spawn actually shoves the occupant north.
    //   We bypass GetRotatedPush here because its base-vector convention is
    //   oriented for "wind-comes-from-N = push-south" semantics, which is
    //   not what "random direction at the centre" means here.
    int push_dx = default_push_dx;
    int push_dy = default_push_dy;
    if (cfg.radial_push) {
      if (rel_row == 0 && rel_col == 0) {
        switch (effect.direction) {
          case Direction::Up:    push_dx = 0;  push_dy = -1; break;
          case Direction::Down:  push_dx = 0;  push_dy = 1;  break;
          case Direction::Left:  push_dx = -1; push_dy = 0;  break;
          case Direction::Right: push_dx = 1;  push_dy = 0;  break;
        }
      } else {
        push_dy = (rel_row > 0) ? 1 : (rel_row < 0 ? -1 : 0);
        push_dx = (rel_col > 0) ? 1 : (rel_col < 0 ? -1 : 0);
      }
    }

    // Check faction filter
    bool should_affect = false;
    switch (cfg.filter) {
      case TargetFilter::All:
        should_affect = true;
        break;
      case TargetFilter::Companion:
        should_affect = (agent->GetFaction() == Faction::COMPANION);
        break;
      case TargetFilter::Enemy:
        should_affect = (agent->GetFaction() == Faction::ENEMY);
        break;
      case TargetFilter::Neutral:
        should_affect = (agent->GetFaction() == Faction::NEUTRAL);
        break;
    }

    if (!should_affect) continue;

    // Cascade depth cap: each agent can be touched by at most
    // kCascadeDepthLimit effects in a single Tick(). After the cap, later
    // effects this tick see the agent but no-op on them (no damage, no
    // push, no status). Non-tick contexts (spawn-time instant effects)
    // pass nullptr and bypass the cap.
    if (apply_count != nullptr) {
      int already = 0;
      auto it = apply_count->find(agent->GetId());
      if (it != apply_count->end()) already = it->second;
      if (already >= kCascadeDepthLimit) continue;
      (*apply_count)[agent->GetId()] = already + 1;
    }

    // Track what we apply for logging
    bool applied_damage = false;
    bool applied_push = false;
    bool applied_status = false;
    int old_health = agent->GetHealth();
    Position old_pos = agent->GetPosition();

    // Apply damage
    if (cfg.damage != 0) {
      if (cfg.damage > 0) {
        agent->TakeDamage(cfg.damage);
      } else {
        agent->Heal(-cfg.damage);
      }
      applied_damage = true;
    }

    // Apply push/pull (negative distance = pull toward source)
    if (cfg.push_distance != 0) {
      ApplyPush(agent, push_dx, push_dy, cfg.push_distance);
      if (agent->GetPosition() != old_pos) {
        applied_push = true;
      }
    }

    // Apply status effect
    if (!cfg.status_applied.empty() && cfg.status_duration > 0) {
      StatusType status = StatusTypeFromString(cfg.status_applied);
      if (status != StatusType::None) {
        agent->ApplyStatus(status, cfg.status_duration);
        applied_status = true;
      }
    }

    // Log per-actor effect application (only non-default values)
    if (LOG_ENABLED(Effects) &&
        (applied_damage || applied_push || applied_status)) {
      std::ostringstream log_msg;
      log_msg << "Applied '" << cfg.name << "' to Agent " << agent->GetId();

      if (applied_damage) {
        int new_health = agent->GetHealth();
        int change = new_health - old_health;
        log_msg << ": " << (change >= 0 ? "+" : "") << change << " HP ("
                << new_health << "/" << agent->GetMaxHealth() << " remaining)";
        if (!agent->IsAlive()) {
          log_msg << " [KILLED]";
        }
      }

      if (applied_push) {
        Position new_pos = agent->GetPosition();
        if (applied_damage) log_msg << ",";
        log_msg << " Pushed (" << old_pos.row << "," << old_pos.col << ") -> ("
                << new_pos.row << "," << new_pos.col << ")";
      }

      if (applied_status) {
        if (applied_damage || applied_push) log_msg << ",";
        log_msg << " Status " << cfg.status_applied << " for "
                << cfg.status_duration << " ticks";
      }

      LOG_EFFECT(log_msg.str());
    }
  }
}

void EffectSystem::ApplyPush(Agent* agent, int dx, int dy, int distance) {
  if (!agent || !agent->IsAlive() || distance == 0) return;

  // Negative distance = pull (reverse direction)
  if (distance < 0) {
    dx = -dx;
    dy = -dy;
    distance = -distance;
  }

  Position pos = agent->GetPosition();

  // Push one cell at a time, stopping at walls
  for (int i = 0; i < distance; ++i) {
    Position next = {pos.row + dy, pos.col + dx};

    // Check if next position is valid
    if (!grid_->IsInBounds(next) || !grid_->IsWalkable(next)) {
      break;  // Blocked by wall
    }

    // Check if next position is occupied by another agent
    const Actor* occupant = object_manager_->GetActorAt(next);
    if (occupant && occupant->IsAlive() && occupant->GetId() != agent->GetId()) {
      break;  // Blocked by another agent
    }

    // Move to next position
    pos = next;
  }

  // Apply final position if it changed
  if (pos != agent->GetPosition()) {
    object_manager_->UpdatePosition(agent->GetId(), pos);
  }
}

}  // namespace companions
