// Copyright 2024
// Snapshot - Complete world state for save/restore
//
// Snapshots capture the full state of a game world, enabling:
// - Save/restore for curriculum learning (train task N from task N-1 checkpoints)
// - Cross-env loading (generate once, load into any compatible env type)
// - Deterministic replay and debugging

#ifndef COMPANIONS_CORE_SNAPSHOT_H_
#define COMPANIONS_CORE_SNAPSHOT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "annotations.h"
#include "cell.h"
#include "fsm/fsm_state.h"
#include "skill_config.h"
#include "types.h"

namespace companions {

// =============================================================================
// CellSnapshot - Serialized cell data
// =============================================================================
struct CellSnapshot {
  CellKind kind = CellKind::Floor;
  CellOrigin origin = CellOrigin::Default;
};

// =============================================================================
// StatusSnapshot - Serialized status effect
// =============================================================================
struct StatusSnapshot {
  int type = 0;      // StatusType as int
  int duration = 0;
};

// =============================================================================
// TagSnapshot - An opaque agent tag, by name (TagTable ids are per env)
// =============================================================================
struct TagSnapshot {
  std::string tag;
  int duration = kPermanentTag;  // Positive ticks left, or kPermanentTag
};

// =============================================================================
// CellTagSnapshot - A zone: a cell's tag, by name (snapshot version 4)
// =============================================================================
struct CellTagSnapshot {
  Position cell;
  std::string tag;
  int duration = kPermanentTag;  // Landed on agents: positive ticks, or kPermanentTag
};

// =============================================================================
// FSMSnapshot - Serialized FSM context (for AgentFSM)
// =============================================================================
struct FSMSnapshot {
  FSMStateType state_type = FSMStateType::None;  // Current FSM state
  int target_id = -1;                            // kInvalidObjectId if none
  std::vector<Position> patrol_path;
  int patrol_index = 0;
  bool patrol_forward = true;
  int detection_range = 3;
  int lose_target_range = 5;

  // RNG state for FSM decisions
  uint64_t rng_state = 0;
  uint64_t rng_inc = 0;

  // Attack runtime state (for mid-attack persistence)
  int attack_tick_counter = 0;
  Position attack_target_position;
  int attack_area_width = 1;
  int attack_area_height = 1;
  int attack_damage = 1;
  TargetFilter attack_filter = TargetFilter::Companion;

  // Attack configuration (snapshot version 3). Without it a restored agent
  // cannot attack: has_attack defaults to false.
  bool has_attack = false;
  std::string attack_effect;  // Effect name spawned on attack, e.g. "zombie_attack"
  int telegraph_ticks = 1;
  int attack_ticks = 1;
  int recovery_ticks = 1;
};

// =============================================================================
// AgentSnapshot - Serialized agent state
// =============================================================================
struct AgentSnapshot {
  int id = 0;
  int type = 0;              // ObjectType as int
  Position position;
  Position prev_position;    // For animation
  int health = 3;
  int max_health = 3;
  int agent_index = -1;
  int faction = 0;           // Faction as int
  int direction = 0;         // Direction as int (for Companions)
  int color = 0;             // ActorColor as int
  bool alive = true;
  // Concrete AgentFSM class ("Zombie", "Goblin", "Dragon"; empty = plain
  // AgentFSM). Restores class behaviour (pathfinding, flying) on load.
  // Snapshot version 3.
  std::string kind;

  // Status effects
  std::vector<StatusSnapshot> statuses;

  // FSM data (only for AgentFSM types)
  bool has_fsm = false;
  FSMSnapshot fsm;

  // Cadence (for AgentFSM)
  std::vector<int> cadence;
  int tick = 0;

  // Snapshot version 4. Opaque tags (any agent), and a companion's skill slots
  // (names; at most kMaxSkillSlots; a missing slot or "", which older files
  // wrote for an empty one, loads as kDefaultSkill) and
  // their cooldowns (missing = 0). Slots and cooldowns are empty for
  // non-companions.
  std::vector<TagSnapshot> tags;
  std::vector<std::string> skills;
  std::vector<int> cooldowns;
};

// =============================================================================
// EffectSnapshot - Serialized active effect
// =============================================================================
struct EffectSnapshot {
  std::string effect_name;        // Config name for registry lookup
  int target_type = 0;            // EffectTarget::Type as int
  Position target_cell;
  int target_actor_id = -1;
  std::vector<int> target_actors;
  int direction = 0;              // Direction as int
  int ticks_remaining = 0;
  bool in_telegraph = true;
  int loops_remaining = 0;
  int source_id = -1;
};

// =============================================================================
// Snapshot - Complete world state
// =============================================================================
struct Snapshot {
  // Grid dimensions
  int rows = 0;
  int cols = 0;

  // Grid cells (row-major order)
  std::vector<CellSnapshot> cells;

  // All agents
  std::vector<AgentSnapshot> agents;

  // Active effects
  std::vector<EffectSnapshot> effects;

  // Timing
  int tick = 0;
  int horizon = 100;

  // RNG state (for env's main RNG)
  uint64_t rng_state = 0;
  uint64_t rng_inc = 0;

  // D4 transform applied
  int d4_transform = 0;

  // Patrol path (for AggroEnv) - stored here even if no FSM agent,
  // so it can be used with specialized enemy types (Zombie/Goblin)
  std::vector<Position> patrol_path;

  // Semantic annotations (cell tags, agent tags). Separate from physical world
  // data so CellKind stays pure terrain. Added in v2; absent in v1 snapshots.
  std::vector<AnnotationSnapshot> annotations;

  // Snapshot version 4. The env's whole SkillBook (builtins included: a level
  // may retune them), but the fixed kDefaultSkill, which may not appear here.
  // Loading resets the book to the builtins, then defines these, so empty =
  // builtins only.
  std::vector<SkillConfig> skills;

  // Snapshot version 4. Zones, in the same frame as `cells` and the cell
  // annotations: on load, d4_transform moves them with the grid.
  std::vector<CellTagSnapshot> cell_tags;

  // ==========================================================================
  // Validation helpers
  // ==========================================================================

  // Check if snapshot has synchro cells
  bool HasSynchroCells() const;

  // Check if snapshot has target cell
  bool HasTargetCell() const;

  // Check if snapshot has patrol path (via FSM agents)
  bool HasPatrolPath() const;

  // Count cells of a given kind
  int CountCells(CellKind kind) const;

  // Throws std::runtime_error unless the v4 data is loadable: every skill
  // passes ValidateSkillConfig and none is kDefaultSkill; agent statuses are known StatusType values (2,
  // the removed Slowed, is rejected); agent and zone tag names non-empty with a
  // duration positive or kPermanentTag; tag and slot names of at most
  // kMaxNameLength bytes; zones inside the grid; at most kMaxSkillSlots slots /
  // cooldowns per agent, cooldowns >= 0. Messages name the skill, agent (index
  // and id) or zone cell. Slot names are not checked against `skills`: an
  // undefined skill loads and is simply unusable.
  void ValidateSkillsTagsZones() const;

  // ==========================================================================
  // Serialization (binary format)
  // ==========================================================================

  // Serialize to binary buffer
  std::vector<uint8_t> Serialize() const;

  // Deserialize from binary buffer
  static Snapshot Deserialize(const std::vector<uint8_t>& data);
};

}  // namespace companions

#endif  // COMPANIONS_CORE_SNAPSHOT_H_
