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

  // Status effects
  std::vector<StatusSnapshot> statuses;

  // FSM data (only for AgentFSM types)
  bool has_fsm = false;
  FSMSnapshot fsm;

  // Cadence (for AgentFSM)
  std::vector<int> cadence;
  int tick = 0;
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
