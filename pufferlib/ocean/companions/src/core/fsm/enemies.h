// Copyright 2024
// AgentFSM subclasses: Zombie, Goblin, Dragon

#ifndef COMPANIONS_CORE_FSM_ENEMIES_H_
#define COMPANIONS_CORE_FSM_ENEMIES_H_

#include <vector>

#include "../object.h"
#include "../object_manager.h"
#include "../pcg32.h"
#include "fsm_state.h"

namespace companions {

// =============================================================================
// Zombie - slow agent, cadence [1,0], uses A* pathfinding
// =============================================================================
class Zombie : public AgentFSM {
 public:
  Zombie(ObjectId id, Position pos);

  std::string GetTypeName() const override { return "Zombie"; }
  char GetChar() const override { return 'Z'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Zombie>(*this);
  }

  // Override MoveTo to use A* pathfinding with cadence
  void MoveTo(Position target, const BaseEnv& env) override;
};

// =============================================================================
// Goblin - fast agent, moves every turn, uses A* pathfinding
// =============================================================================
class Goblin : public AgentFSM {
 public:
  Goblin(ObjectId id, Position pos);

  std::string GetTypeName() const override { return "Goblin"; }
  char GetChar() const override { return 'G'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Goblin>(*this);
  }

  // Override MoveTo to use A* pathfinding
  void MoveTo(Position target, const BaseEnv& env) override;
};

// =============================================================================
// Dragon - flying agent, ignores walls, moves directly toward target
// =============================================================================
class Dragon : public AgentFSM {
 public:
  Dragon(ObjectId id, Position pos);

  std::string GetTypeName() const override { return "Dragon"; }
  char GetChar() const override { return 'D'; }

  std::unique_ptr<Object> Clone() const override {
    return std::make_unique<Dragon>(*this);
  }

  // Flying status
  bool IsFlying() const { return true; }

  // Override MoveTo for direct movement (ignores walls)
  void MoveTo(Position target, const BaseEnv& env) override;
};

// =============================================================================
// Factory Functions
// =============================================================================

// Create a Zombie with FSM configured
// - Cadence: [1,0] (moves every other turn)
// - Detection range: 3
// - Lose target range: 5
Zombie* CreateZombie(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng);

// Create a Goblin with FSM configured
// - Cadence: none (moves every turn)
// - Detection range: 4
// - Lose target range: 6
Goblin* CreateGoblin(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng);

// Create a Dragon with FSM configured
// - Cadence: none (moves every turn)
// - Detection range: 5
// - Lose target range: 8
Dragon* CreateDragon(ObjectManager& mgr, Position pos,
                     const std::vector<Position>& patrol_path,
                     pcg32& rng);

// =============================================================================
// Patrol Path Validation
// =============================================================================

// Validate that patrol path has only orthogonal segments (no diagonals)
// Returns true if valid, false if any segment is diagonal
bool ValidatePatrolPath(const std::vector<Position>& path);

}  // namespace companions

#endif  // COMPANIONS_CORE_FSM_ENEMIES_H_
