// Copyright 2024
// SkillConfig - data-driven companion skills. The env knows how a skill
// targets, moves things and which tags / statuses it lands, never what it
// means ("burning" is an opaque tag; a level may retune or add skills).

#ifndef COMPANIONS_CORE_SKILL_CONFIG_H_
#define COMPANIONS_CORE_SKILL_CONFIG_H_

#include <string>
#include <vector>

#include "agent_config.h"  // TargetFilter
#include "types.h"

namespace companions {

enum class SkillTargeting {
  Self,        // Centre = the caster (after its own motion)
  Ground,      // Centre = the cell `range` away along the aim; a wall stops it
               // on the cell before, holes and agents do not
  Projectile,  // Centre = the first living agent passing `filter` within
               // `range` (a wall stops it), else the last cell reached
};

enum class SkillArea {
  Single,  // The centre cell
  Cross,   // The centre and its 4 orthogonal neighbours
};

enum class SkillMotion {
  None,
  Dash,      // Caster: up to `motion_distance`; crosses holes/agents, lands on the furthest valid cell
  Teleport,  // Caster: exactly `motion_distance`, else closer; ignores what is between
  PushOut,   // Things on the area's ring: `motion_distance` away from the centre (landing rule)
  PullIn,    // One thing on the ring into the centre, if free: above, right, below, left
};

struct SkillTagSpec {
  std::string tag;
  int duration = kPermanentTag;
};

struct SkillConfig {
  std::string name;
  SkillTargeting targeting = SkillTargeting::Projectile;
  int range = 1;
  TargetFilter filter = TargetFilter::All;  // Who is affected (the caster never is)
  SkillArea area = SkillArea::Single;
  SkillMotion motion = SkillMotion::None;
  int motion_distance = 0;
  bool tag_path = false;           // Dash: agents crossed on the way are affected too
  std::vector<SkillTagSpec> tags;  // Landed on every affected agent
  int root_steps = 0;              // Affected agents are rooted for this many next steps
  // Used at step t, usable again at step t + cooldown (0 and 1 both mean
  // every step).
  int cooldown = 0;
};

// Does this skill move its caster (so a rooted caster cannot use it)?
inline bool SkillMovesCaster(const SkillConfig& s) {
  return s.motion == SkillMotion::Dash || s.motion == SkillMotion::Teleport;
}

// Per-env set of skills: the builtins, plus whatever the level defines.
class SkillBook {
 public:
  SkillBook();   // Builtins only
  void Reset();  // Back to builtins only
  // Adds, or replaces a skill of the same name; ignores an empty name.
  void Define(SkillConfig config);
  // Invalidated by Define / Reset: do not hold the pointer across them.
  const SkillConfig* Find(const std::string& name) const;
  const std::vector<SkillConfig>& All() const { return skills_; }

 private:
  std::vector<SkillConfig> skills_;
};

std::string SkillTargetingToString(SkillTargeting t);
SkillTargeting SkillTargetingFromString(const std::string& s);  // Throws on unknown
std::string SkillAreaToString(SkillArea a);
SkillArea SkillAreaFromString(const std::string& s);            // Throws on unknown
std::string SkillMotionToString(SkillMotion m);
SkillMotion SkillMotionFromString(const std::string& s);        // Throws on unknown

}  // namespace companions

#endif  // COMPANIONS_CORE_SKILL_CONFIG_H_
