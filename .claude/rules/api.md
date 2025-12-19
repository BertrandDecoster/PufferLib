---
paths: pufferlib/ocean/companions/src/api/**
---

# C API Integration

The companions game is built as a Windows DLL with a C API for external integration (game engines, FFI bindings).

## Architecture

```
Consumer (UE5, ASCII, etc) → companions_api.dll (C API) → SynchroEnv (C++ game logic)
```

The C API wraps the pure C++17 game logic with POD-only types for safe DLL boundary crossing.

## Key Files

| File | Purpose |
|------|---------|
| `src/api/companions_api.h` | C API header with POD structs |
| `src/api/companions_api.cc` | Implementation wrapping SynchroEnv |
| `tests/test_api.cc` | Unit tests for C API (14 tests) |
| `tests/test_api_parity.cc` | Parity test vs direct C++ (7 tests) |

## Core API

### Lifecycle

```c
Companions_Env* companions_create(const Companions_EnvConfig* config);
void companions_destroy(Companions_Env* env);
void companions_reset(Companions_Env* env, uint32_t seed);
```

### Game Loop

```c
void companions_step(
    Companions_Env* env,
    const Companions_Action* actions,  // [num_agents] movement + interact
    int32_t action_count,
    Companions_StepResult* out_result  // State + transition events
);
```

### State Queries

```c
void companions_get_state(const Companions_Env* env, Companions_GameState* out);
Companions_CellKind companions_get_cell(const Companions_Env* env, int row, int col);
void companions_get_grid(const Companions_Env* env, Companions_CellKind* out_grid);
bool companions_get_agent_by_index(const Companions_Env* env, int index, Companions_AgentState* out);
```

## Key Structs

| Struct | Contents |
|--------|----------|
| `Companions_EnvConfig` | rows, cols, num_companions, num_synchro, map_complexity, horizon, seed |
| `Companions_Action` | movement (0-4), interact (0-1) |
| `Companions_AgentState` | id, position, prev_position, health, facing, fsm_state, statuses |
| `Companions_GameState` | agents[], special_cells[], effects[], done, success, rewards[] |
| `Companions_StepResult` | state + events[] for animation |
| `Companions_Event` | type, tick, subject_id, position data, event-specific fields |

## Event System

Events returned by `companions_step()` for animation:

| Event | Description |
|-------|-------------|
| `Companions_Event_AgentMoved` | Agent successfully moved to new position |
| `Companions_Event_AgentBlocked` | Agent tried to move but was blocked |
| `Companions_Event_EpisodeEnd` | Episode completed (success or timeout) |

## Thread Safety

- Each `Companions_Env` instance is NOT thread-safe
- Different instances can be used from different threads
- `companions_get_error()` uses thread-local storage

## Build

```bash
# Configure with DLL build enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_DLL=ON

# Build the DLL
cmake --build build --target companions_api --config Release

# Output files
# - build/bin/Release/companions_api.dll
# - build/lib/Release/companions_api.lib (import library)
```

## Test

```bash
# Run C API tests
ctest -C Release -R api

# Individual tests
./build/Release/companions_api_test.exe        # Unit tests
./build/Release/companions_api_parity_test.exe # Parity vs direct C++
```

## Parity Test

The parity test (`test_api_parity.cc`) verifies that the C API produces **identical results** to directly calling `SynchroEnv` C++ methods:

- Same agent positions after reset with same seed
- Same rewards after each step with same actions
- Same done/success flags at episode boundaries
- Same grid state (cell kinds)
- Determinism across multiple runs
