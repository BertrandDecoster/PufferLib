// Copyright 2024
// ObjectManager for The Companions game

#ifndef COMPANIONS_CORE_OBJECT_MANAGER_H_
#define COMPANIONS_CORE_OBJECT_MANAGER_H_

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "object.h"
#include "types.h"

namespace companions {

// =============================================================================
// ObjectManager - owns all actors, maintains spatial grid for O(1) lookup
// =============================================================================
class ObjectManager {
 public:
  explicit ObjectManager(int rows = kDefaultGridSize, int cols = kDefaultGridSize);
  ~ObjectManager() = default;

  // Deep copy via Actor::Clone()
  ObjectManager(const ObjectManager& other);
  ObjectManager& operator=(const ObjectManager& other);
  ObjectManager(ObjectManager&&) = default;
  ObjectManager& operator=(ObjectManager&&) = default;

  // Actor creation - returns pointer to created actor (manager owns it)
  template<typename T>
  T* CreateActor(Position pos);

  // Object removal
  void RemoveObject(ObjectId id);
  void Clear();

  // Object lookup by ID
  Object* GetObject(ObjectId id);
  const Object* GetObject(ObjectId id) const;

  // Actor lookup by ID (convenience, returns nullptr for non-Actor)
  Actor* GetActor(ObjectId id);
  const Actor* GetActor(ObjectId id) const;

  // Position mapping - O(1) array access
  Actor* GetActorAt(Position pos);
  const Actor* GetActorAt(Position pos) const;
  Actor* GetActorAt(int row, int col);
  const Actor* GetActorAt(int row, int col) const;
  bool IsOccupied(Position pos) const;
  bool IsOccupied(int row, int col) const;

  // Update position (called after validated movement)
  void UpdatePosition(ObjectId id, Position new_pos);

  // Iteration helpers
  std::vector<Actor*> GetAllActors();
  std::vector<const Actor*> GetAllActors() const;

  std::vector<Agent*> GetAllAgents();
  std::vector<const Agent*> GetAllAgents() const;

  std::vector<Companion*> GetAllCompanions();
  std::vector<const Companion*> GetAllCompanions() const;

  // Filtered iteration over FSM-bearing agents. Cached internally and
  // invalidated on any mutation, so repeated per-step calls (AggroLens has
  // four) are O(1) lookups instead of full-scan dynamic_cast loops.
  // Audit F10.
  const std::vector<AgentFSM*>& GetAllAgentFSMs();
  std::vector<const AgentFSM*> GetAllAgentFSMs() const;

  // Counts
  int GetNumObjects() const { return static_cast<int>(objects_.size()); }
  int GetNumActors() const;
  int GetNumAgents() const;
  int GetNumCompanions() const;

  // Grid size accessors
  int GetRows() const { return rows_; }
  int GetCols() const { return cols_; }

  // D4 transform support: resize grid and transform all actor positions
  // transform_func maps (old_pos, old_rows, old_cols) -> new_pos
  void TransformActorPositions(
      int new_rows, int new_cols,
      std::function<Position(Position, int, int)> transform_func);

 private:
  bool InBounds(int row, int col) const;
  void ClearGrid();

  ObjectId next_id_ = 0;
  int rows_;
  int cols_;
  std::unordered_map<ObjectId, std::unique_ptr<Object>> objects_;
  std::vector<std::vector<Actor*>> actor_grid_;  // [row][col] -> Actor* or nullptr

  // Cached filtered iterator for FSM agents. mutation_version_ is bumped on
  // CreateActor/RemoveObject/Clear; GetAllAgentFSMs rebuilds the cache when
  // fsm_cache_version_ lags. Audit F10.
  mutable uint32_t mutation_version_ = 1;
  mutable uint32_t fsm_cache_version_ = 0;
  mutable std::vector<AgentFSM*> fsm_cache_;
};

// =============================================================================
// Template implementation
// =============================================================================
template<typename T>
T* ObjectManager::CreateActor(Position pos) {
  static_assert(std::is_base_of<Actor, T>::value,
                "T must derive from Actor");

  ObjectId id = next_id_++;
  auto actor = std::make_unique<T>(id, pos);
  T* ptr = actor.get();

  // If it's an Agent, assign agent index
  if (Agent* agent = dynamic_cast<Agent*>(ptr)) {
    agent->SetAgentIndex(GetNumAgents());
  }

  objects_[id] = std::move(actor);

  // Update spatial grid
  if (InBounds(pos.row, pos.col)) {
    actor_grid_[pos.row][pos.col] = ptr;
  }

  ++mutation_version_;
  return ptr;
}

}  // namespace companions

#endif  // COMPANIONS_CORE_OBJECT_MANAGER_H_
