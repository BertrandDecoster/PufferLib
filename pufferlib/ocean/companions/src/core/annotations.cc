// Copyright 2024
// Semantic annotation store implementation.

#include "annotations.h"

#include <algorithm>

namespace companions {

// =============================================================================
// Mutation
// =============================================================================
void AnnotationStore::Add(AnnotationKey key, Annotation ann) {
  entries_.push_back({key, std::move(ann)});
}

void AnnotationStore::RemoveByOwner(int32_t lens_id) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [lens_id](const Entry& e) {
                       return e.ann.owner_lens_id == lens_id;
                     }),
      entries_.end());
}

void AnnotationStore::RemoveByKey(AnnotationKey key, SemanticTag tag) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [&](const Entry& e) {
                       return e.key == key && e.ann.tag == tag;
                     }),
      entries_.end());
}

void AnnotationStore::Clear() { entries_.clear(); }

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
  for (const Entry& e : entries_) {
    if (e.key == key && e.ann.tag == tag) return true;
  }
  return false;
}

std::vector<Position> AnnotationStore::FindCellsWithTag(SemanticTag tag) const {
  std::vector<Position> result;
  for (const Entry& e : entries_) {
    if (e.key.target == AnnotationTarget::Cell && e.ann.tag == tag) {
      result.push_back(e.key.pos);
    }
  }
  return result;
}

std::vector<ObjectId> AnnotationStore::FindAgentsWithTag(SemanticTag tag) const {
  std::vector<ObjectId> result;
  for (const Entry& e : entries_) {
    if (e.key.target == AnnotationTarget::Agent && e.ann.tag == tag) {
      result.push_back(e.key.agent_id);
    }
  }
  return result;
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
  entries_.clear();
  entries_.reserve(s.size());
  for (const AnnotationSnapshot& a : s) {
    Entry e;
    e.key.target = static_cast<AnnotationTarget>(a.target_type);
    e.key.pos = a.pos;
    e.key.agent_id = a.agent_id;
    e.ann.tag = a.tag;
    e.ann.owner_lens_id = a.owner_lens_id;
    for (const auto& kv : a.params) {
      e.ann.params.emplace(kv.first, kv.second);
    }
    entries_.push_back(std::move(e));
  }
}

// =============================================================================
// SemanticTagToString - keep in sync with htn_bridge.py SEMANTIC_TAG_NAMES.
// =============================================================================
std::string SemanticTagToString(SemanticTag tag) {
  switch (tag) {
    case SemanticTag::SynchroGoal: return "SynchroGoal";
    case SemanticTag::AggroTarget: return "AggroTarget";
    case SemanticTag::QuestPickup: return "QuestPickup";
    case SemanticTag::SafeZone:    return "SafeZone";
    case SemanticTag::TargetMob:   return "TargetMob";
    case SemanticTag::SkillGiver:  return "SkillGiver";
    case SemanticTag::Escort:      return "Escort";
    case SemanticTag::HtnName:     return "HtnName";
    case SemanticTag::Room:        return "Room";
    case SemanticTag::_Count:      return "Unknown";
  }
  return "Unknown";
}

}  // namespace companions
