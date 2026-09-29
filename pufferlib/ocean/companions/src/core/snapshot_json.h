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
// nlohmann exception) on invalid JSON, a missing or mistyped key (grid.cells,
// annotations, patrol_path, ... must be arrays; "zones" an object), an unknown
// key in a skill / zone / tag / zone table entry / reaction / tag status /
// weakness object, grid rows / cols <= 0 or more than 2^20 cells, a cell
// entry outside the grid, an unknown enum / status string (enum names are the
// exact spellings SnapshotToJson writes, case-sensitive except statuses; the
// only aliases are the legacy v1 cell kinds "Synchro" / "Target", read as
// Floor), or data
// Snapshot::ValidateSkillsTagsZones rejects. The message names the section
// being read, e.g. "cell_tags[3]: key 'tag' not found",
// "grid.cells[7]: (6, 0) outside 6x9", "agents[0]: unknown faction 'FOE'".
Snapshot SnapshotFromJson(const std::string& json_str);

// File I/O convenience functions
bool SaveSnapshotToJsonFile(const Snapshot& snapshot, const std::string& filepath);
Snapshot LoadSnapshotFromJsonFile(const std::string& filepath);

}  // namespace companions

#endif  // COMPANIONS_CORE_SNAPSHOT_JSON_H_
