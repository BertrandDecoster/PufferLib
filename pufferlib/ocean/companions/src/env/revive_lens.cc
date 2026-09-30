// Copyright 2024
// ReviveLens implementation

#include "revive_lens.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "base_env.h"
#include "../core/grid.h"
#include "../core/object_manager.h"

namespace companions {

namespace {

// A downed companion (alive: the dead are not down)
bool IsDownedBody(const Companion& c) { return c.IsAlive() && c.IsDowned(); }

// A downed body holds the cell (only companions go down)
bool HoldsDownedBody(const BaseEnv& env, Position pos) {
  for (const Companion* c : env.GetObjectManager().GetAllCompanions()) {
    if (IsDownedBody(*c) && c->GetPosition() == pos) return true;
  }
  return false;
}

// The four orthogonal offsets
constexpr int kNeighbours[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

// A cell a reviver may stand on: in the grid, walkable, no downed body
bool IsFreeNeighbourCell(const BaseEnv& env, Position pos) {
  const Grid& grid = env.GetGrid();
  return grid.IsInBounds(pos) && grid.IsWalkable(pos) && !HoldsDownedBody(env, pos);
}

}  // namespace

bool ReviveLens::CanOperateOn(const BaseEnv& env) const {
  return params_valid_ && env.AnyCompanionDowned();
}

bool ReviveLens::IsDone(const BaseEnv& env) const {
  return IsSuccess(env) || env.GetTick() >= env.GetHorizon();
}

bool ReviveLens::IsSuccess(const BaseEnv& env) const {
  return !env.AnyCompanionDowned();
}

double ReviveLens::ComputeReward(const BaseEnv& env, int agent_id) const {
  (void)agent_id;  // Same reward for all agents in cooperative task
  double reward = kTimePenalty;
  // While a success counts (not past an episode ended otherwise:
  // BaseEnv::SuccessCounts)
  if (IsSuccess(env) && env.SuccessCounts()) reward += kSuccessBonus;
  return reward;
}

std::string ReviveLens::GetObjectiveString(const BaseEnv& env) const {
  int down = 0;
  for (const Companion* c : env.GetObjectManager().GetAllCompanions()) {
    if (IsDownedBody(*c)) ++down;
  }
  std::ostringstream ss;
  ss << "Revive: " << down << " downed "
     << (down == 1 ? "ally" : "allies");
  if (IsSuccess(env)) ss << " [SUCCESS]";
  return ss.str();
}

bool ReviveLens::WentDownSince(ObjectId id, int times_downed) const {
  for (const DownsAtActivation& d : downs_at_activation_) {
    if (d.id == id) return times_downed > d.times_downed;
  }
  return true;  // Unknown at the activation: new
}

std::vector<ObjectId> ReviveLens::GetGoalBodies(const BaseEnv& env) const {
  std::vector<ObjectId> goal;
  std::vector<ObjectId> every_downed;
  for (const Companion* c : env.GetObjectManager().GetAllCompanions()) {
    if (!IsDownedBody(*c)) continue;
    every_downed.push_back(c->GetId());
    const bool target =
        std::find(targets_.begin(), targets_.end(), c->GetId()) != targets_.end();
    if (target || WentDownSince(c->GetId(), c->GetTimesDowned())) goal.push_back(c->GetId());
  }
  // No targets (every downed ally), or none left while someone is down
  if (targets_.empty() || goal.empty()) return every_downed;
  return goal;
}

bool ReviveLens::IsGoalCell(const BaseEnv& env, Position pos) const {
  if (!IsFreeNeighbourCell(env, pos)) return false;
  const ObjectManager& om = env.GetObjectManager();
  for (ObjectId id : GetGoalBodies(env)) {
    const Position body = om.GetActor(id)->GetPosition();
    if (std::abs(body.row - pos.row) + std::abs(body.col - pos.col) == 1) return true;
  }
  return false;
}

std::vector<Position> ReviveLens::GetGoalCells(const BaseEnv& env) const {
  std::vector<Position> cells;
  const ObjectManager& om = env.GetObjectManager();
  for (ObjectId id : GetGoalBodies(env)) {
    const Position body = om.GetActor(id)->GetPosition();
    for (const auto& d : kNeighbours) {
      const Position cell{body.row + d[0], body.col + d[1]};
      if (!IsFreeNeighbourCell(env, cell)) continue;
      if (std::find(cells.begin(), cells.end(), cell) == cells.end()) cells.push_back(cell);
    }
  }
  return cells;
}

void ReviveLens::Activate(BaseEnv& env, const LensParams& params) {
  targets_.clear();
  downs_at_activation_.clear();
  params_valid_ = true;
  const auto companions = env.GetObjectManager().GetAllCompanions();
  for (const Companion* c : companions) {
    downs_at_activation_.push_back({c->GetId(), c->GetTimesDowned()});
  }
  for (const Position& pos : params.positions) {
    const Companion* body = nullptr;
    for (const Companion* c : companions) {
      if (IsDownedBody(*c) && c->GetPosition() == pos) {
        body = c;
        break;
      }
    }
    if (!body) {
      params_valid_ = false;
      continue;
    }
    if (std::find(targets_.begin(), targets_.end(), body->GetId()) == targets_.end()) {
      targets_.push_back(body->GetId());
    }
  }
}

}  // namespace companions
