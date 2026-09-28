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
  for (const AnnotationSnapshot& a : annotations) {
    if (a.tag == SemanticTag::SynchroGoal && a.target_type == 0) return true;
  }
  return false;
}

bool Snapshot::HasTargetCell() const {
  for (const AnnotationSnapshot& a : annotations) {
    if (a.tag == SemanticTag::AggroTarget && a.target_type == 0) return true;
  }
  return false;
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

namespace {

bool IsValidTagDuration(int duration) {
  return duration == kPermanentTag || duration > 0;
}

void CheckTag(const std::string& tag, int duration, const char* what) {
  if (tag.empty()) {
    throw std::runtime_error(std::string("Snapshot: ") + what + " with an empty tag name");
  }
  if (!IsValidTagDuration(duration)) {
    throw std::runtime_error(std::string("Snapshot: ") + what + " \"" + tag +
                             "\" has an invalid duration " + std::to_string(duration) +
                             " (positive or -1)");
  }
}

template <typename E>
bool EnumInRange(E value, E last) {
  const int v = static_cast<int>(value);
  return v >= 0 && v <= static_cast<int>(last);
}

}  // namespace

void Snapshot::ValidateSkillsTagsZones() const {
  for (const SkillConfig& skill : skills) {
    if (skill.name.empty()) throw std::runtime_error("Snapshot: skill without a name");
    if (!EnumInRange(skill.targeting, SkillTargeting::Projectile) ||
        !EnumInRange(skill.filter, TargetFilter::Neutral) ||
        !EnumInRange(skill.area, SkillArea::Cross) ||
        !EnumInRange(skill.motion, SkillMotion::PullIn)) {
      throw std::runtime_error("Snapshot: skill \"" + skill.name + "\" has an unknown enum value");
    }
    for (const SkillTagSpec& t : skill.tags) CheckTag(t.tag, t.duration, "skill tag");
  }
  for (const AgentSnapshot& agent : agents) {
    for (const TagSnapshot& t : agent.tags) CheckTag(t.tag, t.duration, "agent tag");
    if (agent.skills.size() > static_cast<size_t>(kMaxSkillSlots) ||
        agent.cooldowns.size() > static_cast<size_t>(kMaxSkillSlots)) {
      throw std::runtime_error("Snapshot: more than kMaxSkillSlots skill slots / cooldowns");
    }
    for (int cooldown : agent.cooldowns) {
      if (cooldown < 0) throw std::runtime_error("Snapshot: negative skill cooldown");
    }
  }
  for (const CellTagSnapshot& z : cell_tags) {
    CheckTag(z.tag, z.duration, "zone");
    if (z.cell.row < 0 || z.cell.row >= rows || z.cell.col < 0 || z.cell.col >= cols) {
      throw std::runtime_error("Snapshot: zone outside the grid");
    }
  }
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

// v4: skill configs and tags (names; the reader validates the values)
void WriteTagList(std::vector<uint8_t>& buffer, const std::vector<TagSnapshot>& tags) {
  WriteValue(buffer, static_cast<uint32_t>(tags.size()));
  for (const TagSnapshot& t : tags) {
    WriteString(buffer, t.tag);
    WriteValue(buffer, t.duration);
  }
}

std::vector<TagSnapshot> ReadTagList(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t size = ReadValue<uint32_t>(ptr, end);
  if (size > 100000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable tag count");
  }
  std::vector<TagSnapshot> tags(size);
  for (TagSnapshot& t : tags) {
    t.tag = ReadString(ptr, end);
    t.duration = ReadValue<int>(ptr, end);
  }
  return tags;
}

void WriteSkill(std::vector<uint8_t>& buffer, const SkillConfig& s) {
  WriteString(buffer, s.name);
  WriteValue(buffer, static_cast<int>(s.targeting));
  WriteValue(buffer, s.range);
  WriteValue(buffer, static_cast<int>(s.filter));
  WriteValue(buffer, static_cast<int>(s.area));
  WriteValue(buffer, static_cast<int>(s.motion));
  WriteValue(buffer, s.motion_distance);
  WriteValue(buffer, s.tag_path);
  WriteValue(buffer, static_cast<uint32_t>(s.tags.size()));
  for (const SkillTagSpec& t : s.tags) {
    WriteString(buffer, t.tag);
    WriteValue(buffer, t.duration);
  }
  WriteValue(buffer, s.root_steps);
  WriteValue(buffer, s.cooldown);
}

SkillConfig ReadSkill(const uint8_t*& ptr, const uint8_t* end) {
  SkillConfig s;
  s.name = ReadString(ptr, end);
  s.targeting = static_cast<SkillTargeting>(ReadValue<int>(ptr, end));
  s.range = ReadValue<int>(ptr, end);
  s.filter = static_cast<TargetFilter>(ReadValue<int>(ptr, end));
  s.area = static_cast<SkillArea>(ReadValue<int>(ptr, end));
  s.motion = static_cast<SkillMotion>(ReadValue<int>(ptr, end));
  s.motion_distance = ReadValue<int>(ptr, end);
  s.tag_path = ReadValue<bool>(ptr, end);
  uint32_t num_tags = ReadValue<uint32_t>(ptr, end);
  if (num_tags > 100000) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable skill tag count");
  }
  s.tags.resize(num_tags);
  for (SkillTagSpec& t : s.tags) {
    t.tag = ReadString(ptr, end);
    t.duration = ReadValue<int>(ptr, end);
  }
  s.root_steps = ReadValue<int>(ptr, end);
  s.cooldown = ReadValue<int>(ptr, end);
  return s;
}

}  // namespace

// =============================================================================
// Snapshot serialization
// =============================================================================

std::vector<uint8_t> Snapshot::Serialize() const {
  std::vector<uint8_t> buffer;

  // Magic number and version
  WriteValue(buffer, static_cast<uint32_t>(0x534E4150));  // "SNAP"
  // Version 2 added annotations; version 3 agent kind + attack config;
  // version 4 skills, agent tags / skill slots / cooldowns and zones.
  WriteValue(buffer, static_cast<uint32_t>(4));

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
      WriteValue(buffer, static_cast<uint8_t>(agent.fsm.state_type));
      WriteValue(buffer, agent.fsm.target_id);
      WritePositionVector(buffer, agent.fsm.patrol_path);
      WriteValue(buffer, agent.fsm.patrol_index);
      WriteValue(buffer, agent.fsm.patrol_forward);
      WriteValue(buffer, agent.fsm.detection_range);
      WriteValue(buffer, agent.fsm.lose_target_range);
      WriteValue(buffer, agent.fsm.rng_state);
      WriteValue(buffer, agent.fsm.rng_inc);
      // Attack runtime state
      WriteValue(buffer, agent.fsm.attack_tick_counter);
      WritePosition(buffer, agent.fsm.attack_target_position);
      WriteValue(buffer, agent.fsm.attack_area_width);
      WriteValue(buffer, agent.fsm.attack_area_height);
      WriteValue(buffer, agent.fsm.attack_damage);
      WriteValue(buffer, static_cast<int>(agent.fsm.attack_filter));
    }

    // Cadence
    WriteVector(buffer, agent.cadence);
    WriteValue(buffer, agent.tick);

    // v3: concrete class + attack configuration
    WriteString(buffer, agent.kind);
    if (agent.has_fsm) {
      WriteValue(buffer, agent.fsm.has_attack);
      WriteString(buffer, agent.fsm.attack_effect);
      WriteValue(buffer, agent.fsm.telegraph_ticks);
      WriteValue(buffer, agent.fsm.attack_ticks);
      WriteValue(buffer, agent.fsm.recovery_ticks);
    }

    // v4: tags, skill slots, cooldowns
    WriteTagList(buffer, agent.tags);
    WriteValue(buffer, static_cast<uint32_t>(agent.skills.size()));
    for (const std::string& name : agent.skills) WriteString(buffer, name);
    WriteVector(buffer, agent.cooldowns);
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

  // Annotations (v2+)
  WriteValue(buffer, static_cast<uint32_t>(annotations.size()));
  for (const auto& a : annotations) {
    WriteValue(buffer, a.target_type);
    WritePosition(buffer, a.pos);
    WriteValue(buffer, a.agent_id);
    WriteValue(buffer, static_cast<uint16_t>(a.tag));
    WriteValue(buffer, a.owner_lens_id);
    WriteValue(buffer, static_cast<uint32_t>(a.params.size()));
    for (const auto& kv : a.params) {
      WriteString(buffer, kv.first);
      WriteString(buffer, kv.second);
    }
  }

  // v4: skill book and zones
  WriteValue(buffer, static_cast<uint32_t>(skills.size()));
  for (const SkillConfig& skill : skills) WriteSkill(buffer, skill);
  WriteValue(buffer, static_cast<uint32_t>(cell_tags.size()));
  for (const CellTagSnapshot& z : cell_tags) {
    WritePosition(buffer, z.cell);
    WriteString(buffer, z.tag);
    WriteValue(buffer, z.duration);
  }

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
  if (version < 1 || version > 4) {
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

  // v1 → v2 CellKind migration. Old enum reserved values 3 (Synchro) and 5
  // (Target) for task-semantic roles that now live in AnnotationStore, and
  // HealArea was at int 4 instead of 3. Remap and emit persistent annotations
  // at the former-semantic positions so downstream code still sees the task
  // roles.  v2 snapshots fall through to a plain static_cast.
  std::vector<AnnotationSnapshot> v1_migrated_annotations;
  for (uint32_t i = 0; i < num_cells; ++i) {
    int kind_int = ReadValue<int>(ptr, end);
    int origin_int = ReadValue<int>(ptr, end);
    if (version == 1) {
      const int row = snap.cols > 0 ? static_cast<int>(i) / snap.cols : 0;
      const int col = snap.cols > 0 ? static_cast<int>(i) % snap.cols : 0;
      switch (kind_int) {
        case 0: snap.cells[i].kind = CellKind::Floor; break;
        case 1: snap.cells[i].kind = CellKind::Wall; break;
        case 2: snap.cells[i].kind = CellKind::Hazard; break;
        case 3: {  // Old Synchro → Floor + SynchroGoal annotation
          snap.cells[i].kind = CellKind::Floor;
          AnnotationSnapshot a;
          a.target_type = 0;
          a.pos = Position{row, col};
          a.agent_id = kInvalidObjectId;
          a.tag = SemanticTag::SynchroGoal;
          a.owner_lens_id = -1;
          v1_migrated_annotations.push_back(std::move(a));
          break;
        }
        case 4: snap.cells[i].kind = CellKind::HealArea; break;  // Old 4 → new 3
        case 5: {  // Old Target → Floor + AggroTarget annotation
          snap.cells[i].kind = CellKind::Floor;
          AnnotationSnapshot a;
          a.target_type = 0;
          a.pos = Position{row, col};
          a.agent_id = kInvalidObjectId;
          a.tag = SemanticTag::AggroTarget;
          a.owner_lens_id = -1;
          v1_migrated_annotations.push_back(std::move(a));
          break;
        }
        default:
          throw std::runtime_error("Snapshot v1: unknown CellKind value");
      }
    } else {
      snap.cells[i].kind = static_cast<CellKind>(kind_int);
    }
    snap.cells[i].origin = static_cast<CellOrigin>(origin_int);
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
      agent.fsm.state_type = static_cast<FSMStateType>(ReadValue<uint8_t>(ptr, end));
      agent.fsm.target_id = ReadValue<int>(ptr, end);
      agent.fsm.patrol_path = ReadPositionVector(ptr, end);
      agent.fsm.patrol_index = ReadValue<int>(ptr, end);
      agent.fsm.patrol_forward = ReadValue<bool>(ptr, end);
      agent.fsm.detection_range = ReadValue<int>(ptr, end);
      agent.fsm.lose_target_range = ReadValue<int>(ptr, end);
      agent.fsm.rng_state = ReadValue<uint64_t>(ptr, end);
      agent.fsm.rng_inc = ReadValue<uint64_t>(ptr, end);
      // Attack runtime state
      agent.fsm.attack_tick_counter = ReadValue<int>(ptr, end);
      agent.fsm.attack_target_position = ReadPosition(ptr, end);
      agent.fsm.attack_area_width = ReadValue<int>(ptr, end);
      agent.fsm.attack_area_height = ReadValue<int>(ptr, end);
      agent.fsm.attack_damage = ReadValue<int>(ptr, end);
      agent.fsm.attack_filter = static_cast<TargetFilter>(ReadValue<int>(ptr, end));
    }

    // Cadence
    agent.cadence = ReadVector<int>(ptr, end);
    agent.tick = ReadValue<int>(ptr, end);

    if (version >= 3) {
      agent.kind = ReadString(ptr, end);
      if (agent.has_fsm) {
        agent.fsm.has_attack = ReadValue<bool>(ptr, end);
        agent.fsm.attack_effect = ReadString(ptr, end);
        agent.fsm.telegraph_ticks = ReadValue<int>(ptr, end);
        agent.fsm.attack_ticks = ReadValue<int>(ptr, end);
        agent.fsm.recovery_ticks = ReadValue<int>(ptr, end);
      }
    }

    if (version >= 4) {
      agent.tags = ReadTagList(ptr, end);
      uint32_t num_slots = ReadValue<uint32_t>(ptr, end);
      if (num_slots > 1000) {
        throw std::runtime_error("Snapshot buffer corrupt: unreasonable skill slot count");
      }
      agent.skills.resize(num_slots);
      for (std::string& name : agent.skills) name = ReadString(ptr, end);
      agent.cooldowns = ReadVector<int>(ptr, end);
    }
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

  // Annotations (v2+). v1 snapshots have no annotations block; the migrated
  // annotations from the v1 cell remap are adopted below.
  if (version >= 2) {
    uint32_t num_annotations = ReadValue<uint32_t>(ptr, end);
    if (num_annotations > 1000000) {
      throw std::runtime_error("Snapshot buffer corrupt: unreasonable annotation count");
    }
    snap.annotations.resize(num_annotations);
    for (uint32_t i = 0; i < num_annotations; ++i) {
      auto& a = snap.annotations[i];
      a.target_type = ReadValue<uint8_t>(ptr, end);
      a.pos = ReadPosition(ptr, end);
      a.agent_id = ReadValue<ObjectId>(ptr, end);
      a.tag = static_cast<SemanticTag>(ReadValue<uint16_t>(ptr, end));
      a.owner_lens_id = ReadValue<int32_t>(ptr, end);
      uint32_t num_params = ReadValue<uint32_t>(ptr, end);
      if (num_params > 1000) {
        throw std::runtime_error("Snapshot buffer corrupt: unreasonable annotation param count");
      }
      a.params.reserve(num_params);
      for (uint32_t j = 0; j < num_params; ++j) {
        std::string k = ReadString(ptr, end);
        std::string v = ReadString(ptr, end);
        a.params.emplace_back(std::move(k), std::move(v));
      }
    }
  } else {
    snap.annotations = std::move(v1_migrated_annotations);
  }

  // Skill book and zones (v4+); older snapshots have none.
  if (version >= 4) {
    uint32_t num_skills = ReadValue<uint32_t>(ptr, end);
    if (num_skills > 100000) {
      throw std::runtime_error("Snapshot buffer corrupt: unreasonable skill count");
    }
    snap.skills.reserve(num_skills);
    for (uint32_t i = 0; i < num_skills; ++i) snap.skills.push_back(ReadSkill(ptr, end));
    uint32_t num_zones = ReadValue<uint32_t>(ptr, end);
    if (num_zones > 10000000) {
      throw std::runtime_error("Snapshot buffer corrupt: unreasonable zone count");
    }
    snap.cell_tags.resize(num_zones);
    for (CellTagSnapshot& z : snap.cell_tags) {
      z.cell = ReadPosition(ptr, end);
      z.tag = ReadString(ptr, end);
      z.duration = ReadValue<int>(ptr, end);
    }
  }

  snap.ValidateSkillsTagsZones();
  return snap;
}

}  // namespace companions
