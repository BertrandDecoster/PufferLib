// Copyright 2024
// Reactions, weaknesses, immunities and tag statuses: the combo rules as data.
// Tags stay opaque names (the env gives them no meaning); these rules say what
// happens when they meet on an agent. BaseEnv resolves them inside each tag
// landing (BaseEnv::LandTag: immunity, the tag and its status, weakness,
// reaction), see the Reactions section of the companions CLAUDE.md.

#ifndef COMPANIONS_CORE_REACTION_H_
#define COMPANIONS_CORE_REACTION_H_

#include <string>
#include <vector>

#include "object.h"
#include "types.h"

namespace companions {

// Two tags on one agent react into a third (level data, BaseEnv::SetReactions).
// Unordered: it fires when `a` lands on an agent carrying `b`, or `b` on one
// carrying `a`. Each affected agent then loses the originals it carries but
// those in `keep`, gets `result` (a permanent tag, unless it is immune; the
// result goes through weaknesses but never triggers a reaction) and takes
// `damage`. With `spread`, when the agent stands on a cell whose zone provides
// `a` or `b`, every affectable agent in that zone's connected region is
// affected (4-neighbour cells of that zone tag), and the region becomes
// `zone_becomes` (a zone by name: the zone table's fields) unless it is "".
// Otherwise the agent alone is affected.
struct ReactionRule {
  std::string a;
  std::string b;
  std::string result;
  std::vector<std::string> keep;  // Of {a, b}: the originals that stay
  int damage = 0;                 // To each affected agent, >= 0
  bool spread = false;
  std::string zone_becomes;  // Needs spread; "" = the region keeps its zone

  bool operator==(const ReactionRule& o) const {
    return a == o.a && b == o.b && result == o.result && keep == o.keep &&
           damage == o.damage && spread == o.spread && zone_becomes == o.zone_becomes;
  }
  bool operator!=(const ReactionRule& o) const { return !(*this == o); }
};

// A tag that applies a status as it lands (level data, BaseEnv::SetTagStatuses):
// `status` for `steps` steps (a step timer, Agent::ApplyStatus).
struct TagStatusRule {
  std::string tag;
  StatusType status = StatusType::Stunned;
  int steps = 1;  // > 0

  bool operator==(const TagStatusRule& o) const {
    return tag == o.tag && status == o.status && steps == o.steps;
  }
  bool operator!=(const TagStatusRule& o) const { return !(*this == o); }
};

// An agent's weakness, the ordered pair (P, S) (per agent,
// BaseEnv::SetWeaknesses): it is defeated when `tag` (S) lands on it, from any
// source, while it stands on a cell whose zone provides `zone` (P). The map
// decides, never the tags it carries: (wet, electrified) differs from
// (electrified, wet).
struct TagWeakness {
  std::string zone;  // P
  std::string tag;   // S

  bool operator==(const TagWeakness& o) const { return zone == o.zone && tag == o.tag; }
  bool operator!=(const TagWeakness& o) const { return !(*this == o); }
};

// Each throws std::runtime_error naming the entry (index and names) and what
// is wrong. Every name: non-empty, at most kMaxNameLength bytes.
// Reactions: a != b, a result, `keep` a subset of {a, b} without repeats,
// damage >= 0, zone_becomes only with spread, at most one rule per unordered
// pair {a, b} (a second one could never fire).
void ValidateReactions(const std::vector<ReactionRule>& rules);
// Tag statuses: a status of the env (stunned, marked, rooted), steps > 0, one
// rule per tag.
void ValidateTagStatuses(const std::vector<TagStatusRule>& rules);
// Weaknesses: no pair twice. Immunities: no tag twice.
void ValidateWeaknesses(const std::vector<TagWeakness>& weak_to);
void ValidateImmunities(const std::vector<std::string>& immune);

}  // namespace companions

#endif  // COMPANIONS_CORE_REACTION_H_
