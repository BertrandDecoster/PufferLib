// Copyright 2024
// Parity test: Verify C API produces identical results to direct C++ API
//
// This test ensures the API wrapper (companions_api.h) doesn't introduce
// any behavioral differences compared to directly calling SynchroEnv methods.

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_api.h"
#include "../src/core/pcg32.h"
#include "../src/core/snapshot.h"
#include "../src/core/types.h"
#include "../src/env/synchro_env.h"

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, env_seed, 0, 100);

  // Reset both
  companions_reset(api_env, env_seed);
  cpp_env.Reset(env_seed);

  // Action RNG
  pcg32 action_rng(action_seed);

  // Track resets for seed management
  uint32_t current_seed = env_seed + 1;

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

    // Verify done flags match
    ASSERT_EQ(api_result.state.done, cpp_result.done);

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

    // Handle episode reset - reset both with same seed
    if (cpp_result.done) {
      companions_reset(api_env, current_seed);
      cpp_env.Reset(current_seed);
      current_seed++;
    }
  }

  companions_destroy(api_env);
  std::cout << "  Completed " << num_steps << " steps with full parity" << std::endl;
}

// Test 4: Episode reset produces matching state
TEST(ParityTest_EpisodeReset) {
  const int rows = 6, cols = 6, agents = 1, synchro = 1;
  const uint32_t seed = 42;
  const int horizon = 50;

  // Create both environments with short horizon
  Companions_EnvConfig api_config = MakeConfig(rows, cols, agents, synchro, 0, seed, horizon);
  Companions_Env* api_env = companions_create(&api_config);
  ASSERT_NOT_NULL(api_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

  // Reset both
  companions_reset(api_env, seed);
  cpp_env.Reset(seed);

  // Run until episode ends
  std::vector<Companions_Action> api_actions(agents, {Companions_Movement_Stay, Companions_Interact_None});
  std::vector<Action> cpp_actions(agents, EncodeAction(MovementAction::Stay, InteractAction::None));

  bool episode_ended = false;
  for (int step = 0; step < horizon + 10 && !episode_ended; step++) {
    Companions_StepResult api_result = {};
    companions_step(api_env, api_actions.data(), agents, &api_result);
    StepResult cpp_result = cpp_env.Step(cpp_actions);

    ASSERT_EQ(api_result.state.done, cpp_result.done);

    if (cpp_result.done) {
      episode_ended = true;

      // Reset both with new seed
      uint32_t new_seed = seed + 100;
      companions_reset(api_env, new_seed);
      cpp_env.Reset(new_seed);

      // Verify post-reset state matches
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
    }
  }

  ASSERT_TRUE(episode_ended);
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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
        case CellKind::Synchro: expected = Companions_CellKind_Synchro; break;
        case CellKind::HealArea: expected = Companions_CellKind_HealArea; break;
        case CellKind::Target: expected = Companions_CellKind_Target; break;
        default: expected = Companions_CellKind_Floor;
      }

      ASSERT_EQ(api_kind, expected);
    }
  }

  // Count synchro cells
  int api_synchro_count = 0;
  for (auto kind : api_grid) {
    if (kind == Companions_CellKind_Synchro) api_synchro_count++;
  }
  ASSERT_EQ(api_synchro_count, synchro);

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
    SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

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
        case CellKind::Synchro: expected = Companions_CellKind_Synchro; break;
        case CellKind::HealArea: expected = Companions_CellKind_HealArea; break;
        case CellKind::Target: expected = Companions_CellKind_Target; break;
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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, 99999, 0, 100);

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
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

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
