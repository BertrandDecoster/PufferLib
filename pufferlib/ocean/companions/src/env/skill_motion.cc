// Copyright 2024
#include "skill_motion.h"

#include <algorithm>
#include <cstdlib>

namespace companions {

bool CanLand(const Grid& grid, const ObjectManager& objects, Position p,
             ObjectId mover) {
  if (!grid.IsInBounds(p) || !grid.IsWalkable(p)) return false;
  const Actor* a = objects.GetActorAt(p);
  return !(a && a->IsAlive() && a->GetId() != mover);
}

void DirectionDelta(Direction dir, int& dr, int& dc) {
  Position p = ApplyMovement({0, 0}, DirectionToMovement(dir));
  dr = p.row;
  dc = p.col;
}

Position ResolveGroundTarget(const Grid& grid, Position from, int dr, int dc,
                             int range) {
  if (dr == 0 && dc == 0) return from;
  Position cur = from;
  for (int i = 0; i < range; ++i) {
    Position next{cur.row + dr, cur.col + dc};
    if (!grid.IsInBounds(next) || !grid.IsPathable(next)) break;
    cur = next;
  }
  return cur;
}

Position ResolveDash(const Grid& grid, const ObjectManager& objects,
                     Position from, int dr, int dc, int distance, ObjectId mover,
                     std::vector<Position>* crossed) {
  if (crossed) crossed->clear();
  if (dr == 0 && dc == 0) return from;
  Position best = from;
  int best_step = 0;
  for (int i = 1; i <= distance; ++i) {
    const Position cur{from.row + dr * i, from.col + dc * i};
    if (!grid.IsInBounds(cur) || !grid.IsPathable(cur)) break;  // A wall
    const Actor* a = objects.GetActorAt(cur);
    if (a && a->IsAlive() && a->GetId() != mover) break;  // The first actor
    if (grid.IsWalkable(cur)) {  // Never ends on a hole
      best = cur;
      best_step = i;
    }
  }
  if (crossed) {
    for (int i = 1; i < best_step; ++i) {
      crossed->push_back({from.row + dr * i, from.col + dc * i});
    }
  }
  return best;
}

Position ResolveTeleport(const Grid& grid, const ObjectManager& objects,
                         Position from, int dr, int dc, int distance, ObjectId mover) {
  if (dr == 0 && dc == 0) return from;
  for (int d = distance; d >= 1; --d) {
    Position p{from.row + dr * d, from.col + dc * d};
    if (CanLand(grid, objects, p, mover)) return p;
  }
  return from;
}

void CasterCandidates(const Grid& grid, Position from, Position landing,
                      std::vector<Position>& cells) {
  cells.clear();
  const int dr = (landing.row > from.row) - (landing.row < from.row);
  const int dc = (landing.col > from.col) - (landing.col < from.col);
  const int steps = std::abs(landing.row - from.row) + std::abs(landing.col - from.col);
  for (int i = steps; i >= 1; --i) {
    const Position p{from.row + dr * i, from.col + dc * i};
    if (grid.IsInBounds(p) && grid.IsWalkable(p)) cells.push_back(p);
  }
}

namespace {
// d * k / n rounded half away from zero (n > 0)
int RoundedShare(int d, int k, int n) {
  const int num = d * k;
  const int q = (2 * std::abs(num) + n) / (2 * n);
  return num < 0 ? -q : q;
}
}  // namespace

void ForcedMovePath(const Grid& grid, Position from, int dr, int dc,
                    std::vector<Position>& cells) {
  cells.clear();
  const int n = std::max(std::abs(dr), std::abs(dc));
  for (int k = 1; k <= n; ++k) {
    const Position p{from.row + RoundedShare(dr, k, n), from.col + RoundedShare(dc, k, n)};
    if (!grid.IsInBounds(p) || !grid.IsWalkable(p)) break;
    cells.push_back(p);
  }
}

}  // namespace companions
