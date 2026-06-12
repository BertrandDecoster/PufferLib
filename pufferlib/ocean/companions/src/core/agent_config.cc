// Copyright 2024
// Agent configuration implementation with CSV loading

#include "agent_config.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "csv_utils.h"
#include "enum_strings.h"

namespace companions {

AgentConfigRegistry& AgentConfigRegistry::Instance() {
  static AgentConfigRegistry instance;
  return instance;
}

void AgentConfigRegistry::Clear() { configs_.clear(); }

void AgentConfigRegistry::RegisterConfig(AgentConfig config) {
  // Replace existing config with same name
  auto it = std::find_if(configs_.begin(), configs_.end(),
                         [&](const AgentConfig& c) {
                           return c.type_name == config.type_name;
                         });
  if (it != configs_.end()) {
    *it = std::move(config);
  } else {
    configs_.push_back(std::move(config));
  }
}

const AgentConfig* AgentConfigRegistry::GetConfig(
    const std::string& type_name) const {
  auto it = std::find_if(configs_.begin(), configs_.end(),
                         [&](const AgentConfig& c) {
                           return c.type_name == type_name;
                         });
  return it != configs_.end() ? &(*it) : nullptr;
}

std::vector<std::string> AgentConfigRegistry::GetAllTypeNames() const {
  std::vector<std::string> names;
  names.reserve(configs_.size());
  for (const auto& config : configs_) {
    names.push_back(config.type_name);
  }
  return names;
}

namespace {

// Trim / ParseCSVLine / SplitOn come from csv_utils.h (shared with
// effect_config.cc).

// Parse cadence string like "1;0" or "" for empty
// (semicolon-separated since comma is the CSV column delimiter).
std::vector<int> ParseCadence(const std::string& str) {
  std::vector<int> result;
  if (str.empty()) return result;

  for (const std::string& item : SplitOn(str, ';')) {
    result.push_back(std::stoi(Trim(item)));
  }
  return result;
}

// Faction / TargetFilter columns parse via the shared CSV-vocabulary
// readers FactionFromCSV / TargetFilterFromCSV (enum_strings.h).

// Parse bool string
bool ParseBool(const std::string& str) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower == "true" || lower == "1" || lower == "yes";
}

}  // namespace

bool AgentConfigRegistry::LoadFromCSV(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open config file: " << path << "\n";
    return false;
  }

  std::string line;

  // Read header line
  if (!std::getline(file, line)) {
    std::cerr << "Empty config file: " << path << "\n";
    return false;
  }

  auto headers = ParseCSVLine(line);

  // Map header names to indices
  std::unordered_map<std::string, size_t> header_idx;
  for (size_t i = 0; i < headers.size(); ++i) {
    std::string h = headers[i];
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    header_idx[h] = i;
  }

  // Helper to get value by header name
  auto get_val = [&header_idx](const std::vector<std::string>& row,
                               const std::string& name,
                               const std::string& default_val = "") -> std::string {
    auto it = header_idx.find(name);
    if (it == header_idx.end() || it->second >= row.size()) {
      return default_val;
    }
    return row[it->second].empty() ? default_val : row[it->second];
  };

  // Read data rows
  int line_num = 1;
  while (std::getline(file, line)) {
    ++line_num;
    if (line.empty() || line[0] == '#') continue;  // Skip empty/comment lines

    auto row = ParseCSVLine(line);
    if (row.empty()) continue;

    try {
      AgentConfig config;

      // Required field
      config.type_name = get_val(row, "type_name");
      if (config.type_name.empty()) {
        std::cerr << path << ": line " << line_num << ": missing type_name\n";
        continue;
      }

      // Display
      std::string char_str = get_val(row, "display_char", "E");
      config.display_char = char_str.empty() ? 'E' : char_str[0];

      // Stats
      config.max_health = std::stoi(get_val(row, "max_health", "3"));
      config.faction = FactionFromCSV(get_val(row, "faction", "enemy"));

      // Movement
      config.cadence = ParseCadence(get_val(row, "cadence", ""));
      config.flying = ParseBool(get_val(row, "flying", "false"));

      // Detection
      config.detection_range = std::stoi(get_val(row, "detection_range", "3"));
      config.lose_target_range =
          std::stoi(get_val(row, "lose_target_range", "5"));

      // Attack
      config.has_attack = ParseBool(get_val(row, "has_attack", "false"));
      if (config.has_attack) {
        config.attack.name = get_val(row, "attack_name", "attack");
        // Use attack_name as effect reference (for Effect-based damage system)
        config.attack_effect = config.attack.name;
        config.attack.telegraph_ticks =
            std::stoi(get_val(row, "telegraph_ticks", "1"));
        config.attack.attack_ticks =
            std::stoi(get_val(row, "attack_ticks", "1"));
        config.attack.recovery_ticks =
            std::stoi(get_val(row, "recovery_ticks", "1"));
        config.attack.damage = std::stoi(get_val(row, "attack_damage", "1"));
        config.attack.area.width =
            std::stoi(get_val(row, "attack_width", "1"));
        config.attack.area.height =
            std::stoi(get_val(row, "attack_height", "1"));
        config.attack.filter =
            TargetFilterFromCSV(get_val(row, "attack_filter", "companion"));
      }

      RegisterConfig(std::move(config));

    } catch (const std::exception& e) {
      std::cerr << path << ": line " << line_num << ": parse error: "
                << e.what() << "\n";
      continue;
    }
  }

  return !configs_.empty();
}

}  // namespace companions
