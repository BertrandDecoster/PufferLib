// Copyright 2024
// Parity test: Verify C API produces identical results to direct C++ API
//
// This test ensures the API wrapper (companions_api.h) doesn't introduce
// any behavioral differences compared to directly calling GameEnv methods.
// companions_create returns a GameEnv (game shell: never done), so the
// direct-C++ reference here is a GameEnv constructed with the same
// config/seed — same generation path, identical level.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "companions_api.h"
#include "../src/core/annotations.h"
#include "../src/core/pcg32.h"
#include "../src/core/snapshot.h"
#include "../src/core/types.h"
#include "../src/env/dodge_lens.h"
#include "../src/env/game_env.h"
#include "../src/env/synchro_env.h"
#include "../src/env/synchro_lens.h"

using namespace companions;

// =============================================================================
// Test macros (reused from test_api.cc)
// =============================================================================
#define TEST(name) \
  void name(); \
  struct name##_registrar { \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance; \
  void name()

#define ASSERT_TRUE(cond) \
  if (!(cond)) { \
    std::ostringstream oss; \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_FALSE(cond) \
  if (cond) { \
    std::ostringstream oss; \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_EQ(a, b) \
  if ((a) != (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_EQ failed: " << (a) << " != " << (b) << " (" << #a << " != " << #b << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_FLOAT_EQ(a, b) \
  if (std::fabs((a) - (b)) > 1e-6) { \
    std::ostringstream oss; \
    oss << "ASSERT_FLOAT_EQ failed: " << (a) << " != " << (b) << " (" << #a << " != " << #b << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_NOT_NULL(ptr) \
  if ((ptr) == nullptr) { \
    std::ostringstream oss; \
    oss << "ASSERT_NOT_NULL failed: " << #ptr << " is null at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Helper: Create matching configs for both APIs
// =============================================================================
static Companions_EnvConfig MakeConfig(int rows = 12, int cols = 12, int companions = 3,
                                  int synchro = 3, int complexity = 0,
                                  uint32_t seed = 42, int horizon = 100) {
  Companions_EnvConfig config = {};
  config.rows = rows;
  config.cols = cols;
  config.num_companions = companions;
  config.num_synchro = synchro;
  config.map_complexity = complexity;
  config.horizon = horizon;
  config.d4_transform = 0;
  config.seed = seed;
  return config;
}

// =============================================================================
// Parity Tests
// =============================================================================

// Test 1: Initial state after reset matches
TEST(ParityTest_InitialState) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  // Create C API environment
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);

  // Create direct C++ environment
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both with same seed
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Get states
  Companions_GameState api_state = {};
  companions_get_state(api_env, &api_state);
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

  // Verify agent count matches
  ASSERT_EQ(api_state.agent_count, static_cast<int>(cpp_agents.size()));
  ASSERT_EQ(api_state.agent_count, agents);

  // Verify each agent position matches
  for (int i = 0; i < agents; i++) {
    ASSERT_EQ(api_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
    ASSERT_EQ(api_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    ASSERT_EQ(api_state.agents[i].health, cpp_agents[i]->GetHealth());
    ASSERT_EQ(api_state.agents[i].alive, cpp_agents[i]->IsAlive());
  }

  // Verify grid dimensions
  ASSERT_EQ(api_state.rows, cpp_env.GetRows());
  ASSERT_EQ(api_state.cols, cpp_env.GetCols());

  // Verify tick
  ASSERT_EQ(api_state.tick, cpp_env.GetTick());

  companions_destroy(api_env);
}

// Test 2: Single step produces same results
TEST(ParityTest_SingleStep) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  // Create both environments
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // All agents stay (simplest action)
  std::vector<Companions_Action> api_actions(agents);
  std::vector<Action> cpp_actions(agents);
  for (int i = 0; i < agents; i++) {
    api_actions[i] = {Companions_Movement_Stay, Companions_Interact_None};
    cpp_actions[i] = EncodeAction(MovementAction::Stay, InteractAction::None);
  }

  // Step both
  Companions_StepResult api_result = {};
  companions_step(api_env, api_actions.data(), agents, &api_result);
  StepResult cpp_result = cpp_env.Step(cpp_actions);

  // Verify done flag matches
  ASSERT_EQ(api_result.state.done, cpp_result.done);

  // Verify rewards match
  for (int i = 0; i < agents; i++) {
    ASSERT_FLOAT_EQ(api_result.state.rewards[i], static_cast<float>(cpp_result.rewards[i]));
  }

  // Verify tick advanced
  ASSERT_EQ(api_result.state.tick, 1);
  ASSERT_EQ(cpp_env.GetTick(), 1);

  // Verify agent positions still match
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
  for (int i = 0; i < agents; i++) {
    ASSERT_EQ(api_result.state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
    ASSERT_EQ(api_result.state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
  }

  companions_destroy(api_env);
}

// Test 3: Multi-step with random actions maintains parity
TEST(ParityTest_MultiStep) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t env_seed = 42;
  const uint32_t action_seed = 123;
  const int num_steps = 200;

  // Create both environments
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, env_seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, env_seed, 0, 100);

  // Reset both
  companions_reset(api_env, env_seed);
  cpp_env.Reset(env_seed);

  // Action RNG
  pcg32 action_rng(action_seed);

  for (int step = 0; step < num_steps; step++) {
    // Generate identical actions for both APIs
    std::vector<Companions_Action> api_actions(agents);
    std::vector<Action> cpp_actions(agents);
    for (int a = 0; a < agents; a++) {
      int mov = action_rng() % 5;
      int interact = action_rng() % 2;
      api_actions[a] = {static_cast<Companions_MovementAction>(mov),
                       static_cast<Companions_InteractAction>(interact)};
      cpp_actions[a] = EncodeAction(static_cast<MovementAction>(mov),
                                    static_cast<InteractAction>(interact));
    }

    // Step both environments
    Companions_StepResult api_result = {};
    companions_step(api_env, api_actions.data(), agents, &api_result);
    StepResult cpp_result = cpp_env.Step(cpp_actions);

    // Verify done flags match. GameEnv has no episode semantics, so done
    // must stay false on both sides for the entire run (even past horizon).
    ASSERT_EQ(api_result.state.done, cpp_result.done);
    ASSERT_FALSE(cpp_result.done);

    // Verify rewards match
    for (int a = 0; a < agents; a++) {
      ASSERT_FLOAT_EQ(api_result.state.rewards[a], static_cast<float>(cpp_result.rewards[a]));
    }

    // Verify agent positions match
    auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
    for (int i = 0; i < agents; i++) {
      ASSERT_EQ(api_result.state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
      ASSERT_EQ(api_result.state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    }
  }

  companions_destroy(api_env);
  std::cout << "  Completed " << num_steps << " steps with full parity" << std::endl;
}

// Test 4: GameEnv never terminates (no horizon done) + explicit reset parity.
// This test previously ran until the horizon fired `done` and reset on it;
// companions_create now returns a GameEnv with no episode semantics, so the
// (stronger) property to verify is: done NEVER fires, even well past the
// horizon, and an explicit reset still produces matching state on both sides.
TEST(ParityTest_NeverDoneAndExplicitReset) {
  const int rows = 6, cols = 6, agents = 1, synchro = 1;
  const uint32_t seed = 42;
  const int horizon = 50;

  // Create both environments with short horizon
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed, horizon);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Step well past the horizon: done must never fire on either side.
  std::vector<Companions_Action> api_actions(agents, {Companions_Movement_Stay, Companions_Interact_None});
  std::vector<Action> cpp_actions(agents, EncodeAction(MovementAction::Stay, InteractAction::None));

  for (int step = 0; step < horizon + 10; step++) {
    Companions_StepResult api_result = {};
    companions_step(api_env, api_actions.data(), agents, &api_result);
    StepResult cpp_result = cpp_env.Step(cpp_actions);

    ASSERT_EQ(api_result.state.done, cpp_result.done);
    ASSERT_FALSE(api_result.state.done);
    ASSERT_FALSE(companions_is_done(api_env));
  }

  // Tick advanced past horizon — the world just keeps going.
  ASSERT_EQ(companions_get_tick(api_env), horizon + 10);
  ASSERT_EQ(cpp_env.GetTick(), horizon + 10);

  // Explicit reset with a new seed still produces matching state.
  uint32_t new_seed = seed + 100;
  companions_reset(api_env, new_seed);
  cpp_env.Reset(new_seed);

  Companions_GameState api_state = {};
  companions_get_state(api_env, &api_state);
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

  ASSERT_EQ(api_state.tick, 0);
  ASSERT_EQ(cpp_env.GetTick(), 0);
  ASSERT_FALSE(api_state.done);

  for (int i = 0; i < agents; i++) {
    ASSERT_EQ(api_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
    ASSERT_EQ(api_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
  }

  companions_destroy(api_env);
}

// Test 5: Grid state matches between APIs
TEST(ParityTest_GridState) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  // Create both environments
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Get full grid from C API
  std::vector<Companions_CellKind> api_grid(rows * cols);
  companions_get_grid(api_env, api_grid.data());

  // Compare with C++ grid
  const auto& cpp_grid = cpp_env.GetGrid();
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      Companions_CellKind api_kind = api_grid[r * cols + c];
      CellKind cpp_kind = cpp_grid.GetCell(r, c).GetKind();

      // Map C++ enum to expected C API enum value
      Companions_CellKind expected;
      switch (cpp_kind) {
        case CellKind::Floor: expected = Companions_CellKind_Floor; break;
        case CellKind::Wall: expected = Companions_CellKind_Wall; break;
        case CellKind::Hazard: expected = Companions_CellKind_Hazard; break;
        case CellKind::HealArea: expected = Companions_CellKind_HealArea; break;
        default: expected = Companions_CellKind_Floor;
      }

      ASSERT_EQ(api_kind, expected);
    }
  }
  (void)synchro;  // synchro goal count is an annotation concern; not
                  // reflected in the physical grid anymore.

  companions_destroy(api_env);
}

// Test 6: Verify movement produces same position changes
TEST(ParityTest_Movement) {
  const int rows = 10, cols = 10, agents = 1, synchro = 1;
  const uint32_t seed = 42;

  // Create both environments
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Try each movement direction
  Companions_MovementAction moves[] = {Companions_Movement_Up, Companions_Movement_Down, Companions_Movement_Left, Companions_Movement_Right, Companions_Movement_Stay};
  MovementAction cpp_moves[] = {MovementAction::Up, MovementAction::Down, MovementAction::Left, MovementAction::Right, MovementAction::Stay};

  for (int m = 0; m < 5; m++) {
    // Get positions before move
    Companions_GameState api_state_before = {};
    companions_get_state(api_env, &api_state_before);
    auto cpp_agents_before = cpp_env.GetObjectManager().GetAllAgents();

    ASSERT_EQ(api_state_before.agents[0].position.row, cpp_agents_before[0]->GetPosition().row);
    ASSERT_EQ(api_state_before.agents[0].position.col, cpp_agents_before[0]->GetPosition().col);

    // Execute move
    Companions_Action api_action = {moves[m], Companions_Interact_None};
    Action cpp_action = EncodeAction(cpp_moves[m], InteractAction::None);

    Companions_StepResult api_result = {};
    companions_step(api_env, &api_action, 1, &api_result);
    cpp_env.Step({cpp_action});

    // Get positions after move
    auto cpp_agents_after = cpp_env.GetObjectManager().GetAllAgents();

    // Verify positions match
    ASSERT_EQ(api_result.state.agents[0].position.row, cpp_agents_after[0]->GetPosition().row);
    ASSERT_EQ(api_result.state.agents[0].position.col, cpp_agents_after[0]->GetPosition().col);
  }

  companions_destroy(api_env);
}

// Test 7: Different seeds produce different but matching states
TEST(ParityTest_DifferentSeeds) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;

  for (uint32_t seed = 1; seed <= 5; seed++) {
    // Create both environments
    Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
    Companions_Env* api_env = companions_create(&api_config);
    ASSERT_NOT_NULL(api_env);
    GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

    // Reset both
    companions_reset(api_env, seed);
    cpp_env.Reset(seed);

    // Verify initial state matches
    Companions_GameState api_state = {};
    companions_get_state(api_env, &api_state);
    auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

    for (int i = 0; i < agents; i++) {
      ASSERT_EQ(api_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
      ASSERT_EQ(api_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    }

    companions_destroy(api_env);
  }
}

// Test 8: Configuration query functions match between APIs
TEST(ParityTest_ConfigQueries) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;
  const int horizon = 100;

  // Create both environments
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed, horizon);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Verify configuration queries match
  ASSERT_EQ(companions_get_rows(api_env), cpp_env.GetRows());
  ASSERT_EQ(companions_get_cols(api_env), cpp_env.GetCols());
  ASSERT_EQ(companions_get_agent_count(api_env), cpp_env.NumAgents());
  ASSERT_EQ(companions_get_tick(api_env), cpp_env.GetTick());
  ASSERT_EQ(companions_is_done(api_env), false);
  ASSERT_EQ(companions_is_success(api_env), cpp_env.IsSuccess());

  // Step a few times and verify tick advances identically
  std::vector<Companions_Action> api_actions(agents, {Companions_Movement_Stay, Companions_Interact_None});
  std::vector<Action> cpp_actions(agents, EncodeAction(MovementAction::Stay, InteractAction::None));

  for (int step = 0; step < 5; step++) {
    Companions_StepResult api_result = {};
    companions_step(api_env, api_actions.data(), agents, &api_result);
    cpp_env.Step(cpp_actions);

    ASSERT_EQ(companions_get_tick(api_env), cpp_env.GetTick());
    ASSERT_EQ(companions_get_tick(api_env), step + 1);
  }

  // Test get_cell matches for several positions
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      Companions_CellKind api_kind = companions_get_cell(api_env, r, c);
      CellKind cpp_kind = cpp_env.GetGrid().GetCell(r, c).GetKind();

      // Map C++ enum to expected C API enum
      Companions_CellKind expected;
      switch (cpp_kind) {
        case CellKind::Floor: expected = Companions_CellKind_Floor; break;
        case CellKind::Wall: expected = Companions_CellKind_Wall; break;
        case CellKind::Hazard: expected = Companions_CellKind_Hazard; break;
        case CellKind::HealArea: expected = Companions_CellKind_HealArea; break;
        default: expected = Companions_CellKind_Floor;
      }
      ASSERT_EQ(api_kind, expected);
    }
  }

  // Test get_agent_by_index matches get_state
  Companions_GameState api_state = {};
  companions_get_state(api_env, &api_state);
  for (int i = 0; i < agents; i++) {
    Companions_AgentState agent_by_index = {};
    bool found = companions_get_agent_by_index(api_env, i, &agent_by_index);
    ASSERT_TRUE(found);
    ASSERT_EQ(agent_by_index.position.row, api_state.agents[i].position.row);
    ASSERT_EQ(agent_by_index.position.col, api_state.agents[i].position.col);
    ASSERT_EQ(agent_by_index.id, api_state.agents[i].id);
  }

  companions_destroy(api_env);
}

// =============================================================================
// Snapshot Parity Tests
// =============================================================================

// Test: DLL save → direct C++ load produces same state
TEST(TestSnapshotParity_DLLSaveDirectLoad) {
  const int rows = 10;
  const int cols = 10;
  const int agents = 2;
  const int synchro = 2;
  const uint32_t seed = 54321;

  // Create C API env
  Companions_EnvConfig config = {};
  config.rows = rows;
  config.cols = cols;
  config.num_companions = agents;
  config.num_synchro = synchro;
  config.map_complexity = 0;
  config.horizon = 100;
  config.seed = seed;

  Companions_Env* api_env = companions_create(&config);
  ASSERT_TRUE(api_env != nullptr);

  // Run a few steps
  Companions_Action actions[2] = {
      {Companions_Movement_Right, Companions_Interact_None},
      {Companions_Movement_Down, Companions_Interact_None}};
  Companions_StepResult result;
  companions_step(api_env, actions, 2, &result);

  // Save via DLL API
  int32_t size = companions_get_snapshot_size(api_env);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool save_ok = companions_save_snapshot(api_env, buffer.data(), size);
  ASSERT_TRUE(save_ok);

  // Get C API state before load
  Companions_GameState api_state;
  companions_get_state(api_env, &api_state);

  // Create direct C++ env and load the snapshot
  GameEnv cpp_env(rows, cols, agents, synchro, 0, 99999, 0, 100);

  // Deserialize and load
  Snapshot snap = Snapshot::Deserialize(buffer);
  cpp_env.LoadSnapshot(snap);

  // Verify parity: agent positions
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(cpp_agents.size(), static_cast<size_t>(api_state.agent_count));

  for (size_t i = 0; i < cpp_agents.size(); ++i) {
    ASSERT_EQ(cpp_agents[i]->GetPosition().row, api_state.agents[i].position.row);
    ASSERT_EQ(cpp_agents[i]->GetPosition().col, api_state.agents[i].position.col);
  }

  // Verify tick
  ASSERT_EQ(cpp_env.GetTick(), api_state.tick);

  companions_destroy(api_env);
}

// Test: Direct C++ save → DLL load produces same state
TEST(TestSnapshotParity_DirectSaveDLLLoad) {
  const int rows = 8;
  const int cols = 8;
  const int agents = 3;
  const int synchro = 2;
  const uint32_t seed = 11111;

  // Create direct C++ env
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Run a few steps
  std::vector<Action> cpp_actions(agents);
  cpp_actions[0] = EncodeAction(MovementAction::Down);
  cpp_actions[1] = EncodeAction(MovementAction::Left);
  cpp_actions[2] = EncodeAction(MovementAction::Right);
  cpp_env.Step(cpp_actions);
  cpp_env.Step(cpp_actions);

  // Save via direct C++ API
  Snapshot snap = cpp_env.SaveSnapshot();
  std::vector<uint8_t> buffer = snap.Serialize();

  // Record C++ state
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
  std::vector<Position> cpp_positions;
  for (const auto* agent : cpp_agents) {
    cpp_positions.push_back(agent->GetPosition());
  }
  int cpp_tick = cpp_env.GetTick();

  // Create C API env and load via DLL
  Companions_EnvConfig config = {};
  config.rows = rows;
  config.cols = cols;
  config.num_companions = agents;
  config.num_synchro = synchro;
  config.map_complexity = 0;
  config.horizon = 100;
  config.seed = 99999;  // Different seed - will be overwritten by snapshot

  Companions_Env* api_env = companions_create(&config);
  ASSERT_TRUE(api_env != nullptr);

  // Load snapshot via DLL
  bool load_ok = companions_load_snapshot(api_env, buffer.data(),
                                              static_cast<int32_t>(buffer.size()));
  ASSERT_TRUE(load_ok);

  // Verify parity
  Companions_GameState api_state;
  companions_get_state(api_env, &api_state);

  ASSERT_EQ(api_state.tick, cpp_tick);
  ASSERT_EQ(api_state.agent_count, static_cast<int32_t>(cpp_positions.size()));

  for (size_t i = 0; i < cpp_positions.size(); ++i) {
    ASSERT_EQ(api_state.agents[i].position.row, cpp_positions[i].row);
    ASSERT_EQ(api_state.agents[i].position.col, cpp_positions[i].col);
  }

  companions_destroy(api_env);
}

// Test: loading a snapshot with ZERO SynchroGoal annotations while the
// default SynchroLens is active must NOT latch success. GameEnv accepts any
// snapshot (no ValidateSnapshot override) — this is the in-game path where a
// plan executor loads e.g. an aggro-task snapshot before swapping lenses. A
// vacuous "0 agents on 0 goals" success here would lie to the executor, and
// silently so if its follow-up lens swap is rejected by CanOperateOn.
TEST(TestSnapshotParity_ZeroGoalSnapshotNoVacuousSuccess) {
  const int rows = 8, cols = 8, agents = 2, synchro = 2;
  const uint32_t seed = 42;

  // Build a snapshot with no SynchroGoal annotations from a direct C++ env.
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);
  Snapshot snap = cpp_env.SaveSnapshot();
  snap.annotations.erase(
      std::remove_if(snap.annotations.begin(), snap.annotations.end(),
                     [](const AnnotationSnapshot& a) {
                       return a.tag == SemanticTag::SynchroGoal;
                     }),
      snap.annotations.end());
  std::vector<uint8_t> buffer = snap.Serialize();

  // Load it into a C API env (GameEnv, SynchroLens active by default).
  Companions_EnvConfig config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&config);
  ASSERT_NOT_NULL(api_env);
  ASSERT_TRUE(companions_load_snapshot(api_env, buffer.data(),
                                       static_cast<int32_t>(buffer.size())));
  ASSERT_FALSE(companions_is_success(api_env));

  // The first step re-evaluates the lens: with 0 goals, success must stay
  // false instead of latching vacuously.
  std::vector<Companions_Action> actions(
      agents, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  companions_step(api_env, actions.data(), agents, &result);
  ASSERT_FALSE(companions_is_success(api_env));

  companions_destroy(api_env);
}

// =============================================================================
// Annotation Parity Tests
// =============================================================================

// Verifies that companions_get_annotations exposes the exact same entries
// (in the same order) as env.GetAnnotations().Serialize() after reset. Since
// win conditions depend on annotation state, a drift here would mean the DLL
// and direct-C++ callers disagree about what the task is.
TEST(ParityTest_Annotations) {
  const int rows = 8, cols = 8, agents = 2, synchro = 2;
  const uint32_t seed = 42;

  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  int32_t api_count = companions_get_annotation_count(api_env);
  auto cpp_serialized = cpp_env.GetAnnotations().Serialize();
  ASSERT_EQ(api_count, static_cast<int32_t>(cpp_serialized.size()));
  ASSERT_TRUE(api_count > 0);  // SynchroGoal tags from Reset, at minimum.

  std::vector<Companions_Annotation> api_anns(api_count);
  int32_t written = companions_get_annotations(api_env, api_anns.data(), api_count);
  ASSERT_EQ(written, api_count);

  for (int32_t i = 0; i < api_count; ++i) {
    const Companions_Annotation& ca = api_anns[i];
    const AnnotationSnapshot& cpp = cpp_serialized[i];
    ASSERT_EQ(ca.target_kind, static_cast<int32_t>(cpp.target_type));
    ASSERT_EQ(ca.pos.row, cpp.pos.row);
    ASSERT_EQ(ca.pos.col, cpp.pos.col);
    ASSERT_EQ(ca.agent_id, cpp.agent_id);
    ASSERT_EQ(ca.tag, static_cast<int32_t>(cpp.tag));
    ASSERT_EQ(ca.owner_lens_id, cpp.owner_lens_id);
    // Reset-placed annotations have no params, so the compact JSON is "{}".
    if (cpp.params.empty()) {
      ASSERT_EQ(std::string(ca.params_json), std::string("{}"));
    }
  }

  companions_destroy(api_env);
}

// Verifies that companions_set_task_lens_with_params stamps the same
// SynchroGoal annotations as env.SetTaskLensWithParams(SynchroLens, params)
// on the direct C++ side.
TEST(ParityTest_Annotations_After_SetTaskLens_With_Params) {
  const int rows = 8, cols = 8, agents = 2, synchro = 2;
  const uint32_t seed = 42;

  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Pick a few Floor cells known to exist on an empty 8x8 synchro map.
  const Companions_Position api_positions[] = {{1, 2}, {3, 4}, {5, 6}};
  const int n_positions = 3;
  std::vector<Position> cpp_positions;
  for (int i = 0; i < n_positions; ++i) {
    cpp_positions.push_back({api_positions[i].row, api_positions[i].col});
  }

  bool api_ok = companions_set_task_lens_with_params(
      api_env, Companions_Lens_Synchro, api_positions, n_positions);
  ASSERT_TRUE(api_ok);

  LensParams cpp_params;
  cpp_params.positions = cpp_positions;
  bool cpp_ok = cpp_env.SetTaskLensWithParams(
      std::make_unique<SynchroLens>(), cpp_params);
  ASSERT_TRUE(cpp_ok);

  int32_t api_count = companions_get_annotation_count(api_env);
  auto cpp_serialized = cpp_env.GetAnnotations().Serialize();
  ASSERT_EQ(api_count, static_cast<int32_t>(cpp_serialized.size()));

  std::vector<Companions_Annotation> api_anns(api_count);
  companions_get_annotations(api_env, api_anns.data(), api_count);

  // The Reset-placed SynchroGoal tags (owner_lens_id == -1) are environment-
  // specific and survive lens activation; only the lens-owned subset (owner
  // == SynchroLens::kOwnerId) is what SetTaskLensWithParams controls.
  // Assert that both sides see the same lens-owned SynchroGoal positions and
  // that they match exactly what we passed in.
  auto collect = [&]() {
    std::set<std::pair<int,int>> api_set, cpp_set;
    for (const auto& a : api_anns) {
      if (a.target_kind == 0 &&
          a.tag == static_cast<int32_t>(SemanticTag::SynchroGoal) &&
          a.owner_lens_id == SynchroLens::kOwnerId) {
        api_set.insert({a.pos.row, a.pos.col});
      }
    }
    for (const auto& a : cpp_serialized) {
      if (a.target_type == 0 && a.tag == SemanticTag::SynchroGoal &&
          a.owner_lens_id == SynchroLens::kOwnerId) {
        cpp_set.insert({a.pos.row, a.pos.col});
      }
    }
    return std::make_pair(api_set, cpp_set);
  };
  auto [api_set, cpp_set] = collect();
  ASSERT_EQ(api_set.size(), cpp_set.size());
  ASSERT_TRUE(api_set == cpp_set);
  ASSERT_EQ(static_cast<int>(api_set.size()), n_positions);
  for (int i = 0; i < n_positions; ++i) {
    std::pair<int,int> expected = {api_positions[i].row, api_positions[i].col};
    ASSERT_TRUE(api_set.count(expected) == 1);
  }

  companions_destroy(api_env);
}

// =============================================================================
// GameEnv vs SynchroEnv generation parity
// =============================================================================

// companions_create switched from SynchroEnv to GameEnv in 0.5.0 with the
// contract "behavior identical except done": same seed/config must produce
// the exact same level (grid, agent spawns, synchro goals) consumers got
// before, because GameEnv::Reset reuses SynchroEnv's generation path.
TEST(GameEnvMatchesSynchroEnvLevelGeneration) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;

  for (uint32_t seed = 1; seed <= 5; seed++) {
    GameEnv game_env(rows, cols, agents, synchro, 2, seed, 0, 100);
    SynchroEnv synchro_env(rows, cols, agents, synchro, 2, seed, 0, 100);

    // Same grid
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        ASSERT_EQ(static_cast<int>(game_env.GetGrid().GetCell(r, c).GetKind()),
                  static_cast<int>(synchro_env.GetGrid().GetCell(r, c).GetKind()));
      }
    }

    // Same agent spawns
    auto game_agents = game_env.GetObjectManager().GetAllAgents();
    auto synchro_agents = synchro_env.GetObjectManager().GetAllAgents();
    ASSERT_EQ(game_agents.size(), synchro_agents.size());
    for (size_t i = 0; i < game_agents.size(); i++) {
      ASSERT_EQ(game_agents[i]->GetPosition().row, synchro_agents[i]->GetPosition().row);
      ASSERT_EQ(game_agents[i]->GetPosition().col, synchro_agents[i]->GetPosition().col);
    }

    // Same synchro goal cells (annotation layer)
    std::set<std::pair<int, int>> game_goals, synchro_goals;
    for (const Position& p :
         game_env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal)) {
      game_goals.insert({p.row, p.col});
    }
    for (const Position& p :
         synchro_env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal)) {
      synchro_goals.insert({p.row, p.col});
    }
    ASSERT_TRUE(game_goals == synchro_goals);
    ASSERT_EQ(static_cast<int>(game_goals.size()), synchro);

    // The one intended difference: episode semantics.
    ASSERT_FALSE(game_env.IsDone());
  }
}

// =============================================================================
// Observation Parity Tests (DLL obs surface vs direct C++)
// =============================================================================

// Direct-C++ observation in the exact layout training uses
// (synchro_wrapper.cc write_observations): tensor planes, then base vector
// features, then the active lens's tail.
static std::vector<float> DirectObservation(const BaseEnv& env, int agent_idx) {
  std::vector<int> shape = env.ObservationShape();
  int tensor_size = shape[0] * shape[1] * shape[2];
  std::vector<float> obs(tensor_size + env.VectorObservationSize());
  env.WriteObservationTensor(obs.data(), agent_idx);
  env.WriteVectorObservation(obs.data() + tensor_size, agent_idx);
  return obs;
}

// Observations crossing the DLL boundary must be byte-identical to direct
// C++ calls — this is what lets Unreal feed trained policies.
TEST(ObsParity_ByteIdenticalAcrossBoundary) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  GameEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Size: 7-plane tensor + 9 base vector features + lens tail (Synchro = 0).
  int32_t api_size = companions_observation_size(api_env);
  ASSERT_EQ(api_size,
            BaseEnv::kNumObservationPlanes * rows * cols + 9 +
                SynchroLens().AdditionalVectorObsSize());

  std::vector<float> cpp_obs = DirectObservation(cpp_env, 0);
  ASSERT_EQ(api_size, static_cast<int32_t>(cpp_obs.size()));

  // Initial observations byte-identical for every agent.
  std::vector<float> api_obs(api_size);
  for (int a = 0; a < agents; ++a) {
    ASSERT_TRUE(companions_write_observation(api_env, a, api_obs.data(), api_size));
    cpp_obs = DirectObservation(cpp_env, a);
    ASSERT_TRUE(std::memcmp(api_obs.data(), cpp_obs.data(),
                            api_size * sizeof(float)) == 0);
  }

  // Step both with identical random actions; observations stay byte-identical.
  pcg32 action_rng(777);
  for (int step = 0; step < 50; ++step) {
    std::vector<Companions_Action> api_actions(agents);
    std::vector<Action> cpp_actions(agents);
    for (int a = 0; a < agents; ++a) {
      int mov = action_rng() % 5;
      api_actions[a] = {static_cast<Companions_MovementAction>(mov),
                        Companions_Interact_None};
      cpp_actions[a] = EncodeAction(static_cast<MovementAction>(mov),
                                    InteractAction::None);
    }
    Companions_StepResult api_result = {};
    companions_step(api_env, api_actions.data(), agents, &api_result);
    cpp_env.Step(cpp_actions);

    for (int a = 0; a < agents; ++a) {
      ASSERT_TRUE(companions_write_observation(api_env, a, api_obs.data(), api_size));
      cpp_obs = DirectObservation(cpp_env, a);
      ASSERT_TRUE(std::memcmp(api_obs.data(), cpp_obs.data(),
                              api_size * sizeof(float)) == 0);
    }
  }

  companions_destroy(api_env);
}

TEST(ObsAPI_ErrorHandling) {
  // Null env
  ASSERT_EQ(companions_observation_size(nullptr), 0);
  float dummy = 0.0f;
  ASSERT_FALSE(companions_write_observation(nullptr, 0, &dummy, 1));

  Companions_EnvConfig config = MakeConfig(8, 8, 2, 2, 0, 7);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  int32_t size = companions_observation_size(env);
  ASSERT_TRUE(size > 0);

  std::vector<float> buf(size);
  // Null buffer
  ASSERT_FALSE(companions_write_observation(env, 0, nullptr, size));
  // Bad agent index (2 agents -> valid indices 0..1)
  ASSERT_FALSE(companions_write_observation(env, -1, buf.data(), size));
  ASSERT_FALSE(companions_write_observation(env, 2, buf.data(), size));
  // Buffer too small
  ASSERT_FALSE(companions_write_observation(env, 0, buf.data(), size - 1));
  // Happy path
  ASSERT_TRUE(companions_write_observation(env, 0, buf.data(), size));

  companions_destroy(env);
}

// =============================================================================
// Lens Swap Tests (synchro world + DodgeLens)
// =============================================================================
//
// Swapping the lens changes ONLY the interpretation layer of the observation:
// the vector tail (Synchro 0 -> Dodge 10 floats), the goal plane (plane 2)
// and the distance-to-goal feature (base feature 3) are lens-driven; every
// physical plane and base feature is untouched, world dynamics are identical
// to a never-swapped control env, and the swap produces no spurious done.
TEST(LensSwap_DodgeObsTailDynamicsAndNoDone) {
  const int rows = 10, cols = 10, agents = 2, synchro = 2;
  const uint32_t seed = 1234;

  Companions_EnvConfig config = MakeConfig(rows, cols, agents, synchro, 0, seed);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  // Control: identical world, lens never swapped.
  Companions_Env* control = companions_create(&config);
  ASSERT_NOT_NULL(control);

  const int plane_size = rows * cols;
  const int tensor_size = BaseEnv::kNumObservationPlanes * plane_size;
  const int kBaseVector = 9;

  int32_t size_before = companions_observation_size(env);
  ASSERT_EQ(size_before,
            tensor_size + kBaseVector + SynchroLens().AdditionalVectorObsSize());

  std::vector<float> before(size_before);
  ASSERT_TRUE(companions_write_observation(env, 0, before.data(), size_before));

  // Swap to DodgeLens (CanOperateOn accepts any env).
  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Dodge));
  ASSERT_EQ(companions_get_task_lens(env), Companions_Lens_Dodge);

  // Obs size changed by exactly the tail delta — callers must re-query
  // companions_observation_size after a lens swap.
  int tail_delta = DodgeLens().AdditionalVectorObsSize() -
                   SynchroLens().AdditionalVectorObsSize();
  ASSERT_TRUE(tail_delta > 0);
  int32_t size_after = companions_observation_size(env);
  ASSERT_EQ(size_after, size_before + tail_delta);

  std::vector<float> after(size_after);
  ASSERT_TRUE(companions_write_observation(env, 0, after.data(), size_after));

  // Physical planes (0=floor, 1=wall, 3=self, 4=others, 5/6=hazards) are
  // unchanged for the same world state.
  const int physical_planes[] = {0, 1, 3, 4, 5, 6};
  for (int p : physical_planes) {
    ASSERT_TRUE(std::memcmp(before.data() + p * plane_size,
                            after.data() + p * plane_size,
                            plane_size * sizeof(float)) == 0);
  }

  // Plane 2 (goal) is lens-driven: SynchroLens marked the synchro goals;
  // DodgeLens has no geometric goals, so the plane is now all zeros.
  float plane2_sum_before = 0.0f;
  float plane2_sum_after = 0.0f;
  for (int i = 0; i < plane_size; ++i) {
    plane2_sum_before += before[2 * plane_size + i];
    plane2_sum_after += after[2 * plane_size + i];
  }
  ASSERT_FLOAT_EQ(plane2_sum_before, static_cast<float>(synchro));
  ASSERT_FLOAT_EQ(plane2_sum_after, 0.0f);

  // Base vector features unchanged except feature 3 (distance to nearest
  // goal — lens-driven, explicit 0 for DodgeLens which has no goals).
  for (int f = 0; f < kBaseVector; ++f) {
    if (f == 3) continue;
    ASSERT_FLOAT_EQ(before[tensor_size + f], after[tensor_size + f]);
  }
  ASSERT_FLOAT_EQ(after[tensor_size + 3], 0.0f);

  // No spurious done from the swap.
  ASSERT_FALSE(companions_is_done(env));

  // Dynamics identical: step swapped env and control with the same actions;
  // world evolution (agent positions) matches and done stays false on both.
  pcg32 action_rng(99);
  for (int step = 0; step < 30; ++step) {
    std::vector<Companions_Action> actions(agents);
    for (int a = 0; a < agents; ++a) {
      actions[a] = {static_cast<Companions_MovementAction>(action_rng() % 5),
                    Companions_Interact_None};
    }
    Companions_StepResult res = {};
    Companions_StepResult control_res = {};
    companions_step(env, actions.data(), agents, &res);
    companions_step(control, actions.data(), agents, &control_res);

    ASSERT_FALSE(res.state.done);
    ASSERT_FALSE(control_res.state.done);
    for (int a = 0; a < agents; ++a) {
      Companions_AgentState s = {};
      Companions_AgentState cs = {};
      ASSERT_TRUE(companions_get_agent_by_index(env, a, &s));
      ASSERT_TRUE(companions_get_agent_by_index(control, a, &cs));
      ASSERT_EQ(s.position.row, cs.position.row);
      ASSERT_EQ(s.position.col, cs.position.col);
    }
  }

  companions_destroy(control);
  companions_destroy(env);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  std::cout << "Running C API Parity Tests...\n" << std::endl;
  std::cout << "Verifying C API produces identical results to direct C++ API\n" << std::endl;

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << std::endl;
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << std::endl;
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << std::endl;
      std::cout << "  Error: " << e.what() << std::endl;
      failed++;
    }
  }

  std::cout << "\n========================================" << std::endl;
  std::cout << "Parity Tests: " << (passed + failed) << std::endl;
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;

  if (failed == 0) {
    std::cout << "\nPARITY VERIFIED: C API matches direct C++ API" << std::endl;
  }

  return failed > 0 ? 1 : 0;
}
