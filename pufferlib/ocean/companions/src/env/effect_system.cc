// Copyright 2024
// EffectSystem implementation

#include "effect_system.h"

#include <iostream>
#include <sstream>

#include "../core/agent_config.h"
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
    // Apply the effect on spawn since there's no telegraph
    active_effects_.push_back(effect);
    ApplyEffectModifiers(active_effects_.back());
    return;
  }

  // Set up loop counter
  effect.loops_remaining = config->loop;

  active_effects_.push_back(effect);
}

void EffectSystem::Tick() {
  // Process effects and collect finished ones for removal
  std::vector<size_t> to_remove;

  for (size_t i = 0; i < active_effects_.size(); ++i) {
    ActiveEffect& effect = active_effects_[i];

    if (effect.ticks_remaining > 0) {
      effect.ticks_remaining--;
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
        ApplyEffectModifiers(effect);
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
            ApplyEffectModifiers(effect);
          }
        } else if (effect.config->loop == -1) {
          // Infinite loop - restart
          if (effect.config->telegraph_ticks > 0) {
            effect.in_telegraph = true;
            effect.ticks_remaining = effect.config->telegraph_ticks;
          } else {
            effect.ticks_remaining = effect.config->active_ticks;
            ApplyEffectModifiers(effect);
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

void EffectSystem::ApplyEffectModifiers(const ActiveEffect& effect) {
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

  // Get rotated push direction
  int push_dx, push_dy;
  cfg.GetRotatedPush(effect.direction, push_dx, push_dy);

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
