// Copyright 2024
// Game logger implementation

#include "game_logger.h"

#include <algorithm>
#include <cctype>

namespace companions {

std::string LogCategoryToString(LogCategory cat) {
  switch (cat) {
    case LogCategory::FSM:
      return "fsm";
    case LogCategory::Effects:
      return "effects";
    case LogCategory::Game:
      return "game";
  }
  return "unknown";
}

std::optional<LogCategory> LogCategoryFromString(const std::string& str) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "fsm") return LogCategory::FSM;
  if (lower == "effects" || lower == "effect") return LogCategory::Effects;
  if (lower == "game") return LogCategory::Game;
  return std::nullopt;
}

GameLogger& GameLogger::Instance() {
  static GameLogger instance;
  return instance;
}

void GameLogger::Enable(LogCategory category) {
  enabled_categories_.insert(category);
}

void GameLogger::Disable(LogCategory category) {
  enabled_categories_.erase(category);
}

void GameLogger::EnableAll() {
  enabled_categories_.insert(LogCategory::FSM);
  enabled_categories_.insert(LogCategory::Effects);
  enabled_categories_.insert(LogCategory::Game);
}

void GameLogger::DisableAll() {
  enabled_categories_.clear();
  buffers_.clear();
}

bool GameLogger::IsEnabled(LogCategory category) const {
  return enabled_categories_.count(category) > 0;
}

void GameLogger::SetCurrentTick(int tick) {
  current_tick_ = tick;
}

int GameLogger::GetCurrentTick() const {
  return current_tick_;
}

void GameLogger::Log(LogCategory category, const std::string& message) {
  if (IsEnabled(category)) {
    buffers_[category].push_back(message);
  }
}

void GameLogger::Flush() {
  // Output in order: FSM -> Effects -> Game
  const std::vector<LogCategory> order = {
      LogCategory::FSM, LogCategory::Effects, LogCategory::Game};

  for (LogCategory cat : order) {
    auto it = buffers_.find(cat);
    if (it != buffers_.end()) {
      for (const std::string& msg : it->second) {
        std::cout << msg << std::endl;
      }
      it->second.clear();
    }
  }
}

std::set<LogCategory> GameLogger::GetEnabledCategories() const {
  return enabled_categories_;
}

bool GameLogger::HasAnyEnabled() const {
  return !enabled_categories_.empty();
}

}  // namespace companions
