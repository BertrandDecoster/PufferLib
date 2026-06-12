// Copyright 2024
// Semantic annotation store implementation.

#include "annotations.h"

#include <algorithm>

namespace companions {

// =============================================================================
// Cache management - bumps mutation_version_ on every mutator.
// See .claude/reviews/companions-audit-2026-04-23.md F5/F6 for rationale.
// =============================================================================
void AnnotationStore::RebuildCacheIfStale() const {
  if (cache_version_ == mutation_version_) return;
  for (auto& v : cells_by_tag_) v.clear();
  for (auto& s : cell_set_by_tag_) s.clear();
  for (auto& v : agents_by_tag_) v.clear();
  for (const Entry& e : entries_) {
    std::size_t idx = static_cast<std::size_t>(e.ann.tag);
    if (idx >= kTagCount) continue;  // defensive: tag out of range
    if (e.key.target == AnnotationTarget::Cell) {
      cells_by_tag_[idx].push_back(e.key.pos);
      cell_set_by_tag_[idx].insert(e.key.pos);
    } else {
      agents_by_tag_[idx].push_back(e.key.agent_id);
    }
  }
  cache_version_ = mutation_version_;
}

// =============================================================================
// Mutation
// =============================================================================
void AnnotationStore::Add(AnnotationKey key, Annotation ann) {
  // Game-rule analog: the board can't hold two agents on the same cell, so
  // two identical (key, tag) entries are rejected. First-write-wins keeps
  // the already-stamped entry (including its owner_lens_id) and ignores the
  // later Add silently. Keeping duplicates out at insertion time means
  // query helpers like FindCellsWithTag(...).size() and HasTag(...) agree
  // on a per-cell basis (no duplicate rows to inflate the count).
  for (const Entry& e : entries_) {
    if (e.key == key && e.ann.tag == ann.tag) return;
  }
  entries_.push_back({key, std::move(ann)});
  ++mutation_version_;
}

void AnnotationStore::RemoveByOwner(int32_t lens_id) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [lens_id](const Entry& e) {
                       return e.ann.owner_lens_id == lens_id;
                     }),
      entries_.end());
  ++mutation_version_;
}

void AnnotationStore::RemoveByKey(AnnotationKey key, SemanticTag tag) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [&](const Entry& e) {
                       return e.key == key && e.ann.tag == tag;
                     }),
      entries_.end());
  ++mutation_version_;
}

void AnnotationStore::Clear() {
  entries_.clear();
  ++mutation_version_;
}

void AnnotationStore::TransformCellPositions(
    const std::function<Position(Position)>& func) {
  for (Entry& e : entries_) {
    if (e.key.target == AnnotationTarget::Cell) {
      e.key.pos = func(e.key.pos);
    }
  }
  ++mutation_version_;
}

// =============================================================================
// Query
// =============================================================================
std::vector<const Annotation*> AnnotationStore::Get(AnnotationKey key) const {
  std::vector<const Annotation*> result;
  for (const Entry& e : entries_) {
    if (e.key == key) result.push_back(&e.ann);
  }
  return result;
}

bool AnnotationStore::HasTag(AnnotationKey key, SemanticTag tag) const {
  std::size_t idx = static_cast<std::size_t>(tag);
  if (idx >= kTagCount) return false;
  RebuildCacheIfStale();
  if (key.target == AnnotationTarget::Cell) {
    return cell_set_by_tag_[idx].count(key.pos) > 0;
  }
  const auto& agents = agents_by_tag_[idx];
  return std::find(agents.begin(), agents.end(), key.agent_id) != agents.end();
}

const std::vector<Position>& AnnotationStore::FindCellsWithTag(
    SemanticTag tag) const {
  static const std::vector<Position> kEmpty;
  std::size_t idx = static_cast<std::size_t>(tag);
  if (idx >= kTagCount) return kEmpty;
  RebuildCacheIfStale();
  return cells_by_tag_[idx];
}

std::vector<ObjectId> AnnotationStore::FindAgentsWithTag(SemanticTag tag) const {
  std::size_t idx = static_cast<std::size_t>(tag);
  if (idx >= kTagCount) return {};
  RebuildCacheIfStale();
  return agents_by_tag_[idx];
}

// =============================================================================
// Snapshot round-trip
// =============================================================================
std::vector<AnnotationSnapshot> AnnotationStore::Serialize() const {
  std::vector<AnnotationSnapshot> result;
  result.reserve(entries_.size());
  for (const Entry& e : entries_) {
    AnnotationSnapshot s;
    s.target_type = static_cast<uint8_t>(e.key.target);
    s.pos = e.key.pos;
    s.agent_id = e.key.agent_id;
    s.tag = e.ann.tag;
    s.params.assign(e.ann.params.begin(), e.ann.params.end());
    s.owner_lens_id = e.ann.owner_lens_id;
    result.push_back(std::move(s));
  }
  return result;
}

void AnnotationStore::Deserialize(const std::vector<AnnotationSnapshot>& s) {
  // Route through Add so the first-write-wins dedup invariant is preserved
  // even on payloads that accidentally contain duplicates (see F5).
  Clear();
  for (const AnnotationSnapshot& a : s) {
    AnnotationKey key;
    key.target = static_cast<AnnotationTarget>(a.target_type);
    key.pos = a.pos;
    key.agent_id = a.agent_id;
    Annotation ann;
    ann.tag = a.tag;
    ann.owner_lens_id = a.owner_lens_id;
    for (const auto& kv : a.params) {
      ann.params.emplace(kv.first, kv.second);
    }
    Add(std::move(key), std::move(ann));
  }
}

#ifndef NDEBUG
bool AnnotationStore::DebugCacheInvariantHolds() const {
  // Force a rebuild.
  ++mutation_version_;
  RebuildCacheIfStale();
  std::array<std::vector<Position>, kTagCount> expected_cells;
  std::array<std::vector<ObjectId>, kTagCount> expected_agents;
  for (const Entry& e : entries_) {
    std::size_t idx = static_cast<std::size_t>(e.ann.tag);
    if (idx >= kTagCount) continue;
    if (e.key.target == AnnotationTarget::Cell) {
      expected_cells[idx].push_back(e.key.pos);
    } else {
      expected_agents[idx].push_back(e.key.agent_id);
    }
  }
  for (std::size_t i = 0; i < kTagCount; ++i) {
    if (cells_by_tag_[i] != expected_cells[i]) return false;
    if (agents_by_tag_[i] != expected_agents[i]) return false;
    if (cell_set_by_tag_[i].size() != expected_cells[i].size()) return false;
    for (const Position& p : expected_cells[i]) {
      if (cell_set_by_tag_[i].count(p) == 0) return false;
    }
  }
  return true;
}
#endif

}  // namespace companions
