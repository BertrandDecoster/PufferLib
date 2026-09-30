---
paths: tests/**, pufferlib/ocean/companions/tests/**
---

# Testing

```bash
# PufferLib tests
pytest tests/

# Companions Python tests (D4 networks, symmetry, parity)
pytest pufferlib/ocean/companions/tests/python/

# Companions C++ tests (after cmake build)
cd pufferlib/ocean/companions/build
ctest

# Or run individual C++ tests
./companions_test                  # core, the vector observation
./companions_fsm_test
./companions_fsm_timing_test
./companions_aggro_test
./companions_dodge_test
./companions_effects_test
./companions_skills_test           # skills, tags, Rooted, friendly fire, previews
./companions_downs_test            # downs, the team counter, TeamDown
./companions_revive_test           # revive, context skills
./companions_zones_test            # zones: lifetime, successor, damage, zone table
./companions_reactions_test        # reactions, weaknesses, immunities, tag statuses
./companions_turn_test             # the phased turn: ledger, motion layers, tag phase, effects, previews
./companions_interrupts_test       # Interrupted, the down cost, the pause, the uniform done
./companions_revive_lens_test      # ReviveLens, start_downed (C++ and C API)
./companions_synchro_wrapper_test  # the RL C wrapper: truncations, the revive task, down cost
./companions_map_generator_test
./companions_d4_transform_test
./companions_snapshot_test
./companions_snapshot_json_test
./companions_task_lens_test
./companions_annotations_test
./companions_viz_test
./companions_npc_panel_test
./companions_api_test
./companions_api_parity_test
./companions_api_reactions_test    # C API 1.5 / 1.6: reports (turn health, odd motions), level data, outcome previews
# (26 suites; by area: the companions CLAUDE.md, "Tests"; on Windows
# they land in build/bin/Release/ and ctest needs -C Release)

# Parity test (generates reference data and validates Python wrapper)
./pufferlib/ocean/companions/tests/python/run_parity_test.sh
```
