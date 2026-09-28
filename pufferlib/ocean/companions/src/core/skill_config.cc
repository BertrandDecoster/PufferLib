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

  return {fireball, lightning_step, teleport, vortex};
}
}  // namespace

SkillBook::SkillBook() { Reset(); }

void SkillBook::Reset() { skills_ = Builtins(); }

void SkillBook::Define(SkillConfig config) {
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

}  // namespace companions
