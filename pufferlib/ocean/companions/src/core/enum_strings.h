// Copyright 2024
// Centralized enum <-> string converters.
//
// Single home for the to/from-string pair of every enum that crosses a text
// boundary (JSON snapshots, CSV configs, logs). The JSON wire format is
// locked by tests/test_snapshot_golden.cc; the per-value strings and the
// reader fallbacks are additionally pinned by tests/test_enum_strings.cc.
//
// Display-only mappings (viz glyphs, ANSI color codes, board characters)
// intentionally do NOT live here - they are rendering concerns, not name
// mappings (see src/viz/renderer.cc, BaseEnv::ToString).

#ifndef COMPANIONS_CORE_ENUM_STRINGS_H_
#define COMPANIONS_CORE_ENUM_STRINGS_H_

#include <string>

#include "agent_config.h"  // TargetFilter
#include "annotations.h"
#include "cell.h"
#include "fsm/fsm_state.h"
#include "object.h"
#include "types.h"

namespace companions {

// Direction. FromString falls back to Up on unknown input.
std::string DirectionToString(Direction dir);
Direction DirectionFromString(const std::string& str);

// Faction. FromString falls back to COMPANION on unknown input.
std::string FactionToString(Faction faction);
Faction FactionFromString(const std::string& str);

// ObjectType. FromString falls back to Object on unknown input.
std::string ObjectTypeToString(ObjectType type);
ObjectType ObjectTypeFromString(const std::string& str);

// StatusType. ToString emits lowercase names ("stunned") - this is the JSON
// wire spelling. FromString is case-insensitive and also accepts the short
// CSV aliases ("stun", "slow", "mark"); unknown input falls back to None.
std::string StatusTypeToString(StatusType type);
StatusType StatusTypeFromString(const std::string& name);

// ActorColor. FromString falls back to None on unknown input.
std::string ActorColorToString(ActorColor color);
ActorColor ActorColorFromString(const std::string& str);

// CellKind. FromString falls back to Floor on unknown input; legacy v1 JSON
// "Synchro"/"Target" kinds also flatten to Floor (their task-semantic role
// lives in the annotations array).
std::string CellKindToString(CellKind kind);
CellKind CellKindFromString(const std::string& str);

// CellOrigin. FromString falls back to Default on unknown input.
std::string CellOriginToString(CellOrigin origin);
CellOrigin CellOriginFromString(const std::string& str);

// FSMStateType. FromString falls back to None on unknown input.
std::string FSMStateTypeToString(FSMStateType type);
FSMStateType FSMStateTypeFromString(const std::string& str);

// SemanticTag - stable string names, keep in sync with htn_bridge.py
// SEMANTIC_TAG_NAMES. FromString falls back to SynchroGoal on unknown input.
std::string SemanticTagToString(SemanticTag tag);
SemanticTag SemanticTagFromString(const std::string& str);

// ---------------------------------------------------------------------------
// CSV config vocabulary (data/agents.csv, data/effects.csv).
//
// These are deliberately SEPARATE from the JSON wire converters above: the
// wire readers are strict (exact spelling, locked by the golden snapshot
// fixtures), while the CSV readers below are shared by both config loaders,
// case-insensitive, and accept both the lowercase CSV vocabulary ("enemy")
// and the uppercase wire names ("ENEMY"). They warn on cerr and return the
// documented default for unknown input. No current data/*.csv row hits a
// fallback (pinned by tests/test_csv_utils.cc registry tests).
// ---------------------------------------------------------------------------

// Default on unknown input: TargetFilter::All (with a cerr warning).
TargetFilter TargetFilterFromCSV(const std::string& str);

// Default on unknown input: Faction::ENEMY (with a cerr warning) - the CSVs
// define enemy archetypes. The strict JSON wire reader FactionFromString
// keeps its separate COMPANION fallback; do not use it for CSV fields.
Faction FactionFromCSV(const std::string& str);

}  // namespace companions

#endif  // COMPANIONS_CORE_ENUM_STRINGS_H_
