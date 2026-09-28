// Copyright 2024
#include "skill_motion.h"

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
  if (dr == 0 && dc == 0) {
    if (crossed) crossed->clear();
    return from;
  }
  Position best = from;
  int best_step = 0;
  Position cur = from;
  for (int i = 1; i <= distance; ++i) {
    cur = {cur.row + dr, cur.col + dc};
    if (!grid.IsInBounds(cur) || !grid.IsPathable(cur)) break;
    if (CanLand(grid, objects, cur, mover)) {
      best = cur;
      best_step = i;
    }
  }
  if (crossed) {
    crossed->clear();
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

}  // namespace companions
