# Build and Test Guide

## Prerequisites

- CMake 3.12+
- C++17 compatible compiler (clang++ or g++)
- Python 3.10+ with venv (for RL training integration)

## C++ Build

### Quick Start

```bash
cd pufferlib/ocean/companions
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
```

### Build Options

| Option | Description |
|--------|-------------|
| `-DCMAKE_BUILD_TYPE=Release` | Optimized build (default) |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug symbols enabled |


### Distribution Directory (dist/)

After building, libraries are automatically synced to `dist/` for downstream consumers (e.g., companions-loop):

```
dist/
  companions_version.hpp      # Version + build timestamp
  data/                       # Runtime assets (agents.csv, effects.csv)
  Debug/
    companions_core.lib, companions_env.lib, companions_viz.lib
  Release/
    companions_core.lib, companions_env.lib, companions_viz.lib
```

This happens via POST_BUILD commands - no manual step needed. The `companions_version.hpp` includes:
- `COMPANIONS_LIB_VERSION` - from CMake project version (e.g., "0.2.0")
- `COMPANIONS_LIB_BUILD_TIME` - timestamp of last build

Downstream projects can use version info to detect stale libraries.
## Running Tests

```bash
cd pufferlib/ocean/companions/build
ctest --output-on-failure
```

### Test Summary

| Test | Description |
|------|-------------|
| `companions_test` | Core functionality (grid, objects, pathfinding) |
| `companions_fsm_test` | FSM state machine for enemy AI |
| `companions_effects_test` | Effect system (damage, healing, push) |
| `companions_aggro_test` | Aggro environment mechanics |
| `companions_dodge_test` | Dodge environment mechanics |
| `companions_map_generator_test` | Procedural map generation |
| `companions_d4_transform_test` | D4 symmetry transforms |

### Running Individual Tests

```bash
./companions_test
./companions_fsm_test
# etc.
```

## Interactive Demo

Play the game manually with keyboard controls:

```bash
cd pufferlib/ocean/companions
./build/companions_demo --env synchro
./build/companions_demo --env aggro
./build/companions_demo --env dodge
```

### Demo Controls

| Key | Action |
|-----|--------|
| Arrow keys / ZQSD | Move |
| Space | Stay |
| R | Reset (random seed) |
| 0-9 + Enter | Reset with specific seed |
| P | Quit |

### Demo Options

```bash
./build/companions_demo --help
```

| Option | Description | Default |
|--------|-------------|---------|
| `--env NAME` | Environment: synchro, aggro, dodge | synchro |
| `--size N` | Grid size NxN | 12 (synchro), 10 (aggro), 7 (dodge) |
| `--companions N` | Number of companions (1-3) | 3 (synchro), 1 (others) |
| `--synchro N` | Number of synchro cells | Same as companions |
| `--complexity N` | Map complexity 0-5 (synchro only) | 0 |
| `--seed N` | Random seed | 42 |
| `--transform N` | D4 symmetry transform 0-7 | 0 |
| `--log CATS` | Enable logging (fsm,effects,game) | None |

### Example Commands

```bash
# Synchro with 2 companions on 8x8 grid
./build/companions_demo --env synchro --size 8 --companions 2

# Aggro with goblin enemy
./build/companions_demo --env aggro --enemy goblin

# Dodge with faster hazards
./build/companions_demo --env dodge --interval 2 --survival 30
```

## Python Extension (PufferLib Integration)

Build the Python extension for RL training:

```bash
# From PufferLib root directory
source .venv/bin/activate
python setup.py build_companions --inplace --force
```

### Test Python Integration

```bash
python -m pufferlib.ocean.companions.synchro
```

Expected output:
```
Testing Synchro environment...
  num_agents: 3
  observation_space: Box(0.0, 1.0, (5, 12, 12), float32)
  action_space: MultiDiscrete([5 2])
  obs shape: (3, 5, 12, 12)
  Completed 1000 steps
  Steps per second: ~125000+
Success!
```

### Parity Test

Validates Python wrapper produces identical results to C++ implementation:

```bash
# Generate reference data (if needed)
./build/parity_generator --env-seed 42 --action-seed 123 --steps 1000 \
  --output tests/python/reference_data/synchro_parity_42.bin

# Run parity test
python tests/python/parity_test.py tests/python/reference_data/synchro_parity_42.bin -v
```

### Python Test Files

| File | Description |
|------|-------------|
| `parity_test.py` | Validates Python/C++ parity |
| `test_d4_networks.py` | D4-equivariant neural network tests |
| `test_d4_symmetry.py` | D4 symmetry invariance evaluation |
| `test_escnn_mps.py` | ESCNN layer MPS compatibility tests |

## Troubleshooting

### CMake not finding compiler

```bash
export CXX=clang++  # or g++
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### Python extension build fails

Ensure the C++ libraries are built first:
```bash
cd pufferlib/ocean/companions/build
cmake --build . -j4
cd ../../../..
python setup.py build_companions --inplace --force
```

### Tests fail to find data files

Run tests from the companions directory:
```bash
cd pufferlib/ocean/companions
./build/companions_test
```

## Development Workflow

1. Make C++ changes
2. Rebuild: `cmake --build build -j4`
3. Run C++ tests: `cd build && ctest --output-on-failure`
4. Test demo: `./build/companions_demo`
5. If wrapper changed:
   - Rebuild Python extension: `python setup.py build_companions --inplace --force`
   - Regenerate parity data: `./build/parity_generator --env-seed 42 --action-seed 123 --steps 1000 --output tests/python/reference_data/synchro_parity_42.bin`
   - Run parity test: `python tests/python/parity_test.py tests/python/reference_data/synchro_parity_42.bin -v`
   - Quick integration test: `python -m pufferlib.ocean.companions.synchro`

## Full Build and Test

Run the complete pipeline with clear error reporting:

```bash
./pufferlib/ocean/companions/scripts/build_and_test.sh
```

This builds C++, runs C++ tests, builds Python bindings, and runs Python integration tests. On failure, it reports the first error. On success, prints "Compilation and Tests all succeeded!"

## VSCode

### Launch Configurations

| Config | Description |
|--------|-------------|
| `Companions: Build and Test` | Full build + test pipeline |
| `Companions: Debug Test` | Debug C++ tests with LLDB |
| `Companions: Play (Synchro/Aggro/Dodge)` | Interactive demos |

### Tasks

| Task | Description |
|------|-------------|
| `companions-build` | Build C++ (cmake --build) |
| `companions-configure` | Configure CMake (Release) |
| `companions-test` | Run all C++ tests (ctest) |
| `companions-parity-test` | Full parity test pipeline |
| `companions-clean` | Remove build directory |
