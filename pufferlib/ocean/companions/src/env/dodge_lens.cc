// Copyright 2024
// DodgeLens implementation

#include "dodge_lens.h"

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

float DodgeLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task

  if (AnyCompanionIncapacitated(env)) {
    return kDeathPenalty;
  }
  return kSurvivalReward;
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
