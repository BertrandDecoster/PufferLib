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
./companions_test
./companions_fsm_test
./companions_aggro_test
./companions_effects_test
./companions_dodge_test
./companions_map_generator_test
./companions_d4_transform_test
./companions_zones_test            # zones: lifetime, successor, damage, zone table
./companions_reactions_test        # reactions, weaknesses, immunities, tag statuses
./companions_api_reactions_test    # C API 1.5: reports, level data, outcome previews
# (the full list, by area: the companions CLAUDE.md, "Tests"; on Windows
# they land in build/bin/Release/ and ctest needs -C Release)

# Parity test (generates reference data and validates Python wrapper)
./pufferlib/ocean/companions/tests/python/run_parity_test.sh
```
