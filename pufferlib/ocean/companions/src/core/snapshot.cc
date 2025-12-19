// Copyright 2024
// Snapshot implementation

#include "snapshot.h"

#include <cstring>
#include <stdexcept>

namespace companions {

// =============================================================================
// Snapshot validation helpers
// =============================================================================

bool Snapshot::HasSynchroCells() const {
  return CountCells(CellKind::Synchro) > 0;
}

bool Snapshot::HasTargetCell() const {
  return CountCells(CellKind::Target) > 0;
}

bool Snapshot::HasPatrolPath() const {
  // Check direct patrol_path field first
  if (!patrol_path.empty()) {
    return true;
  }
  // Fall back to checking FSM agents
  for (const auto& agent : agents) {
    if (agent.has_fsm && !agent.fsm.patrol_path.empty()) {
      return true;
    }
  }
  return false;
}

int Snapshot::CountCells(CellKind kind) const {
  int count = 0;
  for (const auto& cell : cells) {
    if (cell.kind == kind) {
      count++;
    }
  }
  return count;
}

// =============================================================================
// Serialization helpers
// =============================================================================

namespace {

// Write primitive types to buffer
template <typename T>
void WriteValue(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

// Write string (length-prefixed)
void WriteString(std::vector<uint8_t>& buffer, const std::string& str) {
  uint32_t len = static_cast<uint32_t>(str.size());
  WriteValue(buffer, len);
  buffer.insert(buffer.end(), str.begin(), str.end());
}

// Write vector of primitives
template <typename T>
void WriteVector(std::vector<uint8_t>& buffer, const std::vector<T>& vec) {
  uint32_t size = static_cast<uint32_t>(vec.size());
  WriteValue(buffer, size);
  for (const auto& item : vec) {
    WriteValue(buffer, item);
  }
}

// Read primitive types from buffer with bounds checking
template <typename T>
T ReadValue(const uint8_t*& ptr, const uint8_t* end) {
  if (ptr + sizeof(T) > end) {
    throw std::runtime_error("Snapshot buffer underflow: not enough data");
  }
  T value;
  std::memcpy(&value, ptr, sizeof(T));
  ptr += sizeof(T);
  return value;
}

// Read string (length-prefixed) with bounds checking
std::string ReadString(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t len = ReadValue<uint32_t>(ptr, end);
  if (ptr + len > end) {
    throw std::runtime_error("Snapshot buffer underflow: string data truncated");
  }
  std::string str(reinterpret_cast<const char*>(ptr), len);
  ptr += len;
  return str;
}

// Read vector of primitives with bounds checking
template <typename T>
std::vector<T> ReadVector(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t size = ReadValue<uint32_t>(ptr, end);
  if (size > 10000000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable vector size");
  }
  std::vector<T> vec(size);
  for (uint32_t i = 0; i < size; ++i) {
    vec[i] = ReadValue<T>(ptr, end);
  }
  return vec;
}

// Serialize Position
void WritePosition(std::vector<uint8_t>& buffer, const Position& pos) {
  WriteValue(buffer, pos.row);
  WriteValue(buffer, pos.col);
}

Position ReadPosition(const uint8_t*& ptr, const uint8_t* end) {
  Position pos;
  pos.row = ReadValue<int>(ptr, end);
  pos.col = ReadValue<int>(ptr, end);
  return pos;
}

// Serialize vector of Positions
void WritePositionVector(std::vector<uint8_t>& buffer,
                         const std::vector<Position>& positions) {
  uint32_t size = static_cast<uint32_t>(positions.size());
  WriteValue(buffer, size);
  for (const auto& pos : positions) {
    WritePosition(buffer, pos);
  }
}

std::vector<Position> ReadPositionVector(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t size = ReadValue<uint32_t>(ptr, end);
  if (size > 10000000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable position vector size");
  }
  std::vector<Position> positions(size);
  for (uint32_t i = 0; i < size; ++i) {
    positions[i] = ReadPosition(ptr, end);
  }
  return positions;
}

}  // namespace

// =============================================================================
// Snapshot serialization
// =============================================================================

std::vector<uint8_t> Snapshot::Serialize() const {
  std::vector<uint8_t> buffer;

  // Magic number and version
  WriteValue(buffer, static_cast<uint32_t>(0x534E4150));  // "SNAP"
  WriteValue(buffer, static_cast<uint32_t>(1));           // Version 1

  // Grid dimensions
  WriteValue(buffer, rows);
  WriteValue(buffer, cols);

  // Cells
  WriteValue(buffer, static_cast<uint32_t>(cells.size()));
  for (const auto& cell : cells) {
    WriteValue(buffer, static_cast<int>(cell.kind));
    WriteValue(buffer, static_cast<int>(cell.origin));
  }

  // Agents
  WriteValue(buffer, static_cast<uint32_t>(agents.size()));
  for (const auto& agent : agents) {
    WriteValue(buffer, agent.id);
    WriteValue(buffer, agent.type);
    WritePosition(buffer, agent.position);
    WritePosition(buffer, agent.prev_position);
    WriteValue(buffer, agent.health);
    WriteValue(buffer, agent.max_health);
    WriteValue(buffer, agent.agent_index);
    WriteValue(buffer, agent.faction);
    WriteValue(buffer, agent.direction);
    WriteValue(buffer, agent.color);
    WriteValue(buffer, agent.alive);

    // Statuses
    WriteValue(buffer, static_cast<uint32_t>(agent.statuses.size()));
    for (const auto& status : agent.statuses) {
      WriteValue(buffer, status.type);
      WriteValue(buffer, status.duration);
    }

    // FSM
    WriteValue(buffer, agent.has_fsm);
    if (agent.has_fsm) {
      WriteString(buffer, agent.fsm.state_name);
      WriteValue(buffer, agent.fsm.target_id);
      WritePositionVector(buffer, agent.fsm.patrol_path);
      WriteValue(buffer, agent.fsm.patrol_index);
      WriteValue(buffer, agent.fsm.patrol_forward);
      WriteValue(buffer, agent.fsm.detection_range);
      WriteValue(buffer, agent.fsm.lose_target_range);
      WriteValue(buffer, agent.fsm.rng_state);
      WriteValue(buffer, agent.fsm.rng_inc);
    }

    // Cadence
    WriteVector(buffer, agent.cadence);
    WriteValue(buffer, agent.tick);
  }

  // Effects
  WriteValue(buffer, static_cast<uint32_t>(effects.size()));
  for (const auto& effect : effects) {
    WriteString(buffer, effect.effect_name);
    WriteValue(buffer, effect.target_type);
    WritePosition(buffer, effect.target_cell);
    WriteValue(buffer, effect.target_actor_id);
    WriteVector(buffer, effect.target_actors);
    WriteValue(buffer, effect.direction);
    WriteValue(buffer, effect.ticks_remaining);
    WriteValue(buffer, effect.in_telegraph);
    WriteValue(buffer, effect.loops_remaining);
    WriteValue(buffer, effect.source_id);
  }

  // Timing and state
  WriteValue(buffer, tick);
  WriteValue(buffer, horizon);
  WriteValue(buffer, rng_state);
  WriteValue(buffer, rng_inc);
  WriteValue(buffer, d4_transform);

  // Patrol path (for AggroEnv without FSM agents)
  WritePositionVector(buffer, patrol_path);

  return buffer;
}

Snapshot Snapshot::Deserialize(const std::vector<uint8_t>& data) {
  if (data.size() < 8) {
    throw std::runtime_error("Snapshot data too small");
  }

  const uint8_t* ptr = data.data();
  const uint8_t* end = data.data() + data.size();

  // Magic number and version
  uint32_t magic = ReadValue<uint32_t>(ptr, end);
  if (magic != 0x534E4150) {
    throw std::runtime_error("Invalid snapshot magic number");
  }
  uint32_t version = ReadValue<uint32_t>(ptr, end);
  if (version != 1) {
    throw std::runtime_error("Unsupported snapshot version");
  }

  Snapshot snap;

  // Grid dimensions
  snap.rows = ReadValue<int>(ptr, end);
  snap.cols = ReadValue<int>(ptr, end);

  // Cells
  uint32_t num_cells = ReadValue<uint32_t>(ptr, end);
  if (num_cells > 10000000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable cell count");
  }
  snap.cells.resize(num_cells);
  for (uint32_t i = 0; i < num_cells; ++i) {
    snap.cells[i].kind = static_cast<CellKind>(ReadValue<int>(ptr, end));
    snap.cells[i].origin = static_cast<CellOrigin>(ReadValue<int>(ptr, end));
  }

  // Agents
  uint32_t num_agents = ReadValue<uint32_t>(ptr, end);
  if (num_agents > 10000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable agent count");
  }
  snap.agents.resize(num_agents);
  for (uint32_t i = 0; i < num_agents; ++i) {
    auto& agent = snap.agents[i];
    agent.id = ReadValue<int>(ptr, end);
    agent.type = ReadValue<int>(ptr, end);
    agent.position = ReadPosition(ptr, end);
    agent.prev_position = ReadPosition(ptr, end);
    agent.health = ReadValue<int>(ptr, end);
    agent.max_health = ReadValue<int>(ptr, end);
    agent.agent_index = ReadValue<int>(ptr, end);
    agent.faction = ReadValue<int>(ptr, end);
    agent.direction = ReadValue<int>(ptr, end);
    agent.color = ReadValue<int>(ptr, end);
    agent.alive = ReadValue<bool>(ptr, end);

    // Statuses
    uint32_t num_statuses = ReadValue<uint32_t>(ptr, end);
    if (num_statuses > 1000) {
      throw std::runtime_error("Snapshot buffer corrupt: unreasonable status count");
    }
    agent.statuses.resize(num_statuses);
    for (uint32_t j = 0; j < num_statuses; ++j) {
      agent.statuses[j].type = ReadValue<int>(ptr, end);
      agent.statuses[j].duration = ReadValue<int>(ptr, end);
    }

    // FSM
    agent.has_fsm = ReadValue<bool>(ptr, end);
    if (agent.has_fsm) {
      agent.fsm.state_name = ReadString(ptr, end);
      agent.fsm.target_id = ReadValue<int>(ptr, end);
      agent.fsm.patrol_path = ReadPositionVector(ptr, end);
      agent.fsm.patrol_index = ReadValue<int>(ptr, end);
      agent.fsm.patrol_forward = ReadValue<bool>(ptr, end);
      agent.fsm.detection_range = ReadValue<int>(ptr, end);
      agent.fsm.lose_target_range = ReadValue<int>(ptr, end);
      agent.fsm.rng_state = ReadValue<uint64_t>(ptr, end);
      agent.fsm.rng_inc = ReadValue<uint64_t>(ptr, end);
    }

    // Cadence
    agent.cadence = ReadVector<int>(ptr, end);
    agent.tick = ReadValue<int>(ptr, end);
  }

  // Effects
  uint32_t num_effects = ReadValue<uint32_t>(ptr, end);
  if (num_effects > 10000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable effect count");
  }
  snap.effects.resize(num_effects);
  for (uint32_t i = 0; i < num_effects; ++i) {
    auto& effect = snap.effects[i];
    effect.effect_name = ReadString(ptr, end);
    effect.target_type = ReadValue<int>(ptr, end);
    effect.target_cell = ReadPosition(ptr, end);
    effect.target_actor_id = ReadValue<int>(ptr, end);
    effect.target_actors = ReadVector<int>(ptr, end);
    effect.direction = ReadValue<int>(ptr, end);
    effect.ticks_remaining = ReadValue<int>(ptr, end);
    effect.in_telegraph = ReadValue<bool>(ptr, end);
    effect.loops_remaining = ReadValue<int>(ptr, end);
    effect.source_id = ReadValue<int>(ptr, end);
  }

  // Timing and state
  snap.tick = ReadValue<int>(ptr, end);
  snap.horizon = ReadValue<int>(ptr, end);
  snap.rng_state = ReadValue<uint64_t>(ptr, end);
  snap.rng_inc = ReadValue<uint64_t>(ptr, end);
  snap.d4_transform = ReadValue<int>(ptr, end);

  // Patrol path (for AggroEnv without FSM agents)
  snap.patrol_path = ReadPositionVector(ptr, end);

  return snap;
}

}  // namespace companions
