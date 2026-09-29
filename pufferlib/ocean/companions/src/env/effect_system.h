// Copyright 2024
// EffectSystem - handles spawning, planning, and applying effects

#ifndef COMPANIONS_ENV_EFFECT_SYSTEM_H_
#define COMPANIONS_ENV_EFFECT_SYSTEM_H_

#include <string>
#include <vector>

#include "../core/effect_config.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/types.h"

namespace companions {

// When an effect without a wind-up (telegraph_ticks 0) spawned between two
// steps activates (EffectSystem::SpawnEffect). A spawn during a step (an
// FSM's strike in PreStep) is always planned into that turn (BaseEnv).
enum class EffectTiming {
  // As it spawns: applied at once, its push moving at once, one agent after
  // the other, without landing a zone (a host primitive, e.g. "kill")
  Immediate,
  // With the next turn's effects: it waits (in its telegraph, 0 steps left:
  // a warning) and activates when the next Step plans its effects (PlanTurn),
  // applied in that turn's phases (DodgeEnv's hazards, spawned after a step)
  NextTurn,
};

// =============================================================================
// EffectSystem - manages active effects in the game
//
// Responsibilities:
// - Spawning new effects (with telegraph/active phases)
// - Planning each turn's applications (PlanTurn: timers, phases, loops)
// - Applying an application's damage, heal and status (ApplyHits), and a
//   spawn-time application at once (pushes included)
//
// A Step applies the planned applications in its phases (BaseEnv): the pushes
// are forced moves of its motion phase, the hits land after it.
//
// Dependencies:
// - ObjectManager* for accessing agents
// - Grid* for checking walkability during an immediate push
// =============================================================================
class EffectSystem {
 public:
  // Where an effect's damage and heals go. BaseEnv's: the turn's ledger
  // during a Step (the turn's end applies it), at once between two steps.
  // Without a sink: at once (Agent::TakeDamage / Heal). Each returns true
  // when the amount went into the turn's ledger (deferred), false when it
  // applied at once (or not at all).
  class HealthSink {
   public:
    virtual bool Hurt(Agent& agent, int amount) = 0;
    virtual bool Heal(Agent& agent, int amount) = 0;

   protected:
    ~HealthSink() = default;
  };

  // One application of an effect in a turn (PlanTurn): what it does and
  // where, fixed as the turn begins. A copy: the effect may end (or be
  // cancelled) before it applies.
  struct Application {
    const EffectConfig* config = nullptr;  // Into the registry
    ObjectId source = kInvalidObjectId;
    // Its area's centre: an actor target's cell as the turn began (a living
    // one; else the target cell), else the target cell
    Position centre;
    Direction direction = Direction::Up;
  };

  // Non-owning pointers to ObjectManager and Grid (owned by BaseEnv)
  EffectSystem(ObjectManager* object_manager, Grid* grid);

  // Copy semantics (for OpenSpiel State::Clone()). The turn's planned
  // applications are scratch: not copied (a copy starts with none).
  EffectSystem(const EffectSystem& other);
  EffectSystem& operator=(const EffectSystem& other);

  // Update pointers after BaseEnv copy (pointers change during copy)
  void UpdatePointers(ObjectManager* object_manager, Grid* grid);
  // Non-owning; copied like the pointers above (a copy's owner re-points it)
  void SetHealthSink(HealthSink* sink) { health_sink_ = sink; }

  // Spawn a new effect at a target location. With a telegraph it starts in
  // it (telegraph_ticks steps). Without one, by `timing`: Immediate applies
  // it at once (ApplyAtOnce: damage / heal, push, status on every agent it
  // reaches, one after the other; not capped), NextTurn leaves it in its
  // telegraph with 0 steps left, to activate at the next PlanTurn.
  void SpawnEffect(const std::string& effect_name, EffectTarget target,
                   Direction direction = Direction::Up,
                   ObjectId source_id = kInvalidObjectId,
                   EffectTiming timing = EffectTiming::Immediate);

  // The effects' part of a turn's intents phase (BaseEnv::Step, right after
  // PreStep: the strikes the FSMs spawned this turn are in). Every effect
  // advances one step, in order, and what applies this turn is planned into
  // GetPlanned() (replaced), nothing applied:
  //   - an effect still in its telegraph whose source agent is dead as the
  //     turn begins is removed first: a dead attacker's pending attacks never
  //     land;
  //   - its timer counts down; a telegraph that ends (0 steps left, a
  //     NextTurn spawn's included) activates: its active phase begins and it
  //     applies this turn;
  //   - a continuous effect (apply_every_tick) applies on each later turn of
  //     its active phase (once per turn: active_ticks applications in all);
  //   - an active phase that ends restarts a loop (a wind-up again, or
  //     straight into an active phase that applies this turn), unless its
  //     source is dead (a dead source's loop stops at its restart, wind-up or
  //     not); else the effect is finished and removed.
  // Phases already active when the source dies run their course. Effects
  // without a source (kInvalidObjectId) or whose source no longer exists are
  // kept. An effect applies at most once per turn. During a Step deaths wait
  // for the end of the turn: an attacker killed this turn is not dead yet, so
  // its strike activating this turn still lands (CancelDeadSources then
  // removes the rest).
  //
  // source_id is an ObjectId of this env. LoadSnapshot re-creates agents with
  // new ids and maps a snapshot effect's source_id (and its actor targets,
  // and FSM target_id) through the snapshot's agent ids; an id naming no
  // snapshot agent loads as kInvalidObjectId (a source-less effect).
  void PlanTurn();
  // This turn's applications, in effect order (the active effects' order)
  const std::vector<Application>& GetPlanned() const { return planned_; }
  // The turn is over (or aborted): no application left. No allocation.
  void ClearPlanned() { planned_.clear(); }

  // Whether `a` reaches `agent` where it stands now: affectable, not its
  // source, passing its filter, on its area
  bool Reaches(const Application& a, const Agent& agent) const;
  // The forced move of `a` on an agent standing on `cell` (one it reaches):
  // its push (rotated with its direction; radial: away from the centre, the
  // centre by its direction) times push_distance (negative: a pull, the
  // reverse). (0, 0) for none.
  void PushOffset(const Application& a, Position cell, int& dr, int& dc) const;
  // Its hits on `agent`: its damage or heal (the sink: during a Step, the
  // turn's ledger), then its status (during a Step it acts from the next
  // turn: statuses are read as a turn's intents are made)
  void ApplyHits(const Application& a, Agent& agent);

  // The end of a turn: removes every effect still in its telegraph phase
  // whose source is now dead (the turn's deaths), so a dead attacker's
  // pending strikes never land; its active loops stop at their next restart
  // (PlanTurn). No allocation.
  void CancelDeadSources();

  // Accessors
  const std::vector<ActiveEffect>& GetActiveEffects() const {
    return active_effects_;
  }
  void Clear() { active_effects_.clear(); }

  // Add a pre-constructed effect (for snapshot restoration)
  void AddEffect(ActiveEffect effect) { active_effects_.push_back(std::move(effect)); }

 public:
  // At most this many effect applications touch one agent in a turn (the
  // cap of the old sequential cascades, kept): BaseEnv counts them in effect
  // order, the planned pushes first (on whoever stands on their cells before
  // the forced moves), then the hits of the others; the 5th+ application on
  // an agent does nothing to it (no push, no damage, no status). Spawn-time
  // (Immediate) applications are not capped.
  static constexpr int kCascadeDepthLimit = 4;

 private:
  // The application of `effect` as the world is now (its centre resolved)
  Application Plan(const ActiveEffect& effect) const;

  // A spawn-time application (Immediate, between two steps): on every agent
  // it reaches, its damage / heal, its push (ApplyPush), its status
  void ApplyAtOnce(const ActiveEffect& effect);

  // Its push direction on an agent `rel_row`, `rel_col` from its centre
  // (unit cells: dx east, dy south)
  static void PushDirection(const Application& a, int rel_row, int rel_col, int& dx, int& dy);

  // Its damage or heal on `agent` (through the sink); true when deferred
  // into the turn's ledger
  bool HitHealth(const Application& a, Agent& agent);
  // Its status on `agent`; true when one was applied
  bool HitStatus(const Application& a, Agent& agent);
  // The log line of an application on `agent`
  void LogHits(const Application& a, const Agent& agent, int old_health, bool deferred,
               bool damaged, Position old_pos, bool statused) const;

  // True when the effect names a source that exists and is dead
  bool IsSourceDead(const ActiveEffect& effect) const;

  // Apply push to an agent (blocked by walls and other agents)
  void ApplyPush(Agent* agent, int dx, int dy, int distance);

  ObjectManager* object_manager_;  // Non-owning
  Grid* grid_;                     // Non-owning
  HealthSink* health_sink_ = nullptr;  // Non-owning; null: at once
  std::vector<ActiveEffect> active_effects_;
  // The turn's applications (PlanTurn); scratch, reused (no allocation once
  // grown), never copied
  std::vector<Application> planned_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_EFFECT_SYSTEM_H_
