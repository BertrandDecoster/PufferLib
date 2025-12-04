// Copyright 2024
// Agent configuration structures for data-driven enemy/NPC creation

#ifndef COMPANIONS_CORE_AGENT_CONFIG_H_
#define COMPANIONS_CORE_AGENT_CONFIG_H_

#include <string>
#include <vector>

#include "types.h"

namespace companions {

// =============================================================================
// TargetFilter - Who can be affected by an attack/effect
// =============================================================================
enum class TargetFilter {
  All,        // Affects all agents in area
  Companion,  // Only affects companions
  Enemy,      // Only affects enemies
  Neutral,    // Only affects neutral agents
};

// =============================================================================
// AttackArea - Defines the shape and size of an attack's effect area
// =============================================================================
struct AttackArea {
  int width = 1;   // Number of columns
  int height = 1;  // Number of rows
  // Area is centered on target position for odd dimensions
  // For even dimensions, extends right/down from target
};

// =============================================================================
// AttackConfig - Configuration for an agent's attack
// =============================================================================
struct AttackConfig {
  std::string name = "attack";
  int telegraph_ticks = 1;   // Ticks spent telegraphing before attack
  int attack_ticks = 1;      // Ticks the attack is active (usually 1)
  int recovery_ticks = 1;    // Ticks spent recovering after attack
  int damage = 1;            // Base damage dealt (negative = heal)
  AttackArea area;           // Size of attack area
  TargetFilter filter = TargetFilter::Companion;  // Who gets affected
};

// =============================================================================
// AgentConfig - Complete configuration for an agent type
// =============================================================================
struct AgentConfig {
  std::string type_name;           // e.g., "zombie", "goblin", "dragon"

  // Display
  char display_char = 'E';

  // Stats
  int max_health = 3;
  Faction faction = Faction::ENEMY;

  // Movement
  std::vector<int> cadence;        // Empty = every tick, {1,0} = every other tick
  bool flying = false;             // Can ignore walls

  // Detection
  int detection_range = 3;         // Manhattan distance to detect targets
  int lose_target_range = 5;       // Distance to lose target

  // Attack (optional - agents without attacks chase only)
  bool has_attack = false;
  AttackConfig attack;  // Legacy: embedded attack config

  // New: reference effect by name from effects.csv
  // If set, overrides embedded AttackConfig
  std::string attack_effect;  // e.g., "zombie_attack"
};

// =============================================================================
// AgentConfigRegistry - Stores loaded agent configurations
// =============================================================================
class AgentConfigRegistry {
 public:
  static AgentConfigRegistry& Instance();

  // Load configs from CSV file
  bool LoadFromCSV(const std::string& path);

  // Get config by type name (returns nullptr if not found)
  const AgentConfig* GetConfig(const std::string& type_name) const;

  // Register a config (for testing/programmatic setup)
  void RegisterConfig(AgentConfig config);

  // Clear all configs
  void Clear();

  // Get all registered type names
  std::vector<std::string> GetAllTypeNames() const;

 private:
  AgentConfigRegistry() = default;
  std::vector<AgentConfig> configs_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_AGENT_CONFIG_H_
