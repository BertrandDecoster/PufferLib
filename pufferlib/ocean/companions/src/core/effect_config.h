// Copyright 2024
// Effect configuration for data-driven effects (attacks, hazards, abilities)

#ifndef COMPANIONS_CORE_EFFECT_CONFIG_H_
#define COMPANIONS_CORE_EFFECT_CONFIG_H_

#include <string>
#include <vector>

#include "types.h"

namespace companions {

// Forward declaration
enum class TargetFilter;

// =============================================================================
// EffectConfig - Template for effects loaded from effects.csv
//
// Effects can represent:
// - Enemy attacks (zombie_attack, dragon_breath)
// - Environmental hazards (fire_tower, wind_lever)
// - Companion abilities (stun_spell, heal_pulse)
// =============================================================================
struct EffectConfig {
  std::string name;  // Unique identifier, e.g., "zombie_attack"

  // === Timing ===
  int telegraph_ticks = 0;   // Warning ticks before activation
  int active_ticks = 1;      // Duration of effect when active
  int recovery_ticks = 0;    // Cooldown after active phase (for FSM attacks)
  int loop = 0;              // 0=once, N=repeat N+1 times, -1=forever

  // === Area (N×N grid, N is odd) ===
  // Stored as N*N entries describing which cells are affected
  // Center is application point, assumes NORTH direction in CSV
  // Values: 1 = affected, 0 = not affected
  std::vector<int> area;  // e.g., {0,1,0,1,1,1,0,1,0} for 3x3 cross
  TargetFilter filter;    // Who gets affected

  // === Modifiers (rotated by effect direction at runtime) ===
  int damage = 0;           // Health change (negative = heal)
  int push_dx = 0;          // Push direction X (relative to NORTH)
  int push_dy = 0;          // Push direction Y (relative to NORTH)
  int push_distance = 0;    // Cells to push (blocked by walls)
  std::string status_applied;  // "stunned", "slowed", "marked", or ""
  int status_duration = 0;     // Ticks the status lasts

  // When true, push_dx/push_dy are IGNORED for non-center cells: each
  // affected cell pushes its occupant outward (away from the center). The
  // center cell uses the effect's own Direction to decide where to shove.
  // Used by DodgeEnv's cross-shaped wind.
  bool radial_push = false;

  // When true, the effect applies its modifiers on *every* active tick,
  // not just the telegraph→active transition. Default (false) preserves
  // the single-shot behaviour attacks rely on; wind uses true so it keeps
  // blowing through anyone in its area for the whole `active_ticks` span.
  bool apply_every_tick = false;

  // === Visibility ===
  bool telegraph_visible = true;  // Show danger zone during telegraph

  // Helper: get area size (N for N×N grid)
  int GetAreaSize() const;

  // Helper: check if a relative position (from center) is affected
  // Takes into account direction rotation
  bool IsPositionAffected(int rel_row, int rel_col, Direction dir) const;

  // Helper: get rotated push direction
  void GetRotatedPush(Direction dir, int& out_dx, int& out_dy) const;
};

// =============================================================================
// EffectConfigRegistry - Singleton registry for effect configurations
// =============================================================================
class EffectConfigRegistry {
 public:
  static EffectConfigRegistry& Instance();

  // Load configs from CSV file
  bool LoadFromCSV(const std::string& path);

  // Get config by name (returns nullptr if not found)
  const EffectConfig* GetConfig(const std::string& name) const;

  // Register a config (for testing/programmatic setup)
  void RegisterConfig(EffectConfig config);

  // Clear all configs
  void Clear();

  // Get all registered effect names
  std::vector<std::string> GetAllNames() const;

 private:
  EffectConfigRegistry() = default;
  std::vector<EffectConfig> configs_;
};

// =============================================================================
// EffectTarget - Specifies where an effect applies
// =============================================================================
struct EffectTarget {
  enum class Type { Cell, Actor, ActorList };
  Type type = Type::Cell;
  Position cell;                    // For Type::Cell
  ObjectId actor_id = kInvalidObjectId;  // For Type::Actor
  std::vector<ObjectId> actors;     // For Type::ActorList

  // Factory methods
  static EffectTarget AtCell(Position pos);
  static EffectTarget OnActor(ObjectId id);
  static EffectTarget OnActors(std::vector<ObjectId> ids);
};

// =============================================================================
// ActiveEffect - Runtime instance of an effect
// =============================================================================
struct ActiveEffect {
  const EffectConfig* config = nullptr;  // Template from registry
  EffectTarget target;                   // Where effect applies
  Direction direction = Direction::Up;   // Effect facing (for rotation)
  int ticks_remaining = 0;               // Countdown timer
  bool in_telegraph = true;              // true = warning, false = active
  int loops_remaining = 0;               // How many more active phases
  ObjectId source_id = kInvalidObjectId; // Who created it

  // Check if effect is finished
  bool IsFinished() const;

  // Get current center position (resolves actor targets)
  Position GetCenter(const class ObjectManager& mgr) const;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_EFFECT_CONFIG_H_
