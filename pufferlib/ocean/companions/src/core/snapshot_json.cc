// Copyright 2024
// JSON serialization for Snapshot.
//
// The JSON schema is produced by two generic visitors (JsonWriter /
// JsonReader) driven by the single VisitFields list of each snapshot struct
// (snapshot.h). The field list carries the JSON key names (dotted names
// nest, e.g. "grid.rows"); structural differences from the binary format
// (enums as strings, positions as {row,col} objects, the cells array with
// explicit coordinates, the conditional annotation shape) are per-type
// policies inside the visitors, never forks of the field list. The
// golden-fixture test (tests/test_snapshot_golden.cc) locks the schema.

#include "snapshot_json.h"

#include <fstream>
#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"
#include "annotations.h"
#include "cell.h"
#include "object.h"
#include "types.h"

using json = nlohmann::json;

namespace companions {

// =============================================================================
// Helper functions for enum serialization
// =============================================================================

namespace {

std::string ActorColorToString(ActorColor color) {
  switch (color) {
    case ActorColor::None: return "None";
    case ActorColor::Red: return "Red";
    case ActorColor::Green: return "Green";
    case ActorColor::Blue: return "Blue";
    default: return "None";
  }
}

ActorColor StringToActorColor(const std::string& str) {
  if (str == "Red") return ActorColor::Red;
  if (str == "Green") return ActorColor::Green;
  if (str == "Blue") return ActorColor::Blue;
  return ActorColor::None;
}

CellKind StringToCellKind(const std::string& str) {
  if (str == "Wall") return CellKind::Wall;
  if (str == "Hazard") return CellKind::Hazard;
  if (str == "HealArea") return CellKind::HealArea;
  // Legacy v1 JSON: Synchro/Target cells now flatten to Floor; their
  // task-semantic role lives in the annotations array.
  return CellKind::Floor;
}

CellOrigin StringToCellOrigin(const std::string& str) {
  if (str == "Room") return CellOrigin::Room;
  if (str == "Corridor") return CellOrigin::Corridor;
  if (str == "Obstacle") return CellOrigin::Obstacle;
  return CellOrigin::Default;
}

ObjectType StringToObjectType(const std::string& str) {
  if (str == "Actor") return ObjectType::Actor;
  if (str == "Agent") return ObjectType::Agent;
  if (str == "AgentFSM") return ObjectType::AgentFSM;
  if (str == "Companion") return ObjectType::Companion;
  if (str == "Player") return ObjectType::Player;
  if (str == "NPCCompanion") return ObjectType::NPCCompanion;
  return ObjectType::Object;
}

Faction StringToFaction(const std::string& str) {
  if (str == "ENEMY") return Faction::ENEMY;
  if (str == "NEUTRAL") return Faction::NEUTRAL;
  return Faction::COMPANION;
}

Direction StringToDirection(const std::string& str) {
  if (str == "Down") return Direction::Down;
  if (str == "Left") return Direction::Left;
  if (str == "Right") return Direction::Right;
  return Direction::Up;
}

StatusType StringToStatusType(const std::string& str) {
  // StatusTypeToString (object.cc) emits lowercase names; older code briefly
  // matched only capitalized forms, silently dropping statuses on JSON load
  // (caught by the golden-fixture round-trip test). Accept both.
  if (str == "stunned" || str == "Stunned") return StatusType::Stunned;
  if (str == "slowed" || str == "Slowed") return StatusType::Slowed;
  if (str == "marked" || str == "Marked") return StatusType::Marked;
  return StatusType::None;
}

// SemanticTag string conversion (symmetric with Python SEMANTIC_TAG_NAMES).
SemanticTag StringToSemanticTag(const std::string& str) {
  if (str == "SynchroGoal") return SemanticTag::SynchroGoal;
  if (str == "AggroTarget") return SemanticTag::AggroTarget;
  if (str == "QuestPickup") return SemanticTag::QuestPickup;
  if (str == "SafeZone")    return SemanticTag::SafeZone;
  if (str == "TargetMob")   return SemanticTag::TargetMob;
  if (str == "SkillGiver")  return SemanticTag::SkillGiver;
  if (str == "Escort")      return SemanticTag::Escort;
  if (str == "HtnName")     return SemanticTag::HtnName;
  if (str == "Room")        return SemanticTag::Room;
  return SemanticTag::SynchroGoal;  // Fallback
}

// FSMStateType to/from string
std::string FSMStateTypeToString(FSMStateType type) {
  switch (type) {
    case FSMStateType::Patrol: return "Patrol";
    case FSMStateType::Aggro: return "Aggro";
    case FSMStateType::ReturnToPatrol: return "ReturnToPatrol";
    case FSMStateType::Telegraph: return "Telegraph";
    case FSMStateType::Attack: return "Attack";
    case FSMStateType::Recovery: return "Recovery";
    default: return "None";
  }
}

FSMStateType StringToFSMStateType(const std::string& s) {
  if (s == "Patrol") return FSMStateType::Patrol;
  if (s == "Aggro") return FSMStateType::Aggro;
  if (s == "ReturnToPatrol") return FSMStateType::ReturnToPatrol;
  if (s == "Telegraph") return FSMStateType::Telegraph;
  if (s == "Attack") return FSMStateType::Attack;
  if (s == "Recovery") return FSMStateType::Recovery;
  return FSMStateType::None;
}

// Position serialization
json PositionToJson(const Position& pos) {
  return json{{"row", pos.row}, {"col", pos.col}};
}

Position JsonToPosition(const json& j) {
  return Position{j.at("row").get<int>(), j.at("col").get<int>()};
}

// CellSnapshot serialization. Hand-written: the JSON cell shape carries
// explicit (row, col) coordinates that are not CellSnapshot fields.
json CellSnapshotToJson(const CellSnapshot& cell, int row, int col) {
  return json{
    {"row", row},
    {"col", col},
    {"cell_kind", CellKindToString(cell.kind)},
    {"cell_origin", CellOriginToString(cell.origin)}
  };
}

CellSnapshot JsonToCellSnapshot(const json& j) {
  CellSnapshot cell;
  cell.kind = StringToCellKind(j.at("cell_kind").get<std::string>());
  cell.origin = StringToCellOrigin(j.at("cell_origin").get<std::string>());
  return cell;
}

// AnnotationSnapshot serialization. Hand-written corner (see the comment on
// AnnotationSnapshot::VisitFields): the JSON shape is conditional — exactly
// one of pos/agent_id is emitted depending on target_type, "target" is a
// string, and params is a JSON object rather than a pair list.
json AnnotationSnapshotToJson(const AnnotationSnapshot& a) {
  json params = json::object();
  for (const auto& kv : a.params) {
    params[kv.first] = kv.second;
  }
  json j = {
    {"target", a.target_type == 1 ? "Agent" : "Cell"},
    {"tag", SemanticTagToString(a.tag)},
    {"owner_lens_id", a.owner_lens_id},
    {"params", params},
  };
  if (a.target_type == 0) {
    j["pos"] = PositionToJson(a.pos);
  } else {
    j["agent_id"] = a.agent_id;
  }
  return j;
}

AnnotationSnapshot JsonToAnnotationSnapshot(const json& j) {
  AnnotationSnapshot a;
  std::string target = j.at("target").get<std::string>();
  a.target_type = (target == "Agent") ? 1 : 0;
  a.tag = StringToSemanticTag(j.at("tag").get<std::string>());
  a.owner_lens_id = j.value("owner_lens_id", -1);
  if (a.target_type == 0 && j.contains("pos")) {
    a.pos = JsonToPosition(j.at("pos"));
  }
  if (a.target_type == 1 && j.contains("agent_id")) {
    a.agent_id = j.at("agent_id").get<ObjectId>();
  }
  if (j.contains("params") && j.at("params").is_object()) {
    for (auto it = j.at("params").begin(); it != j.at("params").end(); ++it) {
      a.params.emplace_back(it.key(), it.value().get<std::string>());
    }
  }
  return a;
}

// =============================================================================
// JsonWriter - visitor producing the JSON schema
// =============================================================================
class JsonWriter {
 public:
  explicit JsonWriter(json& root) : root_(root) {}

  // Serialize a visitable struct into a standalone JSON object.
  template <class T>
  static json Sub(const T& value) {
    json sub = json::object();
    JsonWriter writer(sub);
    T::VisitFields(value, writer);
    return sub;
  }

  // Scalars (int, bool, uint64_t, ...) and nested visitable structs.
  template <class T>
  void operator()(const char* name, const T& value, bool /*json_optional*/ = false) {
    if constexpr (kHasVisitFields<T>) {
      Slot(name) = Sub(value);
    } else {
      Slot(name) = value;
    }
  }

  void operator()(const char* name, const std::string& s, bool = false) {
    Slot(name) = s;
  }

  void operator()(const char* name, const Position& p, bool = false) {
    Slot(name) = PositionToJson(p);
  }

  void operator()(const char* name, const FSMStateType& v, bool = false) {
    Slot(name) = FSMStateTypeToString(v);
  }
  void operator()(const char* name, const TargetFilter& v, bool = false) {
    Slot(name) = static_cast<int>(v);  // historical: int in JSON too
  }

  // EnumField JSON policy: one entry per enum, calling the existing
  // converters. Binary keeps the raw int (BinaryWriter).
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<ObjectType, IntT> f, bool = false) {
    Slot(name) = ObjectTypeToString(static_cast<ObjectType>(f.value));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<Faction, IntT> f, bool = false) {
    Slot(name) = FactionToString(static_cast<Faction>(f.value));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<Direction, IntT> f, bool = false) {
    Slot(name) = DirectionToString(static_cast<Direction>(f.value));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<ActorColor, IntT> f, bool = false) {
    Slot(name) = ActorColorToString(static_cast<ActorColor>(f.value));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<StatusType, IntT> f, bool = false) {
    Slot(name) = StatusTypeToString(static_cast<StatusType>(f.value));
  }

  // Guarded FSM: object when present, null otherwise (no "has_fsm" key).
  template <class BoolT, class FsmT>
  void operator()(const char* name, GuardedFsmRef<BoolT, FsmT> f, bool = false) {
    if (f.has) {
      Slot(name) = Sub(f.fsm);
    } else {
      Slot(name) = nullptr;
    }
  }

  // Vectors: JSON arrays.
  template <class T>
  void operator()(const char* name, const std::vector<T>& vec, bool = false) {
    json arr = json::array();
    for (const T& item : vec) {
      if constexpr (kHasVisitFields<T>) {
        arr.push_back(Sub(item));
      } else if constexpr (std::is_same_v<T, Position>) {
        arr.push_back(PositionToJson(item));
      } else {
        arr.push_back(item);
      }
    }
    Slot(name) = arr;
  }

  // Cells: objects with explicit (row, col), derived from grid.cols which
  // the Snapshot field list visits before cells.
  void operator()(const char* name, const std::vector<CellSnapshot>& cells,
                  bool = false) {
    const int cols = root_.at("grid").at("cols").get<int>();
    json arr = json::array();
    for (size_t idx = 0; idx < cells.size(); ++idx) {
      const int r = cols > 0 ? static_cast<int>(idx) / cols : 0;
      const int c = cols > 0 ? static_cast<int>(idx) % cols : 0;
      arr.push_back(CellSnapshotToJson(cells[idx], r, c));
    }
    Slot(name) = arr;
  }

  // Annotations: hand-written conditional shape.
  void operator()(const char* name, const std::vector<AnnotationSnapshot>& vec,
                  bool = false) {
    json arr = json::array();
    for (const auto& a : vec) {
      arr.push_back(AnnotationSnapshotToJson(a));
    }
    Slot(name) = arr;
  }

 private:
  // Dotted name -> nested slot ("grid.rows" -> root["grid"]["rows"]).
  json& Slot(const char* name) {
    json* j = &root_;
    std::string n(name);
    size_t start = 0;
    size_t dot;
    while ((dot = n.find('.', start)) != std::string::npos) {
      j = &((*j)[n.substr(start, dot - start)]);
      start = dot + 1;
    }
    return (*j)[n.substr(start)];
  }

  json& root_;
};

// =============================================================================
// JsonReader - visitor consuming the JSON schema
// =============================================================================
class JsonReader {
 public:
  explicit JsonReader(const json& root) : root_(root) {}

  template <class T>
  void operator()(const char* name, T& value, bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    if constexpr (kHasVisitFields<T>) {
      JsonReader reader(*j);
      T::VisitFields(value, reader);
    } else {
      value = j->get<T>();
    }
  }

  void operator()(const char* name, Position& p, bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    p = JsonToPosition(*j);
  }

  void operator()(const char* name, FSMStateType& v, bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    v = StringToFSMStateType(j->get<std::string>());
  }
  void operator()(const char* name, TargetFilter& v, bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    v = static_cast<TargetFilter>(j->get<int>());
  }

  template <class IntT>
  void operator()(const char* name, EnumFieldRef<ObjectType, IntT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    f.value = static_cast<int>(StringToObjectType(j->get<std::string>()));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<Faction, IntT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    f.value = static_cast<int>(StringToFaction(j->get<std::string>()));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<Direction, IntT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    f.value = static_cast<int>(StringToDirection(j->get<std::string>()));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<ActorColor, IntT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    f.value = static_cast<int>(StringToActorColor(j->get<std::string>()));
  }
  template <class IntT>
  void operator()(const char* name, EnumFieldRef<StatusType, IntT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    f.value = static_cast<int>(StringToStatusType(j->get<std::string>()));
  }

  template <class BoolT, class FsmT>
  void operator()(const char* name, GuardedFsmRef<BoolT, FsmT> f,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j || j->is_null()) {
      f.has = false;
      return;
    }
    f.has = true;
    JsonReader reader(*j);
    FSMSnapshot::VisitFields(f.fsm, reader);
  }

  template <class T>
  void operator()(const char* name, std::vector<T>& vec,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    vec.clear();
    vec.reserve(j->size());
    for (const auto& item : *j) {
      T elem{};
      if constexpr (kHasVisitFields<T>) {
        JsonReader reader(item);
        T::VisitFields(elem, reader);
      } else if constexpr (std::is_same_v<T, Position>) {
        elem = JsonToPosition(item);
      } else {
        elem = item.template get<T>();
      }
      vec.push_back(std::move(elem));
    }
  }

  // Cells: sized from grid dimensions, filled by each cell's (row, col).
  void operator()(const char* name, std::vector<CellSnapshot>& cells,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j) return;
    const int rows = root_.at("grid").at("rows").get<int>();
    const int cols = root_.at("grid").at("cols").get<int>();
    cells.assign(static_cast<size_t>(rows) * cols, CellSnapshot{});
    for (const auto& cell_json : *j) {
      int row = cell_json.at("row").get<int>();
      int col = cell_json.at("col").get<int>();
      int idx = row * cols + col;
      cells[static_cast<size_t>(idx)] = JsonToCellSnapshot(cell_json);
    }
  }

  // Annotations: hand-written conditional shape; tolerate non-array values
  // (legacy behavior for v1-format JSON payloads).
  void operator()(const char* name, std::vector<AnnotationSnapshot>& vec,
                  bool json_optional = false) {
    const json* j = Find(name, json_optional);
    if (!j || !j->is_array()) return;
    vec.clear();
    vec.reserve(j->size());
    for (const auto& a_json : *j) {
      vec.push_back(JsonToAnnotationSnapshot(a_json));
    }
  }

 private:
  // Dotted-name lookup. Returns nullptr when an optional field is absent;
  // throws (json::at semantics) when a required field is missing.
  const json* Find(const char* name, bool json_optional) const {
    const json* j = &root_;
    std::string n(name);
    size_t start = 0;
    while (true) {
      size_t dot = n.find('.', start);
      std::string key = (dot == std::string::npos)
                            ? n.substr(start)
                            : n.substr(start, dot - start);
      if (json_optional && (!j->is_object() || !j->contains(key))) {
        return nullptr;
      }
      j = &j->at(key);
      if (dot == std::string::npos) break;
      start = dot + 1;
    }
    return j;
  }

  const json& root_;
};

}  // namespace

// =============================================================================
// Main API
// =============================================================================

// Keep this in sync with the binary version check in snapshot.cc:Serialize.
// Audit F4: JSON path must be version-gated just like binary.
static constexpr int kJsonSnapshotVersion = 2;
static constexpr const char* kJsonSnapshotMagic = "SNAP";

std::string SnapshotToJson(const Snapshot& snapshot) {
  json j;

  // Schema identity — magic + version mirror the binary format.
  j["magic"] = kJsonSnapshotMagic;
  j["version"] = kJsonSnapshotVersion;

  JsonWriter writer(j);
  Snapshot::VisitFields(snapshot, writer);

  return j.dump(2);  // Pretty-print with 2-space indent
}

Snapshot SnapshotFromJson(const std::string& json_str) {
  json j = json::parse(json_str);

  // Schema validation — magic + version. Payloads saved prior to the F4
  // audit fix won't have either; accept them once but reject future drift.
  if (j.contains("magic")) {
    if (!j.at("magic").is_string() ||
        j.at("magic").get<std::string>() != kJsonSnapshotMagic) {
      throw std::runtime_error("Invalid snapshot magic (expected \"SNAP\")");
    }
  }
  if (j.contains("version")) {
    int version = j.at("version").get<int>();
    if (version != kJsonSnapshotVersion) {
      throw std::runtime_error(
          "Unsupported snapshot version: " + std::to_string(version));
    }
  }

  Snapshot snapshot;
  JsonReader reader(j);
  Snapshot::VisitFields(snapshot, reader);
  return snapshot;
}

bool SaveSnapshotToJsonFile(const Snapshot& snapshot, const std::string& filepath) {
  std::ofstream file(filepath);
  if (!file.is_open()) {
    return false;
  }
  file << SnapshotToJson(snapshot);
  return file.good();
}

Snapshot LoadSnapshotFromJsonFile(const std::string& filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath);
  }
  std::string json_str((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  return SnapshotFromJson(json_str);
}

}  // namespace companions
