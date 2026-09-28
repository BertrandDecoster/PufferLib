// Copyright 2024
// JSON serialization for Snapshot

#ifndef COMPANIONS_CORE_SNAPSHOT_JSON_H_
#define COMPANIONS_CORE_SNAPSHOT_JSON_H_

#include <string>

#include "snapshot.h"

namespace companions {

// Serialize snapshot to JSON string (pretty-printed with 2-space indent)
std::string SnapshotToJson(const Snapshot& snapshot);

// Deserialize snapshot from JSON string. Throws std::runtime_error (never a raw
// nlohmann exception) on invalid JSON, a missing or mistyped key, an unknown
// key in a skill / zone / tag object, an unknown enum or status string, or
// data Snapshot::ValidateSkillsTagsZones rejects. The message names the
// section being read, e.g. "cell_tags[3]: key 'tag' not found".
Snapshot SnapshotFromJson(const std::string& json_str);

// File I/O convenience functions
bool SaveSnapshotToJsonFile(const Snapshot& snapshot, const std::string& filepath);
Snapshot LoadSnapshotFromJsonFile(const std::string& filepath);

}  // namespace companions

#endif  // COMPANIONS_CORE_SNAPSHOT_JSON_H_
