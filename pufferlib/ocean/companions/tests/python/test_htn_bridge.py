"""pytest suite for htn_bridge.py.

Exercises the fact↔snapshot translator the companions game uses to talk to
the HTN planner. Covers the parser, SnapshotToFacts (all core predicates
including the annotation-driven HtnName / Room / SkillGiver path), the
FactsToSnapshot round-trip, and compare_states.

Run from repo root:
    pytest pufferlib/ocean/companions/tests/python/test_htn_bridge.py
"""

import sys
import warnings
from pathlib import Path

import pytest

# htn_bridge.py lives at the companions/ root; add it to sys.path so this
# file can be executed without a package install.
_COMPANIONS_ROOT = Path(__file__).resolve().parents[2]
if str(_COMPANIONS_ROOT) not in sys.path:
    sys.path.insert(0, str(_COMPANIONS_ROOT))

import htn_bridge as bridge  # noqa: E402
from htn_bridge import (  # noqa: E402
    CellKind,
    Faction,
    FactsToSnapshot,
    FSMStateType,
    LevelLayout,
    RoomDef,
    SEMANTIC_TAG_NAMES,
    SemanticTag,
    SnapshotToFacts,
    compare_states,
    format_fact,
    get_puzzle1_layout,
    parse_fact,
    validate_snapshot,
)

_GOLDEN_SNAPSHOT = _COMPANIONS_ROOT / "tests" / "data" / "golden_snapshot_v2.json"


# =============================================================================
# Fact parsing / formatting
# =============================================================================


@pytest.mark.parametrize(
    "fact,expected",
    [
        ("at(player, main)", ("at", ["player", "main"])),
        ("isEnemy(guard1)", ("isEnemy", ["guard1"])),
        ("connected(main, storage)", ("connected", ["main", "storage"])),
        ("hasTag(guard1, burning)", ("hasTag", ["guard1", "burning"])),
        # Trailing period tolerated (Prolog style).
        ("at(player, main).", ("at", ["player", "main"])),
        # Zero-arg predicate.
        ("goalReached", ("goalReached", [])),
        ("goalReached()", ("goalReached", [])),
        # Whitespace tolerated.
        ("at(  player  ,   main )", ("at", ["player", "main"])),
    ],
)
def test_parse_fact_roundtrip(fact, expected):
    assert parse_fact(fact) == expected


@pytest.mark.parametrize(
    "bad",
    [
        "",
        "not a fact at all",
        "at(missing_close",
        "(no_predicate)",
    ],
)
def test_parse_fact_rejects_malformed(bad):
    assert parse_fact(bad) is None


def test_format_fact_with_and_without_args():
    assert format_fact("at", ["player", "main"]) == "at(player, main)"
    assert format_fact("goalReached", []) == "goalReached"


def test_parse_format_roundtrip():
    facts = [
        "at(player, main)",
        "isEnemy(guard1)",
        "hasTag(guard1, burning)",
        "hasAggro(guard1, player)",
        "connected(main, storage)",
        "roomHasHazard(storage, oil)",
    ]
    for f in facts:
        pred, args = parse_fact(f)
        assert format_fact(pred, args) == f


# =============================================================================
# Layout sanity
# =============================================================================


def test_puzzle1_layout_wellformed():
    layout = get_puzzle1_layout()
    assert layout.rows > 0 and layout.cols > 0
    # Every room occupies at least one cell and has no duplicate cells.
    for room_name, room in layout.rooms.items():
        assert room.cells, f"room {room_name} has no cells"
        assert len(room.cells) == len(set(room.cells))
    # Spawn points are inside known rooms.
    for entity, pos in layout.spawn_points.items():
        room = layout.get_room_at(*pos)
        assert room is not None, f"{entity} spawn {pos} lands outside any room"


def test_get_room_center_is_inside_room():
    layout = get_puzzle1_layout()
    for room_name in layout.rooms:
        center = layout.get_room_center(room_name)
        assert center is not None
        assert layout.get_room_at(*center) == room_name


# =============================================================================
# SnapshotToFacts — core predicates
# =============================================================================


def _empty_snapshot(rows, cols):
    return {
        "rows": rows,
        "cols": cols,
        "cells": [{"kind": int(CellKind.Floor), "origin": 0} for _ in range(rows * cols)],
        "agents": [],
        "effects": [],
        "tick": 0,
        "horizon": 100,
        "annotations": [],
    }


def _htn_name_ann(agent_id, name):
    """HtnName annotation dict mapping an agent id to its HTN entity name."""
    return {
        "target": "Agent",
        "agent_id": agent_id,
        "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
        "owner_lens_id": -1,
        "params": {"name": name},
    }


def _legacy_facts_to_snapshot(layout):
    """Construct FactsToSnapshot on the legacy (no base snapshot) path,
    asserting it warns about the hardcoded entity->id mapping."""
    with pytest.warns(UserWarning, match="LEGACY"):
        return FactsToSnapshot(layout)


def _agent(
    *,
    agent_id,
    row,
    col,
    faction=Faction.Companion,
    alive=True,
    statuses=None,
    has_fsm=False,
    fsm_target=-1,
):
    return {
        "id": agent_id,
        "type": 4,
        "position": {"row": row, "col": col},
        "prev_position": {"row": row, "col": col},
        "health": 3,
        "max_health": 3,
        "agent_index": -1,
        "faction": int(faction),
        "direction": 0,
        "color": 0,
        "alive": alive,
        "statuses": statuses or [],
        "has_fsm": has_fsm,
        "fsm": {
            "state_type": int(FSMStateType.Aggro if fsm_target >= 0 else FSMStateType.NoState),
            "target_id": fsm_target,
            "patrol_path": [],
            "patrol_index": 0,
            "patrol_forward": True,
            "detection_range": 3,
            "lose_target_range": 5,
        },
        "cadence": [],
        "tick": 0,
    }


def test_snapshot_to_facts_emits_at_and_isEnemy_from_layout():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    # Player in main, guard1 in storage (an Enemy).
    main_center = layout.get_room_center("main")
    storage_center = layout.get_room_center("storage")
    snap["agents"] = [
        _agent(agent_id=0, row=main_center[0], col=main_center[1]),
        _agent(agent_id=2, row=storage_center[0], col=storage_center[1], faction=Faction.Enemy),
    ]
    snap["annotations"] = [_htn_name_ann(0, "player"), _htn_name_ann(2, "guard1")]
    facts = SnapshotToFacts(layout).convert(snap)
    assert "at(player, main)" in facts
    assert "at(guard1, storage)" in facts
    assert "isEnemy(guard1)" in facts
    assert "isEnemy(player)" not in facts


def test_snapshot_to_facts_emits_connected_and_roomHasHazard():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    facts = SnapshotToFacts(layout).convert(snap)
    assert "connected(main, storage)" in facts
    assert "connected(main, generator)" in facts
    assert "roomHasHazard(storage, oil)" in facts
    assert "roomHasHazard(generator, electricity)" in facts


def test_snapshot_to_facts_emits_hasTag_from_statuses():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    storage_center = layout.get_room_center("storage")
    snap["agents"] = [
        _agent(
            agent_id=2,
            row=storage_center[0],
            col=storage_center[1],
            faction=Faction.Enemy,
            statuses=[{"type": 1, "duration": 3}],  # 1 == burning
        )
    ]
    snap["annotations"] = [_htn_name_ann(2, "guard1")]
    facts = SnapshotToFacts(layout).convert(snap)
    assert "hasTag(guard1, burning)" in facts


def test_snapshot_to_facts_emits_hasAggro_from_fsm():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    main_center = layout.get_room_center("main")
    storage_center = layout.get_room_center("storage")
    snap["agents"] = [
        _agent(agent_id=0, row=main_center[0], col=main_center[1]),
        _agent(
            agent_id=2,
            row=storage_center[0],
            col=storage_center[1],
            faction=Faction.Enemy,
            has_fsm=True,
            fsm_target=0,  # targets player (id=0)
        ),
    ]
    snap["annotations"] = [_htn_name_ann(0, "player"), _htn_name_ann(2, "guard1")]
    facts = SnapshotToFacts(layout).convert(snap)
    assert "hasAggro(guard1, player)" in facts


def test_dead_agents_produce_no_facts():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    main_center = layout.get_room_center("main")
    snap["agents"] = [_agent(agent_id=0, row=main_center[0], col=main_center[1], alive=False)]
    facts = SnapshotToFacts(layout).convert(snap)
    assert not any(f.startswith("at(player") for f in facts)
    assert "isEnemy(player)" not in facts


# =============================================================================
# SnapshotToFacts — annotation-driven path (the whole point of the refactor)
# =============================================================================


def test_annotation_driven_at_and_isEnemy_without_hardcoded_layout():
    # Empty layout — this proves the bridge can read room/entity names straight
    # off the annotation layer instead of the hardcoded puzzle1 map.
    dummy = LevelLayout(rows=3, cols=3, rooms={})
    snap = _empty_snapshot(3, 3)
    snap["agents"] = [_agent(agent_id=42, row=1, col=1, faction=Faction.Enemy)]
    snap["annotations"] = [
        {
            "target": "Agent",
            "agent_id": 42,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
            "owner_lens_id": -1,
            "params": {"name": "dragon_boss"},
        },
        {
            "target": "Cell",
            "pos": {"row": 1, "col": 1},
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.Room],
            "owner_lens_id": -1,
            "params": {"room": "throne"},
        },
    ]
    facts = SnapshotToFacts(dummy).convert(snap)
    assert "at(dragon_boss, throne)" in facts
    assert "isEnemy(dragon_boss)" in facts


def test_skill_giver_annotation_emits_givesSkill():
    dummy = LevelLayout(rows=3, cols=3, rooms={})
    snap = _empty_snapshot(3, 3)
    snap["agents"] = [_agent(agent_id=7, row=1, col=1)]
    snap["annotations"] = [
        {
            "target": "Agent",
            "agent_id": 7,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
            "owner_lens_id": -1,
            "params": {"name": "sparky"},
        },
        {
            "target": "Agent",
            "agent_id": 7,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.SkillGiver],
            "owner_lens_id": -1,
            "params": {"skill": "electric"},
        },
    ]
    facts = SnapshotToFacts(dummy).convert(snap)
    assert "givesSkill(sparky, electric)" in facts


def test_target_mob_annotation_emits_isTargetMob():
    dummy = LevelLayout(rows=3, cols=3, rooms={})
    snap = _empty_snapshot(3, 3)
    snap["agents"] = [_agent(agent_id=9, row=0, col=0, faction=Faction.Enemy)]
    snap["annotations"] = [
        {
            "target": "Agent",
            "agent_id": 9,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
            "owner_lens_id": -1,
            "params": {"name": "boss"},
        },
        {
            "target": "Agent",
            "agent_id": 9,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.TargetMob],
            "owner_lens_id": -1,
            "params": {},
        },
    ]
    facts = SnapshotToFacts(dummy).convert(snap)
    assert "isTargetMob(boss)" in facts


def test_annotation_room_overrides_layout_mapping():
    # If both layout and annotation assign a cell to a room, annotation wins.
    layout = LevelLayout(
        rows=3,
        cols=3,
        rooms={"layout_room": RoomDef(name="layout_room", cells=[(1, 1)])},
    )
    snap = _empty_snapshot(3, 3)
    snap["agents"] = [_agent(agent_id=5, row=1, col=1)]
    snap["annotations"] = [
        {
            "target": "Agent",
            "agent_id": 5,
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.HtnName],
            "owner_lens_id": -1,
            "params": {"name": "npc"},
        },
        {
            "target": "Cell",
            "pos": {"row": 1, "col": 1},
            "tag": SEMANTIC_TAG_NAMES[SemanticTag.Room],
            "owner_lens_id": -1,
            "params": {"room": "annotated_room"},
        },
    ]
    facts = SnapshotToFacts(layout).convert(snap)
    assert "at(npc, annotated_room)" in facts
    assert "at(npc, layout_room)" not in facts


# =============================================================================
# FactsToSnapshot round-trip
# =============================================================================


def test_facts_to_snapshot_populates_agents():
    layout = get_puzzle1_layout()
    facts = [
        "at(player, main)",
        "at(guard1, storage)",
        "isEnemy(guard1)",
        "hasTag(guard1, burning)",
        "hasAggro(guard1, player)",
    ]
    snap = _legacy_facts_to_snapshot(layout).convert(facts)
    by_name = {a["id"]: a for a in snap["agents"]}
    assert 0 in by_name and 2 in by_name
    player = by_name[0]
    guard1 = by_name[2]
    # Positions snap to room centers.
    assert tuple(player["position"].values()) == layout.get_room_center("main")
    assert tuple(guard1["position"].values()) == layout.get_room_center("storage")
    # Faction and status round-trip.
    assert guard1["faction"] == int(Faction.Enemy)
    assert any(s["type"] == 1 for s in guard1["statuses"])  # burning == 1
    # Aggro persisted via FSM.
    assert guard1["has_fsm"]
    assert guard1["fsm"]["target_id"] == 0


def test_facts_to_snapshot_emits_htn_name_annotations():
    layout = get_puzzle1_layout()
    snap = _legacy_facts_to_snapshot(layout).convert(
        ["at(player, main)", "at(guard1, storage)"]
    )
    names = {}
    for ann in snap["annotations"]:
        if ann["tag"] == SEMANTIC_TAG_NAMES[SemanticTag.HtnName]:
            names[ann["agent_id"]] = ann["params"]["name"]
    assert names.get(0) == "player"
    assert names.get(2) == "guard1"


def test_facts_to_snapshot_emits_room_annotations_for_every_room_cell():
    layout = get_puzzle1_layout()
    snap = _legacy_facts_to_snapshot(layout).convert([])
    # Every cell that belongs to a room in the layout should have a Room
    # annotation. This is what lets SnapshotToFacts shed the LevelLayout.
    room_ann_positions = {
        (ann["pos"]["row"], ann["pos"]["col"], ann["params"]["room"])
        for ann in snap["annotations"]
        if ann["tag"] == SEMANTIC_TAG_NAMES[SemanticTag.Room]
    }
    for room_name, room in layout.rooms.items():
        for (r, c) in room.cells:
            assert (r, c, room_name) in room_ann_positions


def test_facts_snapshot_facts_roundtrip():
    layout = get_puzzle1_layout()
    initial = [
        "at(player, main)",
        "at(guard1, storage)",
        "isEnemy(guard1)",
        "hasTag(guard1, burning)",
    ]
    snap = _legacy_facts_to_snapshot(layout).convert(initial)
    recovered = SnapshotToFacts(layout).convert(snap)
    # The initial facts must all reappear; connected/roomHasHazard also emerge
    # from the layout, which is fine.
    for f in initial:
        assert f in recovered, f"lost fact after round-trip: {f}"


# =============================================================================
# compare_states
# =============================================================================


def test_compare_states_matches_when_equal():
    result = compare_states(
        ["at(player, main)", "isEnemy(guard1)"],
        ["at(player, main)", "isEnemy(guard1)", "connected(main, storage)"],
    )
    assert result["match"] is True
    assert result["missing"] == []
    # "Extra" may be non-empty — it's the actual superset minus expected.
    assert "connected(main, storage)" in result["extra"]


def test_compare_states_flags_missing():
    result = compare_states(
        ["at(player, main)", "isEnemy(boss)"],
        ["at(player, main)"],
    )
    assert result["match"] is False
    assert "isEnemy(boss)" in result["missing"]


# =============================================================================
# Top-level helpers
# =============================================================================


def test_high_level_snapshot_to_facts_honours_layout_registry():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    main_center = layout.get_room_center("main")
    snap["agents"] = [_agent(agent_id=0, row=main_center[0], col=main_center[1])]
    snap["annotations"] = [_htn_name_ann(0, "player")]
    facts = bridge.snapshot_to_facts(snap, "puzzle1")
    assert "at(player, main)" in facts


def test_high_level_unknown_level_raises():
    with pytest.raises(ValueError):
        bridge.snapshot_to_facts({}, "not_a_level")


# =============================================================================
# validate_snapshot — schema checks
# =============================================================================


def test_validate_snapshot_accepts_flat_bridge_shape():
    validate_snapshot(_empty_snapshot(3, 3))  # must not raise


def test_validate_snapshot_accepts_real_game_json_shape():
    # The C++ serializer (snapshot_json.cc) nests rows/cols/cells under
    # "grid" — the golden fixture is the ground truth for that shape.
    import json

    with open(_GOLDEN_SNAPSHOT) as f:
        snapshot = json.load(f)
    validate_snapshot(snapshot)  # must not raise


@pytest.mark.parametrize("missing", ["rows", "cols", "cells", "agents"])
def test_validate_snapshot_names_missing_flat_key(missing):
    snap = _empty_snapshot(3, 3)
    del snap[missing]
    with pytest.raises(ValueError, match=missing):
        validate_snapshot(snap)


@pytest.mark.parametrize("missing", ["rows", "cols", "cells"])
def test_validate_snapshot_names_missing_nested_grid_key(missing):
    snap = {
        "grid": {"rows": 3, "cols": 3, "cells": []},
        "agents": [],
    }
    del snap["grid"][missing]
    with pytest.raises(ValueError, match=rf"grid\.{missing}"):
        validate_snapshot(snap)


def test_validate_snapshot_rejects_non_dict():
    with pytest.raises(ValueError, match="dict"):
        validate_snapshot(["not", "a", "snapshot"])


def test_snapshot_to_facts_rejects_invalid_snapshot():
    layout = get_puzzle1_layout()
    with pytest.raises(ValueError, match="rows"):
        SnapshotToFacts(layout).convert({})


# =============================================================================
# HtnName fallback — generic names + warning instead of hardcoded ids
# =============================================================================


def test_agent_without_htn_name_falls_back_to_generic_name_and_warns():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    main_center = layout.get_room_center("main")
    # Agent id 0 used to silently become "player" via a hardcoded mapping.
    snap["agents"] = [_agent(agent_id=0, row=main_center[0], col=main_center[1])]
    with pytest.warns(UserWarning, match="HtnName"):
        facts = SnapshotToFacts(layout).convert(snap)
    assert "at(agent0, main)" in facts
    assert not any("player" in f for f in facts)


def test_annotated_agents_produce_no_fallback_warning():
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(layout.rows, layout.cols)
    main_center = layout.get_room_center("main")
    snap["agents"] = [_agent(agent_id=0, row=main_center[0], col=main_center[1])]
    snap["annotations"] = [_htn_name_ann(0, "player")]
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        facts = SnapshotToFacts(layout).convert(snap)
    assert "at(player, main)" in facts


# =============================================================================
# FactsToSnapshot — entity ids from base snapshot annotations
# =============================================================================


def test_facts_to_snapshot_derives_ids_from_base_snapshot():
    layout = get_puzzle1_layout()
    base = _empty_snapshot(layout.rows, layout.cols)
    base["annotations"] = [
        _htn_name_ann(11, "hero"),
        _htn_name_ann(37, "ogre"),
    ]
    with warnings.catch_warnings():
        warnings.simplefilter("error")  # data-driven path must not warn
        converter = FactsToSnapshot(layout, base_snapshot=base)
    assert converter.entity_to_id == {"hero": 11, "ogre": 37}

    snap = converter.convert(
        ["at(hero, main)", "at(ogre, storage)", "isEnemy(ogre)", "hasAggro(ogre, hero)"]
    )
    by_id = {a["id"]: a for a in snap["agents"]}
    assert 11 in by_id and 37 in by_id
    assert by_id[37]["fsm"]["target_id"] == 11  # aggro target resolved via annotations


def test_facts_to_snapshot_base_snapshot_is_validated():
    layout = get_puzzle1_layout()
    with pytest.raises(ValueError, match="agents"):
        FactsToSnapshot(layout, base_snapshot={"rows": 3, "cols": 3, "cells": []})


def test_facts_to_snapshot_without_base_snapshot_warns_legacy():
    layout = get_puzzle1_layout()
    with pytest.warns(UserWarning, match="LEGACY"):
        converter = FactsToSnapshot(layout)
    # The legacy mapping is preserved for fact-only flows.
    assert converter.entity_to_id["player"] == 0
    assert converter.entity_to_id["guard1"] == 2


def test_high_level_facts_to_snapshot_accepts_base_snapshot():
    layout = get_puzzle1_layout()
    base = _empty_snapshot(layout.rows, layout.cols)
    base["annotations"] = [_htn_name_ann(5, "scout")]
    snap = bridge.facts_to_snapshot(["at(scout, main)"], "puzzle1", base_snapshot=base)
    assert any(a["id"] == 5 for a in snap["agents"])


# =============================================================================
# Game-emitted JSON enums — faction/status arrive as strings, not ints
# =============================================================================


def _golden_layout():
    """One-room layout covering the golden fixture's 4x5 grid."""
    return LevelLayout(
        rows=4,
        cols=5,
        rooms={
            "arena": RoomDef(
                name="arena",
                cells=[(r, c) for r in range(4) for c in range(5)],
            )
        },
    )


def test_snapshot_to_facts_reads_game_json_enums():
    # Regression for the string-vs-int enum gap: snapshot_json.cc emits
    # faction as "ENEMY" and statuses as {"status_type": "slowed"}; the
    # bridge must fire isEnemy/hasTag facts on that wire form, not just on
    # its own int-coded partial snapshots. The golden fixture is the ground
    # truth for the game shape. (Agent 1 has no HtnName annotation, so the
    # generic-name fallback warning is expected.)
    import json

    with open(_GOLDEN_SNAPSHOT) as f:
        snapshot = json.load(f)
    with pytest.warns(UserWarning):
        facts = SnapshotToFacts(_golden_layout()).convert(snapshot)
    assert "isEnemy(warden)" in facts
    assert "hasTag(warden, slowed)" in facts
    # Agent 1 (alive, stunned+marked) emits tags under its fallback name.
    assert any("stunned" in f for f in facts if f.startswith("hasTag("))
    assert any("marked" in f for f in facts if f.startswith("hasTag("))
    # Agent 3 is dead and must contribute no facts.
    assert not any("agent3" in f for f in facts)


@pytest.mark.parametrize(
    "value,expected",
    [
        ("ENEMY", Faction.Enemy),
        ("COMPANION", Faction.Companion),
        ("NEUTRAL", Faction.Neutral),
        (int(Faction.Enemy), Faction.Enemy),
        ("DRAGONKIN", None),
        (99, None),
    ],
)
def test_parse_faction_accepts_both_wire_forms(value, expected):
    assert bridge.parse_faction(value) == expected


@pytest.mark.parametrize(
    "status,expected",
    [
        ({"status_type": "stunned", "duration": 3}, "stunned"),
        ({"status_type": "none", "duration": 0}, None),
        ({"type": 1, "duration": -1}, "burning"),  # bridge-internal int vocab
        ({"type": 99, "duration": -1}, None),
        ({}, None),
    ],
)
def test_status_tag_accepts_both_wire_forms(status, expected):
    assert bridge.status_tag(status) == expected


# =============================================================================
# validate_snapshot — geometry value checks (not just key presence)
# =============================================================================


@pytest.mark.parametrize("bad", [0, -1, "3", 2.5, True])
def test_validate_snapshot_rejects_non_positive_cols(bad):
    snap = _empty_snapshot(3, 3)
    snap["cols"] = bad
    with pytest.raises(ValueError, match="cols"):
        validate_snapshot(snap)


def test_validate_snapshot_rejects_cells_length_mismatch():
    snap = _empty_snapshot(3, 3)
    snap["cells"] = snap["cells"][:-1]
    with pytest.raises(ValueError, match="cells"):
        validate_snapshot(snap)


def test_convert_raises_clean_error_not_zerodivision_on_cols_zero():
    # Before the geometry checks, cols=0 with non-empty cells reached
    # `i // cols` in _cell_facts and crashed with ZeroDivisionError.
    layout = get_puzzle1_layout()
    snap = _empty_snapshot(3, 3)
    snap["cols"] = 0
    with pytest.raises(ValueError, match="cols"):
        SnapshotToFacts(layout).convert(snap)
