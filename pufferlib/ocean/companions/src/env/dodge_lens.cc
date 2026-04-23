// Copyright 2024
// DodgeLens implementation

#include "dodge_lens.h"

#include <sstream>

#include "base_env.h"
#include "../core/object_manager.h"

namespace companions {

bool DodgeLens::CanOperateOn(const BaseEnv& env) const {
  // DodgeLens can work on any env with companions
  (void)env;
  return true;
}

bool DodgeLens::IsDone(const BaseEnv& env) const {
  return AnyCompanionIncapacitated(env) || env.GetTick() >= env.GetHorizon();
}

bool DodgeLens::IsSuccess(const BaseEnv& env) const {
  return !AnyCompanionIncapacitated(env) && env.GetTick() >= env.GetHorizon();
}

double DodgeLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task
  bool any_dead = AnyCompanionIncapacitated(env);
  double reward = 0.0;
  if (!any_dead) reward += kSurvivalBonus;
  if (IsSuccess(env)) reward += kWinReward;
  if (any_dead) reward += kDeathPenalty;
  return reward;
}

std::string DodgeLens::GetObjectiveString(const BaseEnv& env) const {
  std::ostringstream ss;
  ss << "Dodge: survive until horizon (" << env.GetTick() << "/"
     << env.GetHorizon() << ")";
  if (AnyCompanionIncapacitated(env)) {
    ss << " [FAILED - companion incapacitated]";
  } else if (IsSuccess(env)) {
    ss << " [SUCCESS]";
  }
  return ss.str();
}

bool DodgeLens::AnyCompanionIncapacitated(const BaseEnv& env) const {
  const ObjectManager& om = env.GetObjectManager();

  for (const Companion* companion : om.GetAllCompanions()) {
    // Check if companion is dead (health <= 0) or not alive
    if (companion->IsDead() || !companion->IsAlive()) {
      return true;
    }
  }
  return false;
}

}  // namespace companions
