// Copyright 2024
// Pathfinder implementation - A* search for 4-connected grids

#include "pathfinder.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace companions {

namespace {
// Euclidean distance heuristic - admissible for 4-connected grids
// Naturally prioritizes reducing the larger axis first
inline double EuclideanDistance(Position a, Position b) {
  int dr = a.row - b.row;
  int dc = a.col - b.col;
  return std::sqrt(dr * dr + dc * dc);
}
}  // namespace

Pathfinder::Pathfinder(const Grid& grid) : grid_(grid) {}

Position Pathfinder::Step(Position pos, JumpDirection dir) const {
  switch (dir) {
    case JumpDirection::Up:
      return {pos.row - 1, pos.col};
    case JumpDirection::Down:
      return {pos.row + 1, pos.col};
    case JumpDirection::Left:
      return {pos.row, pos.col - 1};
    case JumpDirection::Right:
      return {pos.row, pos.col + 1};
  }
  return pos;
}

bool Pathfinder::IsHorizontal(JumpDirection dir) const {
  return dir == JumpDirection::Left || dir == JumpDirection::Right;
}

std::vector<Pathfinder::JumpDirection> Pathfinder::GetPrunedDirections(
    const std::optional<JumpDirection>& /*parent_dir*/) const {
  // Standard A* behavior: explore all 4 directions
  std::vector<JumpDirection> dirs = {JumpDirection::Left, JumpDirection::Right,
                                     JumpDirection::Up, JumpDirection::Down};

  // When RNG is set, shuffle directions to randomize tie-breaking.
  // This produces varied but still optimal paths when multiple directions
  // have the same f-cost (e.g., diagonal movement to target).
  if (rng_) {
    std::shuffle(dirs.begin(), dirs.end(), *rng_);
  }

  return dirs;
}

bool Pathfinder::HasForcedNeighbor(Position /*pos*/, JumpDirection /*dir*/) const {
  // Not used in basic A* mode
  return false;
}

std::optional<Position> Pathfinder::Jump(Position pos, JumpDirection dir,
                                         Position /*goal*/) const {
  Position next = Step(pos, dir);

  // Check bounds and walkability
  if (!grid_.IsInBounds(next) || !grid_.IsWalkable(next)) {
    return std::nullopt;
  }

  // In A* mode, just return the adjacent cell
  return next;
}

std::vector<std::pair<Position, Pathfinder::JumpDirection>>
Pathfinder::GetSuccessors(Position pos,
                          const std::optional<JumpDirection>& parent_dir,
                          Position goal) const {
  std::vector<std::pair<Position, JumpDirection>> successors;

  auto directions = GetPrunedDirections(parent_dir);

  for (JumpDirection dir : directions) {
    auto jump_point = Jump(pos, dir, goal);
    if (jump_point) {
      successors.emplace_back(*jump_point, dir);
    }
  }

  return successors;
}

std::vector<Position> Pathfinder::ReconstructPath(
    const std::unordered_map<Position, Position, PositionHash>& came_from,
    Position current) const {
  std::vector<Position> path;
  path.push_back(current);

  while (came_from.count(current)) {
    current = came_from.at(current);
    path.push_back(current);
  }

  std::reverse(path.begin(), path.end());
  return path;
}

std::vector<Position> Pathfinder::InterpolatePath(
    const std::vector<Position>& jump_points) const {
  // In A* mode, path is already fully interpolated
  return jump_points;
}

std::vector<Position> Pathfinder::FindPath(Position from, Position to) const {
  // Edge cases
  if (from == to) {
    return {from};
  }
  if (!grid_.IsInBounds(from) || !grid_.IsInBounds(to)) {
    return {};
  }
  if (!grid_.IsWalkable(from) || !grid_.IsWalkable(to)) {
    return {};
  }

  // A* search
  std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
  std::unordered_map<Position, int, PositionHash> g_score;
  std::unordered_map<Position, Position, PositionHash> came_from;
  std::unordered_map<Position, JumpDirection, PositionHash> came_from_dir;
  std::unordered_set<Position, PositionHash> closed;

  g_score[from] = 0;
  open.push({from, 0, EuclideanDistance(from, to)});

  while (!open.empty()) {
    Node current = open.top();
    open.pop();

    // Skip if already processed
    if (closed.count(current.pos)) {
      continue;
    }
    closed.insert(current.pos);

    // Goal reached
    if (current.pos == to) {
      return ReconstructPath(came_from, current.pos);
    }

    // Get parent direction for pruning (not used in basic A*)
    std::optional<JumpDirection> parent_dir;
    if (came_from_dir.count(current.pos)) {
      parent_dir = came_from_dir[current.pos];
    }

    // Get successors
    auto successors = GetSuccessors(current.pos, parent_dir, to);

    for (const auto& [next_pos, dir] : successors) {
      if (closed.count(next_pos)) {
        continue;
      }

      int tentative_g = current.g_cost + 1;  // Uniform cost

      if (!g_score.count(next_pos) || tentative_g < g_score[next_pos]) {
        g_score[next_pos] = tentative_g;
        came_from[next_pos] = current.pos;
        came_from_dir[next_pos] = dir;

        double f = tentative_g + EuclideanDistance(next_pos, to);
        open.push({next_pos, tentative_g, f});
      }
    }
  }

  return {};  // No path found
}

int Pathfinder::GetDistance(Position from, Position to) const {
  auto path = FindPath(from, to);
  if (path.empty()) {
    return -1;
  }
  return static_cast<int>(path.size()) - 1;  // Number of steps
}

bool Pathfinder::IsReachable(Position from, Position to) const {
  return !FindPath(from, to).empty();
}

std::vector<Position> Pathfinder::GetReachableCells(Position from) const {
  if (!grid_.IsInBounds(from) || !grid_.IsWalkable(from)) {
    return {};
  }

  std::vector<Position> reachable;
  reachable.reserve(grid_.GetRows() * grid_.GetCols());  // Upper bound
  std::unordered_set<Position, PositionHash> visited;
  std::queue<Position> frontier;

  frontier.push(from);
  visited.insert(from);

  while (!frontier.empty()) {
    Position current = frontier.front();
    frontier.pop();
    reachable.push_back(current);

    for (const Position& neighbor : grid_.GetWalkableNeighbors(current)) {
      if (!visited.count(neighbor)) {
        visited.insert(neighbor);
        frontier.push(neighbor);
      }
    }
  }

  return reachable;
}

}  // namespace companions
