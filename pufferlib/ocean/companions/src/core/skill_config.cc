// Copyright 2024
#include "skill_config.h"

#include <stdexcept>
#include <utility>

namespace companions {

namespace {
std::vector<SkillConfig> Builtins() {
  SkillConfig fireball;
  fireball.name = "fireball";
  fireball.targeting = SkillTargeting::Ground;
  fireball.range = 3;
  fireball.area = SkillArea::Cross;
  fireball.motion = SkillMotion::PushOut;
  fireball.motion_distance = 1;
  fireball.tags = {{"burning", kPermanentTag}};
  fireball.cooldown = 3;

  SkillConfig lightning_step;
  lightning_step.name = "lightningStep";
  lightning_step.targeting = SkillTargeting::Self;
  lightning_step.area = SkillArea::Cross;  // Around the landing cell
  lightning_step.motion = SkillMotion::Dash;
  lightning_step.motion_distance = 4;
  lightning_step.tag_path = true;
  lightning_step.tags = {{"electrified", kPermanentTag}};
  lightning_step.cooldown = 3;
  lightning_step.self_tags = false;  // Its caster lands on the centre of its own cross

  SkillConfig teleport;
  teleport.name = "teleport";
  teleport.targeting = SkillTargeting::Self;
  teleport.motion = SkillMotion::Teleport;
  teleport.motion_distance = 3;
  teleport.cooldown = 4;

  SkillConfig vortex;
  vortex.name = "vortex";
  vortex.targeting = SkillTargeting::Ground;
  vortex.range = 3;
  vortex.area = SkillArea::Cross;
  vortex.motion = SkillMotion::PullIn;
  vortex.motion_distance = 1;
  vortex.root_steps = 1;
  vortex.cooldown = 4;
  vortex.self_root = false;  // Pulled into its own vortex, the caster is not rooted

  // Brings a downed ally on the faced cell back with half its max HP (rounded
  // up). A builtin a level may retune, unlike the attack.
  SkillConfig revive;
  revive.name = "revive";
  revive.targeting = SkillTargeting::Projectile;
  revive.range = 1;
  revive.area = SkillArea::Single;
  revive.filter = TargetFilter::Companion;
  revive.affects_downed = true;
  revive.revive_percent = 50;
  revive.cooldown = 0;

  // The fixed default skill (kDefaultSkill): strikes the faced cell.
  SkillConfig attack;
  attack.name = kDefaultSkill;
  attack.targeting = SkillTargeting::Projectile;
  attack.range = 1;
  attack.area = SkillArea::Single;
  attack.damage = 1;
  attack.friendly_fire = false;
  attack.cooldown = 0;

  return {fireball, lightning_step, teleport, vortex, revive, attack};
}
}  // namespace

void RejectDefaultSkillName(const std::string& name) {
  if (name == kDefaultSkill) {
    throw std::runtime_error(std::string("'") + kDefaultSkill + "' is the fixed default skill");
  }
}

SkillBook::SkillBook() { Reset(); }

void SkillBook::Reset() { skills_ = Builtins(); }

void SkillBook::Define(SkillConfig config) {
  if (config.name.empty()) return;  // Never a skill
  RejectDefaultSkillName(config.name);
  ValidateSkillConfig(config);      // Throws before any change
  for (SkillConfig& s : skills_) {
    if (s.name == config.name) {
      s = std::move(config);
      return;
    }
  }
  skills_.push_back(std::move(config));
}

const SkillConfig* SkillBook::Find(const std::string& name) const {
  for (const SkillConfig& s : skills_) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

std::string SkillTargetingToString(SkillTargeting t) {
  switch (t) {
    case SkillTargeting::Self: return "self";
    case SkillTargeting::Ground: return "ground";
    case SkillTargeting::Projectile: return "projectile";
  }
  return "projectile";
}

SkillTargeting SkillTargetingFromString(const std::string& s) {
  if (s == "self") return SkillTargeting::Self;
  if (s == "ground") return SkillTargeting::Ground;
  if (s == "projectile") return SkillTargeting::Projectile;
  throw std::runtime_error("Unknown skill targeting: " + s);
}

std::string SkillAreaToString(SkillArea a) {
  switch (a) {
    case SkillArea::Single: return "single";
    case SkillArea::Cross: return "cross";
  }
  return "single";
}

SkillArea SkillAreaFromString(const std::string& s) {
  if (s == "single") return SkillArea::Single;
  if (s == "cross") return SkillArea::Cross;
  throw std::runtime_error("Unknown skill area: " + s);
}

std::string SkillMotionToString(SkillMotion m) {
  switch (m) {
    case SkillMotion::None: return "none";
    case SkillMotion::Dash: return "dash";
    case SkillMotion::Teleport: return "teleport";
    case SkillMotion::PushOut: return "push_out";
    case SkillMotion::PullIn: return "pull_in";
  }
  return "none";
}

SkillMotion SkillMotionFromString(const std::string& s) {
  if (s == "none") return SkillMotion::None;
  if (s == "dash") return SkillMotion::Dash;
  if (s == "teleport") return SkillMotion::Teleport;
  if (s == "push_out") return SkillMotion::PushOut;
  if (s == "pull_in") return SkillMotion::PullIn;
  throw std::runtime_error("Unknown skill motion: " + s);
}

std::string TargetFilterToString(TargetFilter f) {
  switch (f) {
    case TargetFilter::All: return "all";
    case TargetFilter::Companion: return "companion";
    case TargetFilter::Enemy: return "enemy";
    case TargetFilter::Neutral: return "neutral";
  }
  return "all";
}

TargetFilter TargetFilterFromString(const std::string& s) {
  if (s == "all") return TargetFilter::All;
  if (s == "companion") return TargetFilter::Companion;
  if (s == "enemy") return TargetFilter::Enemy;
  if (s == "neutral") return TargetFilter::Neutral;
  throw std::runtime_error("Unknown skill filter: " + s);
}

namespace {

template <typename E>
bool EnumInRange(E value, E last) {
  const int v = static_cast<int>(value);
  return v >= 0 && v <= static_cast<int>(last);
}

// "name is 40 bytes, at most 31"
std::string NameLengthError(const std::string& name) {
  return "name is " + std::to_string(name.size()) + " bytes, at most " +
         std::to_string(kMaxNameLength);
}

}  // namespace

void ValidateSkillConfig(const SkillConfig& s) {
  if (s.name.empty()) throw std::runtime_error("skill without a name");
  const std::string where = "skill '" + s.name + "': ";
  if (!IsValidNameLength(s.name)) throw std::runtime_error(where + NameLengthError(s.name));
  auto check_enum = [&](bool ok, const char* field, int value) {
    if (!ok) {
      throw std::runtime_error(where + field + " has an unknown value " + std::to_string(value));
    }
  };
  check_enum(EnumInRange(s.targeting, SkillTargeting::Projectile), "targeting",
             static_cast<int>(s.targeting));
  check_enum(EnumInRange(s.filter, TargetFilter::Neutral), "filter", static_cast<int>(s.filter));
  check_enum(EnumInRange(s.area, SkillArea::Cross), "area", static_cast<int>(s.area));
  check_enum(EnumInRange(s.motion, SkillMotion::PullIn), "motion", static_cast<int>(s.motion));
  auto check_non_negative = [&](const char* field, int value) {
    if (value < 0) {
      throw std::runtime_error(where + field + " must be >= 0 (got " + std::to_string(value) + ")");
    }
  };
  check_non_negative("range", s.range);
  check_non_negative("motion_distance", s.motion_distance);
  check_non_negative("damage", s.damage);
  check_non_negative("root_steps", s.root_steps);
  check_non_negative("cooldown", s.cooldown);
  if (s.revive_percent < 0 || s.revive_percent > 100) {
    throw std::runtime_error(where + "revive_percent must be in [0, 100] (got " +
                             std::to_string(s.revive_percent) + ")");
  }
  if (s.revive_percent > 0 && !s.affects_downed) {
    throw std::runtime_error(where + "revive_percent needs affects_downed (only the downed revive)");
  }
  if (s.affects_downed) {
    // It can only revive: nothing else reaches the downed.
    auto reject = [&](bool present, const char* field, const char* why) {
      if (present) {
        throw std::runtime_error(where + field + " with affects_downed: " + why);
      }
    };
    reject(!s.tags.empty(), "tags", "tags cannot land on the downed");
    reject(s.damage > 0, "damage", "damage cannot hurt the downed");
    reject(s.root_steps > 0, "root_steps", "the downed cannot be rooted");
    reject(s.motion != SkillMotion::None, "motion", "a motion would move a body");
  }
  for (size_t i = 0; i < s.tags.size(); ++i) {
    const SkillTagSpec& t = s.tags[i];
    const std::string tag_where = where + "tags[" + std::to_string(i) + "]";
    if (t.tag.empty()) throw std::runtime_error(tag_where + ": empty tag name");
    if (!IsValidNameLength(t.tag)) {
      throw std::runtime_error(tag_where + " ('" + t.tag + "'): " + NameLengthError(t.tag));
    }
    if (t.duration != kPermanentTag && t.duration <= 0) {
      throw std::runtime_error(tag_where + " ('" + t.tag + "'): duration must be -1 or > 0 (got " +
                               std::to_string(t.duration) + ")");
    }
  }
}

}  // namespace companions
