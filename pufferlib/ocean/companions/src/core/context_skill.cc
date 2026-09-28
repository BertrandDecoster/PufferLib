// Copyright 2024
// Context skills (see context_skill.h)

#include "context_skill.h"

#include <stdexcept>

namespace companions {

std::string ContextConditionToString(ContextCondition c) {
  switch (c) {
    case ContextCondition::AdjacentDownedAlly: return "adjacent_downed_ally";
  }
  return "unknown";
}

ContextCondition ContextConditionFromString(const std::string& s) {
  if (s == "adjacent_downed_ally") return ContextCondition::AdjacentDownedAlly;
  throw std::runtime_error("unknown context condition '" + s + "'");
}

std::vector<ContextSkillRule> DefaultContextSkills() {
  return {{ContextCondition::AdjacentDownedAlly, 0, "revive"}};
}

void ValidateContextSkills(const std::vector<ContextSkillRule>& rules, const SkillBook& book) {
  for (size_t i = 0; i < rules.size(); ++i) {
    const ContextSkillRule& r = rules[i];
    const std::string where = "context_skills[" + std::to_string(i) + "] (" +
                              ContextConditionToString(r.condition) + ", slot " +
                              std::to_string(r.slot) + ", '" + r.skill + "'): ";
    if (ContextConditionToString(r.condition) == "unknown") {
      throw std::runtime_error(where + "condition: unknown value " +
                               std::to_string(static_cast<int>(r.condition)));
    }
    if (r.slot < 0 || r.slot >= kMaxSkillSlots) {
      throw std::runtime_error(where + "slot: out of range [0, " +
                               std::to_string(kMaxSkillSlots) + ")");
    }
    if (r.skill.empty()) throw std::runtime_error(where + "skill: none named");
    const SkillConfig* skill = book.Find(r.skill);
    if (!skill) throw std::runtime_error(where + "skill: unknown skill '" + r.skill + "'");
    if (skill->cooldown != 0) {
      throw std::runtime_error(where + "skill: '" + r.skill + "' has cooldown " +
                               std::to_string(skill->cooldown) +
                               " (a context skill must have none)");
    }
  }
}

}  // namespace companions
