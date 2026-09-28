// Copyright 2024
// Context skills (see context_skill.h)

#include "context_skill.h"

#include <stdexcept>

namespace companions {

namespace {

// Every condition and its JSON name: IsKnown, ToString and FromString all
// read it.
struct ConditionName {
  ContextCondition condition;
  const char* name;
};
constexpr ConditionName kConditionNames[] = {
    {ContextCondition::AdjacentDownedAlly, "adjacent_downed_ally"},
};

const ConditionName* FindCondition(ContextCondition c) {
  for (const ConditionName& entry : kConditionNames) {
    if (entry.condition == c) return &entry;
  }
  return nullptr;
}

}  // namespace

bool IsKnown(ContextCondition c) { return FindCondition(c) != nullptr; }

std::string ContextConditionToString(ContextCondition c) {
  const ConditionName* entry = FindCondition(c);
  return entry ? entry->name : "unknown";
}

ContextCondition ContextConditionFromString(const std::string& s) {
  for (const ConditionName& entry : kConditionNames) {
    if (s == entry.name) return entry.condition;
  }
  throw std::runtime_error("unknown context condition '" + s + "'");
}

std::vector<ContextSkillRule> DefaultContextSkills() {
  return {{ContextCondition::AdjacentDownedAlly, 0, "revive"}};
}

bool IsUsableWith(const ContextSkillRule& rule, const SkillBook& book) {
  const SkillConfig* skill = book.Find(rule.skill);
  return skill && skill->cooldown == 0;
}

void ValidateContextSkills(const std::vector<ContextSkillRule>& rules, const SkillBook& book) {
  for (size_t i = 0; i < rules.size(); ++i) {
    const ContextSkillRule& r = rules[i];
    const std::string where = "context_skills[" + std::to_string(i) + "] (" +
                              ContextConditionToString(r.condition) + ", slot " +
                              std::to_string(r.slot) + ", '" + r.skill + "'): ";
    if (!IsKnown(r.condition)) {
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
