// Copyright 2024
// EffectSystem - handles spawning, ticking, and applying effects

#ifndef COMPANIONS_ENV_EFFECT_SYSTEM_H_
#define COMPANIONS_ENV_EFFECT_SYSTEM_H_

#include <string>
#include <vector>

#include "../core/effect_config.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/types.h"

namespace companions {

// =============================================================================
// EffectSystem - manages active effects in the game
//
// Responsibilities:
// - Spawning new effects (with telegraph/active phases)
// - Ticking effects each game step
// - Applying damage, push, and status modifiers
//
// Dependencies:
// - ObjectManager* for accessing agents
// - Grid* for checking walkability during push
// =============================================================================
class EffectSystem {
 public:
  // Non-owning pointers to ObjectManager and Grid (owned by BaseEnv)
  EffectSystem(ObjectManager* object_manager, Grid* grid);

  // Copy semantics (for OpenSpiel State::Clone())
  EffectSystem(const EffectSystem& other);
  EffectSystem& operator=(const EffectSystem& other);

  // Update pointers after BaseEnv copy (pointers change during copy)
  void UpdatePointers(ObjectManager* object_manager, Grid* grid);

  // Spawn a new effect at a target location
  void SpawnEffect(const std::string& effect_name, EffectTarget target,
                   Direction direction = Direction::Up,
                   ObjectId source_id = kInvalidObjectId);

  // Tick all active effects (advance timers, apply damage/push on phase change)
  void Tick();

  // Accessors
  const std::vector<ActiveEffect>& GetActiveEffects() const {
    return active_effects_;
  }
  void Clear() { active_effects_.clear(); }

 private:
  // Apply effect modifiers (damage, push, status) to targets in area
  void ApplyEffectModifiers(const ActiveEffect& effect);

  // Apply push to an agent (blocked by walls and other agents)
  void ApplyPush(Agent* agent, int dx, int dy, int distance);

  ObjectManager* object_manager_;  // Non-owning
  Grid* grid_;                     // Non-owning
  std::vector<ActiveEffect> active_effects_;
};

}  // namespace companions

#endif  // COMPANIONS_ENV_EFFECT_SYSTEM_H_
