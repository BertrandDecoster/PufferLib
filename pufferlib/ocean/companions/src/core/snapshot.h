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
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "annotations.h"
#include "cell.h"
#include "context_skill.h"
#include "fsm/fsm_state.h"
#include "reaction.h"
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
// CellTagSnapshot - A zone: a cell's tag, by name (snapshot version 4), and
// its fields (version 7; BaseEnv::CellTag)
// =============================================================================
// Each field is the cell's own when present, and the snapshot's zone table's
// for `tag` when absent (nullopt: Snapshot::CellZone; a tag the table does not
// define: the ZoneDef defaults). Absent is how a hand-written or generated
// level says "as the table says" ({row, col, tag} in JSON); files before v7
// carry the duration only (always present). SaveSnapshot writes every field,
// resolved, so a per-cell override, a zone mid-life (its remaining steps) and
// a cell created before its tag was redefined load as they were.
struct CellTagSnapshot {
  Position cell;
  std::string tag;
  std::optional<int> duration;  // Landed on agents: positive ticks, or kPermanentTag
  // Steps the zone still lasts, read between two steps (the steps to come),
  // or kPermanentTag
  std::optional<int> steps;
  std::optional<std::string> then;  // Its successor's tag; "" = none
  std::optional<int> damage;        // Per landing, >= 0
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

// True for the ObjectTypes (as int) built as a Companion: Companion, Player,
// NPCCompanion. Only they carry skill slots and go down (snapshot v5).
bool IsCompanionType(int object_type);

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
  // Concrete AgentFSM class, one of EnemyKinds() ("Zombie", "Goblin",
  // "Dragon"; empty = plain AgentFSM). Restores class behaviour (pathfinding,
  // flying) on load. Snapshot version 3.
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

  // Snapshot version 5: downs (companions only). A downed companion is alive
  // at 0 HP and has gone down at least once; its downs load as already
  // reported (BaseEnv::GetLastDowns).
  bool downed = false;
  int times_downed = 0;

  // Snapshot version 7 (any agent type): its weaknesses (P, S) and the tags
  // it is immune to (BaseEnv::SetWeaknesses / SetImmunities). Older files:
  // none.
  std::vector<TagWeakness> weak_to;
  std::vector<std::string> immune;
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

  // Snapshot version 5: the level's max downs (BaseEnv::SetMaxDowns, >= 1).
  int max_downs = kDefaultMaxDowns;

  // Snapshot version 6: the level's context skill rules (BaseEnv::
  // SetContextSkills). Absent (nullopt: older files, a JSON level without the
  // key) = DefaultContextSkills(), so every level has revive; present =
  // exactly these rules (empty = no override). SaveSnapshot always writes
  // them.
  std::optional<std::vector<ContextSkillRule>> context_skills;

  // Snapshot version 7: the level's combo rules as data. Older files (and a
  // JSON level without the keys): none, like a fresh env.
  // The zone table (BaseEnv::DefineZone), by tag: what a zone created by name
  // is (a successor, a reaction's zone_becomes, the host's SetCellTag by
  // name), and what a cell_tags entry's absent fields take. LoadSnapshot
  // sets it before the cells.
  std::map<std::string, ZoneDef> zones;
  std::vector<ReactionRule> reactions;      // BaseEnv::SetReactions, in level order
  std::vector<TagStatusRule> tag_statuses;  // BaseEnv::SetTagStatuses

  // The fields `zone` loads with: the table's for its tag (`zones`; the
  // ZoneDef defaults when it does not define it), overridden by those the
  // cell has.
  ZoneDef CellZone(const CellTagSnapshot& zone) const;

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
  // cooldowns per agent, cooldowns >= 0; every non-empty slot names a builtin
  // or one of `skills` (the book LoadSnapshot builds), so slots always hold a
  // real skill ("" is kDefaultSkill); an agent's kind is empty, or one of
  // EnemyKinds() on an AgentFSM agent; v5 downs: max_downs >= 1, downed /
  // times_downed only on a companion type (IsCompanionType), times_downed
  // >= 0, a downed agent at 0 HP with times_downed >= 1 and no statuses;
  // every agent's max_health >= 1; v6: the context skill rules (absent: the
  // default ones) pass ValidateContextSkills with that same book (so a level
  // may retune a rule's skill, but not give it a cooldown); v7: the zone
  // table passes ValidateZoneTable, each zone cell's present fields
  // ValidateZoneDef (a duration or steps of 0 or below -1, a negative damage,
  // an overlong then are rejected; any valid name may follow, defined or
  // not: cycles are legal), the reactions ValidateReactions, the tag
  // statuses ValidateTagStatuses (an unknown status is rejected), each
  // agent's weak_to / immune ValidateWeaknesses / ValidateImmunities.
  // Messages name the skill, agent (index and id), zone cell or rule
  // (context_skills[i], zones['tag'], reactions[i], tag_statuses[i],
  // weak_to[i], immune[i]).
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
