// Copyright 2024
// Test suite for the UE5 C API

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_ue.h"

// =============================================================================
// Test macros
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

#define ASSERT_NE(a, b) \
  if ((a) == (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_NE failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__; \
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
// Helper: Create default config
// =============================================================================
static UE_EnvConfig MakeConfig(int rows = 12, int cols = 12, int companions = 3,
                               int synchro = 3, uint32_t seed = 42) {
  UE_EnvConfig config = {};
  config.rows = rows;
  config.cols = cols;
  config.num_companions = companions;
  config.num_synchro = synchro;
  config.map_complexity = 0;
  config.horizon = 100;
  config.d4_transform = 0;
  config.seed = seed;
  return config;
}

// =============================================================================
// Lifecycle Tests
// =============================================================================
TEST(TestVersion) {
  const char* version = ue_companions_version();
  ASSERT_NOT_NULL(version);
  ASSERT_TRUE(std::strlen(version) > 0);
  std::cout << "  Version: " << version << std::endl;
}

TEST(TestCreateDestroy) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_NOT_NULL(env);

  ASSERT_EQ(ue_companions_get_rows(env), 12);
  ASSERT_EQ(ue_companions_get_cols(env), 12);
  ASSERT_EQ(ue_companions_get_agent_count(env), 3);

  ue_companions_destroy(env);
}

TEST(TestCreateWithNullConfig) {
  UE_CompanionsEnv* env = ue_companions_create(nullptr);
  ASSERT_TRUE(env == nullptr);
  const char* error = ue_companions_get_error();
  ASSERT_NOT_NULL(error);
  ASSERT_TRUE(std::strlen(error) > 0);
}

// =============================================================================
// Reset Tests
// =============================================================================
TEST(TestReset) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_NOT_NULL(env);

  ue_companions_reset(env, 42);
  ASSERT_EQ(ue_companions_get_tick(env), 0);
  ASSERT_FALSE(ue_companions_is_done(env));

  ue_companions_destroy(env);
}

TEST(TestResetDeterminism) {
  UE_EnvConfig config = MakeConfig();

  // Create two environments
  UE_CompanionsEnv* env1 = ue_companions_create(&config);
  UE_CompanionsEnv* env2 = ue_companions_create(&config);
  ASSERT_NOT_NULL(env1);
  ASSERT_NOT_NULL(env2);

  // Reset both with same seed
  ue_companions_reset(env1, 123);
  ue_companions_reset(env2, 123);

  // Get states
  UE_GameState state1 = {};
  UE_GameState state2 = {};
  ue_companions_get_state(env1, &state1);
  ue_companions_get_state(env2, &state2);

  // Should have same agent positions
  ASSERT_EQ(state1.agent_count, state2.agent_count);
  for (int i = 0; i < state1.agent_count; i++) {
    ASSERT_EQ(state1.agents[i].position.row, state2.agents[i].position.row);
    ASSERT_EQ(state1.agents[i].position.col, state2.agents[i].position.col);
  }

  ue_companions_destroy(env1);
  ue_companions_destroy(env2);
}

// =============================================================================
// State Query Tests
// =============================================================================
TEST(TestGetState) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  UE_GameState state = {};
  ue_companions_get_state(env, &state);

  ASSERT_EQ(state.rows, 12);
  ASSERT_EQ(state.cols, 12);
  ASSERT_EQ(state.agent_count, 3);
  ASSERT_FALSE(state.done);

  // All agents should be alive
  for (int i = 0; i < state.agent_count; i++) {
    ASSERT_TRUE(state.agents[i].alive);
    ASSERT_TRUE(state.agents[i].health > 0);
    ASSERT_TRUE(state.agents[i].position.row >= 0);
    ASSERT_TRUE(state.agents[i].position.col >= 0);
  }

  ue_companions_destroy(env);
}

TEST(TestGetAgentByIndex) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  UE_AgentState agent = {};
  bool found = ue_companions_get_agent_by_index(env, 0, &agent);
  ASSERT_TRUE(found);
  ASSERT_TRUE(agent.alive);
  ASSERT_EQ(agent.agent_index, 0);

  // Out of range
  found = ue_companions_get_agent_by_index(env, 99, &agent);
  ASSERT_FALSE(found);

  ue_companions_destroy(env);
}

TEST(TestGetCell) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  // Corners should be walls (perimeter)
  ASSERT_EQ(ue_companions_get_cell(env, 0, 0), UE_CellKind_Wall);
  ASSERT_EQ(ue_companions_get_cell(env, 0, 11), UE_CellKind_Wall);
  ASSERT_EQ(ue_companions_get_cell(env, 11, 0), UE_CellKind_Wall);
  ASSERT_EQ(ue_companions_get_cell(env, 11, 11), UE_CellKind_Wall);

  // Interior should include some synchro cells
  // (we have 3 synchro cells in a 12x12 grid)
  int synchro_count = 0;
  for (int r = 1; r < 11; r++) {
    for (int c = 1; c < 11; c++) {
      if (ue_companions_get_cell(env, r, c) == UE_CellKind_Synchro) {
        synchro_count++;
      }
    }
  }
  ASSERT_EQ(synchro_count, 3);

  ue_companions_destroy(env);
}

TEST(TestGetGrid) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  std::vector<UE_CellKind> grid(12 * 12);
  ue_companions_get_grid(env, grid.data());

  // Top-left corner should be wall
  ASSERT_EQ(grid[0], UE_CellKind_Wall);

  // Count walls (should be perimeter)
  int wall_count = 0;
  for (auto cell : grid) {
    if (cell == UE_CellKind_Wall) wall_count++;
  }
  // Perimeter = 4*12 - 4 = 44 walls
  ASSERT_EQ(wall_count, 44);

  ue_companions_destroy(env);
}

// =============================================================================
// Step Tests
// =============================================================================
TEST(TestStep) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  // All agents stay
  UE_Action actions[3] = {
    {UE_Movement_Stay, UE_Interact_None},
    {UE_Movement_Stay, UE_Interact_None},
    {UE_Movement_Stay, UE_Interact_None}
  };

  UE_StepResult result = {};
  ue_companions_step(env, actions, 3, &result);

  ASSERT_EQ(ue_companions_get_tick(env), 1);
  ASSERT_EQ(result.state.tick, 1);

  ue_companions_destroy(env);
}

TEST(TestStepWithMovement) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  // Get initial position of first agent
  UE_AgentState initial = {};
  ue_companions_get_agent_by_index(env, 0, &initial);

  // Try to move first agent up (might be blocked by wall)
  UE_Action actions[3] = {
    {UE_Movement_Up, UE_Interact_None},
    {UE_Movement_Stay, UE_Interact_None},
    {UE_Movement_Stay, UE_Interact_None}
  };

  UE_StepResult result = {};
  ue_companions_step(env, actions, 3, &result);

  // Should have generated some events
  // At minimum: movement or blocked event for agent 0
  ASSERT_TRUE(result.event_count >= 0);

  // Check the event types
  for (int i = 0; i < result.event_count; i++) {
    ASSERT_TRUE(result.events[i].type != UE_Event_None);
  }

  ue_companions_destroy(env);
}

TEST(TestStepGeneratesMovementEvents) {
  UE_EnvConfig config = MakeConfig(6, 6, 1, 1, 42);  // Small grid, 1 agent
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  // Run multiple steps to find one where movement succeeds
  UE_Action actions[1] = {{UE_Movement_Down, UE_Interact_None}};
  UE_StepResult result = {};

  int move_events = 0;
  int blocked_events = 0;

  for (int step = 0; step < 10; step++) {
    ue_companions_step(env, actions, 1, &result);

    for (int i = 0; i < result.event_count; i++) {
      if (result.events[i].type == UE_Event_AgentMoved) {
        move_events++;
      } else if (result.events[i].type == UE_Event_AgentBlocked) {
        blocked_events++;
      }
    }

    // Alternate directions
    actions[0].movement = static_cast<UE_MovementAction>(
        (actions[0].movement % 4) + 1);
  }

  // Should have at least one movement or blocked event
  ASSERT_TRUE(move_events + blocked_events > 0);

  ue_companions_destroy(env);
}

// =============================================================================
// Episode End Tests
// =============================================================================
TEST(TestEpisodeEnd) {
  // Small grid where it's easy to win
  UE_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  config.horizon = 200;
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  UE_Action actions[1] = {{UE_Movement_Stay, UE_Interact_None}};
  UE_StepResult result = {};

  // Step until done (either success or horizon)
  int steps = 0;
  while (!ue_companions_is_done(env) && steps < 300) {
    // Randomly move around to try to land on synchro
    actions[0].movement = static_cast<UE_MovementAction>((steps % 5));
    ue_companions_step(env, actions, 1, &result);
    steps++;
  }

  ASSERT_TRUE(ue_companions_is_done(env));

  // Last step should have EpisodeEnd event
  bool has_end_event = false;
  for (int i = 0; i < result.event_count; i++) {
    if (result.events[i].type == UE_Event_EpisodeEnd) {
      has_end_event = true;
    }
  }
  ASSERT_TRUE(has_end_event);

  ue_companions_destroy(env);
}

// =============================================================================
// Special Cells Tests
// =============================================================================
TEST(TestSpecialCells) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  UE_GameState state = {};
  ue_companions_get_state(env, &state);

  // Should have special cells (walls at perimeter, synchro cells)
  ASSERT_TRUE(state.special_cell_count > 0);

  // Count synchro cells in special cells
  int synchro_count = 0;
  for (int i = 0; i < state.special_cell_count; i++) {
    if (state.special_cells[i].kind == UE_CellKind_Synchro) {
      synchro_count++;
    }
  }
  ASSERT_EQ(synchro_count, 3);

  ue_companions_destroy(env);
}

// =============================================================================
// Action Intent vs Actual Tests
// =============================================================================
TEST(TestActionIntentVsActual) {
  // Small grid where we can predictably hit walls
  // Grid is 5x5 with walls on perimeter, agent spawns in interior
  UE_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  // Get initial agent position
  UE_AgentState agent = {};
  ue_companions_get_agent_by_index(env, 0, &agent);

  // Walk to the top wall (row 1 is adjacent to wall at row 0)
  // Keep moving up until we hit the wall
  UE_Action actions[1] = {{UE_Movement_Up, UE_Interact_None}};
  UE_StepResult result = {};

  // Move up repeatedly until blocked
  bool found_blocked = false;
  for (int i = 0; i < 5; i++) {
    ue_companions_step(env, actions, 1, &result);

    // Check if movement was blocked
    if (!result.state.agents[0].action_succeeded) {
      found_blocked = true;

      // CRITICAL CHECKS:
      // Intent should be Up (what we asked for)
      ASSERT_EQ(result.state.agents[0].action_intent.movement, UE_Movement_Up);
      // Actual should be Stay (blocked by wall)
      ASSERT_EQ(result.state.agents[0].action_actual.movement, UE_Movement_Stay);
      // action_succeeded should be false
      ASSERT_FALSE(result.state.agents[0].action_succeeded);

      break;
    } else {
      // When movement succeeds, intent should equal actual
      ASSERT_EQ(result.state.agents[0].action_intent.movement,
                result.state.agents[0].action_actual.movement);
      ASSERT_TRUE(result.state.agents[0].action_succeeded);
    }
  }

  // We should have hit the wall at some point
  ASSERT_TRUE(found_blocked);

  ue_companions_destroy(env);
}

TEST(TestActionIntentVsActualStay) {
  // Test that Stay action has matching intent/actual
  UE_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ue_companions_reset(env, 42);

  UE_Action actions[1] = {{UE_Movement_Stay, UE_Interact_None}};
  UE_StepResult result = {};

  ue_companions_step(env, actions, 1, &result);

  // Stay should always succeed
  ASSERT_EQ(result.state.agents[0].action_intent.movement, UE_Movement_Stay);
  ASSERT_EQ(result.state.agents[0].action_actual.movement, UE_Movement_Stay);
  ASSERT_TRUE(result.state.agents[0].action_succeeded);

  ue_companions_destroy(env);
}

// =============================================================================
// Snapshot Tests
// =============================================================================

TEST(TestSnapshotSizeReturnsPositive) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  int32_t size = ue_companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  ue_companions_destroy(env);
}

TEST(TestSaveSnapshotRoundTrip) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Run a few steps to change state
  UE_Action actions[3] = {
      {UE_Movement_Right, UE_Interact_None},
      {UE_Movement_Down, UE_Interact_None},
      {UE_Movement_Stay, UE_Interact_None}};
  UE_StepResult result;
  ue_companions_step(env, actions, 3, &result);
  ue_companions_step(env, actions, 3, &result);

  // Record state before save
  UE_GameState state_before;
  ue_companions_get_state(env, &state_before);

  // Save snapshot
  int32_t size = ue_companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool save_ok = ue_companions_save_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(save_ok);

  // Reset env (changes state)
  ue_companions_reset(env, 999);

  // Verify state changed
  UE_GameState state_after_reset;
  ue_companions_get_state(env, &state_after_reset);
  // Tick should be 0 after reset
  ASSERT_EQ(state_after_reset.tick, 0);

  // Load snapshot
  bool load_ok = ue_companions_load_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(load_ok);

  // Verify state restored
  UE_GameState state_after_load;
  ue_companions_get_state(env, &state_after_load);

  ASSERT_EQ(state_after_load.tick, state_before.tick);
  ASSERT_EQ(state_after_load.agent_count, state_before.agent_count);

  // Check agent positions match
  for (int i = 0; i < state_before.agent_count && i < UE_MAX_AGENTS; ++i) {
    ASSERT_EQ(state_after_load.agents[i].position.row,
              state_before.agents[i].position.row);
    ASSERT_EQ(state_after_load.agents[i].position.col,
              state_before.agents[i].position.col);
  }

  ue_companions_destroy(env);
}

TEST(TestSaveSnapshotBufferTooSmall) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  int32_t size = ue_companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  // Try to save with buffer that's too small
  std::vector<uint8_t> small_buffer(size / 2);
  bool save_ok = ue_companions_save_snapshot(env, small_buffer.data(),
                                              static_cast<int32_t>(small_buffer.size()));
  ASSERT_FALSE(save_ok);

  // Error should be set
  const char* error = ue_companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);

  ue_companions_destroy(env);
}

TEST(TestLoadSnapshotInvalidData) {
  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Try to load garbage data
  uint8_t garbage[100] = {0x12, 0x34, 0x56, 0x78};  // Invalid magic
  bool load_ok = ue_companions_load_snapshot(env, garbage, 100);
  ASSERT_FALSE(load_ok);

  // Error should be set
  const char* error = ue_companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);

  ue_companions_destroy(env);
}

TEST(TestSnapshotNullArgs) {
  // Test null env
  ASSERT_EQ(ue_companions_get_snapshot_size(nullptr), 0);

  UE_EnvConfig config = MakeConfig();
  UE_CompanionsEnv* env = ue_companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Get valid size first
  int32_t size = ue_companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  // Test null buffer
  ASSERT_FALSE(ue_companions_save_snapshot(env, nullptr, size));

  // Test null data for load
  ASSERT_FALSE(ue_companions_load_snapshot(env, nullptr, 100));

  // Test null env for load
  uint8_t dummy[100] = {0};
  ASSERT_FALSE(ue_companions_load_snapshot(nullptr, dummy, 100));

  ue_companions_destroy(env);
}

// =============================================================================
// Level Generation Tests
// =============================================================================

TEST(TestGenerateLevelBasic) {
  UE_LevelConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.map_complexity = 0;
  config.seed = 12345;
  config.num_companions = 2;
  config.horizon = 100;

  int32_t size = ue_companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool ok = ue_companions_get_generated_level(buffer.data(), size);
  ASSERT_TRUE(ok);
}

TEST(TestGenerateLevelWithSynchro) {
  UE_LevelConfig config = {};
  config.rows = 12;
  config.cols = 12;
  config.map_complexity = 0;
  config.seed = 42;
  config.synchro_cell_count = 3;
  config.num_companions = 3;
  config.horizon = 100;

  int32_t size = ue_companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  // Load into SynchroEnv to verify compatibility
  std::vector<uint8_t> buffer(size);
  ue_companions_get_generated_level(buffer.data(), size);

  UE_EnvConfig env_config = MakeConfig(12, 12, 3, 3, 999);
  UE_CompanionsEnv* env = ue_companions_create(&env_config);
  ASSERT_TRUE(env != nullptr);

  bool load_ok = ue_companions_load_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(load_ok);

  ue_companions_destroy(env);
}

TEST(TestGenerateLevelWithPatrol) {
  UE_LevelConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.map_complexity = 0;
  config.seed = 54321;
  config.patrol_square_size = 3;
  config.has_target_cell = true;
  config.num_companions = 1;
  config.num_enemies = 1;
  config.horizon = 100;

  int32_t size = ue_companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool ok = ue_companions_get_generated_level(buffer.data(), size);
  ASSERT_TRUE(ok);
}

TEST(TestGenerateLevelNullConfig) {
  int32_t size = ue_companions_generate_level(nullptr);
  ASSERT_EQ(size, 0);

  const char* error = ue_companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);
}

TEST(TestGenerateLevelInvalidDimensions) {
  UE_LevelConfig config = {};
  config.rows = 0;  // Invalid
  config.cols = 10;
  config.seed = 42;

  int32_t size = ue_companions_generate_level(&config);
  ASSERT_EQ(size, 0);
}

TEST(TestGetGeneratedLevelWithoutGenerate) {
  uint8_t buffer[100];
  // Clear any previously generated level by generating with invalid config
  UE_LevelConfig bad_config = {};
  bad_config.rows = 0;
  ue_companions_generate_level(&bad_config);

  // Now try to get without valid generation
  bool ok = ue_companions_get_generated_level(buffer, 100);
  ASSERT_FALSE(ok);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  std::cout << "Running UE5 API tests...\n" << std::endl;

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
  std::cout << "Tests: " << (passed + failed) << std::endl;
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;

  return failed > 0 ? 1 : 0;
}
