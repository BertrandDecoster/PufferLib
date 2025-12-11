// Copyright 2024
// Parity test: Verify UE C API produces identical results to direct C++ API
//
// This test ensures the UE API wrapper (companions_ue.h) doesn't introduce
// any behavioral differences compared to directly calling SynchroEnv methods.

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_ue.h"
#include "../src/core/pcg32.h"
#include "../src/core/types.h"
#include "../src/env/synchro_env.h"

using namespace companions;

// =============================================================================
// Test macros (reused from test_ue_api.cc)
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
static UE_EnvConfig MakeUEConfig(int rows = 12, int cols = 12, int companions = 3,
                                  int synchro = 3, int complexity = 0,
                                  uint32_t seed = 42, int horizon = 100) {
  UE_EnvConfig config = {};
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

  // Create UE API environment
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);

  // Create direct C++ environment
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both with same seed
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // Get states
  UE_GameState ue_state = {};
  ue_companions_get_state(ue_env, &ue_state);
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

  // Verify agent count matches
  ASSERT_EQ(ue_state.agent_count, static_cast<int>(cpp_agents.size()));
  ASSERT_EQ(ue_state.agent_count, agents);

  // Verify each agent position matches
  for (int i = 0; i < agents; i++) {
    ASSERT_EQ(ue_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
    ASSERT_EQ(ue_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    ASSERT_EQ(ue_state.agents[i].health, cpp_agents[i]->GetHealth());
    ASSERT_EQ(ue_state.agents[i].alive, cpp_agents[i]->IsAlive());
  }

  // Verify grid dimensions
  ASSERT_EQ(ue_state.rows, cpp_env.GetRows());
  ASSERT_EQ(ue_state.cols, cpp_env.GetCols());

  // Verify tick
  ASSERT_EQ(ue_state.tick, cpp_env.GetTick());

  ue_companions_destroy(ue_env);
}

// Test 2: Single step produces same results
TEST(ParityTest_SingleStep) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  // Create both environments
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // All agents stay (simplest action)
  std::vector<UE_Action> ue_actions(agents);
  std::vector<Action> cpp_actions(agents);
  for (int i = 0; i < agents; i++) {
    ue_actions[i] = {UE_Movement_Stay, UE_Interact_None};
    cpp_actions[i] = EncodeAction(MovementAction::Stay, InteractAction::None);
  }

  // Step both
  UE_StepResult ue_result = {};
  ue_companions_step(ue_env, ue_actions.data(), agents, &ue_result);
  StepResult cpp_result = cpp_env.Step(cpp_actions);

  // Verify done flag matches
  ASSERT_EQ(ue_result.state.done, cpp_result.done);

  // Verify rewards match
  for (int i = 0; i < agents; i++) {
    ASSERT_FLOAT_EQ(ue_result.state.rewards[i], static_cast<float>(cpp_result.rewards[i]));
  }

  // Verify tick advanced
  ASSERT_EQ(ue_result.state.tick, 1);
  ASSERT_EQ(cpp_env.GetTick(), 1);

  // Verify agent positions still match
  auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
  for (int i = 0; i < agents; i++) {
    ASSERT_EQ(ue_result.state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
    ASSERT_EQ(ue_result.state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
  }

  ue_companions_destroy(ue_env);
}

// Test 3: Multi-step with random actions maintains parity
TEST(ParityTest_MultiStep) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t env_seed = 42;
  const uint32_t action_seed = 123;
  const int num_steps = 200;

  // Create both environments
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, env_seed);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, env_seed, 0, 100);

  // Reset both
  ue_companions_reset(ue_env, env_seed);
  cpp_env.Reset(env_seed);

  // Action RNG
  pcg32 action_rng(action_seed);

  // Track resets for seed management
  uint32_t current_seed = env_seed + 1;

  for (int step = 0; step < num_steps; step++) {
    // Generate identical actions for both APIs
    std::vector<UE_Action> ue_actions(agents);
    std::vector<Action> cpp_actions(agents);
    for (int a = 0; a < agents; a++) {
      int mov = action_rng() % 5;
      int interact = action_rng() % 2;
      ue_actions[a] = {static_cast<UE_MovementAction>(mov),
                       static_cast<UE_InteractAction>(interact)};
      cpp_actions[a] = EncodeAction(static_cast<MovementAction>(mov),
                                    static_cast<InteractAction>(interact));
    }

    // Step both environments
    UE_StepResult ue_result = {};
    ue_companions_step(ue_env, ue_actions.data(), agents, &ue_result);
    StepResult cpp_result = cpp_env.Step(cpp_actions);

    // Verify done flags match
    ASSERT_EQ(ue_result.state.done, cpp_result.done);

    // Verify rewards match
    for (int a = 0; a < agents; a++) {
      ASSERT_FLOAT_EQ(ue_result.state.rewards[a], static_cast<float>(cpp_result.rewards[a]));
    }

    // Verify agent positions match
    auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();
    for (int i = 0; i < agents; i++) {
      ASSERT_EQ(ue_result.state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
      ASSERT_EQ(ue_result.state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    }

    // Handle episode reset - reset both with same seed
    if (cpp_result.done) {
      ue_companions_reset(ue_env, current_seed);
      cpp_env.Reset(current_seed);
      current_seed++;
    }
  }

  ue_companions_destroy(ue_env);
  std::cout << "  Completed " << num_steps << " steps with full parity" << std::endl;
}

// Test 4: Episode reset produces matching state
TEST(ParityTest_EpisodeReset) {
  const int rows = 6, cols = 6, agents = 1, synchro = 1;
  const uint32_t seed = 42;
  const int horizon = 50;

  // Create both environments with short horizon
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed, horizon);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

  // Reset both
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // Run until episode ends
  std::vector<UE_Action> ue_actions(agents, {UE_Movement_Stay, UE_Interact_None});
  std::vector<Action> cpp_actions(agents, EncodeAction(MovementAction::Stay, InteractAction::None));

  bool episode_ended = false;
  for (int step = 0; step < horizon + 10 && !episode_ended; step++) {
    UE_StepResult ue_result = {};
    ue_companions_step(ue_env, ue_actions.data(), agents, &ue_result);
    StepResult cpp_result = cpp_env.Step(cpp_actions);

    ASSERT_EQ(ue_result.state.done, cpp_result.done);

    if (cpp_result.done) {
      episode_ended = true;

      // Reset both with new seed
      uint32_t new_seed = seed + 100;
      ue_companions_reset(ue_env, new_seed);
      cpp_env.Reset(new_seed);

      // Verify post-reset state matches
      UE_GameState ue_state = {};
      ue_companions_get_state(ue_env, &ue_state);
      auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

      ASSERT_EQ(ue_state.tick, 0);
      ASSERT_EQ(cpp_env.GetTick(), 0);
      ASSERT_FALSE(ue_state.done);

      for (int i = 0; i < agents; i++) {
        ASSERT_EQ(ue_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
        ASSERT_EQ(ue_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
      }
    }
  }

  ASSERT_TRUE(episode_ended);
  ue_companions_destroy(ue_env);
}

// Test 5: Grid state matches between APIs
TEST(ParityTest_GridState) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;

  // Create both environments
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // Get full grid from UE API
  std::vector<UE_CellKind> ue_grid(rows * cols);
  ue_companions_get_grid(ue_env, ue_grid.data());

  // Compare with C++ grid
  const auto& cpp_grid = cpp_env.GetGrid();
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      UE_CellKind ue_kind = ue_grid[r * cols + c];
      CellKind cpp_kind = cpp_grid.GetCell(r, c).GetKind();

      // Map C++ enum to expected UE enum value
      UE_CellKind expected;
      switch (cpp_kind) {
        case CellKind::Floor: expected = UE_CellKind_Floor; break;
        case CellKind::Wall: expected = UE_CellKind_Wall; break;
        case CellKind::Hazard: expected = UE_CellKind_Hazard; break;
        case CellKind::Synchro: expected = UE_CellKind_Synchro; break;
        case CellKind::HealArea: expected = UE_CellKind_HealArea; break;
        case CellKind::Target: expected = UE_CellKind_Target; break;
        default: expected = UE_CellKind_Floor;
      }

      ASSERT_EQ(ue_kind, expected);
    }
  }

  // Count synchro cells
  int ue_synchro_count = 0;
  for (auto kind : ue_grid) {
    if (kind == UE_CellKind_Synchro) ue_synchro_count++;
  }
  ASSERT_EQ(ue_synchro_count, synchro);

  ue_companions_destroy(ue_env);
}

// Test 6: Verify movement produces same position changes
TEST(ParityTest_Movement) {
  const int rows = 10, cols = 10, agents = 1, synchro = 1;
  const uint32_t seed = 42;

  // Create both environments
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

  // Reset both
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // Try each movement direction
  UE_MovementAction moves[] = {UE_Movement_Up, UE_Movement_Down, UE_Movement_Left, UE_Movement_Right, UE_Movement_Stay};
  MovementAction cpp_moves[] = {MovementAction::Up, MovementAction::Down, MovementAction::Left, MovementAction::Right, MovementAction::Stay};

  for (int m = 0; m < 5; m++) {
    // Get positions before move
    UE_GameState ue_state_before = {};
    ue_companions_get_state(ue_env, &ue_state_before);
    auto cpp_agents_before = cpp_env.GetObjectManager().GetAllAgents();

    ASSERT_EQ(ue_state_before.agents[0].position.row, cpp_agents_before[0]->GetPosition().row);
    ASSERT_EQ(ue_state_before.agents[0].position.col, cpp_agents_before[0]->GetPosition().col);

    // Execute move
    UE_Action ue_action = {moves[m], UE_Interact_None};
    Action cpp_action = EncodeAction(cpp_moves[m], InteractAction::None);

    UE_StepResult ue_result = {};
    ue_companions_step(ue_env, &ue_action, 1, &ue_result);
    cpp_env.Step({cpp_action});

    // Get positions after move
    auto cpp_agents_after = cpp_env.GetObjectManager().GetAllAgents();

    // Verify positions match
    ASSERT_EQ(ue_result.state.agents[0].position.row, cpp_agents_after[0]->GetPosition().row);
    ASSERT_EQ(ue_result.state.agents[0].position.col, cpp_agents_after[0]->GetPosition().col);
  }

  ue_companions_destroy(ue_env);
}

// Test 7: Different seeds produce different but matching states
TEST(ParityTest_DifferentSeeds) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;

  for (uint32_t seed = 1; seed <= 5; seed++) {
    // Create both environments
    UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed);
    UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
    ASSERT_NOT_NULL(ue_env);
    SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, 100);

    // Reset both
    ue_companions_reset(ue_env, seed);
    cpp_env.Reset(seed);

    // Verify initial state matches
    UE_GameState ue_state = {};
    ue_companions_get_state(ue_env, &ue_state);
    auto cpp_agents = cpp_env.GetObjectManager().GetAllAgents();

    for (int i = 0; i < agents; i++) {
      ASSERT_EQ(ue_state.agents[i].position.row, cpp_agents[i]->GetPosition().row);
      ASSERT_EQ(ue_state.agents[i].position.col, cpp_agents[i]->GetPosition().col);
    }

    ue_companions_destroy(ue_env);
  }
}

// Test 8: Configuration query functions match between APIs
TEST(ParityTest_ConfigQueries) {
  const int rows = 12, cols = 12, agents = 3, synchro = 3;
  const uint32_t seed = 42;
  const int horizon = 100;

  // Create both environments
  UE_EnvConfig ue_config = MakeUEConfig(rows, cols, agents, synchro, 0, seed, horizon);
  UE_CompanionsEnv* ue_env = ue_companions_create(&ue_config);
  ASSERT_NOT_NULL(ue_env);
  SynchroEnv cpp_env(rows, cols, agents, synchro, 0, seed, 0, horizon);

  // Reset both
  ue_companions_reset(ue_env, seed);
  cpp_env.Reset(seed);

  // Verify configuration queries match
  ASSERT_EQ(ue_companions_get_rows(ue_env), cpp_env.GetRows());
  ASSERT_EQ(ue_companions_get_cols(ue_env), cpp_env.GetCols());
  ASSERT_EQ(ue_companions_get_agent_count(ue_env), cpp_env.NumAgents());
  ASSERT_EQ(ue_companions_get_tick(ue_env), cpp_env.GetTick());
  ASSERT_EQ(ue_companions_is_done(ue_env), false);
  ASSERT_EQ(ue_companions_is_success(ue_env), cpp_env.IsSuccess());

  // Step a few times and verify tick advances identically
  std::vector<UE_Action> ue_actions(agents, {UE_Movement_Stay, UE_Interact_None});
  std::vector<Action> cpp_actions(agents, EncodeAction(MovementAction::Stay, InteractAction::None));

  for (int step = 0; step < 5; step++) {
    UE_StepResult ue_result = {};
    ue_companions_step(ue_env, ue_actions.data(), agents, &ue_result);
    cpp_env.Step(cpp_actions);

    ASSERT_EQ(ue_companions_get_tick(ue_env), cpp_env.GetTick());
    ASSERT_EQ(ue_companions_get_tick(ue_env), step + 1);
  }

  // Test get_cell matches for several positions
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      UE_CellKind ue_kind = ue_companions_get_cell(ue_env, r, c);
      CellKind cpp_kind = cpp_env.GetGrid().GetCell(r, c).GetKind();

      // Map C++ enum to expected UE enum
      UE_CellKind expected;
      switch (cpp_kind) {
        case CellKind::Floor: expected = UE_CellKind_Floor; break;
        case CellKind::Wall: expected = UE_CellKind_Wall; break;
        case CellKind::Hazard: expected = UE_CellKind_Hazard; break;
        case CellKind::Synchro: expected = UE_CellKind_Synchro; break;
        case CellKind::HealArea: expected = UE_CellKind_HealArea; break;
        case CellKind::Target: expected = UE_CellKind_Target; break;
        default: expected = UE_CellKind_Floor;
      }
      ASSERT_EQ(ue_kind, expected);
    }
  }

  // Test get_agent_by_index matches get_state
  UE_GameState ue_state = {};
  ue_companions_get_state(ue_env, &ue_state);
  for (int i = 0; i < agents; i++) {
    UE_AgentState agent_by_index = {};
    bool found = ue_companions_get_agent_by_index(ue_env, i, &agent_by_index);
    ASSERT_TRUE(found);
    ASSERT_EQ(agent_by_index.position.row, ue_state.agents[i].position.row);
    ASSERT_EQ(agent_by_index.position.col, ue_state.agents[i].position.col);
    ASSERT_EQ(agent_by_index.id, ue_state.agents[i].id);
  }

  ue_companions_destroy(ue_env);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  std::cout << "Running UE API Parity Tests...\n" << std::endl;
  std::cout << "Verifying UE C API produces identical results to direct C++ API\n" << std::endl;

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
    std::cout << "\nPARITY VERIFIED: UE API matches direct C++ API" << std::endl;
  }

  return failed > 0 ? 1 : 0;
}
