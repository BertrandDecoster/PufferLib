// Copyright 2024
// Effect configuration implementation

#include "effect_config.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "agent_config.h"  // For TargetFilter
#include "object_manager.h"

namespace companions {

// =============================================================================
// EffectConfig helpers
// =============================================================================

int EffectConfig::GetAreaSize() const {
  if (area.empty()) return 1;
  int n = static_cast<int>(std::sqrt(area.size()));
  return n;
}

bool EffectConfig::IsPositionAffected(int rel_row, int rel_col,
                                       Direction dir) const {
  int n = GetAreaSize();
  int half = n / 2;

  // Rotate relative position based on direction
  // CSV assumes NORTH (up = negative row)
  int rotated_row = rel_row;
  int rotated_col = rel_col;

  switch (dir) {
    case Direction::Up:  // NORTH - no rotation
      break;
    case Direction::Right:  // EAST - rotate 90° CW
      rotated_row = rel_col;
      rotated_col = -rel_row;
      break;
    case Direction::Down:  // SOUTH - rotate 180°
      rotated_row = -rel_row;
      rotated_col = -rel_col;
      break;
    case Direction::Left:  // WEST - rotate 90° CCW
      rotated_row = -rel_col;
      rotated_col = rel_row;
      break;
  }

  // Convert to array index (center is at half, half)
  int array_row = rotated_row + half;
  int array_col = rotated_col + half;

  if (array_row < 0 || array_row >= n || array_col < 0 || array_col >= n) {
    return false;
  }

  int idx = array_row * n + array_col;
  return idx < static_cast<int>(area.size()) && area[idx] != 0;
}

void EffectConfig::GetRotatedPush(Direction dir, int& out_dx,
                                   int& out_dy) const {
  // CSV assumes NORTH direction
  int dx = push_dx;
  int dy = push_dy;

  switch (dir) {
    case Direction::Up:  // NORTH - no rotation
      out_dx = dx;
      out_dy = dy;
      break;
    case Direction::Right:  // EAST - rotate 90° CW
      out_dx = dy;
      out_dy = -dx;
      break;
    case Direction::Down:  // SOUTH - rotate 180°
      out_dx = -dx;
      out_dy = -dy;
      break;
    case Direction::Left:  // WEST - rotate 90° CCW
      out_dx = -dy;
      out_dy = dx;
      break;
  }
}

// =============================================================================
// EffectTarget factory methods
// =============================================================================

EffectTarget EffectTarget::AtCell(Position pos) {
  EffectTarget t;
  t.type = Type::Cell;
  t.cell = pos;
  return t;
}

EffectTarget EffectTarget::OnActor(ObjectId id) {
  EffectTarget t;
  t.type = Type::Actor;
  t.actor_id = id;
  return t;
}

EffectTarget EffectTarget::OnActors(std::vector<ObjectId> ids) {
  EffectTarget t;
  t.type = Type::ActorList;
  t.actors = std::move(ids);
  return t;
}

// =============================================================================
// ActiveEffect
// =============================================================================

bool ActiveEffect::IsFinished() const {
  if (config == nullptr) return true;
  if (ticks_remaining > 0) return false;
  if (in_telegraph) return false;  // Still need to do active phase
  if (loops_remaining > 0) return false;
  if (config->loop == -1) return false;  // Infinite loop
  return true;
}

Position ActiveEffect::GetCenter(const ObjectManager& mgr) const {
  switch (target.type) {
    case EffectTarget::Type::Cell:
      return target.cell;
    case EffectTarget::Type::Actor: {
      const Actor* actor = mgr.GetActor(target.actor_id);
      if (actor && actor->IsAlive()) {
        return actor->GetPosition();
      }
      return target.cell;  // Fallback
    }
    case EffectTarget::Type::ActorList:
      // For actor list, use first actor's position as center
      if (!target.actors.empty()) {
        const Actor* actor = mgr.GetActor(target.actors[0]);
        if (actor && actor->IsAlive()) {
          return actor->GetPosition();
        }
      }
      return target.cell;
  }
  return target.cell;
}

// =============================================================================
// EffectConfigRegistry
// =============================================================================

EffectConfigRegistry& EffectConfigRegistry::Instance() {
  static EffectConfigRegistry instance;
  return instance;
}

void EffectConfigRegistry::Clear() { configs_.clear(); }

void EffectConfigRegistry::RegisterConfig(EffectConfig config) {
  // Check for duplicate
  for (auto& existing : configs_) {
    if (existing.name == config.name) {
      existing = std::move(config);
      return;
    }
  }
  configs_.push_back(std::move(config));
}

const EffectConfig* EffectConfigRegistry::GetConfig(
    const std::string& name) const {
  for (const auto& config : configs_) {
    if (config.name == name) {
      return &config;
    }
  }
  return nullptr;
}

std::vector<std::string> EffectConfigRegistry::GetAllNames() const {
  std::vector<std::string> names;
  names.reserve(configs_.size());
  for (const auto& config : configs_) {
    names.push_back(config.name);
  }
  return names;
}

namespace {

// Helper to parse semicolon-delimited area string
std::vector<int> ParseArea(const std::string& area_str) {
  std::vector<int> result;
  if (area_str.empty()) {
    result.push_back(1);  // Default: single cell
    return result;
  }

  std::stringstream ss(area_str);
  std::string token;
  while (std::getline(ss, token, ';')) {
    if (!token.empty()) {
      result.push_back(std::stoi(token));
    }
  }

  if (result.empty()) {
    result.push_back(1);
  }
  return result;
}

// Helper to parse TargetFilter
TargetFilter ParseFilter(const std::string& filter_str) {
  if (filter_str == "all") return TargetFilter::All;
  if (filter_str == "companion") return TargetFilter::Companion;
  if (filter_str == "enemy") return TargetFilter::Enemy;
  if (filter_str == "neutral") return TargetFilter::Neutral;
  return TargetFilter::All;  // Default
}

// Helper to trim whitespace
std::string Trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

bool EffectConfigRegistry::LoadFromCSV(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::string line;

  // Skip header
  if (!std::getline(file, line)) {
    return false;
  }

  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::stringstream ss(line);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, ',')) {
      tokens.push_back(Trim(token));
    }

    // Expected columns:
    // name,telegraph,active,recovery,loop,area,filter,damage,push_dx,push_dy,push_dist,status,status_dur,visible
    if (tokens.size() < 14) continue;

    EffectConfig config;
    config.name = tokens[0];
    config.telegraph_ticks = std::stoi(tokens[1]);
    config.active_ticks = std::stoi(tokens[2]);
    config.recovery_ticks = std::stoi(tokens[3]);
    config.loop = std::stoi(tokens[4]);
    config.area = ParseArea(tokens[5]);
    config.filter = ParseFilter(tokens[6]);
    config.damage = std::stoi(tokens[7]);
    config.push_dx = std::stoi(tokens[8]);
    config.push_dy = std::stoi(tokens[9]);
    config.push_distance = std::stoi(tokens[10]);
    config.status_applied = tokens[11];
    config.status_duration = std::stoi(tokens[12]);
    config.telegraph_visible = (tokens[13] == "true" || tokens[13] == "1");

    RegisterConfig(std::move(config));
  }

  return true;
}

}  // namespace companions
