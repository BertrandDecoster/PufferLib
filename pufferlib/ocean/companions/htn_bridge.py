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
from typing import Any, Dict, List, Optional, Set, Tuple
from dataclasses import dataclass, field
from enum import IntEnum


# =============================================================================
# Enums matching C++ types
# =============================================================================

class CellKind(IntEnum):
    """Maps to companions::CellKind"""
    Floor = 0
    Wall = 1
    Hazard = 2
    Synchro = 3
    HealArea = 4
    Target = 5


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
        """
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

            # Determine entity name (hardcoded mapping for now)
            entity_name = self._agent_id_to_name(agent_id, agent_type)

            # at(?entity, ?room) - requires room lookup
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
                    target_name = self._agent_id_to_name(target_id, 0)  # Type unknown
                    facts.append(format_fact("hasAggro", [entity_name, target_name]))

        return facts

    def _cell_facts(self, snapshot: Dict[str, Any]) -> List[str]:
        """Generate facts from grid cells."""
        facts = []
        cells = snapshot.get("cells", [])
        rows = snapshot.get("rows", 0)
        cols = snapshot.get("cols", 0)

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

    def _agent_id_to_name(self, agent_id: int, agent_type: int) -> str:
        """
        Map agent ID to HTN entity name.

        This is hardcoded for now. In production, this mapping would come
        from level metadata or agent config.
        """
        # Default mapping based on ID
        # ID 0 is typically player, higher IDs are enemies
        if agent_id == 0:
            return "player"
        elif agent_id == 1:
            return "warden"
        elif agent_id == 2:
            return "guard1"
        elif agent_id == 3:
            return "guard2"
        else:
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
    """

    def __init__(self, layout: LevelLayout):
        self.layout = layout
        self.entity_to_id: Dict[str, int] = {
            "player": 0,
            "warden": 1,
            "guard1": 2,
            "guard2": 3,
        }

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


def facts_to_snapshot(facts: List[str], level_name: str) -> Dict[str, Any]:
    """
    Convert HTN facts to a Companions snapshot.

    Args:
        facts: List of HTN fact strings
        level_name: Name of the level (for layout lookup)

    Returns:
        Partial snapshot dictionary
    """
    layout = get_layout(level_name)
    if not layout:
        raise ValueError(f"Unknown level: {level_name}")

    converter = FactsToSnapshot(layout)
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

    print("\nBridge module loaded successfully!")
