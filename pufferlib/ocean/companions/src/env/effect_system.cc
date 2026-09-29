// Copyright 2024
// EffectSystem implementation

#include "effect_system.h"

#include <algorithm>
#include <cstdlib>
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
      health_sink_(other.health_sink_),
      active_effects_(other.active_effects_) {}

EffectSystem& EffectSystem::operator=(const EffectSystem& other) {
  if (this != &other) {
    object_manager_ = other.object_manager_;
    grid_ = other.grid_;
    health_sink_ = other.health_sink_;
    active_effects_ = other.active_effects_;
    planned_.clear();  // Scratch: never copied
  }
  return *this;
}

void EffectSystem::UpdatePointers(ObjectManager* object_manager, Grid* grid) {
  object_manager_ = object_manager;
  grid_ = grid;
}

void EffectSystem::SpawnEffect(const std::string& effect_name,
                                EffectTarget target, Direction direction,
                                ObjectId source_id, EffectTiming timing) {
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
  effect.loops_remaining = config->loop;

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

  if (config->telegraph_ticks > 0) {
    // Start in telegraph phase
    effect.in_telegraph = true;
    effect.ticks_remaining = config->telegraph_ticks;
  } else if (timing == EffectTiming::NextTurn) {
    // No wind-up, for the next turn: a telegraph of 1 step, which the next
    // PlanTurn ends (1 -> 0) and so activates, as it would a telegraph-1
    // effect spawned between two steps
    effect.in_telegraph = true;
    effect.ticks_remaining = 1;
  } else {
    // Skip to active phase and apply immediately (a spawn-time application:
    // not capped)
    effect.in_telegraph = false;
    effect.ticks_remaining = config->active_ticks;
    active_effects_.push_back(effect);
    ApplyAtOnce(active_effects_.back());
    return;
  }

  active_effects_.push_back(effect);
}

void EffectSystem::PlanTurn() {
  // Each effect advances one step, in order; what applies this turn is
  // planned (a copy: the effect may be removed below). Finished effects are
  // compacted in place (no allocation).
  planned_.clear();
  size_t kept = 0;
  for (size_t i = 0; i < active_effects_.size(); ++i) {
    ActiveEffect& effect = active_effects_[i];
    bool remove = false;

    if (effect.in_telegraph && IsSourceDead(effect)) {
      // A dead attacker's pending (telegraphed) attack never lands. Active
      // phases run their course; a loop stops at its next restart (below).
      LOG_EFFECT("'" << (effect.config ? effect.config->name : std::string("?"))
                     << "': Cancelled (source " << effect.source_id << " is dead)");
      remove = true;
    } else {
      if (effect.ticks_remaining > 0) {
        effect.ticks_remaining--;
      }

      // Continuous effects (apply_every_tick) apply on every turn they
      // spend in the active phase, not just the transition: after the
      // decrement, still active and not at the end-of-phase branch below.
      // The transition turn applies too (a telegraph ending, below), so the
      // number of applications equals `active_ticks`, one per turn.
      if (effect.ticks_remaining > 0 && !effect.in_telegraph &&
          effect.config && effect.config->apply_every_tick) {
        planned_.push_back(Plan(effect));
      }

      // Check for phase transitions
      if (effect.ticks_remaining == 0) {
        if (effect.in_telegraph) {
          // Transition from telegraph to active phase: it applies this turn
          effect.in_telegraph = false;
          effect.ticks_remaining = effect.config->active_ticks;

          if (LOG_ENABLED(Effects)) {
            const Position pos = Plan(effect).centre;
            LOG_EFFECT("'" << effect.config->name << "' at (" << pos.row << ","
                           << pos.col << "): Telegraph -> Active");
          }
          planned_.push_back(Plan(effect));
        } else if ((effect.loops_remaining > 0 || effect.config->loop == -1) &&
                   IsSourceDead(effect)) {
          // A dead source's loop stops at its restart, wind-up or not (a
          // loop without telegraph would otherwise go straight on hitting).
          LOG_EFFECT("'" << effect.config->name << "': Loop cancelled (source "
                         << effect.source_id << " is dead)");
          remove = true;
        } else if (effect.loops_remaining > 0 || effect.config->loop == -1) {
          // Active phase complete: a loop restarts (a count, or forever)
          if (effect.loops_remaining > 0) {
            effect.loops_remaining--;
            LOG_EFFECT("'" << effect.config->name << "': Loop restart ("
                           << effect.loops_remaining << " remaining)");
          }
          if (effect.config->telegraph_ticks > 0) {
            effect.in_telegraph = true;
            effect.ticks_remaining = effect.config->telegraph_ticks;
          } else {
            effect.ticks_remaining = effect.config->active_ticks;
            planned_.push_back(Plan(effect));
          }
        } else {
          // Effect is finished (its application this turn, if any, stays planned)
          LOG_EFFECT("'" << effect.config->name << "': Completed");
          remove = true;
        }
      }
    }

    if (remove) continue;
    if (kept != i) active_effects_[kept] = std::move(effect);
    ++kept;
  }
  active_effects_.erase(active_effects_.begin() + static_cast<std::ptrdiff_t>(kept),
                        active_effects_.end());
}

void EffectSystem::CancelDeadSources() {
  active_effects_.erase(
      std::remove_if(active_effects_.begin(), active_effects_.end(),
                     [this](const ActiveEffect& effect) {
                       if (!effect.in_telegraph || !IsSourceDead(effect)) return false;
                       LOG_EFFECT("'" << (effect.config ? effect.config->name : std::string("?"))
                                      << "': Cancelled (source " << effect.source_id
                                      << " died this turn)");
                       return true;
                     }),
      active_effects_.end());
}

bool EffectSystem::IsSourceDead(const ActiveEffect& effect) const {
  if (effect.source_id == kInvalidObjectId) return false;
  const Actor* source = object_manager_->GetActor(effect.source_id);
  return source && !source->IsAlive();
}

EffectSystem::Application EffectSystem::Plan(const ActiveEffect& effect) const {
  Application a;
  a.config = effect.config;
  a.source = effect.source_id;
  a.direction = effect.direction;
  a.centre = effect.target.cell;
  // An actor-targeted effect centres on its living target's cell
  if (effect.target.type == EffectTarget::Type::Actor) {
    const Actor* target_actor = object_manager_->GetActor(effect.target.actor_id);
    if (target_actor && target_actor->IsAlive()) a.centre = target_actor->GetPosition();
  }
  return a;
}

bool EffectSystem::Reaches(const Application& a, const Agent& agent) const {
  if (!a.config || !agent.IsAffectable()) return false;
  // Never its source
  if (a.source != kInvalidObjectId && agent.GetId() == a.source) return false;
  switch (a.config->filter) {
    case TargetFilter::All:
      break;
    case TargetFilter::Companion:
      if (agent.GetFaction() != Faction::COMPANION) return false;
      break;
    case TargetFilter::Enemy:
      if (agent.GetFaction() != Faction::ENEMY) return false;
      break;
    case TargetFilter::Neutral:
      if (agent.GetFaction() != Faction::NEUTRAL) return false;
      break;
  }
  const Position p = agent.GetPosition();
  return a.config->IsPositionAffected(p.row - a.centre.row, p.col - a.centre.col, a.direction);
}

void EffectSystem::PushDirection(const Application& a, int rel_row, int rel_col, int& dx,
                                 int& dy) {
  // - Non-radial: the effect's facing-derived vector (GetRotatedPush).
  // - Radial + off the centre: the sign of the offset to the centre (pushes
  //   outward).
  // - Radial + centre: the effect's Direction straight to a cardinal push, so
  //   a Direction::Up spawn shoves the occupant north. GetRotatedPush is
  //   bypassed here: its base-vector convention is oriented for
  //   "wind-comes-from-N = push-south" semantics, which is not what "random
  //   direction at the centre" means here.
  const EffectConfig& cfg = *a.config;
  if (!cfg.radial_push) {
    cfg.GetRotatedPush(a.direction, dx, dy);
    return;
  }
  if (rel_row == 0 && rel_col == 0) {
    dx = dy = 0;
    switch (a.direction) {
      case Direction::Up:    dy = -1; break;
      case Direction::Down:  dy = 1;  break;
      case Direction::Left:  dx = -1; break;
      case Direction::Right: dx = 1;  break;
    }
    return;
  }
  dy = (rel_row > 0) ? 1 : (rel_row < 0 ? -1 : 0);
  dx = (rel_col > 0) ? 1 : (rel_col < 0 ? -1 : 0);
}

void EffectSystem::PushOffset(const Application& a, Position cell, int& dr, int& dc) const {
  dr = dc = 0;
  if (!a.config || a.config->push_distance == 0) return;
  int dx = 0, dy = 0;
  PushDirection(a, cell.row - a.centre.row, cell.col - a.centre.col, dx, dy);
  // A negative distance pulls: the reverse
  dr = dy * a.config->push_distance;
  dc = dx * a.config->push_distance;
}

bool EffectSystem::HitHealth(const Application& a, Agent& agent) {
  const int damage = a.config->damage;
  if (damage > 0) {
    if (health_sink_) return health_sink_->Hurt(agent, damage);
    agent.TakeDamage(damage);
  } else if (damage < 0) {
    if (health_sink_) return health_sink_->Heal(agent, -damage);
    agent.Heal(-damage);
  }
  return false;
}

bool EffectSystem::HitStatus(const Application& a, Agent& agent) {
  const EffectConfig& cfg = *a.config;
  if (cfg.status_applied.empty() || cfg.status_duration <= 0) return false;
  const StatusType status = StatusTypeFromString(cfg.status_applied);
  if (status == StatusType::None) return false;
  agent.ApplyStatus(status, cfg.status_duration);
  return true;
}

void EffectSystem::ApplyHits(const Application& a, Agent& agent) {
  if (!a.config) return;
  const int old_health = agent.GetHealth();
  const bool deferred = HitHealth(a, agent);
  const bool statused = HitStatus(a, agent);
  LogHits(a, agent, old_health, deferred, a.config->damage != 0, agent.GetPosition(), statused);
}

void EffectSystem::ApplyAtOnce(const ActiveEffect& effect) {
  if (!effect.config) return;
  const Application a = Plan(effect);
  const EffectConfig& cfg = *a.config;
  for (Agent* agent : object_manager_->GetAllAgents()) {
    if (!Reaches(a, *agent)) continue;
    const Position old_pos = agent->GetPosition();
    int dx = 0, dy = 0;
    PushDirection(a, old_pos.row - a.centre.row, old_pos.col - a.centre.col, dx, dy);
    const int old_health = agent->GetHealth();
    // Damage, push (negative distance = pull toward the source), status
    const bool deferred = HitHealth(a, *agent);
    if (cfg.push_distance != 0) ApplyPush(agent, dx, dy, cfg.push_distance);
    const bool statused = HitStatus(a, *agent);
    LogHits(a, *agent, old_health, deferred, cfg.damage != 0, old_pos, statused);
  }
}

void EffectSystem::LogHits(const Application& a, const Agent& agent, int old_health,
                           bool deferred, bool damaged, Position old_pos, bool statused) const {
  const bool pushed = agent.GetPosition() != old_pos;
  if (!LOG_ENABLED(Effects) || !(damaged || pushed || statused)) return;
  const EffectConfig& cfg = *a.config;
  std::ostringstream log_msg;
  log_msg << "Applied '" << cfg.name << "' to Agent " << agent.GetId();
  if (damaged) {
    // The effect's own amount: during a Step into the turn's ledger (applied
    // at the end of the turn), between two steps at once
    const int new_health = agent.GetHealth();
    log_msg << ": " << (cfg.damage > 0 ? "-" : "+") << std::abs(cfg.damage) << " HP";
    if (deferred) {
      log_msg << " (into this turn's total, applied at its end)";
    } else {
      log_msg << " (" << (new_health - old_health >= 0 ? "+" : "")
              << new_health - old_health << ": " << new_health << "/"
              << agent.GetMaxHealth() << " remaining)";
    }
    if (!agent.IsAlive()) {
      log_msg << " [KILLED]";
    } else if (agent.IsDowned()) {
      log_msg << " [DOWNED]";
    }
  }
  if (pushed) {
    const Position new_pos = agent.GetPosition();
    if (damaged) log_msg << ",";
    log_msg << " Pushed (" << old_pos.row << "," << old_pos.col << ") -> (" << new_pos.row
            << "," << new_pos.col << ")";
  }
  if (statused) {
    if (damaged || pushed) log_msg << ",";
    log_msg << " Status " << cfg.status_applied << " for " << cfg.status_duration << " ticks";
  }
  LOG_EFFECT(log_msg.str());
}

void EffectSystem::ApplyPush(Agent* agent, int dx, int dy, int distance) {
  if (!agent || !agent->IsAffectable() || distance == 0) return;

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
