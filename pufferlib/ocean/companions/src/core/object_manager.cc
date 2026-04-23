// Copyright 2024
// ObjectManager implementation

#include "object_manager.h"

#include <algorithm>
#include <cassert>

namespace companions {

ObjectManager::ObjectManager(int rows, int cols)
    : rows_(rows),
      cols_(cols),
      actor_grid_(rows, std::vector<Actor*>(cols, nullptr)) {}

ObjectManager::ObjectManager(const ObjectManager& other)
    : next_id_(other.next_id_),
      rows_(other.rows_),
      cols_(other.cols_),
      actor_grid_(rows_, std::vector<Actor*>(cols_, nullptr)) {
  // Deep copy all objects via Clone()
  for (const auto& [id, obj] : other.objects_) {
    auto cloned = obj->Clone();
    Object* ptr = cloned.get();
    objects_[id] = std::move(cloned);

    // Update spatial grid if it's an Actor
    if (Actor* actor = dynamic_cast<Actor*>(ptr)) {
      Position pos = actor->GetPosition();
      if (InBounds(pos.row, pos.col)) {
        actor_grid_[pos.row][pos.col] = actor;
      }
    }
  }
}

ObjectManager& ObjectManager::operator=(const ObjectManager& other) {
  if (this != &other) {
    // Clear existing state
    objects_.clear();

    // Copy dimensions
    next_id_ = other.next_id_;
    rows_ = other.rows_;
    cols_ = other.cols_;

    // Resize and clear grid
    actor_grid_.assign(rows_, std::vector<Actor*>(cols_, nullptr));

    // Deep copy all objects via Clone()
    for (const auto& [id, obj] : other.objects_) {
      auto cloned = obj->Clone();
      Object* ptr = cloned.get();
      objects_[id] = std::move(cloned);

      // Update spatial grid if it's an Actor
      if (Actor* actor = dynamic_cast<Actor*>(ptr)) {
        Position pos = actor->GetPosition();
        if (InBounds(pos.row, pos.col)) {
          actor_grid_[pos.row][pos.col] = actor;
        }
      }
    }
  }
  return *this;
}

bool ObjectManager::InBounds(int row, int col) const {
  return row >= 0 && row < rows_ && col >= 0 && col < cols_;
}

void ObjectManager::ClearGrid() {
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      actor_grid_[r][c] = nullptr;
    }
  }
}

void ObjectManager::RemoveObject(ObjectId id) {
  auto it = objects_.find(id);
  if (it == objects_.end()) return;

  // Remove from spatial grid if it's an Actor
  if (Actor* actor = dynamic_cast<Actor*>(it->second.get())) {
    Position pos = actor->GetPosition();
    if (InBounds(pos.row, pos.col) && actor_grid_[pos.row][pos.col] == actor) {
      actor_grid_[pos.row][pos.col] = nullptr;
    }
  }

  objects_.erase(it);
  ++mutation_version_;
}

void ObjectManager::Clear() {
  objects_.clear();
  ClearGrid();
  next_id_ = 0;
  ++mutation_version_;
}

Object* ObjectManager::GetObject(ObjectId id) {
  auto it = objects_.find(id);
  return (it != objects_.end()) ? it->second.get() : nullptr;
}

const Object* ObjectManager::GetObject(ObjectId id) const {
  auto it = objects_.find(id);
  return (it != objects_.end()) ? it->second.get() : nullptr;
}

Actor* ObjectManager::GetActor(ObjectId id) {
  return dynamic_cast<Actor*>(GetObject(id));
}

const Actor* ObjectManager::GetActor(ObjectId id) const {
  return dynamic_cast<const Actor*>(GetObject(id));
}

Actor* ObjectManager::GetActorAt(int row, int col) {
  if (!InBounds(row, col)) return nullptr;
  return actor_grid_[row][col];
}

const Actor* ObjectManager::GetActorAt(int row, int col) const {
  if (!InBounds(row, col)) return nullptr;
  return actor_grid_[row][col];
}

Actor* ObjectManager::GetActorAt(Position pos) {
  return GetActorAt(pos.row, pos.col);
}

const Actor* ObjectManager::GetActorAt(Position pos) const {
  return GetActorAt(pos.row, pos.col);
}

bool ObjectManager::IsOccupied(int row, int col) const {
  const Actor* actor = GetActorAt(row, col);
  return actor && actor->IsAlive();
}

bool ObjectManager::IsOccupied(Position pos) const {
  return IsOccupied(pos.row, pos.col);
}

void ObjectManager::UpdatePosition(ObjectId id, Position new_pos) {
  Actor* actor = GetActor(id);
  if (!actor) return;

  Position old_pos = actor->GetPosition();

  // Remove from old position in grid
  if (InBounds(old_pos.row, old_pos.col) &&
      actor_grid_[old_pos.row][old_pos.col] == actor) {
    actor_grid_[old_pos.row][old_pos.col] = nullptr;
  }

  // Add to new position in grid
  if (InBounds(new_pos.row, new_pos.col)) {
    actor_grid_[new_pos.row][new_pos.col] = actor;
  }

  // Update actor's position
  actor->SetPosition(new_pos, PositionUpdateKey{});
}

std::vector<Actor*> ObjectManager::GetAllActors() {
  std::vector<Actor*> result;
  result.reserve(objects_.size());
  for (auto& [id, obj] : objects_) {
    if (Actor* actor = dynamic_cast<Actor*>(obj.get())) {
      result.push_back(actor);
    }
  }
  return result;
}

std::vector<const Actor*> ObjectManager::GetAllActors() const {
  std::vector<const Actor*> result;
  result.reserve(objects_.size());
  for (const auto& [id, obj] : objects_) {
    if (const Actor* actor = dynamic_cast<const Actor*>(obj.get())) {
      result.push_back(actor);
    }
  }
  return result;
}

std::vector<Agent*> ObjectManager::GetAllAgents() {
  std::vector<Agent*> result;
  result.reserve(objects_.size());
  for (auto& [id, obj] : objects_) {
    if (Agent* agent = dynamic_cast<Agent*>(obj.get())) {
      result.push_back(agent);
    }
  }
  // Sort by agent index for consistent ordering
  std::sort(result.begin(), result.end(),
            [](const Agent* a, const Agent* b) {
              return a->GetAgentIndex() < b->GetAgentIndex();
            });
  return result;
}

std::vector<const Agent*> ObjectManager::GetAllAgents() const {
  std::vector<const Agent*> result;
  result.reserve(objects_.size());
  for (const auto& [id, obj] : objects_) {
    if (const Agent* agent = dynamic_cast<const Agent*>(obj.get())) {
      result.push_back(agent);
    }
  }
  // Sort by agent index for consistent ordering
  std::sort(result.begin(), result.end(),
            [](const Agent* a, const Agent* b) {
              return a->GetAgentIndex() < b->GetAgentIndex();
            });
  return result;
}

std::vector<Companion*> ObjectManager::GetAllCompanions() {
  std::vector<Companion*> result;
  result.reserve(objects_.size());
  for (auto& [id, obj] : objects_) {
    if (Companion* comp = dynamic_cast<Companion*>(obj.get())) {
      result.push_back(comp);
    }
  }
  // Sort by agent index for consistent ordering
  std::sort(result.begin(), result.end(),
            [](const Companion* a, const Companion* b) {
              return a->GetAgentIndex() < b->GetAgentIndex();
            });
  return result;
}

std::vector<const Companion*> ObjectManager::GetAllCompanions() const {
  std::vector<const Companion*> result;
  result.reserve(objects_.size());
  for (const auto& [id, obj] : objects_) {
    if (const Companion* comp = dynamic_cast<const Companion*>(obj.get())) {
      result.push_back(comp);
    }
  }
  // Sort by agent index for consistent ordering
  std::sort(result.begin(), result.end(),
            [](const Companion* a, const Companion* b) {
              return a->GetAgentIndex() < b->GetAgentIndex();
            });
  return result;
}

const std::vector<AgentFSM*>& ObjectManager::GetAllAgentFSMs() {
  if (fsm_cache_version_ != mutation_version_) {
    fsm_cache_.clear();
    for (auto& [id, obj] : objects_) {
      if (AgentFSM* fsm = dynamic_cast<AgentFSM*>(obj.get())) {
        fsm_cache_.push_back(fsm);
      }
    }
    std::sort(fsm_cache_.begin(), fsm_cache_.end(),
              [](const AgentFSM* a, const AgentFSM* b) {
                return a->GetId() < b->GetId();
              });
    fsm_cache_version_ = mutation_version_;
  }
  return fsm_cache_;
}

std::vector<const AgentFSM*> ObjectManager::GetAllAgentFSMs() const {
  std::vector<const AgentFSM*> result;
  result.reserve(objects_.size());
  for (const auto& [id, obj] : objects_) {
    if (const AgentFSM* fsm = dynamic_cast<const AgentFSM*>(obj.get())) {
      result.push_back(fsm);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const AgentFSM* a, const AgentFSM* b) {
              return a->GetId() < b->GetId();
            });
  return result;
}

int ObjectManager::GetNumActors() const {
  int count = 0;
  for (const auto& [id, obj] : objects_) {
    if (dynamic_cast<const Actor*>(obj.get())) {
      count++;
    }
  }
  return count;
}

int ObjectManager::GetNumAgents() const {
  int count = 0;
  for (const auto& [id, obj] : objects_) {
    if (dynamic_cast<const Agent*>(obj.get())) {
      count++;
    }
  }
  return count;
}

int ObjectManager::GetNumCompanions() const {
  int count = 0;
  for (const auto& [id, obj] : objects_) {
    if (dynamic_cast<const Companion*>(obj.get())) {
      count++;
    }
  }
  return count;
}

void ObjectManager::TransformActorPositions(
    int new_rows, int new_cols,
    std::function<Position(Position, int, int)> transform_func) {
  int old_rows = rows_;
  int old_cols = cols_;

  // Collect actors and their new positions
  std::vector<std::pair<Actor*, Position>> updates;
  for (auto& [id, obj] : objects_) {
    if (Actor* actor = dynamic_cast<Actor*>(obj.get())) {
      Position old_pos = actor->GetPosition();
      Position new_pos = transform_func(old_pos, old_rows, old_cols);
      updates.push_back({actor, new_pos});
    }
  }

  // Resize the spatial grid to new dimensions
  rows_ = new_rows;
  cols_ = new_cols;
  actor_grid_.assign(new_rows, std::vector<Actor*>(new_cols, nullptr));

  // Apply new positions
  for (auto& [actor, new_pos] : updates) {
    actor->SetPosition(new_pos, PositionUpdateKey{});
    if (InBounds(new_pos.row, new_pos.col)) {
      actor_grid_[new_pos.row][new_pos.col] = actor;
    }
  }
}

}  // namespace companions
