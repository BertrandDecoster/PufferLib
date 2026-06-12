// Copyright 2024
// Semantic annotation store. Separates task-semantic tags (SynchroGoal,
// SkillGiver, Room, ...) from physical world data (CellKind, Agent, Grid).
//
// Annotations are key-value records attached to either a cell position or an
// agent id. They are typically placed by a TaskLens during Activate() and
// removed on Deactivate() via owner_lens_id; annotations with
// owner_lens_id == -1 are persistent (e.g. placed by the map generator or the
// HTN planner and intended to survive lens swaps).

#ifndef COMPANIONS_CORE_ANNOTATIONS_H_
#define COMPANIONS_CORE_ANNOTATIONS_H_

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "types.h"

namespace companions {

// =============================================================================
// SemanticTag - enumerated task-level tag names.
// Extending this enum is a minor C++ ABI change (DLL consumers see ints).
// Keep symmetric with SEMANTIC_TAG_NAMES in htn_bridge.py.
// =============================================================================
enum class SemanticTag : uint16_t {
  // Cell-role tags placed by the HTN planner / task lenses.
  SynchroGoal = 0,   // All companions must occupy this cell.
  AggroTarget = 1,   // Lure an enemy to this cell.
  QuestPickup = 2,   // Item pickup location.
  SafeZone = 3,      // Retreat destination for dodge-style tasks.

  // Agent-role tags.
  TargetMob = 4,     // HTN plan marks this agent as the objective.
  SkillGiver = 5,    // Defeating grants a skill (params: "skill").
  Escort = 6,        // NPC to protect.
  HtnName = 7,       // Stable string handle for HTN facts (params: "name").

  // Cell-grouping tag (replaces hardcoded LevelLayout.rooms in htn_bridge.py).
  Room = 8,          // Cell belongs to a named room (params: "room").

  _Count,            // Sentinel, not a real tag.
};

// =============================================================================
// AnnotationTarget, AnnotationKey
// =============================================================================
enum class AnnotationTarget : uint8_t { Cell = 0, Agent = 1 };

struct AnnotationKey {
  AnnotationTarget target = AnnotationTarget::Cell;
  Position pos{-1, -1};               // Used when target == Cell.
  ObjectId agent_id = kInvalidObjectId;  // Used when target == Agent.

  bool operator==(const AnnotationKey& other) const {
    if (target != other.target) return false;
    if (target == AnnotationTarget::Cell) return pos == other.pos;
    return agent_id == other.agent_id;
  }
  bool operator!=(const AnnotationKey& other) const { return !(*this == other); }
};

struct AnnotationKeyHash {
  std::size_t operator()(const AnnotationKey& k) const {
    std::size_t h = std::hash<int>()(static_cast<int>(k.target));
    if (k.target == AnnotationTarget::Cell) {
      h ^= (std::hash<int>()(k.pos.row) << 1);
      h ^= (std::hash<int>()(k.pos.col) << 3);
    } else {
      h ^= (std::hash<int>()(k.agent_id) << 1);
    }
    return h;
  }
};

// =============================================================================
// Annotation
// =============================================================================
struct Annotation {
  SemanticTag tag = SemanticTag::SynchroGoal;
  std::unordered_map<std::string, std::string> params;
  int32_t owner_lens_id = -1;   // -1 means persistent (not auto-removed on lens swap).
};

// =============================================================================
// AnnotationSnapshot - serializable form of a single annotation.
// Goes into Snapshot::annotations for round-trip through save/load.
// =============================================================================
struct AnnotationSnapshot {
  uint8_t target_type = 0;   // 0 = Cell, 1 = Agent.
  Position pos{-1, -1};
  ObjectId agent_id = kInvalidObjectId;
  SemanticTag tag = SemanticTag::SynchroGoal;
  std::vector<std::pair<std::string, std::string>> params;
  int32_t owner_lens_id = -1;

  // Single-source field list (see snapshot.h). Order is the v2 binary wire
  // layout — do not reorder. The JSON form of annotations is hand-written
  // (AnnotationSnapshotToJson in snapshot_json.cc) because its shape is
  // conditional: "target" is a string, exactly one of pos/agent_id is
  // emitted, and params is a JSON object. This list still drives the binary
  // format and the introspection/round-trip tests.
  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("target", self.target_type);  // uint8_t
    v("pos", self.pos);
    v("agent_id", self.agent_id);
    v("tag", self.tag);             // binary: uint16_t
    v("owner_lens_id", self.owner_lens_id);
    v("params", self.params);
  }
};

// =============================================================================
// AnnotationStore - owns all semantic tags for a BaseEnv.
// Thread compat: single-threaded, like the rest of the env.
// =============================================================================
class AnnotationStore {
 public:
  AnnotationStore() = default;
  AnnotationStore(const AnnotationStore&) = default;
  AnnotationStore(AnnotationStore&&) = default;
  AnnotationStore& operator=(const AnnotationStore&) = default;
  AnnotationStore& operator=(AnnotationStore&&) = default;

  // Mutation ---------------------------------------------------------------
  void Add(AnnotationKey key, Annotation ann);
  void RemoveByOwner(int32_t lens_id);
  void RemoveByKey(AnnotationKey key, SemanticTag tag);
  void Clear();

  // Rewrite the Position of every Cell-target entry via `func`. Agent-target
  // entries are keyed by ObjectId and are left untouched. Used by
  // BaseEnv::ApplyD4Transform to keep annotation positions in the same
  // frame as the grid and actors after a symmetry rotation.
  void TransformCellPositions(
      const std::function<Position(Position)>& func);

  // Query ------------------------------------------------------------------
  // All annotations attached to `key`. Pointers remain valid until any
  // mutating call on this store.
  std::vector<const Annotation*> Get(AnnotationKey key) const;
  bool HasTag(AnnotationKey key, SemanticTag tag) const;

  // Tag-indexed lookups. Order is unspecified but stable between mutations.
  std::vector<Position> FindCellsWithTag(SemanticTag tag) const;
  std::vector<ObjectId> FindAgentsWithTag(SemanticTag tag) const;

  std::size_t Size() const { return entries_.size(); }
  bool Empty() const { return entries_.empty(); }

  // Snapshot round-trip ----------------------------------------------------
  std::vector<AnnotationSnapshot> Serialize() const;
  void Deserialize(const std::vector<AnnotationSnapshot>& s);

#ifndef NDEBUG
  // Debug-only paranoid check: rebuild tag indices from scratch and compare
  // to the cached ones. Tests call this after random mutation sequences to
  // catch drift if a mutator ever forgets to bump mutation_version_.
  bool DebugCacheInvariantHolds() const;
#endif

 private:
  // Storage: one entry per (key, tag) pair. Multimap would work but the
  // explicit vector keeps iteration order deterministic for snapshot output.
  struct Entry {
    AnnotationKey key;
    Annotation ann;
  };
  std::vector<Entry> entries_;

  // Version-counter + lazy-rebuild cache of tag-indexed lookups. Mutators
  // bump mutation_version_; RebuildCacheIfStale() rebuilds when queried.
  // See .claude/reviews/companions-audit-2026-04-23.md F5/F6.
  static constexpr std::size_t kTagCount =
      static_cast<std::size_t>(SemanticTag::_Count);
  mutable uint32_t mutation_version_ = 1;  // starts ahead of cache_version_ to force first build
  mutable uint32_t cache_version_ = 0;
  mutable std::array<std::vector<Position>, kTagCount> cells_by_tag_;
  mutable std::array<std::unordered_set<Position, PositionHash>, kTagCount>
      cell_set_by_tag_;
  mutable std::array<std::vector<ObjectId>, kTagCount> agents_by_tag_;

  void RebuildCacheIfStale() const;
};

// =============================================================================
// SemanticTagToString - stable string names (keep in sync with htn_bridge.py).
// =============================================================================
std::string SemanticTagToString(SemanticTag tag);

}  // namespace companions

#endif  // COMPANIONS_CORE_ANNOTATIONS_H_
