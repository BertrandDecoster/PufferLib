// Copyright 2024
// Line and landing rules of skills, as pure functions of the world.
//
// Line rule: a line travels along (dr, dc); a non-pathable cell (wall) or the
// grid edge stops it on the cell before; holes (pathable, not walkable) and
// actors do not. Landing rule: something moved never ends on a hole or on
// another living actor.
//
// (dr, dc) is a unit orthogonal step; a zero direction (0, 0) is a no-op:
// every resolver returns `from` (and a dash crosses nothing).

#ifndef COMPANIONS_ENV_SKILL_MOTION_H_
#define COMPANIONS_ENV_SKILL_MOTION_H_

#include <functional>
#include <vector>

#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/types.h"

namespace companions {

// Unit step of a facing, as ApplyMovement moves.
void DirectionDelta(Direction dir, int& dr, int& dc);

// Cell `range` away from `from` by the line rule (`from` itself if a wall is adjacent).
Position ResolveGroundTarget(const Grid& grid, Position from, int dr, int dc, int range);

// Up to `distance` cells by the line rule, landing on the furthest valid
// cell (or `from`). `crossed`, if given, receives the cells strictly between
// `from` and the landing cell. `mover` does not block itself.
Position ResolveDash(const Grid& grid, const ObjectManager& objects,
                     Position from, int dr, int dc, int distance, ObjectId mover,
                     std::vector<Position>* crossed = nullptr);

// ResolveDash with the landing rule given: `can_land(p)` says whether the
// mover may end on `p` (ResolveDash passes CanLand on `objects`; a caller
// may read another view of the world, e.g. a caster already moved).
Position ResolveDashWith(const Grid& grid, Position from, int dr, int dc, int distance,
                         const std::function<bool(Position)>& can_land,
                         std::vector<Position>* crossed = nullptr);

// Exactly `distance` cells by (dr, dc), ignoring what lies between; if that
// cell is out of bounds, not walkable or occupied, `distance - 1`, and so on
// down to 1. Returns `from` if none is valid.
Position ResolveTeleport(const Grid& grid, const ObjectManager& objects,
                         Position from, int dr, int dc, int distance, ObjectId mover);

// Can `mover` end a motion on `p` (in bounds, walkable, no other living actor)?
bool CanLand(const Grid& grid, const ObjectManager& objects, Position p, ObjectId mover);

}  // namespace companions

#endif  // COMPANIONS_ENV_SKILL_MOTION_H_
