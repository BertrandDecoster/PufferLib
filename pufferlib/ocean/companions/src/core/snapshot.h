// Copyright 2024
// Snapshot - Complete world state for save/restore
//
// Snapshots capture the full state of a game world, enabling:
// - Save/restore for curriculum learning (train task N from task N-1 checkpoints)
// - Cross-env loading (generate once, load into any compatible env type)
// - Deterministic replay and debugging
//
// SINGLE-SOURCE SERIALIZATION: every snapshot struct lists its fields exactly
// once in a static VisitFields() template. The binary writer/reader
// (snapshot.cc) and the JSON writer/reader (snapshot_json.cc) are four
// generic visitors over that one list, so adding a field is one line here
// (plus regenerating golden fixtures under a version bump if the wire format
// changes). The visitor signature is:
//
//   v(const char* serialized_name, field [, bool json_optional])
//
// - serialized_name is the JSON key; dotted names ("grid.rows") nest. The
//   binary format ignores names and relies purely on visitation order, which
//   therefore MUST NOT be reordered (it is the v2 wire layout).
// - json_optional=true means the JSON reader tolerates a missing key
//   (leaves the default); binary always reads/writes the field.

#ifndef COMPANIONS_CORE_SNAPSHOT_H_
#define COMPANIONS_CORE_SNAPSHOT_H_

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "annotations.h"
#include "cell.h"
#include "fsm/fsm_state.h"
#include "types.h"

namespace companions {

// Enums whose definitions live in object.h (not included here to keep
// snapshot.h light); only the names are needed as EnumFieldRef tags.
enum class ObjectType;
enum class StatusType;
enum class ActorColor;

// =============================================================================
// Field-visitor support
// =============================================================================

// Marks an int-typed struct field that logically holds enum E. Binary
// serializes the raw int (historical layout); JSON serializes the enum's
// string name via the converters in snapshot_json.cc.
template <typename E, typename IntT>
struct EnumFieldRef {
  IntT& value;
};

template <typename E, typename IntT>
EnumFieldRef<E, IntT> EnumField(IntT& value) {
  return EnumFieldRef<E, IntT>{value};
}

// Marks the (has_fsm, fsm) pair of AgentSnapshot. Binary writes the bool then
// conditionally the struct; JSON writes the FSM object or null (no "has_fsm"
// key — its truth lives in the null-ness of "fsm").
template <typename BoolT, typename FsmT>
struct GuardedFsmRef {
  BoolT& has;
  FsmT& fsm;
};

template <typename BoolT, typename FsmT>
GuardedFsmRef<BoolT, FsmT> GuardedFsm(BoolT& has, FsmT& fsm) {
  return GuardedFsmRef<BoolT, FsmT>{has, fsm};
}

namespace detail {
// Probe visitor used only to detect the presence of VisitFields.
struct VisitProbe {
  template <typename... Args>
  void operator()(Args&&...) const {}
};
}  // namespace detail

// Trait: does T expose static VisitFields(Self&, V&&)?
template <typename T, typename = void>
struct HasVisitFields : std::false_type {};
template <typename T>
struct HasVisitFields<T, std::void_t<decltype(T::VisitFields(
                             std::declval<T&>(),
                             std::declval<detail::VisitProbe&>()))>>
    : std::true_type {};
template <typename T>
inline constexpr bool kHasVisitFields = HasVisitFields<T>::value;

// Drift guard: a struct member added without updating VisitFields would
// otherwise compile silently and be dropped by every serializer. sizeof is
// the only compile-time signal that the struct changed. Padding and stdlib
// layout (std::string/std::vector sizes) make the value platform-specific,
// so the assert is gated to the primary dev toolchain (macOS arm64 /
// libc++); other platforms are covered by the field-count checks in
// tests/test_snapshot_fields.cc and by the golden-fixture tests.
#if defined(__APPLE__) && defined(__aarch64__)
#define COMPANIONS_SNAPSHOT_SIZE_GUARD(Struct, expected)                  \
  static_assert(sizeof(Struct) == (expected),                            \
                #Struct                                                   \
                " changed: update VisitFields, the golden fixtures "     \
                "(under a version bump), and this assert")
#else
#define COMPANIONS_SNAPSHOT_SIZE_GUARD(Struct, expected) \
  static_assert(true, "size guard active on macOS arm64 only")
#endif

// =============================================================================
// CellSnapshot - Serialized cell data
// =============================================================================
// No VisitFields: cells are serialized by dedicated vector<CellSnapshot>
// policies in the visitors because the two formats disagree structurally
// (binary: flat kind/origin int pairs; JSON: objects with explicit row/col
// coordinates derived from the grid dimensions).
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

  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("status_type", EnumField<StatusType>(self.type));
    v("duration", self.duration);
  }
};
COMPANIONS_SNAPSHOT_SIZE_GUARD(StatusSnapshot, 8);

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

  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("state_type", self.state_type);  // binary: uint8_t; JSON: string name
    v("target_id", self.target_id);
    v("patrol_path", self.patrol_path);
    v("patrol_index", self.patrol_index);
    v("patrol_forward", self.patrol_forward);
    v("detection_range", self.detection_range);
    v("lose_target_range", self.lose_target_range);
    v("rng_state", self.rng_state);
    v("rng_inc", self.rng_inc);
    // Attack runtime state: json_optional for older JSON payloads that
    // predate mid-attack persistence.
    v("attack_tick_counter", self.attack_tick_counter, true);
    v("attack_target_position", self.attack_target_position, true);
    v("attack_area_width", self.attack_area_width, true);
    v("attack_area_height", self.attack_area_height, true);
    v("attack_damage", self.attack_damage, true);
    v("attack_filter", self.attack_filter, true);  // int in both formats
  }
};
COMPANIONS_SNAPSHOT_SIZE_GUARD(FSMSnapshot, 96);

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

  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("id", self.id);
    v("agent_type", EnumField<ObjectType>(self.type));
    v("position", self.position);
    v("prev_position", self.prev_position);
    v("health", self.health);
    v("max_health", self.max_health);
    v("agent_index", self.agent_index);
    v("faction", EnumField<Faction>(self.faction));
    v("direction", EnumField<Direction>(self.direction));
    v("color", EnumField<ActorColor>(self.color));
    v("alive", self.alive);
    v("statuses", self.statuses);
    v("fsm", GuardedFsm(self.has_fsm, self.fsm));
    v("cadence", self.cadence);
    v("tick", self.tick);
  }
};
COMPANIONS_SNAPSHOT_SIZE_GUARD(AgentSnapshot, 216);

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

  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("effect_name", self.effect_name);
    v("target_type", self.target_type);
    v("target_cell", self.target_cell);
    v("target_actor_id", self.target_actor_id);
    v("target_actors", self.target_actors);
    v("direction", EnumField<Direction>(self.direction));
    v("ticks_remaining", self.ticks_remaining);
    v("in_telegraph", self.in_telegraph);
    v("loops_remaining", self.loops_remaining);
    v("source_id", self.source_id);
  }
};
COMPANIONS_SNAPSHOT_SIZE_GUARD(EffectSnapshot, 88);

COMPANIONS_SNAPSHOT_SIZE_GUARD(AnnotationSnapshot, 56);

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

  // Visitation order is the v2 binary wire layout — do not reorder.
  // The magic number and version live outside the field list (written by
  // Serialize / SnapshotToJson directly).
  template <class Self, class V>
  static void VisitFields(Self& self, V&& v) {
    v("grid.rows", self.rows);
    v("grid.cols", self.cols);
    v("grid.cells", self.cells);
    v("agents", self.agents);
    v("effects", self.effects);
    v("tick", self.tick);
    v("horizon", self.horizon);
    v("rng_state.state", self.rng_state);
    v("rng_state.inc", self.rng_inc);
    v("d4_value", self.d4_transform);
    v("patrol_path", self.patrol_path);
    // json_optional: v1-format JSON predates annotations.
    v("annotations", self.annotations, true);
  }

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
COMPANIONS_SNAPSHOT_SIZE_GUARD(Snapshot, 160);

#undef COMPANIONS_SNAPSHOT_SIZE_GUARD

}  // namespace companions

#endif  // COMPANIONS_CORE_SNAPSHOT_H_
