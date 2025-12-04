// Copyright 2024
// Pathfinder - A* search for 4-connected grids

#ifndef COMPANIONS_CORE_PATHFINDER_H_
#define COMPANIONS_CORE_PATHFINDER_H_

#include <optional>
#include <random>
#include <vector>

#include "grid.h"
#include "types.h"

namespace companions {

// =============================================================================
// Pathfinder - A* search for 4-connected grids
//
// Current Implementation: Standard A* with Euclidean heuristic
// - Optimal paths guaranteed for 4-connected grids
// - O(V log V) time complexity with priority queue
// - Euclidean distance heuristic is admissible (always ≤ actual cost)
// - Euclidean naturally prioritizes reducing the larger axis first
//
// Future Optimization: Jump Point Search 4-way (JPS4)
// - Structure prepared with JumpDirection enum and Jump() method
// - JPS4 can reduce explored nodes by jumping over straight corridors
// - Maintains optimality while improving average-case performance
// - See: Harabor & Grastien, "Online Graph Pruning for Pathfinding" (2011)
//
// Migration Path:
// - Implement forced neighbor detection in HasForcedNeighbor()
// - Enable jumping logic in Jump() method (currently returns adjacent cell)
// - Activate pruning in GetPrunedDirections() (currently explores all 4 dirs)
// - Add interpolation in InterpolatePath() to fill gaps between jump points
// =============================================================================
class Pathfinder {
 public:
  explicit Pathfinder(const Grid& grid);

  // Optional RNG for randomizing tie-breaking when multiple paths are equally
  // optimal (e.g., diagonal movement). When set, the direction exploration
  // order is shuffled, producing varied but still optimal paths.
  void SetRng(std::mt19937* rng) { rng_ = rng; }

  // Main API
  // Returns empty vector if no path exists
  std::vector<Position> FindPath(Position from, Position to) const;

  // Returns -1 if unreachable
  int GetDistance(Position from, Position to) const;

  // Connectivity check
  bool IsReachable(Position from, Position to) const;

  // Flood fill - returns all positions reachable from start
  std::vector<Position> GetReachableCells(Position from) const;

 private:
  // Direction for neighbor expansion
  enum class JumpDirection { Left, Right, Up, Down };

  // A* node for open list
  struct Node {
    Position pos;
    int g_cost;     // Cost from start (integer - grid steps)
    double f_cost;  // g + heuristic (double for Euclidean)

    bool operator>(const Node& other) const { return f_cost > other.f_cost; }
  };

  // A* core functions
  std::optional<Position> Jump(Position pos, JumpDirection dir,
                               Position goal) const;
  std::vector<std::pair<Position, JumpDirection>> GetSuccessors(
      Position pos, const std::optional<JumpDirection>& parent_dir,
      Position goal) const;
  bool HasForcedNeighbor(Position pos, JumpDirection dir) const;

  // Helper functions
  Position Step(Position pos, JumpDirection dir) const;
  bool IsHorizontal(JumpDirection dir) const;
  std::vector<JumpDirection> GetPrunedDirections(
      const std::optional<JumpDirection>& parent_dir) const;

  // Reconstruct path from parent map
  std::vector<Position> ReconstructPath(
      const std::unordered_map<Position, Position, PositionHash>& came_from,
      Position current) const;

  // Fill in intermediate positions between jump points
  std::vector<Position> InterpolatePath(
      const std::vector<Position>& jump_points) const;

  const Grid& grid_;
  std::mt19937* rng_ = nullptr;  // Optional RNG for direction randomization
};

}  // namespace companions

#endif  // COMPANIONS_CORE_PATHFINDER_H_
