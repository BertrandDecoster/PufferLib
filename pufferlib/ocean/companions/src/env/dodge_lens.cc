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
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool DodgeLens::IsSuccess(const BaseEnv& env) const {
  return !AnyCompanionIncapacitated(env) && env.GetTick() >= env.GetHorizon();
}

double DodgeLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task
  if (AnyCompanionIncapacitated(env)) return 0.0;  // No success either
  double reward = kSurvivalBonus;
  // While a success counts (not past an episode ended otherwise:
  // BaseEnv::SuccessCounts)
  if (IsSuccess(env) && env.SuccessCounts()) reward += kWinReward;
  return reward;
}

std::string DodgeLens::GetObjectiveString(const BaseEnv& env) const {
  std::ostringstream ss;
  ss << "Dodge: survive until horizon (" << env.GetTick() << "/"
     << env.GetHorizon() << ")";
  if (AnyCompanionIncapacitated(env)) {
    ss << " [companion down]";
  } else if (IsSuccess(env)) {
    ss << " [SUCCESS]";
  }
  return ss.str();
}

bool DodgeLens::AnyCompanionIncapacitated(const BaseEnv& env) const {
  const ObjectManager& om = env.GetObjectManager();

  for (const Companion* companion : om.GetAllCompanions()) {
    // Down (0 HP, alive) or dead
    if (!companion->IsAffectable()) return true;
  }
  return false;
}

}  // namespace companions
