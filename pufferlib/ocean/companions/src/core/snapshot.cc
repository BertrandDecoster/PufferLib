// Copyright 2024
// Snapshot implementation

#include "snapshot.h"

#include "fsm/enemies.h"  // FindEnemyKind
#include "object.h"  // StatusType

#include <cstring>
#include <stdexcept>

namespace companions {

bool IsCompanionType(int object_type) {
  switch (static_cast<ObjectType>(object_type)) {
    case ObjectType::Companion:
    case ObjectType::Player:
    case ObjectType::NPCCompanion:
      return true;
    default:
      return false;
  }
}

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

ZoneDef Snapshot::CellZone(const CellTagSnapshot& zone) const {
  auto it = zones.find(zone.tag);
  ZoneDef def = it == zones.end() ? ZoneDef{} : it->second;
  if (zone.duration) def.duration = *zone.duration;
  if (zone.steps) def.steps = *zone.steps;
  if (zone.then) def.then = *zone.then;
  if (zone.damage) def.damage = *zone.damage;
  return def;
}

namespace {

bool IsValidTagDuration(int duration) { return IsValidTimer(duration); }

// `what` names the tag's owner, e.g. "zone at (2, 3)".
void CheckTag(const std::string& tag, int duration, const std::string& what) {
  if (tag.empty()) {
    throw std::runtime_error("Snapshot: " + what + " has an empty tag name");
  }
  if (!IsValidNameLength(tag)) {
    throw std::runtime_error("Snapshot: " + what + " tag \"" + tag + "\" is " +
                             std::to_string(tag.size()) + " bytes, at most " +
                             std::to_string(kMaxNameLength));
  }
  if (!IsValidTagDuration(duration)) {
    throw std::runtime_error("Snapshot: " + what + " tag \"" + tag + "\" has an invalid duration " +
                             std::to_string(duration) + " (1.." + std::to_string(kMaxTimerSteps) +
                             ", or -1)");
  }
}

// A StatusType value this build knows (2, the removed Slowed, is not one).
bool IsKnownStatus(int type) {
  switch (static_cast<StatusType>(type)) {
    case StatusType::None:
    case StatusType::Stunned:
    case StatusType::Marked:
    case StatusType::Rooted:
      return true;
  }
  return false;
}

std::string CellText(const Position& p) {
  return "(" + std::to_string(p.row) + ", " + std::to_string(p.col) + ")";
}

}  // namespace

void Snapshot::ValidateSkillsTagsZones() const {
  // The book LoadSnapshot will build: the builtins, then these skills
  SkillBook book;
  for (size_t i = 0; i < skills.size(); ++i) {
    try {
      // Define validates the config and rejects the default skill's name,
      // but silently skips an unnamed skill: reject that one here.
      if (skills[i].name.empty()) throw std::runtime_error("skill without a name");
      book.Define(skills[i]);
    } catch (const std::runtime_error& e) {
      // The index says which skill when it has no name.
      throw std::runtime_error("Snapshot: skills[" + std::to_string(i) + "]: " + e.what());
    }
  }
  if (max_downs < 1) {
    throw std::runtime_error("Snapshot: max_downs must be >= 1 (got " + std::to_string(max_downs) +
                             ")");
  }
  // The rules LoadSnapshot will set, against that book. Absent, the default
  // ones: a level retuning their skill with a cooldown is rejected too.
  try {
    ValidateContextSkills(context_skills ? *context_skills : DefaultContextSkills(), book);
  } catch (const std::runtime_error& e) {
    throw std::runtime_error(std::string("Snapshot: ") + e.what() +
                             (context_skills ? "" : " (the default rules: the snapshot has none)"));
  }
  // v7: the level's combo rules (names only need to be valid: a successor or
  // zone_becomes the table does not define gets the defaults, cycles are legal)
  try {
    ValidateZoneTable(zones);
    ValidateReactions(reactions);
    ValidateTagStatuses(tag_statuses);
  } catch (const std::runtime_error& e) {
    throw std::runtime_error(std::string("Snapshot: ") + e.what());
  }
  for (size_t i = 0; i < agents.size(); ++i) {
    const AgentSnapshot& agent = agents[i];
    const std::string who = "agent #" + std::to_string(i) + " (id " + std::to_string(agent.id) + ")";
    // A revive brings back a percent of it, at least 1 HP (Companion::Revive)
    if (agent.max_health < 1) {
      throw std::runtime_error("Snapshot: " + who + ": max_health must be >= 1 (got " +
                               std::to_string(agent.max_health) + ")");
    }
    if (!agent.kind.empty()) {
      if (agent.type != static_cast<int>(ObjectType::AgentFSM)) {
        throw std::runtime_error("Snapshot: " + who + ": kind \"" + agent.kind +
                                 "\" is only for an AgentFSM agent");
      }
      if (!FindEnemyKind(agent.kind)) {
        std::string known;
        for (const EnemyKind& k : EnemyKinds()) known += (known.empty() ? "" : ", ") + std::string(k.name);
        throw std::runtime_error("Snapshot: " + who + ": unknown kind \"" + agent.kind +
                                 "\" (known: " + known + ")");
      }
    }
    for (const TagSnapshot& t : agent.tags) CheckTag(t.tag, t.duration, who);
    try {  // v7, any agent type
      ValidateWeaknesses(agent.weak_to);
      ValidateImmunities(agent.immune);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error("Snapshot: " + who + ": " + e.what());
    }
    for (size_t k = 0; k < agent.statuses.size(); ++k) {
      const int type = agent.statuses[k].type;
      if (IsKnownStatus(type)) continue;
      const std::string where = "Snapshot: " + who + ": statuses[" + std::to_string(k) + "]: ";
      if (type == 2) throw std::runtime_error(where + "status 2 (slowed) was removed");
      throw std::runtime_error(where + "unknown status " + std::to_string(type));
    }
    if (agent.skills.size() > static_cast<size_t>(kMaxSkillSlots) ||
        agent.cooldowns.size() > static_cast<size_t>(kMaxSkillSlots)) {
      throw std::runtime_error("Snapshot: " + who + " has " + std::to_string(agent.skills.size()) +
                               " skill slots and " + std::to_string(agent.cooldowns.size()) +
                               " cooldowns, at most " + std::to_string(kMaxSkillSlots) + " each");
    }
    for (size_t slot = 0; slot < agent.skills.size(); ++slot) {
      const std::string& name = agent.skills[slot];
      if (!IsValidNameLength(name)) {
        throw std::runtime_error("Snapshot: " + who + ": skills[" + std::to_string(slot) + "] \"" +
                                 name + "\" is " + std::to_string(name.size()) +
                                 " bytes, at most " + std::to_string(kMaxNameLength));
      }
      // Slots always hold a real skill ("" is kDefaultSkill)
      if (!name.empty() && book.Find(name) == nullptr) {
        throw std::runtime_error("Snapshot: " + who + ": skills[" + std::to_string(slot) + "] \"" +
                                 name + "\" is an unknown skill (neither a builtin nor in the "
                                 "snapshot's skills)");
      }
    }
    for (size_t slot = 0; slot < agent.cooldowns.size(); ++slot) {
      if (agent.cooldowns[slot] < 0) {
        throw std::runtime_error("Snapshot: " + who + ": cooldowns[" + std::to_string(slot) +
                                 "] must be >= 0 (got " + std::to_string(agent.cooldowns[slot]) +
                                 ")");
      }
    }
    if (agent.downed || agent.times_downed != 0) {
      if (!IsCompanionType(agent.type)) {
        throw std::runtime_error("Snapshot: " + who +
                                 ": downed / times_downed on a non-companion (only a "
                                 "companion goes down)");
      }
      if (agent.downed && agent.health != 0) {
        throw std::runtime_error("Snapshot: " + who + ": downed with " +
                                 std::to_string(agent.health) + " HP, a downed companion has 0 HP");
      }
      if (agent.times_downed < 0 || (agent.downed && agent.times_downed < 1)) {
        throw std::runtime_error("Snapshot: " + who +
                                 ": times_downed must be >= 0 (>= 1 when downed), got " +
                                 std::to_string(agent.times_downed));
      }
      // Going down clears them, and a downed agent accepts none
      if (agent.downed && !agent.statuses.empty()) {
        throw std::runtime_error("Snapshot: " + who +
                                 ": downed with statuses, a downed companion has none");
      }
    }
  }
  for (const CellTagSnapshot& z : cell_tags) {
    const std::string where = "zone at " + CellText(z.cell);
    CheckTag(z.tag, z.duration.value_or(kPermanentTag), where);
    // v7: the fields the cell has (the table's, checked above, fill the others)
    ZoneDef own;
    if (z.duration) own.duration = *z.duration;
    if (z.steps) own.steps = *z.steps;
    if (z.then) own.then = *z.then;
    if (z.damage) own.damage = *z.damage;
    try {
      ValidateZoneDef(where, own);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error(std::string("Snapshot: ") + e.what());
    }
    if (z.cell.row < 0 || z.cell.row >= rows || z.cell.col < 0 || z.cell.col >= cols) {
      throw std::runtime_error("Snapshot: zone \"" + z.tag + "\" at " + CellText(z.cell) +
                               " is outside the " + std::to_string(rows) + "x" +
                               std::to_string(cols) + " grid");
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
  if (sizeof(T) > static_cast<size_t>(end - ptr)) {  // Never forms a pointer past the end
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
  if (len > static_cast<size_t>(end - ptr)) {
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

// A count read from the buffer must fit in what is left of it, each item
// taking at least `min_item_bytes`: a corrupt count then fails here instead of
// allocating for it.
void CheckCountFits(uint32_t count, size_t min_item_bytes, const uint8_t* ptr,
                    const uint8_t* end, const char* what) {
  const size_t left = static_cast<size_t>(end - ptr);
  if (count > left / min_item_bytes) {
    throw std::runtime_error(std::string("Snapshot buffer corrupt: ") + what + " " +
                             std::to_string(count) + " exceeds the " + std::to_string(left) +
                             " bytes left");
  }
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
  WriteValue(buffer, s.damage);
  WriteValue(buffer, s.friendly_fire);
  WriteValue(buffer, s.self_tags);
  WriteValue(buffer, s.self_motion);
  WriteValue(buffer, s.self_root);
  WriteValue(buffer, s.self_damage);
  // v6
  WriteValue(buffer, static_cast<uint8_t>(s.affects_downed));  // Read as a byte
  WriteValue(buffer, s.revive_percent);
}

// `version`: the file's; a record before v6 has no revive fields.
SkillConfig ReadSkill(const uint8_t*& ptr, const uint8_t* end, uint32_t version) {
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
  s.damage = ReadValue<int>(ptr, end);
  s.friendly_fire = ReadValue<bool>(ptr, end);
  s.self_tags = ReadValue<bool>(ptr, end);
  s.self_motion = ReadValue<bool>(ptr, end);
  s.self_root = ReadValue<bool>(ptr, end);
  s.self_damage = ReadValue<bool>(ptr, end);
  if (version >= 6) {
    // A byte, not a bool: any value read from the buffer is a valid one
    s.affects_downed = ReadValue<uint8_t>(ptr, end) != 0;
    s.revive_percent = ReadValue<int>(ptr, end);
  }
  return s;
}

// v6: the context skill rules (the reader validates them)
void WriteContextSkills(std::vector<uint8_t>& buffer, const std::vector<ContextSkillRule>& rules) {
  WriteValue(buffer, static_cast<uint32_t>(rules.size()));
  for (const ContextSkillRule& r : rules) {
    WriteValue(buffer, static_cast<int>(r.condition));
    WriteValue(buffer, r.slot);
    WriteString(buffer, r.skill);
  }
}

std::vector<ContextSkillRule> ReadContextSkills(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t count = ReadValue<uint32_t>(ptr, end);
  // condition, slot, skill name length
  CheckCountFits(count, 2 * sizeof(int) + sizeof(uint32_t), ptr, end, "context skill count");
  std::vector<ContextSkillRule> rules(count);
  for (ContextSkillRule& r : rules) {
    r.condition = static_cast<ContextCondition>(ReadValue<int>(ptr, end));
    r.slot = ReadValue<int>(ptr, end);
    r.skill = ReadString(ptr, end);
  }
  return rules;
}

// v7: names (the reader validates them)
void WriteStringList(std::vector<uint8_t>& buffer, const std::vector<std::string>& names) {
  WriteValue(buffer, static_cast<uint32_t>(names.size()));
  for (const std::string& name : names) WriteString(buffer, name);
}

std::vector<std::string> ReadStringList(const uint8_t*& ptr, const uint8_t* end, const char* what) {
  uint32_t count = ReadValue<uint32_t>(ptr, end);
  CheckCountFits(count, sizeof(uint32_t), ptr, end, what);  // Each: a length
  std::vector<std::string> names(count);
  for (std::string& name : names) name = ReadString(ptr, end);
  return names;
}

// v7: an agent's weaknesses and immunities, closing its record
void WriteAgentRules(std::vector<uint8_t>& buffer, const AgentSnapshot& agent) {
  WriteValue(buffer, static_cast<uint32_t>(agent.weak_to.size()));
  for (const TagWeakness& w : agent.weak_to) {
    WriteString(buffer, w.zone);
    WriteString(buffer, w.tag);
  }
  WriteStringList(buffer, agent.immune);
}

void ReadAgentRules(const uint8_t*& ptr, const uint8_t* end, AgentSnapshot& agent) {
  uint32_t count = ReadValue<uint32_t>(ptr, end);
  CheckCountFits(count, 2 * sizeof(uint32_t), ptr, end, "weakness count");  // Two lengths
  agent.weak_to.resize(count);
  for (TagWeakness& w : agent.weak_to) {
    w.zone = ReadString(ptr, end);
    w.tag = ReadString(ptr, end);
  }
  agent.immune = ReadStringList(ptr, end, "immunity count");
}

// A zone record's fields (v7): a byte of those present, then each present
// one, in this order. Older records have the duration only.
constexpr uint8_t kZoneHasDuration = 1;
constexpr uint8_t kZoneHasSteps = 2;
constexpr uint8_t kZoneHasThen = 4;
constexpr uint8_t kZoneHasDamage = 8;

void WriteZoneFields(std::vector<uint8_t>& buffer, const CellTagSnapshot& z) {
  const uint8_t mask = static_cast<uint8_t>((z.duration ? kZoneHasDuration : 0) |
                                            (z.steps ? kZoneHasSteps : 0) |
                                            (z.then ? kZoneHasThen : 0) |
                                            (z.damage ? kZoneHasDamage : 0));
  WriteValue(buffer, mask);
  if (z.duration) WriteValue(buffer, *z.duration);
  if (z.steps) WriteValue(buffer, *z.steps);
  if (z.then) WriteString(buffer, *z.then);
  if (z.damage) WriteValue(buffer, *z.damage);
}

void ReadZoneFields(const uint8_t*& ptr, const uint8_t* end, CellTagSnapshot& z) {
  const uint8_t mask = ReadValue<uint8_t>(ptr, end);
  if (mask & ~(kZoneHasDuration | kZoneHasSteps | kZoneHasThen | kZoneHasDamage)) {
    throw std::runtime_error("Snapshot buffer corrupt: unknown zone fields " +
                             std::to_string(mask));
  }
  if (mask & kZoneHasDuration) z.duration = ReadValue<int>(ptr, end);
  if (mask & kZoneHasSteps) z.steps = ReadValue<int>(ptr, end);
  if (mask & kZoneHasThen) z.then = ReadString(ptr, end);
  if (mask & kZoneHasDamage) z.damage = ReadValue<int>(ptr, end);
}

// v7: the level's zone table, reactions and tag statuses, last in the file
void WriteLevelRules(std::vector<uint8_t>& buffer, const Snapshot& snap) {
  WriteValue(buffer, static_cast<uint32_t>(snap.zones.size()));
  for (const auto& [tag, zone] : snap.zones) {
    WriteString(buffer, tag);
    WriteValue(buffer, zone.duration);
    WriteValue(buffer, zone.steps);
    WriteString(buffer, zone.then);
    WriteValue(buffer, zone.damage);
  }
  WriteValue(buffer, static_cast<uint32_t>(snap.reactions.size()));
  for (const ReactionRule& r : snap.reactions) {
    WriteString(buffer, r.a);
    WriteString(buffer, r.b);
    WriteString(buffer, r.result);
    WriteStringList(buffer, r.keep);
    WriteValue(buffer, r.damage);
    WriteValue(buffer, static_cast<uint8_t>(r.spread));  // Read as a byte
    WriteString(buffer, r.zone_becomes);
  }
  WriteValue(buffer, static_cast<uint32_t>(snap.tag_statuses.size()));
  for (const TagStatusRule& r : snap.tag_statuses) {
    WriteString(buffer, r.tag);
    WriteValue(buffer, static_cast<int>(r.status));
    WriteValue(buffer, r.steps);
  }
}

void ReadLevelRules(const uint8_t*& ptr, const uint8_t* end, Snapshot& snap) {
  uint32_t count = ReadValue<uint32_t>(ptr, end);
  // tag length, duration, steps, then length, damage
  CheckCountFits(count, 5 * sizeof(int), ptr, end, "zone table count");
  for (uint32_t i = 0; i < count; ++i) {
    std::string tag = ReadString(ptr, end);
    ZoneDef zone;
    zone.duration = ReadValue<int>(ptr, end);
    zone.steps = ReadValue<int>(ptr, end);
    zone.then = ReadString(ptr, end);
    zone.damage = ReadValue<int>(ptr, end);
    if (!snap.zones.emplace(std::move(tag), std::move(zone)).second) {
      throw std::runtime_error("Snapshot buffer corrupt: a zone table tag twice");
    }
  }
  count = ReadValue<uint32_t>(ptr, end);
  // a, b, result lengths, keep count, damage, zone_becomes length, spread (a byte)
  CheckCountFits(count, 6 * sizeof(uint32_t) + 1, ptr, end, "reaction count");
  snap.reactions.resize(count);
  for (ReactionRule& r : snap.reactions) {
    r.a = ReadString(ptr, end);
    r.b = ReadString(ptr, end);
    r.result = ReadString(ptr, end);
    r.keep = ReadStringList(ptr, end, "reaction keep count");
    r.damage = ReadValue<int>(ptr, end);
    r.spread = ReadValue<uint8_t>(ptr, end) != 0;  // A byte: any value read is a valid one
    r.zone_becomes = ReadString(ptr, end);
  }
  count = ReadValue<uint32_t>(ptr, end);
  // tag length, status, steps
  CheckCountFits(count, 3 * sizeof(int), ptr, end, "tag status count");
  snap.tag_statuses.resize(count);
  for (TagStatusRule& r : snap.tag_statuses) {
    r.tag = ReadString(ptr, end);
    r.status = static_cast<StatusType>(ReadValue<int>(ptr, end));  // Validated with the rest
    r.steps = ReadValue<int>(ptr, end);
  }
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
  // version 4 skills, agent tags / skill slots / cooldowns and zones;
  // version 5 downs (agent downed / times_downed, max_downs); version 6
  // skills' affects_downed / revive_percent and the context skill rules;
  // version 7 the zone table, each zone's fields, reactions, tag statuses and
  // each agent's weaknesses / immunities.
  WriteValue(buffer, static_cast<uint32_t>(7));

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

    // v5: downs
    WriteValue(buffer, static_cast<uint8_t>(agent.downed));  // Read as a byte
    WriteValue(buffer, agent.times_downed);

    // v7: weaknesses, immunities
    WriteAgentRules(buffer, agent);
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
    WriteZoneFields(buffer, z);  // v7: those present (v4-v6: the duration)
  }

  // v5: the level's max downs
  WriteValue(buffer, max_downs);

  // v6: the context skill rules, when present (a byte: has them, then the list)
  WriteValue(buffer, static_cast<uint8_t>(context_skills.has_value()));
  if (context_skills) WriteContextSkills(buffer, *context_skills);

  // v7: the zone table, the reactions and the tag statuses
  WriteLevelRules(buffer, *this);

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
  if (version < 1 || version > 7) {
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

    if (version >= 5) {
      // A byte, not a bool: any value read from the buffer is a valid one
      agent.downed = ReadValue<uint8_t>(ptr, end) != 0;
      agent.times_downed = ReadValue<int>(ptr, end);
    }

    if (version >= 7) ReadAgentRules(ptr, end, agent);
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
    // name length, 9 ints (targeting .. damage), tag count, 6 bools (tag_path,
    // friendly_fire, self_tags, self_motion, self_root, self_damage); v6:
    // affects_downed (a byte), revive_percent
    const size_t v6_bytes = version >= 6 ? sizeof(uint8_t) + sizeof(int) : 0;
    CheckCountFits(num_skills,
                   sizeof(uint32_t) + 9 * sizeof(int) + sizeof(uint32_t) + 6 * sizeof(bool) + v6_bytes,
                   ptr, end, "skill count");
    snap.skills.reserve(num_skills);
    for (uint32_t i = 0; i < num_skills; ++i) snap.skills.push_back(ReadSkill(ptr, end, version));
    uint32_t num_zones = ReadValue<uint32_t>(ptr, end);
    if (num_zones > 10000000) {
      throw std::runtime_error("Snapshot buffer corrupt: unreasonable zone count");
    }
    // cell, tag name length, then the duration (v4-v6) or the fields' byte (v7)
    CheckCountFits(num_zones,
                   2 * sizeof(int) + sizeof(uint32_t) + (version >= 7 ? 1 : sizeof(int)), ptr,
                   end, "zone count");
    snap.cell_tags.resize(num_zones);
    for (CellTagSnapshot& z : snap.cell_tags) {
      z.cell = ReadPosition(ptr, end);
      z.tag = ReadString(ptr, end);
      if (version >= 7) {
        ReadZoneFields(ptr, end, z);
      } else {
        z.duration = ReadValue<int>(ptr, end);  // The other fields: the table (none)
      }
    }
  }

  // The level's max downs (v5+); older snapshots take the default.
  if (version >= 5) snap.max_downs = ReadValue<int>(ptr, end);

  // The context skill rules (v6+); older snapshots have none (the default
  // rules on load).
  if (version >= 6 && ReadValue<uint8_t>(ptr, end) != 0) {
    snap.context_skills = ReadContextSkills(ptr, end);
  }

  // The level's combo rules (v7+); older snapshots have none.
  if (version >= 7) {
    ReadLevelRules(ptr, end, snap);
    // The last block: anything after it is corrupt (older versions did not
    // check, and keep loading as they did)
    if (ptr != end) {
      throw std::runtime_error("Snapshot buffer corrupt: " +
                               std::to_string(static_cast<size_t>(end - ptr)) +
                               " trailing bytes after the last block");
    }
  }

  snap.ValidateSkillsTagsZones();
  return snap;
}

}  // namespace companions
