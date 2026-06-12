// Copyright 2024
// DodgeLens implementation

#include "dodge_lens.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "base_env.h"
#include "../core/effect_config.h"
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

void DodgeLens::WriteVectorObs(const BaseEnv& env, const Agent& agent,
                               float* buffer) const {
  // `agent` is the SAME agent whose base features precede this tail (BaseEnv
  // resolves GetAllAgents()[player] once and passes it down). Logic otherwise
  // moved verbatim from the former DodgeEnv::VectorObservation tail so the
  // model input is f(world state, active lens) for any BaseEnv.
  Position my_pos = agent.GetPosition();
  float max_dim = static_cast<float>(std::max(env.GetRows(), env.GetCols()));
  int idx = 0;

  // Feature: Survival progress (1)
  // horizon <= 0 means "no survival deadline", so progress is 0 (a
  // horizonless world is never near its deadline) — also guards div-by-zero.
  int horizon = env.GetHorizon();
  buffer[idx++] = horizon > 0
      ? static_cast<float>(horizon - env.GetTick()) / horizon
      : 0.0f;

  // Feature: Number of active effects normalized (1)
  constexpr int kMaxEffects = 10;  // Normalization cap
  buffer[idx++] = std::min(
      1.0f, static_cast<float>(env.GetActiveEffects().size()) / kMaxEffects);

  // Features: Danger in each direction (4) - how close is nearest ACTIVE hazard
  // Direction order: Up, Down, Left, Right
  float danger_up = 1.0f, danger_down = 1.0f, danger_left = 1.0f, danger_right = 1.0f;

  // Features: Telegraph danger in each direction (4)
  float telegraph_up = 1.0f, telegraph_down = 1.0f, telegraph_left = 1.0f, telegraph_right = 1.0f;

  for (const auto& effect : env.GetActiveEffects()) {
    if (!effect.config) continue;

    Position center = effect.target.cell;

    // Check all cells affected by this effect
    int area_size = effect.config->GetAreaSize();
    int half = area_size / 2;

    for (int dr = -half; dr <= half; ++dr) {
      for (int dc = -half; dc <= half; ++dc) {
        int area_idx = (dr + half) * area_size + (dc + half);
        if (area_idx < 0 || area_idx >= static_cast<int>(effect.config->area.size())) continue;
        if (effect.config->area[area_idx] == 0) continue;

        Position affected = {center.row + dr, center.col + dc};

        // Calculate relative position and distance
        int delta_row = affected.row - my_pos.row;
        int delta_col = affected.col - my_pos.col;
        float dist = static_cast<float>(std::abs(delta_row) + std::abs(delta_col));
        float norm_dist = dist / max_dim;

        // Determine primary direction and update danger values
        if (delta_row < 0 && std::abs(delta_row) >= std::abs(delta_col)) {
          // Effect is above
          if (effect.in_telegraph) {
            telegraph_up = std::min(telegraph_up, norm_dist);
          } else {
            danger_up = std::min(danger_up, norm_dist);
          }
        } else if (delta_row > 0 && std::abs(delta_row) >= std::abs(delta_col)) {
          // Effect is below
          if (effect.in_telegraph) {
            telegraph_down = std::min(telegraph_down, norm_dist);
          } else {
            danger_down = std::min(danger_down, norm_dist);
          }
        } else if (delta_col < 0) {
          // Effect is left
          if (effect.in_telegraph) {
            telegraph_left = std::min(telegraph_left, norm_dist);
          } else {
            danger_left = std::min(danger_left, norm_dist);
          }
        } else if (delta_col > 0) {
          // Effect is right
          if (effect.in_telegraph) {
            telegraph_right = std::min(telegraph_right, norm_dist);
          } else {
            danger_right = std::min(danger_right, norm_dist);
          }
        }
        // If delta_row == 0 && delta_col == 0, player is on the effect - very dangerous!
        // This would set all directions to 0 distance
      }
    }
  }

  // Add danger features (inverted: 0 = far/safe, 1 = close/dangerous)
  buffer[idx++] = 1.0f - danger_up;
  buffer[idx++] = 1.0f - danger_down;
  buffer[idx++] = 1.0f - danger_left;
  buffer[idx++] = 1.0f - danger_right;

  // Add telegraph features
  buffer[idx++] = 1.0f - telegraph_up;
  buffer[idx++] = 1.0f - telegraph_down;
  buffer[idx++] = 1.0f - telegraph_left;
  buffer[idx++] = 1.0f - telegraph_right;
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
