// Copyright 2024
// JSON serialization for Snapshot

#include "snapshot_json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"
#include "annotations.h"
#include "cell.h"
#include "object.h"
#include "skill_config.h"
#include "types.h"

using json = nlohmann::json;

namespace companions {

// =============================================================================
// Helper functions for enum serialization
// =============================================================================

namespace {

// -----------------------------------------------------------------------------
// Reading with context: every error names the section being read, e.g.
// "cell_tags[3]: key 'tag' not found". nlohmann exceptions become
// std::runtime_error; our own runtime_errors already say where.
// -----------------------------------------------------------------------------

// nlohmann's message without its "[json.exception.type_error.302] " prefix.
std::string JsonErrorText(const json::exception& e) {
  const std::string what = e.what();
  if (what.rfind("[json.exception.", 0) == 0) {
    const size_t close = what.find("] ");
    if (close != std::string::npos) return what.substr(close + 2);
  }
  return what;
}

template <typename F>
auto InSection(const std::string& section, F&& read) -> decltype(read()) {
  try {
    return read();
  } catch (const json::exception& e) {
    throw std::runtime_error(section + ": " + JsonErrorText(e));
  }
}

void RequireObject(const json& j, const std::string& section) {
  if (!j.is_object()) {
    throw std::runtime_error(section + ": type must be object, but is " + j.type_name());
  }
}

const json& Key(const json& j, const char* key, const std::string& section) {
  RequireObject(j, section);
  auto it = j.find(key);
  if (it == j.end()) throw std::runtime_error(section + ": key '" + key + "' not found");
  return *it;
}

template <typename T>
T Get(const json& j, const char* key, const std::string& section) {
  const json& v = Key(j, key, section);
  return InSection(section + ": " + key, [&] { return v.get<T>(); });
}

template <typename T>
T GetOr(const json& j, const char* key, T fallback, const std::string& section) {
  return j.contains(key) ? Get<T>(j, key, section) : fallback;
}

const json& GetArray(const json& j, const char* key, const std::string& section) {
  const json& v = Key(j, key, section);
  if (!v.is_array()) {
    throw std::runtime_error(section + ": " + key + ": type must be array, but is " +
                             v.type_name());
  }
  return v;
}

std::string Indexed(const std::string& section, size_t i) {
  return section + "[" + std::to_string(i) + "]";
}

// The strict name parsers below throw this on a name no writer emits.
[[noreturn]] void UnknownName(const std::string& section, const char* key,
                              const std::string& name) {
  throw std::runtime_error(section + ": unknown " + key + " '" + name + "'");
}

// Throws on a key of `j` (an object) not in `allowed`; `owner` names the
// object. No aliases: "motion_distance" only gets a hint towards "distance".
void CheckKeys(const json& j, std::initializer_list<const char*> allowed,
               const std::string& owner) {
  for (auto it = j.begin(); it != j.end(); ++it) {
    bool known = false;
    bool has_distance = false;
    for (const char* key : allowed) {
      known = known || it.key() == key;
      has_distance = has_distance || std::string(key) == "distance";
    }
    if (known) continue;
    std::string message = owner + ": unknown key '" + it.key() + "'";
    if (has_distance && it.key() == "motion_distance") message += " (did you mean 'distance'?)";
    throw std::runtime_error(message);
  }
}

std::string ActorColorToString(ActorColor color) {
  switch (color) {
    case ActorColor::None: return "None";
    case ActorColor::Red: return "Red";
    case ActorColor::Green: return "Green";
    case ActorColor::Blue: return "Blue";
    default: return "None";
  }
}

// Strict name parsers: exactly the spellings the *ToString writers emit
// (case-sensitive); anything else throws naming `section`. No silent
// fallback, so a typo in a hand-authored level is an error, not Floor / Up.
ActorColor StringToActorColor(const std::string& str, const std::string& section) {
  if (str == "None") return ActorColor::None;
  if (str == "Red") return ActorColor::Red;
  if (str == "Green") return ActorColor::Green;
  if (str == "Blue") return ActorColor::Blue;
  UnknownName(section, "color", str);
}

CellKind StringToCellKind(const std::string& str, const std::string& section) {
  if (str == "Floor") return CellKind::Floor;
  if (str == "Wall") return CellKind::Wall;
  if (str == "Hazard") return CellKind::Hazard;
  if (str == "HealArea") return CellKind::HealArea;
  // Legacy v1 JSON: Synchro/Target cells now flatten to Floor; their
  // task-semantic role lives in the annotations array.
  if (str == "Synchro" || str == "Target") return CellKind::Floor;
  UnknownName(section, "cell_kind", str);
}

CellOrigin StringToCellOrigin(const std::string& str, const std::string& section) {
  if (str == "Default") return CellOrigin::Default;
  if (str == "Room") return CellOrigin::Room;
  if (str == "Corridor") return CellOrigin::Corridor;
  if (str == "Obstacle") return CellOrigin::Obstacle;
  UnknownName(section, "cell_origin", str);
}

ObjectType StringToObjectType(const std::string& str, const std::string& section) {
  if (str == "Object") return ObjectType::Object;
  if (str == "Actor") return ObjectType::Actor;
  if (str == "Agent") return ObjectType::Agent;
  if (str == "AgentFSM") return ObjectType::AgentFSM;
  if (str == "Companion") return ObjectType::Companion;
  if (str == "Player") return ObjectType::Player;
  if (str == "NPCCompanion") return ObjectType::NPCCompanion;
  UnknownName(section, "agent_type", str);
}

Faction StringToFaction(const std::string& str, const std::string& section) {
  if (str == "COMPANION") return Faction::COMPANION;
  if (str == "ENEMY") return Faction::ENEMY;
  if (str == "NEUTRAL") return Faction::NEUTRAL;
  UnknownName(section, "faction", str);
}

Direction StringToDirection(const std::string& str, const std::string& section) {
  if (str == "Up") return Direction::Up;
  if (str == "Down") return Direction::Down;
  if (str == "Left") return Direction::Left;
  if (str == "Right") return Direction::Right;
  UnknownName(section, "direction", str);
}

// SemanticTag string conversion (symmetric with Python SEMANTIC_TAG_NAMES).
SemanticTag StringToSemanticTag(const std::string& str, const std::string& section) {
  if (str == "SynchroGoal") return SemanticTag::SynchroGoal;
  if (str == "AggroTarget") return SemanticTag::AggroTarget;
  if (str == "QuestPickup") return SemanticTag::QuestPickup;
  if (str == "SafeZone")    return SemanticTag::SafeZone;
  if (str == "TargetMob")   return SemanticTag::TargetMob;
  if (str == "SkillGiver")  return SemanticTag::SkillGiver;
  if (str == "Escort")      return SemanticTag::Escort;
  if (str == "HtnName")     return SemanticTag::HtnName;
  if (str == "Room")        return SemanticTag::Room;
  UnknownName(section, "annotation tag", str);
}

// Position serialization
json PositionToJson(const Position& pos) {
  return json{{"row", pos.row}, {"col", pos.col}};
}

Position JsonToPosition(const json& j) {
  return Position{j.at("row").get<int>(), j.at("col").get<int>()};
}

// CellSnapshot serialization
json CellSnapshotToJson(const CellSnapshot& cell, int row, int col) {
  return json{
    {"row", row},
    {"col", col},
    {"cell_kind", CellKindToString(cell.kind)},
    {"cell_origin", CellOriginToString(cell.origin)}
  };
}

CellSnapshot JsonToCellSnapshot(const json& j, const std::string& section) {
  CellSnapshot cell;
  cell.kind = StringToCellKind(j.at("cell_kind").get<std::string>(), section);
  cell.origin = StringToCellOrigin(j.at("cell_origin").get<std::string>(), section);
  return cell;
}

// StatusSnapshot serialization
json StatusSnapshotToJson(const StatusSnapshot& status) {
  return json{
    {"status_type", StatusTypeToString(static_cast<StatusType>(status.type))},
    {"duration", status.duration}
  };
}

// Status names are case-insensitive; one StatusTypeFromString does not know
// (anything but "none") is an error.
StatusSnapshot JsonToStatusSnapshot(const json& j, const std::string& section) {
  StatusSnapshot status;
  const std::string name = Get<std::string>(j, "status_type", section);
  const StatusType type = StatusTypeFromString(name);
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (type == StatusType::None && lower != "none") {
    throw std::runtime_error(section + ": unknown status '" + name + "'");
  }
  status.type = static_cast<int>(type);
  status.duration = Get<int>(j, "duration", section);
  return status;
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

FSMStateType StringToFSMStateType(const std::string& s, const std::string& section) {
  if (s == "None") return FSMStateType::None;
  if (s == "Patrol") return FSMStateType::Patrol;
  if (s == "Aggro") return FSMStateType::Aggro;
  if (s == "ReturnToPatrol") return FSMStateType::ReturnToPatrol;
  if (s == "Telegraph") return FSMStateType::Telegraph;
  if (s == "Attack") return FSMStateType::Attack;
  if (s == "Recovery") return FSMStateType::Recovery;
  UnknownName(section, "FSM state_type", s);
}

// FSMSnapshot serialization
json FSMSnapshotToJson(const FSMSnapshot& fsm) {
  json j{
    {"state_type", FSMStateTypeToString(fsm.state_type)},
    {"target_id", fsm.target_id},
    {"patrol_index", fsm.patrol_index},
    {"patrol_forward", fsm.patrol_forward},
    {"detection_range", fsm.detection_range},
    {"lose_target_range", fsm.lose_target_range},
    {"rng_state", fsm.rng_state},
    {"rng_inc", fsm.rng_inc},
    {"attack_tick_counter", fsm.attack_tick_counter},
    {"attack_target_position", PositionToJson(fsm.attack_target_position)},
    {"attack_area_width", fsm.attack_area_width},
    {"attack_area_height", fsm.attack_area_height},
    {"attack_damage", fsm.attack_damage},
    {"attack_filter", static_cast<int>(fsm.attack_filter)},
    {"has_attack", fsm.has_attack},
    {"attack_effect", fsm.attack_effect},
    {"telegraph_ticks", fsm.telegraph_ticks},
    {"attack_ticks", fsm.attack_ticks},
    {"recovery_ticks", fsm.recovery_ticks}
  };

  json patrol = json::array();
  for (const auto& pos : fsm.patrol_path) {
    patrol.push_back(PositionToJson(pos));
  }
  j["patrol_path"] = patrol;

  return j;
}

// `section` is "agents[i].fsm".
FSMSnapshot JsonToFSMSnapshot(const json& j, const std::string& section) {
  FSMSnapshot fsm;
  fsm.state_type = StringToFSMStateType(j.at("state_type").get<std::string>(), section);
  fsm.target_id = j.at("target_id").get<int>();
  fsm.patrol_index = j.at("patrol_index").get<int>();
  fsm.patrol_forward = j.at("patrol_forward").get<bool>();
  fsm.detection_range = j.at("detection_range").get<int>();
  fsm.lose_target_range = j.at("lose_target_range").get<int>();
  fsm.rng_state = j.at("rng_state").get<uint64_t>();
  fsm.rng_inc = j.at("rng_inc").get<uint64_t>();

  for (const auto& pos_json : GetArray(j, "patrol_path", section)) {
    fsm.patrol_path.push_back(JsonToPosition(pos_json));
  }

  // Attack runtime state
  if (j.contains("attack_tick_counter")) {
    fsm.attack_tick_counter = j.at("attack_tick_counter").get<int>();
  }
  if (j.contains("attack_target_position")) {
    fsm.attack_target_position = JsonToPosition(j.at("attack_target_position"));
  }
  if (j.contains("attack_area_width")) {
    fsm.attack_area_width = j.at("attack_area_width").get<int>();
  }
  if (j.contains("attack_area_height")) {
    fsm.attack_area_height = j.at("attack_area_height").get<int>();
  }
  if (j.contains("attack_damage")) {
    fsm.attack_damage = j.at("attack_damage").get<int>();
  }
  if (j.contains("attack_filter")) {
    fsm.attack_filter = static_cast<TargetFilter>(j.at("attack_filter").get<int>());
  }

  // Attack configuration (optional; absent = no attack)
  fsm.has_attack = j.value("has_attack", false);
  fsm.attack_effect = j.value("attack_effect", std::string());
  fsm.telegraph_ticks = j.value("telegraph_ticks", 1);
  fsm.attack_ticks = j.value("attack_ticks", 1);
  fsm.recovery_ticks = j.value("recovery_ticks", 1);

  return fsm;
}

// Tags by name: {"tag", "duration"} (duration absent = permanent, -1). Values
// are validated by Snapshot::ValidateSkillsTagsZones.
json TagToJson(const std::string& tag, int duration) {
  return json{{"tag", tag}, {"duration", duration}};
}

// `section` prefixes JSON errors, `owner` an unknown key.
TagSnapshot JsonToTag(const json& j, const std::string& section, const std::string& owner) {
  RequireObject(j, section);
  CheckKeys(j, {"tag", "duration"}, owner);
  return {Get<std::string>(j, "tag", section), GetOr<int>(j, "duration", kPermanentTag, section)};
}

// SkillConfig: every field but "name" is optional and takes the SkillConfig
// default when absent (so "filter" defaults to "all", "damage" to 0,
// "friendly_fire" and the "self_*" flags to true). "distance" is
// motion_distance.
json SkillConfigToJson(const SkillConfig& s) {
  json tags = json::array();
  for (const SkillTagSpec& t : s.tags) tags.push_back(TagToJson(t.tag, t.duration));
  return json{
    {"name", s.name},
    {"targeting", SkillTargetingToString(s.targeting)},
    {"range", s.range},
    {"filter", TargetFilterToString(s.filter)},
    {"area", SkillAreaToString(s.area)},
    {"motion", SkillMotionToString(s.motion)},
    {"distance", s.motion_distance},
    {"tag_path", s.tag_path},
    {"tags", tags},
    {"damage", s.damage},
    {"root_steps", s.root_steps},
    {"cooldown", s.cooldown},
    {"friendly_fire", s.friendly_fire},
    {"self_tags", s.self_tags},
    {"self_motion", s.self_motion},
    {"self_root", s.self_root},
    {"self_damage", s.self_damage}
  };
}

// `section` is "skills[i]". JSON errors read "skills[i] ('frost'): range: ...",
// the others "skill 'frost': ...". Values are checked by ValidateSkillConfig.
SkillConfig JsonToSkillConfig(const json& j, const std::string& section) {
  RequireObject(j, section);
  SkillConfig s;
  s.name = Get<std::string>(j, "name", section);
  const std::string sec = section + " ('" + s.name + "')";
  const std::string owner = "skill '" + s.name + "'";
  CheckKeys(j, {"name", "targeting", "range", "filter", "area", "motion", "distance", "tag_path",
                "tags", "damage", "root_steps", "cooldown", "friendly_fire", "self_tags",
                "self_motion", "self_root", "self_damage"},
            owner);
  // An enum field, parsed by `from_string` (which throws on an unknown value).
  auto enum_field = [&](const char* key, auto& field, auto from_string) {
    if (!j.contains(key)) return;
    const std::string value = Get<std::string>(j, key, sec);
    try {
      field = from_string(value);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error(owner + ": " + e.what());
    }
  };
  enum_field("targeting", s.targeting, SkillTargetingFromString);
  s.range = GetOr<int>(j, "range", s.range, sec);
  enum_field("filter", s.filter, TargetFilterFromString);
  enum_field("area", s.area, SkillAreaFromString);
  enum_field("motion", s.motion, SkillMotionFromString);
  s.motion_distance = GetOr<int>(j, "distance", s.motion_distance, sec);
  s.tag_path = GetOr<bool>(j, "tag_path", s.tag_path, sec);
  if (j.contains("tags")) {
    const json& tags = GetArray(j, "tags", sec);
    for (size_t i = 0; i < tags.size(); ++i) {
      TagSnapshot t = JsonToTag(tags[i], Indexed(sec + ": tags", i), Indexed(owner + ": tags", i));
      s.tags.push_back({t.tag, t.duration});
    }
  }
  s.damage = GetOr<int>(j, "damage", s.damage, sec);
  s.root_steps = GetOr<int>(j, "root_steps", s.root_steps, sec);
  s.cooldown = GetOr<int>(j, "cooldown", s.cooldown, sec);
  s.friendly_fire = GetOr<bool>(j, "friendly_fire", s.friendly_fire, sec);
  s.self_tags = GetOr<bool>(j, "self_tags", s.self_tags, sec);
  s.self_motion = GetOr<bool>(j, "self_motion", s.self_motion, sec);
  s.self_root = GetOr<bool>(j, "self_root", s.self_root, sec);
  s.self_damage = GetOr<bool>(j, "self_damage", s.self_damage, sec);
  return s;
}

// AgentSnapshot serialization
json AgentSnapshotToJson(const AgentSnapshot& agent) {
  json j{
    {"id", agent.id},
    {"agent_type", ObjectTypeToString(static_cast<ObjectType>(agent.type))},
    {"position", PositionToJson(agent.position)},
    {"prev_position", PositionToJson(agent.prev_position)},
    {"health", agent.health},
    {"max_health", agent.max_health},
    {"agent_index", agent.agent_index},
    {"faction", FactionToString(static_cast<Faction>(agent.faction))},
    {"direction", DirectionToString(static_cast<Direction>(agent.direction))},
    {"color", ActorColorToString(static_cast<ActorColor>(agent.color))},
    {"alive", agent.alive}
  };
  if (!agent.kind.empty()) {
    j["kind"] = agent.kind;
  }

  // Statuses
  json statuses = json::array();
  for (const auto& status : agent.statuses) {
    statuses.push_back(StatusSnapshotToJson(status));
  }
  j["statuses"] = statuses;

  // FSM (null if no FSM)
  if (agent.has_fsm) {
    j["fsm"] = FSMSnapshotToJson(agent.fsm);
  } else {
    j["fsm"] = nullptr;
  }

  // Cadence
  j["cadence"] = agent.cadence;
  j["tick"] = agent.tick;

  // Tags, skill slots and cooldowns (v4; slots only for companions)
  json tags = json::array();
  for (const TagSnapshot& t : agent.tags) tags.push_back(TagToJson(t.tag, t.duration));
  j["tags"] = tags;
  if (!agent.skills.empty()) j["skills"] = agent.skills;
  if (!agent.cooldowns.empty()) j["cooldowns"] = agent.cooldowns;

  return j;
}

// `section` is "agents[i]"; the caller turns other JSON errors into it.
AgentSnapshot JsonToAgentSnapshot(const json& j, const std::string& section) {
  AgentSnapshot agent;
  agent.id = j.at("id").get<int>();
  agent.type =
      static_cast<int>(StringToObjectType(j.at("agent_type").get<std::string>(), section));
  agent.position = JsonToPosition(j.at("position"));
  agent.prev_position = JsonToPosition(j.at("prev_position"));
  agent.health = j.at("health").get<int>();
  agent.max_health = j.at("max_health").get<int>();
  agent.agent_index = j.at("agent_index").get<int>();
  agent.faction = static_cast<int>(StringToFaction(j.at("faction").get<std::string>(), section));
  agent.direction =
      static_cast<int>(StringToDirection(j.at("direction").get<std::string>(), section));
  agent.color = static_cast<int>(StringToActorColor(j.at("color").get<std::string>(), section));
  agent.alive = j.at("alive").get<bool>();
  agent.kind = j.value("kind", std::string());

  // Statuses
  const json& statuses = GetArray(j, "statuses", section);
  for (size_t i = 0; i < statuses.size(); ++i) {
    agent.statuses.push_back(JsonToStatusSnapshot(statuses[i], Indexed(section + ".statuses", i)));
  }

  // FSM
  if (j.at("fsm").is_null()) {
    agent.has_fsm = false;
  } else {
    agent.has_fsm = true;
    agent.fsm = JsonToFSMSnapshot(j.at("fsm"), section + ".fsm");
  }

  // Cadence
  agent.cadence = j.at("cadence").get<std::vector<int>>();
  agent.tick = j.at("tick").get<int>();

  // Tags, skill slots and cooldowns (v4; absent = none / empty / 0)
  if (j.contains("tags")) {
    const json& tags = GetArray(j, "tags", section);
    for (size_t i = 0; i < tags.size(); ++i) {
      const std::string tag_section = Indexed(section + ".tags", i);
      agent.tags.push_back(JsonToTag(tags[i], tag_section, tag_section));
    }
  }
  if (j.contains("skills")) agent.skills = Get<std::vector<std::string>>(j, "skills", section);
  if (j.contains("cooldowns")) agent.cooldowns = Get<std::vector<int>>(j, "cooldowns", section);

  return agent;
}

// EffectSnapshot serialization
json EffectSnapshotToJson(const EffectSnapshot& effect) {
  json j{
    {"effect_name", effect.effect_name},
    {"target_type", effect.target_type},
    {"target_cell", PositionToJson(effect.target_cell)},
    {"target_actor_id", effect.target_actor_id},
    {"target_actors", effect.target_actors},
    {"direction", DirectionToString(static_cast<Direction>(effect.direction))},
    {"ticks_remaining", effect.ticks_remaining},
    {"in_telegraph", effect.in_telegraph},
    {"loops_remaining", effect.loops_remaining},
    {"source_id", effect.source_id}
  };
  return j;
}

EffectSnapshot JsonToEffectSnapshot(const json& j, const std::string& section) {
  EffectSnapshot effect;
  effect.effect_name = j.at("effect_name").get<std::string>();
  effect.target_type = j.at("target_type").get<int>();
  effect.target_cell = JsonToPosition(j.at("target_cell"));
  effect.target_actor_id = j.at("target_actor_id").get<int>();
  effect.target_actors = j.at("target_actors").get<std::vector<int>>();
  effect.direction =
      static_cast<int>(StringToDirection(j.at("direction").get<std::string>(), section));
  effect.ticks_remaining = j.at("ticks_remaining").get<int>();
  effect.in_telegraph = j.at("in_telegraph").get<bool>();
  effect.loops_remaining = j.at("loops_remaining").get<int>();
  effect.source_id = j.at("source_id").get<int>();
  return effect;
}

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

AnnotationSnapshot JsonToAnnotationSnapshot(const json& j, const std::string& section) {
  AnnotationSnapshot a;
  std::string target = j.at("target").get<std::string>();
  if (target != "Agent" && target != "Cell") UnknownName(section, "annotation target", target);
  a.target_type = (target == "Agent") ? 1 : 0;
  a.tag = StringToSemanticTag(j.at("tag").get<std::string>(), section);
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

}  // namespace

// =============================================================================
// Main API
// =============================================================================

// Keep this in sync with the binary version check in snapshot.cc:Serialize.
// Audit F4: JSON path must be version-gated just like binary.
// 2: annotations (agent kind / attack config are optional keys); 4: skills,
// agent tags / skill slots / cooldowns, cell_tags (zones). There never was a
// JSON 3: the number follows the binary format. Versions 2..4 load.
static constexpr int kJsonSnapshotVersion = 4;
static constexpr int kMinJsonSnapshotVersion = 2;
static constexpr const char* kJsonSnapshotMagic = "SNAP";
// rows * cols cap, so a hand-authored level can not make us allocate gigabytes.
static constexpr int64_t kMaxJsonGridCells = int64_t{1} << 20;

std::string SnapshotToJson(const Snapshot& snapshot) {
  json j;

  // Schema identity — magic + version mirror the binary format.
  j["magic"] = kJsonSnapshotMagic;
  j["version"] = kJsonSnapshotVersion;

  // Grid
  j["grid"]["rows"] = snapshot.rows;
  j["grid"]["cols"] = snapshot.cols;

  // Cells (row-major order with coordinates)
  json cells = json::array();
  for (int r = 0; r < snapshot.rows; ++r) {
    for (int c = 0; c < snapshot.cols; ++c) {
      int idx = r * snapshot.cols + c;
      cells.push_back(CellSnapshotToJson(snapshot.cells[idx], r, c));
    }
  }
  j["grid"]["cells"] = cells;

  // Agents
  json agents = json::array();
  for (const auto& agent : snapshot.agents) {
    agents.push_back(AgentSnapshotToJson(agent));
  }
  j["agents"] = agents;

  // Effects
  json effects = json::array();
  for (const auto& effect : snapshot.effects) {
    effects.push_back(EffectSnapshotToJson(effect));
  }
  j["effects"] = effects;

  // Timing
  j["tick"] = snapshot.tick;
  j["horizon"] = snapshot.horizon;

  // RNG state
  j["rng_state"]["state"] = snapshot.rng_state;
  j["rng_state"]["inc"] = snapshot.rng_inc;

  // D4 transform
  j["d4_value"] = snapshot.d4_transform;

  // Patrol path
  json patrol = json::array();
  for (const auto& pos : snapshot.patrol_path) {
    patrol.push_back(PositionToJson(pos));
  }
  j["patrol_path"] = patrol;

  // Semantic annotations
  json annotations = json::array();
  for (const auto& a : snapshot.annotations) {
    annotations.push_back(AnnotationSnapshotToJson(a));
  }
  j["annotations"] = annotations;

  // Skill book (builtins included) and zones
  json skills = json::array();
  for (const SkillConfig& skill : snapshot.skills) skills.push_back(SkillConfigToJson(skill));
  j["skills"] = skills;
  json cell_tags = json::array();
  for (const CellTagSnapshot& z : snapshot.cell_tags) {
    cell_tags.push_back(json{{"row", z.cell.row}, {"col", z.cell.col},
                             {"tag", z.tag}, {"duration", z.duration}});
  }
  j["cell_tags"] = cell_tags;

  return j.dump(2);  // Pretty-print with 2-space indent
}

namespace {

Snapshot ReadSnapshot(const json& j) {
  RequireObject(j, "snapshot");

  // Schema validation — magic + version. Payloads saved prior to the F4
  // audit fix won't have either; accept them once but reject future drift.
  if (j.contains("magic")) {
    if (!j.at("magic").is_string() ||
        j.at("magic").get<std::string>() != kJsonSnapshotMagic) {
      throw std::runtime_error("Invalid snapshot magic (expected \"SNAP\")");
    }
  }
  if (j.contains("version")) {
    int version = Get<int>(j, "version", "snapshot");
    if (version < kMinJsonSnapshotVersion || version > kJsonSnapshotVersion) {
      throw std::runtime_error(
          "Unsupported snapshot version: " + std::to_string(version));
    }
  }

  Snapshot snapshot;

  // Grid: positive dimensions, at most kMaxJsonGridCells cells
  const json& grid = Key(j, "grid", "snapshot");
  snapshot.rows = Get<int>(grid, "rows", "grid");
  snapshot.cols = Get<int>(grid, "cols", "grid");
  const std::string dims = std::to_string(snapshot.rows) + "x" + std::to_string(snapshot.cols);
  if (snapshot.rows <= 0 || snapshot.cols <= 0) {
    throw std::runtime_error("grid: rows and cols must be > 0 (got " + dims + ")");
  }
  if (int64_t{snapshot.rows} * int64_t{snapshot.cols} > kMaxJsonGridCells) {
    throw std::runtime_error("grid: " + dims + " is too many cells (at most " +
                             std::to_string(kMaxJsonGridCells) + ")");
  }
  snapshot.cells.resize(static_cast<size_t>(snapshot.rows) * static_cast<size_t>(snapshot.cols));

  // Cells: {row, col, ...} entries inside the grid (absent cells stay Floor)
  const json& cells = GetArray(grid, "cells", "grid");
  for (size_t i = 0; i < cells.size(); ++i) {
    const std::string section = Indexed("grid.cells", i);
    InSection(section, [&] {
      const json& cell_json = cells[i];
      const int row = cell_json.at("row").get<int>();
      const int col = cell_json.at("col").get<int>();
      if (row < 0 || row >= snapshot.rows || col < 0 || col >= snapshot.cols) {
        throw std::runtime_error(section + ": (" + std::to_string(row) + ", " +
                                 std::to_string(col) + ") outside " + dims);
      }
      snapshot.cells[static_cast<size_t>(row) * snapshot.cols + col] =
          JsonToCellSnapshot(cell_json, section);
    });
  }

  // Agents
  const json& agents = GetArray(j, "agents", "snapshot");
  for (size_t i = 0; i < agents.size(); ++i) {
    const std::string section = Indexed("agents", i);
    snapshot.agents.push_back(
        InSection(section, [&] { return JsonToAgentSnapshot(agents[i], section); }));
  }

  // Effects
  const json& effects = GetArray(j, "effects", "snapshot");
  for (size_t i = 0; i < effects.size(); ++i) {
    const std::string section = Indexed("effects", i);
    snapshot.effects.push_back(
        InSection(section, [&] { return JsonToEffectSnapshot(effects[i], section); }));
  }

  // Timing
  snapshot.tick = Get<int>(j, "tick", "snapshot");
  snapshot.horizon = Get<int>(j, "horizon", "snapshot");

  // RNG state
  InSection("rng_state", [&] {
    snapshot.rng_state = j.at("rng_state").at("state").get<uint64_t>();
    snapshot.rng_inc = j.at("rng_state").at("inc").get<uint64_t>();
  });

  // D4 transform
  snapshot.d4_transform = Get<int>(j, "d4_value", "snapshot");

  // Patrol path
  const json& patrol = GetArray(j, "patrol_path", "snapshot");
  for (size_t i = 0; i < patrol.size(); ++i) {
    snapshot.patrol_path.push_back(
        InSection(Indexed("patrol_path", i), [&] { return JsonToPosition(patrol[i]); }));
  }

  // Semantic annotations (optional: absent in v1-format JSON)
  if (j.contains("annotations")) {
    const json& annotations = GetArray(j, "annotations", "snapshot");
    for (size_t i = 0; i < annotations.size(); ++i) {
      const std::string section = Indexed("annotations", i);
      snapshot.annotations.push_back(
          InSection(section, [&] { return JsonToAnnotationSnapshot(annotations[i], section); }));
    }
  }

  // Skill book and zones (v4; absent = builtins only / no zones)
  if (j.contains("skills")) {
    const json& skills = GetArray(j, "skills", "snapshot");
    for (size_t i = 0; i < skills.size(); ++i) {
      snapshot.skills.push_back(JsonToSkillConfig(skills[i], Indexed("skills", i)));
    }
  }
  if (j.contains("cell_tags")) {
    const json& zones = GetArray(j, "cell_tags", "snapshot");
    for (size_t i = 0; i < zones.size(); ++i) {
      const std::string section = Indexed("cell_tags", i);
      const json& z = zones[i];
      RequireObject(z, section);
      CheckKeys(z, {"row", "col", "tag", "duration"}, section);
      CellTagSnapshot zone;
      zone.cell = Position{Get<int>(z, "row", section), Get<int>(z, "col", section)};
      zone.tag = Get<std::string>(z, "tag", section);
      zone.duration = GetOr<int>(z, "duration", kPermanentTag, section);
      snapshot.cell_tags.push_back(zone);
    }
  }

  snapshot.ValidateSkillsTagsZones();
  return snapshot;
}

}  // namespace

Snapshot SnapshotFromJson(const std::string& json_str) {
  json j;
  try {
    j = json::parse(json_str);
  } catch (const json::exception& e) {
    throw std::runtime_error("invalid JSON: " + JsonErrorText(e));
  }
  // Every read names its section; this only catches what slipped through.
  return InSection("snapshot", [&] { return ReadSnapshot(j); });
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
