---
paths: pufferlib/ocean/companions/src/ue_api/**
---

# UE5 API Integration

The companions game is built as a Windows DLL with a C API for Unreal Engine 5 integration.

## Architecture

```
UE5 Plugin → companions_ue.dll (C API) → SynchroEnv (C++ game logic)
```

The C API wraps the pure C++17 game logic with POD-only types for safe DLL boundary crossing.

## Key Files

| File | Purpose |
|------|---------|
| `src/ue_api/companions_ue.h` | C API header with POD structs |
| `src/ue_api/companions_ue.cc` | Implementation wrapping SynchroEnv |
| `tests/test_ue_api.cc` | Unit tests for C API (14 tests) |
| `tests/test_ue_api_parity.cc` | Parity test vs direct C++ (7 tests) |

## Core API

### Lifecycle

```c
UE_CompanionsEnv* ue_companions_create(const UE_EnvConfig* config);
void ue_companions_destroy(UE_CompanionsEnv* env);
void ue_companions_reset(UE_CompanionsEnv* env, uint32_t seed);
```

### Game Loop

```c
void ue_companions_step(
    UE_CompanionsEnv* env,
    const UE_Action* actions,      // [num_agents] movement + interact
    int32_t action_count,
    UE_StepResult* out_result      // State + transition events
);
```

### State Queries

```c
void ue_companions_get_state(const UE_CompanionsEnv* env, UE_GameState* out);
UE_CellKind ue_companions_get_cell(const UE_CompanionsEnv* env, int row, int col);
void ue_companions_get_grid(const UE_CompanionsEnv* env, UE_CellKind* out_grid);
bool ue_companions_get_agent_by_index(const UE_CompanionsEnv* env, int index, UE_AgentState* out);
```

## Key Structs

| Struct | Contents |
|--------|----------|
| `UE_EnvConfig` | rows, cols, num_companions, num_synchro, map_complexity, horizon, seed |
| `UE_Action` | movement (0-4), interact (0-1) |
| `UE_AgentState` | id, position, prev_position, health, facing, fsm_state, statuses |
| `UE_GameState` | agents[], special_cells[], effects[], done, success, rewards[] |
| `UE_StepResult` | state + events[] for animation |
| `UE_Event` | type, tick, subject_id, position data, event-specific fields |

## Event System

Events returned by `ue_companions_step()` for animation:

| Event | Description |
|-------|-------------|
| `UE_Event_AgentMoved` | Agent successfully moved to new position |
| `UE_Event_AgentBlocked` | Agent tried to move but was blocked |
| `UE_Event_EpisodeEnd` | Episode completed (success or timeout) |

## Thread Safety

- Each `UE_CompanionsEnv` instance is NOT thread-safe
- Different instances can be used from different threads
- `ue_companions_get_error()` uses thread-local storage

## Build

```bash
# Configure with DLL build enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_UE5_DLL=ON

# Build the DLL
cmake --build build --target companions_ue --config Release

# Output files
# - build/bin/Release/companions_ue.dll
# - build/lib/Release/companions_ue.lib (import library)
```

## Test

```bash
# Run UE API tests
ctest -C Release -R ue

# Individual tests
./build/Release/companions_ue_test.exe        # Unit tests
./build/Release/companions_ue_parity_test.exe # Parity vs direct C++
```

## Parity Test

The parity test (`test_ue_api_parity.cc`) verifies that the UE C API produces **identical results** to directly calling `SynchroEnv` C++ methods:

- Same agent positions after reset with same seed
- Same rewards after each step with same actions
- Same done/success flags at episode boundaries
- Same grid state (cell kinds)
- Determinism across multiple runs

## UE5 Plugin Integration

Copy to your UE5 project:

```
Plugins/CompanionsPlugin/ThirdParty/Companions/
├── companions_ue.dll
├── companions_ue.lib
└── companions_ue.h
```

In your `.Build.cs`:

```csharp
PublicAdditionalLibraries.Add(Path.Combine(LibPath, "companions_ue.lib"));
PublicDelayLoadDLLs.Add("companions_ue.dll");
RuntimeDependencies.Add("$(BinaryOutputDir)/companions_ue.dll", DLLPath);
```
