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

// kDefaultSkill ("attack", types.h) is the fixed default skill. Throws
// std::runtime_error("'attack' is the fixed default skill") when `name` is
// it: a level may not define it (SkillBook::Define, snapshot validation).
void RejectDefaultSkillName(const std::string& name);

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
  // PushOut / PullIn move things on the area's ring (the caster too, when it
  // stands there: see friendly_fire / self_motion); with SkillArea::Single
  // there is no ring, so they do nothing.
  PushOut,   // Things on the area's ring: `motion_distance` away from the centre (landing rule)
  // PullIn: one thing on the ring into the centre, if free: above, right,
  // below, left. Always exactly one cell (ignores `motion_distance`); needs a
  // Cross area and a free, walkable centre, so a Self-targeted PullIn never
  // pulls (the caster stands on the centre).
  PullIn,
};

struct SkillTagSpec {
  std::string tag;
  int duration = kPermanentTag;
};

struct SkillConfig {
  std::string name;
  SkillTargeting targeting = SkillTargeting::Projectile;
  int range = 1;
  TargetFilter filter = TargetFilter::All;  // Who is affected (see also friendly_fire)
  SkillArea area = SkillArea::Single;
  SkillMotion motion = SkillMotion::None;
  int motion_distance = 0;
  bool tag_path = false;           // Dash: agents crossed on the way are affected too
  std::vector<SkillTagSpec> tags;  // Landed on every affected agent
  // Health every affected agent loses (Agent::TakeDamage, so Marked applies
  // and 0 HP kills), after the tags and before root / area motion. Marked's
  // x1.5 truncates toward zero: 1 damage stays 1, 2 becomes 3, 3 becomes 4.
  int damage = 0;
  int root_steps = 0;              // Affected agents are rooted for this many next steps
  // 0: no cooldown. n: blocked for the n steps after the one it was used in
  // (usable again at step t + n + 1); a step timer (see Agent::BeginStep).
  int cooldown = 0;
  // Who the skill affects, on top of `filter`. Off: only agents not of the
  // caster's faction (enemies, neutrals): allies and the caster get no tag,
  // no push / pull, no root, and a projectile flies past them. On: allies are
  // affected like anyone else, and so is the caster when it stands in its own
  // area (after its own motion), unless a self_* flag below spares it that
  // effect (they mean nothing with friendly_fire off).
  bool friendly_fire = true;
  bool self_tags = true;    // The caster gets the skill's tags
  bool self_motion = true;  // PushOut / PullIn may move the caster
  bool self_root = true;    // root_steps roots the caster
  bool self_damage = true;  // damage hurts the caster
};

// Does this skill move its caster (so a rooted caster cannot use it)?
inline bool SkillMovesCaster(const SkillConfig& s) {
  return s.motion == SkillMotion::Dash || s.motion == SkillMotion::Teleport;
}

// Per-env set of skills: the builtins, plus whatever the level defines.
class SkillBook {
 public:
  SkillBook();   // Builtins only (kDefaultSkill included)
  void Reset();  // Back to builtins only
  // Adds, or replaces a skill of the same name; ignores an empty name. Throws
  // std::runtime_error (book unchanged) unless ValidateSkillConfig accepts it,
  // and for kDefaultSkill, which is fixed.
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
// A skill's filter: "all" / "companion" / "enemy" / "neutral" (strict: an
// unknown string throws, unlike ParseTargetFilter).
std::string TargetFilterToString(TargetFilter f);
TargetFilter TargetFilterFromString(const std::string& s);      // Throws on unknown

// Throws std::runtime_error naming the skill and the field unless `s` is
// usable: non-empty name of at most kMaxNameLength bytes; range,
// motion_distance, damage, root_steps and cooldown >= 0; tag names non-empty, of at
// most kMaxNameLength bytes, with a duration of kPermanentTag or > 0; enums in
// range.
void ValidateSkillConfig(const SkillConfig& s);

}  // namespace companions

#endif  // COMPANIONS_CORE_SKILL_CONFIG_H_
