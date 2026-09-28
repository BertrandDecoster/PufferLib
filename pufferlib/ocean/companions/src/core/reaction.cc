// Copyright 2024
// Reactions, weaknesses, immunities and tag statuses (see reaction.h)

#include "reaction.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace companions {

namespace {

// `what` names the field in messages
void CheckName(const std::string& where, const char* what, const std::string& name) {
  if (name.empty()) throw std::runtime_error(where + what + ": empty");
  if (!IsValidNameLength(name)) {
    throw std::runtime_error(where + what + ": longer than " + std::to_string(kMaxNameLength) +
                             " bytes");
  }
}

// Whether v[i] already appears before i
template <typename T>
bool SeenBefore(const std::vector<T>& v, size_t i) {
  return std::find(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(i), v[i]) !=
         v.begin() + static_cast<std::ptrdiff_t>(i);
}

}  // namespace

void ValidateReactions(const std::vector<ReactionRule>& rules) {
  for (size_t i = 0; i < rules.size(); ++i) {
    const ReactionRule& r = rules[i];
    const std::string where =
        "reactions[" + std::to_string(i) + "] ('" + r.a + "' + '" + r.b + "'): ";
    CheckName(where, "a", r.a);
    CheckName(where, "b", r.b);
    CheckName(where, "result", r.result);
    if (r.a == r.b) throw std::runtime_error(where + "a and b: the same tag");
    for (size_t k = 0; k < r.keep.size(); ++k) {
      if (r.keep[k] != r.a && r.keep[k] != r.b) {
        throw std::runtime_error(where + "keep: '" + r.keep[k] + "' is neither a nor b");
      }
      if (SeenBefore(r.keep, k)) {
        throw std::runtime_error(where + "keep: '" + r.keep[k] + "' twice");
      }
    }
    if (r.damage < 0) throw std::runtime_error(where + "damage: negative");
    if (!r.zone_becomes.empty()) {
      if (!r.spread) throw std::runtime_error(where + "zone_becomes: needs spread");
      CheckName(where, "zone_becomes", r.zone_becomes);
    }
    for (size_t j = 0; j < i; ++j) {
      const ReactionRule& o = rules[j];
      if ((o.a == r.a && o.b == r.b) || (o.a == r.b && o.b == r.a)) {
        throw std::runtime_error(where + "the pair already reacts in reactions[" +
                                 std::to_string(j) + "]");
      }
    }
  }
}

void ValidateTagStatuses(const std::vector<TagStatusRule>& rules) {
  for (size_t i = 0; i < rules.size(); ++i) {
    const TagStatusRule& r = rules[i];
    const std::string where = "tag_statuses[" + std::to_string(i) + "] ('" + r.tag + "'): ";
    CheckName(where, "tag", r.tag);
    if (r.status != StatusType::Stunned && r.status != StatusType::Marked &&
        r.status != StatusType::Rooted) {
      throw std::runtime_error(where + "status: unknown value " +
                               std::to_string(static_cast<int>(r.status)));
    }
    if (r.steps <= 0) throw std::runtime_error(where + "steps: must be > 0");
    for (size_t j = 0; j < i; ++j) {
      if (rules[j].tag == r.tag) {
        throw std::runtime_error(where + "the tag already has tag_statuses[" +
                                 std::to_string(j) + "]");
      }
    }
  }
}

void ValidateWeaknesses(const std::vector<TagWeakness>& weak_to) {
  for (size_t i = 0; i < weak_to.size(); ++i) {
    const TagWeakness& w = weak_to[i];
    const std::string where =
        "weak_to[" + std::to_string(i) + "] ('" + w.zone + "', '" + w.tag + "'): ";
    CheckName(where, "zone", w.zone);
    CheckName(where, "tag", w.tag);
    if (SeenBefore(weak_to, i)) {
      throw std::runtime_error(where + "twice");
    }
  }
}

void ValidateImmunities(const std::vector<std::string>& immune) {
  for (size_t i = 0; i < immune.size(); ++i) {
    const std::string where = "immune[" + std::to_string(i) + "] ('" + immune[i] + "'): ";
    CheckName(where, "tag", immune[i]);
    if (SeenBefore(immune, i)) {
      throw std::runtime_error(where + "twice");
    }
  }
}

}  // namespace companions
