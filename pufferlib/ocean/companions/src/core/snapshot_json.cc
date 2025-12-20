// Copyright 2024
// JSON serialization for Snapshot

#include "snapshot_json.h"

#include <fstream>
#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"
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
  if (str == "Synchro") return CellKind::Synchro;
  if (str == "HealArea") return CellKind::HealArea;
  if (str == "Target") return CellKind::Target;
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
  if (str == "Stunned") return StatusType::Stunned;
  if (str == "Slowed") return StatusType::Slowed;
  if (str == "Marked") return StatusType::Marked;
  return StatusType::None;
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

CellSnapshot JsonToCellSnapshot(const json& j) {
  CellSnapshot cell;
  cell.kind = StringToCellKind(j.at("cell_kind").get<std::string>());
  cell.origin = StringToCellOrigin(j.at("cell_origin").get<std::string>());
  return cell;
}

// StatusSnapshot serialization
json StatusSnapshotToJson(const StatusSnapshot& status) {
  return json{
    {"status_type", StatusTypeToString(static_cast<StatusType>(status.type))},
    {"duration", status.duration}
  };
}

StatusSnapshot JsonToStatusSnapshot(const json& j) {
  StatusSnapshot status;
  status.type = static_cast<int>(StringToStatusType(j.at("status_type").get<std::string>()));
  status.duration = j.at("duration").get<int>();
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

FSMStateType StringToFSMStateType(const std::string& s) {
  if (s == "Patrol") return FSMStateType::Patrol;
  if (s == "Aggro") return FSMStateType::Aggro;
  if (s == "ReturnToPatrol") return FSMStateType::ReturnToPatrol;
  if (s == "Telegraph") return FSMStateType::Telegraph;
  if (s == "Attack") return FSMStateType::Attack;
  if (s == "Recovery") return FSMStateType::Recovery;
  return FSMStateType::None;
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
    {"attack_filter", static_cast<int>(fsm.attack_filter)}
  };

  json patrol = json::array();
  for (const auto& pos : fsm.patrol_path) {
    patrol.push_back(PositionToJson(pos));
  }
  j["patrol_path"] = patrol;

  return j;
}

FSMSnapshot JsonToFSMSnapshot(const json& j) {
  FSMSnapshot fsm;
  fsm.state_type = StringToFSMStateType(j.at("state_type").get<std::string>());
  fsm.target_id = j.at("target_id").get<int>();
  fsm.patrol_index = j.at("patrol_index").get<int>();
  fsm.patrol_forward = j.at("patrol_forward").get<bool>();
  fsm.detection_range = j.at("detection_range").get<int>();
  fsm.lose_target_range = j.at("lose_target_range").get<int>();
  fsm.rng_state = j.at("rng_state").get<uint64_t>();
  fsm.rng_inc = j.at("rng_inc").get<uint64_t>();

  for (const auto& pos_json : j.at("patrol_path")) {
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

  return fsm;
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

  return j;
}

AgentSnapshot JsonToAgentSnapshot(const json& j) {
  AgentSnapshot agent;
  agent.id = j.at("id").get<int>();
  agent.type = static_cast<int>(StringToObjectType(j.at("agent_type").get<std::string>()));
  agent.position = JsonToPosition(j.at("position"));
  agent.prev_position = JsonToPosition(j.at("prev_position"));
  agent.health = j.at("health").get<int>();
  agent.max_health = j.at("max_health").get<int>();
  agent.agent_index = j.at("agent_index").get<int>();
  agent.faction = static_cast<int>(StringToFaction(j.at("faction").get<std::string>()));
  agent.direction = static_cast<int>(StringToDirection(j.at("direction").get<std::string>()));
  agent.color = static_cast<int>(StringToActorColor(j.at("color").get<std::string>()));
  agent.alive = j.at("alive").get<bool>();

  // Statuses
  for (const auto& status_json : j.at("statuses")) {
    agent.statuses.push_back(JsonToStatusSnapshot(status_json));
  }

  // FSM
  if (j.at("fsm").is_null()) {
    agent.has_fsm = false;
  } else {
    agent.has_fsm = true;
    agent.fsm = JsonToFSMSnapshot(j.at("fsm"));
  }

  // Cadence
  agent.cadence = j.at("cadence").get<std::vector<int>>();
  agent.tick = j.at("tick").get<int>();

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

EffectSnapshot JsonToEffectSnapshot(const json& j) {
  EffectSnapshot effect;
  effect.effect_name = j.at("effect_name").get<std::string>();
  effect.target_type = j.at("target_type").get<int>();
  effect.target_cell = JsonToPosition(j.at("target_cell"));
  effect.target_actor_id = j.at("target_actor_id").get<int>();
  effect.target_actors = j.at("target_actors").get<std::vector<int>>();
  effect.direction = static_cast<int>(StringToDirection(j.at("direction").get<std::string>()));
  effect.ticks_remaining = j.at("ticks_remaining").get<int>();
  effect.in_telegraph = j.at("in_telegraph").get<bool>();
  effect.loops_remaining = j.at("loops_remaining").get<int>();
  effect.source_id = j.at("source_id").get<int>();
  return effect;
}

}  // namespace

// =============================================================================
// Main API
// =============================================================================

std::string SnapshotToJson(const Snapshot& snapshot) {
  json j;

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

  return j.dump(2);  // Pretty-print with 2-space indent
}

Snapshot SnapshotFromJson(const std::string& json_str) {
  json j = json::parse(json_str);

  Snapshot snapshot;

  // Grid
  snapshot.rows = j.at("grid").at("rows").get<int>();
  snapshot.cols = j.at("grid").at("cols").get<int>();

  // Pre-allocate cells
  snapshot.cells.resize(snapshot.rows * snapshot.cols);

  // Parse cells
  for (const auto& cell_json : j.at("grid").at("cells")) {
    int row = cell_json.at("row").get<int>();
    int col = cell_json.at("col").get<int>();
    int idx = row * snapshot.cols + col;
    snapshot.cells[idx] = JsonToCellSnapshot(cell_json);
  }

  // Agents
  for (const auto& agent_json : j.at("agents")) {
    snapshot.agents.push_back(JsonToAgentSnapshot(agent_json));
  }

  // Effects
  for (const auto& effect_json : j.at("effects")) {
    snapshot.effects.push_back(JsonToEffectSnapshot(effect_json));
  }

  // Timing
  snapshot.tick = j.at("tick").get<int>();
  snapshot.horizon = j.at("horizon").get<int>();

  // RNG state
  snapshot.rng_state = j.at("rng_state").at("state").get<uint64_t>();
  snapshot.rng_inc = j.at("rng_state").at("inc").get<uint64_t>();

  // D4 transform
  snapshot.d4_transform = j.at("d4_value").get<int>();

  // Patrol path
  for (const auto& pos_json : j.at("patrol_path")) {
    snapshot.patrol_path.push_back(JsonToPosition(pos_json));
  }

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
