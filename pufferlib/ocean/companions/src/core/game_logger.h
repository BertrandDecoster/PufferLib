// Copyright 2024
// Category-based logging system for The Companions game

#ifndef COMPANIONS_CORE_GAME_LOGGER_H_
#define COMPANIONS_CORE_GAME_LOGGER_H_

#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace companions {

// =============================================================================
// LogCategory - Categories of log messages
// =============================================================================
enum class LogCategory {
  FSM,      // FSM state transitions and timers
  Effects,  // Effect creation, phase transitions, application
  Game,     // Game rendering (displayed last)
};

// Convert category to string for display
std::string LogCategoryToString(LogCategory cat);

// Parse category from string (case-insensitive)
std::optional<LogCategory> LogCategoryFromString(const std::string& str);

// =============================================================================
// GameLogger - Singleton logger with category filtering and buffering
// =============================================================================
class GameLogger {
 public:
  static GameLogger& Instance();

  // Enable/disable specific categories
  void Enable(LogCategory category);
  void Disable(LogCategory category);
  void EnableAll();
  void DisableAll();
  bool IsEnabled(LogCategory category) const;

  // Tick tracking for log messages
  void SetCurrentTick(int tick);
  int GetCurrentTick() const;

  // Log a message (buffered by category)
  void Log(LogCategory category, const std::string& message);

  // Flush all buffered logs in order: FSM -> Effects -> Game
  void Flush();

  // Get currently enabled categories
  std::set<LogCategory> GetEnabledCategories() const;

  // Check if any category is enabled
  bool HasAnyEnabled() const;

 private:
  GameLogger() = default;
  std::set<LogCategory> enabled_categories_;
  int current_tick_ = 0;

  // Buffered logs per category
  std::map<LogCategory, std::vector<std::string>> buffers_;
};

// =============================================================================
// LogStream - RAII helper for stream-based logging
// =============================================================================
class LogStream {
 public:
  LogStream(LogCategory category, bool enabled)
      : category_(category), enabled_(enabled) {}

  ~LogStream() {
    if (enabled_) {
      GameLogger::Instance().Log(category_, stream_.str());
    }
  }

  template <typename T>
  LogStream& operator<<(const T& value) {
    if (enabled_) {
      stream_ << value;
    }
    return *this;
  }

 private:
  LogCategory category_;
  bool enabled_;
  std::ostringstream stream_;
};

// =============================================================================
// Logging macros - primary interface
// =============================================================================

#define LOG_ENABLED(category) \
  ::companions::GameLogger::Instance().IsEnabled(::companions::LogCategory::category)

#define LOG_FSM(msg) \
  ::companions::LogStream(::companions::LogCategory::FSM, LOG_ENABLED(FSM)) \
      << "[FSM] T" << ::companions::GameLogger::Instance().GetCurrentTick() << " " << msg

#define LOG_EFFECT(msg) \
  ::companions::LogStream(::companions::LogCategory::Effects, LOG_ENABLED(Effects)) \
      << "[Effect] T" << ::companions::GameLogger::Instance().GetCurrentTick() << " " << msg

#define LOG_GAME(msg) \
  ::companions::LogStream(::companions::LogCategory::Game, LOG_ENABLED(Game)) \
      << "[Game] T" << ::companions::GameLogger::Instance().GetCurrentTick() << " " << msg

}  // namespace companions

#endif  // COMPANIONS_CORE_GAME_LOGGER_H_
