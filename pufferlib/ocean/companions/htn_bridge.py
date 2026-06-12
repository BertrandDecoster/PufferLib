"""
HTN Bridge - Converts between HTN facts and PufferLib Companions snapshots.

This module provides bidirectional conversion between:
- InductorHTN facts (Prolog-style predicates)
- Companions Snapshot structure (C++ game state)

Use cases:
1. Generate HTN facts from a game snapshot for planning
2. Validate that planned HTN state changes match expected game state
3. Display game state during HTN plan playback

Fact-to-Snapshot Mapping (hardcoded for initial implementation):
| HTN Fact                        | Snapshot Field                      |
|---------------------------------|-------------------------------------|
| at(?entity, ?room)              | AgentSnapshot.position + room map   |
| isEnemy(?e)                     | AgentSnapshot.faction = Enemy       |
| hasTag(?e, burning)             | AgentSnapshot.statuses              |
| hasAggro(?enemy, ?target)       | FSMSnapshot.target_id               |
| roomHasHazard(?room, oil)       | CellSnapshot.kind = Hazard          |
| connected(?roomA, ?roomB)       | Room adjacency from grid            |
| vulnerableTo(?entity, ?tag)     | (metadata, not in snapshot)         |
"""

import json
import re
import warnings
from typing import Any, Dict, List, Optional, Set, Tuple
from dataclasses import dataclass, field
from enum import IntEnum


# =============================================================================
# Enums matching C++ types
# =============================================================================

class CellKind(IntEnum):
    """Maps to companions::CellKind (cell.h).

    Task-semantic roles (SynchroGoal, AggroTarget, ...) are NOT CellKinds;
    they live in the annotation layer (see SemanticTag below).
    """
    Floor = 0
    Wall = 1
    Hazard = 2
    HealArea = 3


class Faction(IntEnum):
    """Maps to companions::Faction"""
    Companion = 0
    Enemy = 1
    Neutral = 2


class FSMStateType(IntEnum):
    """Maps to companions::FSMStateType"""
    NoState = 0
    Patrol = 1
    Aggro = 2
    ReturnToPatrol = 3


class SemanticTag(IntEnum):
    """Maps to companions::SemanticTag. Keep in sync with annotations.h."""
    SynchroGoal = 0
    AggroTarget = 1
    QuestPickup = 2
    SafeZone = 3
    TargetMob = 4
    SkillGiver = 5
    Escort = 6
    HtnName = 7
    Room = 8


# String <-> SemanticTag, mirrored in C++ SemanticTagToString() (annotations.cc).
SEMANTIC_TAG_NAMES: Dict[SemanticTag, str] = {
    SemanticTag.SynchroGoal: "SynchroGoal",
    SemanticTag.AggroTarget: "AggroTarget",
    SemanticTag.QuestPickup: "QuestPickup",
    SemanticTag.SafeZone:    "SafeZone",
    SemanticTag.TargetMob:   "TargetMob",
    SemanticTag.SkillGiver:  "SkillGiver",
    SemanticTag.Escort:      "Escort",
    SemanticTag.HtnName:     "HtnName",
    SemanticTag.Room:        "Room",
}
SEMANTIC_TAG_FROM_NAME = {v: k for k, v in SEMANTIC_TAG_NAMES.items()}


# =============================================================================
# Annotation helpers (read semantic tags attached to cells and agents)
# =============================================================================

def _iter_annotations(snapshot: Dict[str, Any]):
    """Yield each annotation dict in the snapshot (empty list if absent)."""
    for ann in snapshot.get("annotations", []) or []:
        yield ann


def cells_with_tag(snapshot: Dict[str, Any], tag: SemanticTag) -> List[Tuple[int, int]]:
    """Return (row, col) positions tagged with `tag` in the snapshot."""
    tag_name = SEMANTIC_TAG_NAMES[tag]
    out: List[Tuple[int, int]] = []
    for ann in _iter_annotations(snapshot):
        if ann.get("target") != "Cell" or ann.get("tag") != tag_name:
            continue
        pos = ann.get("pos", {})
        r, c = pos.get("row", -1), pos.get("col", -1)
        if r >= 0 and c >= 0:
            out.append((r, c))
    return out


def agents_with_tag(
    snapshot: Dict[str, Any], tag: SemanticTag
) -> List[Tuple[int, Dict[str, str]]]:
    """Return (agent_id, params_dict) for each agent annotated with `tag`."""
    tag_name = SEMANTIC_TAG_NAMES[tag]
    out: List[Tuple[int, Dict[str, str]]] = []
    for ann in _iter_annotations(snapshot):
        if ann.get("target") != "Agent" or ann.get("tag") != tag_name:
            continue
        out.append((ann.get("agent_id", -1), ann.get("params", {}) or {}))
    return out


def agent_htn_name(snapshot: Dict[str, Any], agent_id: int) -> Optional[str]:
    """Read the HtnName annotation for an agent (None if absent)."""
    for aid, params in agents_with_tag(snapshot, SemanticTag.HtnName):
        if aid == agent_id:
            name = params.get("name")
            if name:
                return name
    return None


def cell_room_name(snapshot: Dict[str, Any], row: int, col: int) -> Optional[str]:
    """Read the Room annotation for a cell position (None if absent)."""
    tag_name = SEMANTIC_TAG_NAMES[SemanticTag.Room]
    for ann in _iter_annotations(snapshot):
        if ann.get("target") != "Cell" or ann.get("tag") != tag_name:
            continue
        pos = ann.get("pos", {})
        if pos.get("row") == row and pos.get("col") == col:
            return (ann.get("params", {}) or {}).get("room")
    return None


# =============================================================================
# Snapshot schema validation
# =============================================================================

def _grid_scope(snapshot: Dict[str, Any]) -> Tuple[Dict[str, Any], str]:
    """Return (dict holding rows/cols/cells, key prefix for error messages).

    The game's JSON snapshots (snapshot_json.cc, see
    tests/data/golden_snapshot_v2.json) nest the grid under a "grid" key:
    {"grid": {"rows": ..., "cols": ..., "cells": [...]}, "agents": [...]}.
    The bridge's own partial snapshots (FactsToSnapshot) keep rows/cols/cells
    at the top level. Both shapes are accepted.
    """
    grid = snapshot.get("grid")
    if isinstance(grid, dict):
        return grid, "grid."
    return snapshot, ""


def validate_snapshot(snapshot: Any) -> None:
    """Validate that `snapshot` has the keys the bridge consumes.

    Raises ValueError naming the first missing key. Required:
    - grid geometry: "rows", "cols", "cells" (top-level, or nested under
      "grid" as the game's snapshot_json.cc emits them)
    - "agents"
    """
    if not isinstance(snapshot, dict):
        raise ValueError(f"snapshot must be a dict, got {type(snapshot).__name__}")
    scope, prefix = _grid_scope(snapshot)
    for key in ("rows", "cols", "cells"):
        if key not in scope:
            raise ValueError(f"snapshot missing required key: {prefix}{key}")
    if "agents" not in snapshot:
        raise ValueError("snapshot missing required key: agents")


def _grid_info(snapshot: Dict[str, Any]) -> Tuple[int, int, List[Dict[str, Any]]]:
    """Return (rows, cols, cells) from either accepted snapshot shape."""
    scope, _ = _grid_scope(snapshot)
    return scope.get("rows", 0), scope.get("cols", 0), scope.get("cells", [])


# =============================================================================
# Room Definition - HTN operates at room level, game at cell level
# =============================================================================

@dataclass
class RoomDef:
    """Definition of a room region in the grid."""
    name: str
    cells: List[Tuple[int, int]]  # List of (row, col) positions
    hazard: Optional[str] = None  # "oil", "electricity", etc.
    connections: List[str] = field(default_factory=list)


@dataclass
class LevelLayout:
    """
    Level layout defining room-to-grid mapping.

    This is the hardcoded mapping between HTN room names and actual grid cells.
    Each level needs its own layout definition.
    """
    rows: int
    cols: int
    rooms: Dict[str, RoomDef]
    spawn_points: Dict[str, Tuple[int, int]] = field(default_factory=dict)

    def get_room_at(self, row: int, col: int) -> Optional[str]:
        """Get room name for a grid position."""
        for room_name, room_def in self.rooms.items():
            if (row, col) in room_def.cells:
                return room_name
        return None

    def get_room_center(self, room_name: str) -> Optional[Tuple[int, int]]:
        """Get center position of a room."""
        if room_name not in self.rooms:
            return None
        cells = self.rooms[room_name].cells
        if not cells:
            return None
        avg_row = sum(c[0] for c in cells) // len(cells)
        avg_col = sum(c[1] for c in cells) // len(cells)
        return (avg_row, avg_col)


# =============================================================================
# Tag Mapping
# =============================================================================

# HTN tag names to status type IDs (matching game's StatusType enum)
TAG_TO_STATUS_TYPE = {
    "burning": 1,
    "frozen": 2,
    "electrified": 3,
    "disabled": 4,
    "steam": 5,
}

STATUS_TYPE_TO_TAG = {v: k for k, v in TAG_TO_STATUS_TYPE.items()}

# Hazard types in HTN to CellKind
HAZARD_TO_CELL_KIND = {
    "oil": CellKind.Hazard,      # Oil pools
    "electricity": CellKind.Hazard,  # Electrical hazards
    "lava": CellKind.Hazard,
}


# =============================================================================
# Fact Parsing Utilities
# =============================================================================

def parse_fact(fact_str: str) -> Optional[Tuple[str, List[str]]]:
    """
    Parse a Prolog-style fact into (predicate_name, [args]).

    Examples:
        "at(player, main)" -> ("at", ["player", "main"])
        "isEnemy(guard1)" -> ("isEnemy", ["guard1"])
        "connected(main, storage)" -> ("connected", ["main", "storage"])

    Returns None if parsing fails.
    """
    fact_str = fact_str.strip()
    if fact_str.endswith('.'):
        fact_str = fact_str[:-1]

    match = re.match(r'^(\w+)\(([^)]*)\)$', fact_str)
    if not match:
        # Might be a fact with no arguments
        match = re.match(r'^(\w+)$', fact_str)
        if match:
            return (match.group(1), [])
        return None

    predicate = match.group(1)
    args_str = match.group(2).strip()

    if not args_str:
        return (predicate, [])

    # Split args, handling potential whitespace
    args = [arg.strip() for arg in args_str.split(',')]
    return (predicate, args)


def format_fact(predicate: str, args: List[str]) -> str:
    """Format a predicate and args back into a fact string."""
    if not args:
        return f"{predicate}"
    return f"{predicate}({', '.join(args)})"


# =============================================================================
# Snapshot to Facts Conversion
# =============================================================================

class SnapshotToFacts:
    """
    Converts a Companions Snapshot (JSON) to HTN facts.

    Requires a LevelLayout to map grid positions to room names.
    """

    def __init__(self, layout: LevelLayout):
        self.layout = layout

    def convert(self, snapshot: Dict[str, Any]) -> List[str]:
        """
        Convert a snapshot dictionary to a list of HTN facts.

        Args:
            snapshot: Snapshot as parsed JSON (from snapshot_json.cc output)

        Returns:
            List of fact strings like ["at(player, main)", "isEnemy(guard1)"]

        Raises:
            ValueError: if the snapshot is missing required keys
                (rows/cols/cells/agents), see validate_snapshot().
        """
        validate_snapshot(snapshot)

        facts = []

        # Extract room connections from layout
        facts.extend(self._room_connections())

        # Extract room hazards from layout
        facts.extend(self._room_hazards())

        # Extract agent facts
        facts.extend(self._agent_facts(snapshot))

        # Extract cell-based facts (hazards marked in grid)
        facts.extend(self._cell_facts(snapshot))

        return facts

    def _room_connections(self) -> List[str]:
        """Generate connected(?roomA, ?roomB) facts from layout."""
        facts = []
        for room_name, room_def in self.layout.rooms.items():
            for connected_room in room_def.connections:
                facts.append(format_fact("connected", [room_name, connected_room]))
        return facts

    def _room_hazards(self) -> List[str]:
        """Generate roomHasHazard(?room, ?type) facts from layout."""
        facts = []
        for room_name, room_def in self.layout.rooms.items():
            if room_def.hazard:
                facts.append(format_fact("roomHasHazard", [room_name, room_def.hazard]))
        return facts

    def _agent_facts(self, snapshot: Dict[str, Any]) -> List[str]:
        """Generate facts about agents."""
        facts = []
        agents = snapshot.get("agents", [])

        for agent in agents:
            agent_id = agent.get("id", 0)
            agent_type = agent.get("type", 0)
            position = agent.get("position", {})
            row = position.get("row", -1)
            col = position.get("col", -1)
            faction = agent.get("faction", 0)
            alive = agent.get("alive", True)

            if not alive:
                continue

            # Determine entity name: annotation first, then legacy mapping.
            entity_name = self._agent_id_to_name(agent_id, agent_type, snapshot)

            # at(?entity, ?room) - annotation takes precedence over layout map.
            room = cell_room_name(snapshot, row, col)
            if room is None:
                room = self.layout.get_room_at(row, col)
            if room:
                facts.append(format_fact("at", [entity_name, room]))

            # isEnemy(?entity)
            if faction == Faction.Enemy:
                facts.append(format_fact("isEnemy", [entity_name]))

            # hasTag(?entity, ?tag) from statuses
            for status in agent.get("statuses", []):
                status_type = status.get("type", 0)
                if status_type in STATUS_TYPE_TO_TAG:
                    tag = STATUS_TYPE_TO_TAG[status_type]
                    facts.append(format_fact("hasTag", [entity_name, tag]))

            # hasAggro(?enemy, ?target) from FSM
            if agent.get("has_fsm", False):
                fsm = agent.get("fsm", {})
                target_id = fsm.get("target_id", -1)
                if target_id >= 0:
                    target_name = self._agent_id_to_name(target_id, 0, snapshot)
                    facts.append(format_fact("hasAggro", [entity_name, target_name]))

        # Emit skill-giver / quest facts derived from agent annotations.
        for aid, params in agents_with_tag(snapshot, SemanticTag.SkillGiver):
            giver_name = self._agent_id_to_name(aid, 0, snapshot)
            skill = params.get("skill")
            if skill:
                facts.append(format_fact("givesSkill", [giver_name, skill]))
        for aid, _ in agents_with_tag(snapshot, SemanticTag.TargetMob):
            mob_name = self._agent_id_to_name(aid, 0, snapshot)
            facts.append(format_fact("isTargetMob", [mob_name]))

        return facts

    def _cell_facts(self, snapshot: Dict[str, Any]) -> List[str]:
        """Generate facts from grid cells."""
        facts = []
        _, cols, cells = _grid_info(snapshot)

        # Track which rooms have which hazards from actual cells
        room_hazards: Dict[str, Set[str]] = {}

        for i, cell in enumerate(cells):
            row = i // cols
            col = i % cols
            kind = cell.get("kind", 0)

            if kind == CellKind.Hazard:
                room = self.layout.get_room_at(row, col)
                if room:
                    # Note: actual hazard type would need additional cell metadata
                    # For now, rely on layout definition
                    pass

        return facts

    def _agent_id_to_name(self, agent_id: int, agent_type: int,
                           snapshot: Optional[Dict[str, Any]] = None) -> str:
        """
        Map agent ID to HTN entity name.

        Uses the HtnName annotation on the agent (params["name"]). Snapshots
        without HtnName annotations get a generic "agent<id>" name and a
        warning - levels should emit HtnName annotations on spawn so entity
        identity lives in data, not in code.
        """
        del agent_type  # retained for call-site compatibility; unused
        if snapshot is not None:
            name = agent_htn_name(snapshot, agent_id)
            if name:
                return name

        warnings.warn(
            f"Snapshot lacks an HtnName annotation for agent {agent_id}; "
            f"using generic name 'agent{agent_id}'. Levels should emit "
            "HtnName annotations (SemanticTag.HtnName, params={'name': ...}) "
            "on spawn.",
            stacklevel=2,
        )
        return f"agent{agent_id}"


# =============================================================================
# Facts to Snapshot Conversion
# =============================================================================

class FactsToSnapshot:
    """
    Converts HTN facts to a partial Companions Snapshot.

    This is useful for:
    - Validating expected state matches actual state
    - Generating initial game state from HTN level definition

    Entity name -> agent id mapping comes from the `base_snapshot`'s HtnName
    annotations when one is provided. Without a base snapshot there is no
    data source for ids, so a LEGACY hardcoded mapping is used (and warned
    about) purely to keep old fact-only flows alive.
    """

    # LEGACY: hardcoded entity->id mapping, used ONLY when no base snapshot
    # with HtnName annotations is available. Kept for fact-only conversion
    # flows (e.g. generating an initial snapshot purely from HTN facts).
    # New code should pass base_snapshot so the mapping lives in data.
    LEGACY_ENTITY_TO_ID: Dict[str, int] = {
        "player": 0,
        "warden": 1,
        "guard1": 2,
        "guard2": 3,
    }

    def __init__(self, layout: LevelLayout,
                 base_snapshot: Optional[Dict[str, Any]] = None):
        self.layout = layout
        if base_snapshot is not None:
            validate_snapshot(base_snapshot)
            self.entity_to_id: Dict[str, int] = {}
            for aid, params in agents_with_tag(base_snapshot, SemanticTag.HtnName):
                name = params.get("name")
                if name:
                    self.entity_to_id[name] = aid
        else:
            warnings.warn(
                "FactsToSnapshot created without a base snapshot; falling "
                "back to the LEGACY hardcoded entity->id mapping "
                "(player/warden/guard1/guard2). Pass a snapshot carrying "
                "HtnName annotations to derive the mapping from data.",
                stacklevel=2,
            )
            self.entity_to_id = dict(self.LEGACY_ENTITY_TO_ID)

    def convert(self, facts: List[str]) -> Dict[str, Any]:
        """
        Convert a list of HTN facts to a partial snapshot dictionary.

        Note: This creates a partial snapshot - some fields will have defaults.
        """
        snapshot: Dict[str, Any] = {
            "rows": self.layout.rows,
            "cols": self.layout.cols,
            "cells": [],
            "agents": [],
            "effects": [],
            "tick": 0,
            "horizon": 100,
            "annotations": [],
        }

        # Initialize cells grid
        for row in range(self.layout.rows):
            for col in range(self.layout.cols):
                cell = {"kind": int(CellKind.Floor), "origin": 0}

                # Check if this position is in any room
                room = self.layout.get_room_at(row, col)
                if room is None:
                    cell["kind"] = int(CellKind.Wall)
                elif self.layout.rooms[room].hazard:
                    cell["kind"] = int(CellKind.Hazard)

                snapshot["cells"].append(cell)

        # Parse facts and build agents
        agents_data: Dict[str, Dict[str, Any]] = {}

        for fact_str in facts:
            parsed = parse_fact(fact_str)
            if not parsed:
                continue

            predicate, args = parsed

            if predicate == "at" and len(args) == 2:
                entity, room = args
                if entity not in agents_data:
                    agents_data[entity] = self._default_agent(entity)

                pos = self.layout.get_room_center(room)
                if pos:
                    agents_data[entity]["position"] = {"row": pos[0], "col": pos[1]}

            elif predicate == "isEnemy" and len(args) == 1:
                entity = args[0]
                if entity not in agents_data:
                    agents_data[entity] = self._default_agent(entity)
                agents_data[entity]["faction"] = int(Faction.Enemy)

            elif predicate == "hasTag" and len(args) == 2:
                entity, tag = args
                if entity not in agents_data:
                    agents_data[entity] = self._default_agent(entity)

                if tag in TAG_TO_STATUS_TYPE:
                    status = {"type": TAG_TO_STATUS_TYPE[tag], "duration": -1}
                    agents_data[entity]["statuses"].append(status)

            elif predicate == "hasAggro" and len(args) == 2:
                entity, target = args
                if entity not in agents_data:
                    agents_data[entity] = self._default_agent(entity)

                target_id = self.entity_to_id.get(target, -1)
                agents_data[entity]["has_fsm"] = True
                agents_data[entity]["fsm"]["state_type"] = int(FSMStateType.Aggro)
                agents_data[entity]["fsm"]["target_id"] = target_id

        # Convert agents dict to list
        snapshot["agents"] = list(agents_data.values())

        # Emit HtnName annotations so a subsequent SnapshotToFacts can recover
        # entity names without the hardcoded fallback.
        for entity_name, agent in agents_data.items():
            snapshot["annotations"].append({
                "target": "Agent",
                "agent_id": agent["id"],
                "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
                "owner_lens_id": -1,
                "params": {"name": entity_name},
            })

        # Emit Room annotations for every cell in every defined room, so the
        # HTN room membership is captured in the snapshot rather than only the
        # Python-side LevelLayout.
        for room_name, room_def in self.layout.rooms.items():
            for (r, c) in room_def.cells:
                snapshot["annotations"].append({
                    "target": "Cell",
                    "pos": {"row": r, "col": c},
                    "tag": SEMANTIC_TAG_NAMES[SemanticTag.Room],
                    "owner_lens_id": -1,
                    "params": {"room": room_name},
                })

        return snapshot

    def _default_agent(self, entity: str) -> Dict[str, Any]:
        """Create a default agent dict for an entity."""
        agent_id = self.entity_to_id.get(entity, len(self.entity_to_id))
        if entity not in self.entity_to_id:
            self.entity_to_id[entity] = agent_id

        return {
            "id": agent_id,
            "type": 4,  # AgentFSM by default
            "position": {"row": 0, "col": 0},
            "prev_position": {"row": 0, "col": 0},
            "health": 3,
            "max_health": 3,
            "agent_index": -1,
            "faction": int(Faction.Companion),
            "direction": 0,
            "color": 0,
            "alive": True,
            "statuses": [],
            "has_fsm": False,
            "fsm": {
                "state_type": int(FSMStateType.NoState),
                "target_id": -1,
                "patrol_path": [],
                "patrol_index": 0,
                "patrol_forward": True,
                "detection_range": 3,
                "lose_target_range": 5,
            },
            "cadence": [],
            "tick": 0,
        }


# =============================================================================
# State Comparison
# =============================================================================

def compare_states(expected_facts: List[str], actual_facts: List[str]) -> Dict[str, Any]:
    """
    Compare expected HTN facts with actual facts.

    Returns:
        Dict with:
        - missing: facts in expected but not in actual
        - extra: facts in actual but not in expected
        - match: whether all expected facts are present
    """
    expected_set = set(expected_facts)
    actual_set = set(actual_facts)

    missing = expected_set - actual_set
    extra = actual_set - expected_set

    return {
        "missing": list(missing),
        "extra": list(extra),
        "match": len(missing) == 0,
    }


# =============================================================================
# Layout Definitions (Hardcoded for initial levels)
# =============================================================================

def get_puzzle1_layout() -> LevelLayout:
    """
    Layout definition for puzzle1 (The Grease Trap).

    This defines the mapping between HTN room names and grid cells.
    """
    # 8x8 grid with rooms in quadrants
    rooms = {
        "main": RoomDef(
            name="main",
            cells=[(r, c) for r in range(1, 4) for c in range(1, 4)],
            connections=["storage", "generator", "corridor"],
        ),
        "storage": RoomDef(
            name="storage",
            cells=[(r, c) for r in range(1, 4) for c in range(4, 7)],
            hazard="oil",
            connections=["main"],
        ),
        "generator": RoomDef(
            name="generator",
            cells=[(r, c) for r in range(4, 7) for c in range(1, 4)],
            hazard="electricity",
            connections=["main", "corridor"],
        ),
        "corridor": RoomDef(
            name="corridor",
            cells=[(r, c) for r in range(4, 7) for c in range(4, 7)],
            connections=["main", "generator", "exit"],
        ),
        "exit": RoomDef(
            name="exit",
            cells=[(7, 4), (7, 5), (7, 6)],
            connections=["corridor"],
        ),
    }

    spawn_points = {
        "player": (2, 2),
        "warden": (2, 3),
        "guard1": (2, 5),
        "guard2": (5, 5),
    }

    return LevelLayout(rows=9, cols=8, rooms=rooms, spawn_points=spawn_points)


# Registry of level layouts
LEVEL_LAYOUTS: Dict[str, LevelLayout] = {
    "puzzle1": get_puzzle1_layout(),
}


def get_layout(level_name: str) -> Optional[LevelLayout]:
    """Get layout for a level by name."""
    return LEVEL_LAYOUTS.get(level_name)


# =============================================================================
# High-level API
# =============================================================================

def snapshot_to_facts(snapshot: Dict[str, Any], level_name: str) -> List[str]:
    """
    Convert a Companions snapshot to HTN facts.

    Args:
        snapshot: Snapshot dictionary (from JSON)
        level_name: Name of the level (for layout lookup)

    Returns:
        List of HTN fact strings
    """
    layout = get_layout(level_name)
    if not layout:
        raise ValueError(f"Unknown level: {level_name}")

    converter = SnapshotToFacts(layout)
    return converter.convert(snapshot)


def facts_to_snapshot(facts: List[str], level_name: str,
                      base_snapshot: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """
    Convert HTN facts to a Companions snapshot.

    Args:
        facts: List of HTN fact strings
        level_name: Name of the level (for layout lookup)
        base_snapshot: Optional snapshot whose HtnName annotations provide
            the entity->id mapping. Without it a legacy hardcoded mapping
            is used (and warned about).

    Returns:
        Partial snapshot dictionary
    """
    layout = get_layout(level_name)
    if not layout:
        raise ValueError(f"Unknown level: {level_name}")

    converter = FactsToSnapshot(layout, base_snapshot=base_snapshot)
    return converter.convert(facts)


def validate_plan_state(
    expected_facts: List[str],
    snapshot: Dict[str, Any],
    level_name: str
) -> Dict[str, Any]:
    """
    Validate that a game snapshot matches expected HTN facts.

    Args:
        expected_facts: Facts that should be true after plan execution
        snapshot: Actual game state snapshot
        level_name: Level name for layout lookup

    Returns:
        Comparison result with missing/extra facts
    """
    actual_facts = snapshot_to_facts(snapshot, level_name)
    return compare_states(expected_facts, actual_facts)


# =============================================================================
# CLI for testing
# =============================================================================

if __name__ == "__main__":
    import sys

    # Test with puzzle1 layout
    layout = get_puzzle1_layout()
    print(f"Puzzle1 layout: {layout.rows}x{layout.cols} grid")
    print(f"Rooms: {list(layout.rooms.keys())}")

    # Test fact parsing
    test_facts = [
        "at(player, main)",
        "at(guard1, storage)",
        "isEnemy(guard1)",
        "hasTag(guard1, burning)",
        "roomHasHazard(storage, oil)",
        "connected(main, storage)",
    ]

    print("\nParsing test facts:")
    for fact in test_facts:
        parsed = parse_fact(fact)
        print(f"  {fact} -> {parsed}")

    # Test facts to snapshot
    converter = FactsToSnapshot(layout)
    snapshot = converter.convert(test_facts)
    print(f"\nGenerated snapshot has {len(snapshot['agents'])} agents")
    for agent in snapshot["agents"]:
        print(f"  Agent {agent['id']}: pos={agent['position']}, faction={agent['faction']}")

    # Test snapshot to facts
    reverse_converter = SnapshotToFacts(layout)
    recovered_facts = reverse_converter.convert(snapshot)
    print(f"\nRecovered {len(recovered_facts)} facts from snapshot")
    for fact in recovered_facts[:10]:
        print(f"  {fact}")

    # Test annotation-driven path: synthetic snapshot w/ HtnName + Room tags
    # replaces the hardcoded id→name / layout-room lookup.
    anno_snapshot = {
        "rows": 3, "cols": 3,
        "cells": [{"kind": 0, "origin": 0} for _ in range(9)],
        "agents": [{
            "id": 42, "type": 4, "position": {"row": 1, "col": 1},
            "faction": int(Faction.Enemy), "alive": True, "statuses": [],
        }],
        "effects": [], "tick": 0, "horizon": 100,
        "annotations": [
            {"target": "Agent", "agent_id": 42,
             "tag": "HtnName", "params": {"name": "dragon_boss"}},
            {"target": "Cell", "pos": {"row": 1, "col": 1},
             "tag": "Room", "params": {"room": "throne"}},
            {"target": "Agent", "agent_id": 42,
             "tag": "SkillGiver", "params": {"skill": "fire"}},
        ],
    }
    dummy_layout = LevelLayout(rows=3, cols=3, rooms={})
    anno_facts = SnapshotToFacts(dummy_layout).convert(anno_snapshot)
    print("\nAnnotation-driven facts (no hardcoded id/room map):")
    for fact in anno_facts:
        print(f"  {fact}")
    assert "at(dragon_boss, throne)" in anno_facts, "expected annotation-driven at()"
    assert "isEnemy(dragon_boss)" in anno_facts, "expected annotation-driven isEnemy()"
    assert "givesSkill(dragon_boss, fire)" in anno_facts, \
        "expected SkillGiver annotation to emit givesSkill() fact"

    print("\nBridge module loaded successfully!")
