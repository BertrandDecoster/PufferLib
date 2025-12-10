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

# Parity test (generates reference data and validates Python wrapper)
./pufferlib/ocean/companions/tests/python/run_parity_test.sh
```
