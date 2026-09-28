// Copyright 2024
// Context skills - level data saying "when <condition>, slot <n> is <skill>".
// A companion slot holds an equipped skill (SetCompanionSkill, snapshots) and
// an effective one: the skill of the first rule whose condition holds for that
// companion and slot, else the equipped one (BaseEnv::EffectiveSkill). Nothing
// is swapped: the env computes it whenever it reads intentions.

#ifndef COMPANIONS_CORE_CONTEXT_SKILL_H_
#define COMPANIONS_CORE_CONTEXT_SKILL_H_

#include <string>
#include <vector>

#include "skill_config.h"

namespace companions {

// When a rule applies. Evaluated by BaseEnv::ContextHolds, one case per value:
// a new condition is a new value, its name below and its case there.
enum class ContextCondition {
  // A downed agent of the companion's faction on one of its 4 orthogonal
  // neighbours ("adjacent_downed_ally")
  AdjacentDownedAlly = 0,
};

// The JSON names. FromString throws std::runtime_error naming an unknown
// string; ToString gives "unknown" for a value out of range.
std::string ContextConditionToString(ContextCondition c);
ContextCondition ContextConditionFromString(const std::string& s);

struct ContextSkillRule {
  ContextCondition condition = ContextCondition::AdjacentDownedAlly;
  int slot = 0;       // 0-based, < kMaxSkillSlots
  std::string skill;  // A skill of the env's book, with no cooldown

  bool operator==(const ContextSkillRule& o) const {
    return condition == o.condition && slot == o.slot && skill == o.skill;
  }
  bool operator!=(const ContextSkillRule& o) const { return !(*this == o); }
};

// The rules of a level that says nothing: next to a downed ally, slot 0 is
// "revive" (so every level has revive).
std::vector<ContextSkillRule> DefaultContextSkills();

// Throws std::runtime_error naming the rule (index, condition, slot) and what
// is wrong unless every rule is usable with `book`: a known condition, a slot
// in [0, kMaxSkillSlots), a skill the book has, with cooldown 0 (cooldowns
// belong to the equipped skill: a context skill neither reads nor spends the
// slot's).
void ValidateContextSkills(const std::vector<ContextSkillRule>& rules, const SkillBook& book);

}  // namespace companions

#endif  // COMPANIONS_CORE_CONTEXT_SKILL_H_
