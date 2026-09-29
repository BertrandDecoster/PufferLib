// Copyright 2024
// Line, path and landing rules of skills, as pure functions of the world.
//
// Line rule (a ground target, a projectile): a line travels along (dr, dc);
// a non-pathable cell (wall) or the grid edge stops it on the cell before;
// holes (pathable, not walkable) and actors do not. Landing rule: something
// moved never ends on a hole or on another living actor.
// Path rule (BaseEnv's one motion phase): a dash, a push or a pull stops at
// the first blocker on its path, on the cell before it. For a dash, a wall or
// a living actor (it jumps holes, but never ends on one); for a forced move
// (a push / pull), a wall, a hole or a living actor. A teleport only needs
// its landing cell.
//
// (dr, dc) is a unit orthogonal step, but for a forced move's summed offset;
// a zero direction (0, 0) is a no-op: every resolver returns `from` (and a
// dash crosses nothing).

#ifndef COMPANIONS_ENV_SKILL_MOTION_H_
#define COMPANIONS_ENV_SKILL_MOTION_H_

#include <vector>

#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/types.h"

namespace companions {

// Unit step of a facing, as ApplyMovement moves.
void DirectionDelta(Direction dir, int& dr, int& dc);

// Cell `range` away from `from` by the line rule (`from` itself if a wall is adjacent).
Position ResolveGroundTarget(const Grid& grid, Position from, int dr, int dc, int range);

// Up to `distance` cells by the path rule of a dash, with the world as it
// is: a wall, the grid edge or the first living actor but `mover` stops it;
// it lands on the furthest walkable cell before that (or `from`). `crossed`,
// if given, receives the cells strictly between `from` and the landing cell
// (holes included).
Position ResolveDash(const Grid& grid, const ObjectManager& objects,
                     Position from, int dr, int dc, int distance, ObjectId mover,
                     std::vector<Position>* crossed = nullptr);

// Exactly `distance` cells by (dr, dc), ignoring what lies between; if that
// cell is out of bounds, not walkable or occupied, `distance - 1`, and so on
// down to 1. Returns `from` if none is valid.
Position ResolveTeleport(const Grid& grid, const ObjectManager& objects,
                         Position from, int dr, int dc, int distance, ObjectId mover);

// Where a dash / teleport from `from` to `landing` (on one axis) may end,
// preferred first, into `cells` (replaced): the landing, then each closer
// walkable cell of its line. Empty when `landing` is `from`.
void CasterCandidates(const Grid& grid, Position from, Position landing,
                      std::vector<Position>& cells);

// The cells a forced move of offset (dr, dc) crosses, into `cells`
// (replaced), in order, up to (not including) the first cell out of the grid
// or not walkable (a wall, a hole). On an axis, the straight line; else the
// line toward `from + (dr, dc)` in max(|dr|, |dc|) cells, the k-th at
// (dr * k / n, dc * k / n) rounded half away from zero (a diagonal step
// passes between two corner cells without reading them). Empty for (0, 0).
void ForcedMovePath(const Grid& grid, Position from, int dr, int dc,
                    std::vector<Position>& cells);

// Can `mover` end a motion on `p` (in bounds, walkable, no other living actor)?
bool CanLand(const Grid& grid, const ObjectManager& objects, Position p, ObjectId mover);

}  // namespace companions

#endif  // COMPANIONS_ENV_SKILL_MOTION_H_
