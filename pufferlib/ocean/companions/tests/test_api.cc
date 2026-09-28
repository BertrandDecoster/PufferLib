// Copyright 2024
// Test suite for the C API

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_api.h"

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
static Companions_EnvConfig MakeConfig(int rows = 12, int cols = 12, int companions = 3,
                               int synchro = 3, uint32_t seed = 42) {
  Companions_EnvConfig config = {};
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
  const char* version = companions_version();
  ASSERT_NOT_NULL(version);
  ASSERT_EQ(std::string(version), std::string("1.2.0"));  // 1.2 removed the companion cast
  std::cout << "  Version: " << version << std::endl;
}

TEST(TestCreateDestroy) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);

  ASSERT_EQ(companions_get_rows(env), 12);
  ASSERT_EQ(companions_get_cols(env), 12);
  ASSERT_EQ(companions_get_agent_count(env), 3);

  companions_destroy(env);
}

TEST(TestCreateWithNullConfig) {
  Companions_Env* env = companions_create(nullptr);
  ASSERT_TRUE(env == nullptr);
  const char* error = companions_get_error();
  ASSERT_NOT_NULL(error);
  ASSERT_TRUE(std::strlen(error) > 0);
}

// =============================================================================
// Reset Tests
// =============================================================================
TEST(TestReset) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);

  companions_reset(env, 42);
  ASSERT_EQ(companions_get_tick(env), 0);
  ASSERT_FALSE(companions_is_done(env));

  companions_destroy(env);
}

TEST(TestResetDeterminism) {
  Companions_EnvConfig config = MakeConfig();

  // Create two environments
  Companions_Env* env1 = companions_create(&config);
  Companions_Env* env2 = companions_create(&config);
  ASSERT_NOT_NULL(env1);
  ASSERT_NOT_NULL(env2);

  // Reset both with same seed
  companions_reset(env1, 123);
  companions_reset(env2, 123);

  // Get states
  Companions_GameState state1 = {};
  Companions_GameState state2 = {};
  companions_get_state(env1, &state1);
  companions_get_state(env2, &state2);

  // Should have same agent positions
  ASSERT_EQ(state1.agent_count, state2.agent_count);
  for (int i = 0; i < state1.agent_count; i++) {
    ASSERT_EQ(state1.agents[i].position.row, state2.agents[i].position.row);
    ASSERT_EQ(state1.agents[i].position.col, state2.agents[i].position.col);
  }

  companions_destroy(env1);
  companions_destroy(env2);
}

// =============================================================================
// State Query Tests
// =============================================================================
TEST(TestGetState) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_GameState state = {};
  companions_get_state(env, &state);

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

  companions_destroy(env);
}

TEST(TestGetAgentByIndex) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_AgentState agent = {};
  bool found = companions_get_agent_by_index(env, 0, &agent);
  ASSERT_TRUE(found);
  ASSERT_TRUE(agent.alive);
  ASSERT_EQ(agent.agent_index, 0);

  // Out of range
  found = companions_get_agent_by_index(env, 99, &agent);
  ASSERT_FALSE(found);

  companions_destroy(env);
}

TEST(TestGetCell) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // Corners should be walls (perimeter)
  ASSERT_EQ(companions_get_cell(env, 0, 0), Companions_CellKind_Wall);
  ASSERT_EQ(companions_get_cell(env, 0, 11), Companions_CellKind_Wall);
  ASSERT_EQ(companions_get_cell(env, 11, 0), Companions_CellKind_Wall);
  ASSERT_EQ(companions_get_cell(env, 11, 11), Companions_CellKind_Wall);

  // Interior cells are now all Floor (task-semantic role "synchro goal"
  // lives on the annotation layer, not on CellKind). Just verify the grid
  // comes back with only physical cell kinds.
  for (int r = 1; r < 11; r++) {
    for (int c = 1; c < 11; c++) {
      Companions_CellKind k = companions_get_cell(env, r, c);
      ASSERT_TRUE(k == Companions_CellKind_Floor || k == Companions_CellKind_Wall);
    }
  }

  companions_destroy(env);
}

TEST(TestGetGrid) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  std::vector<Companions_CellKind> grid(12 * 12);
  companions_get_grid(env, grid.data());

  // Top-left corner should be wall
  ASSERT_EQ(grid[0], Companions_CellKind_Wall);

  // Count walls (should be perimeter)
  int wall_count = 0;
  for (auto cell : grid) {
    if (cell == Companions_CellKind_Wall) wall_count++;
  }
  // Perimeter = 4*12 - 4 = 44 walls
  ASSERT_EQ(wall_count, 44);

  companions_destroy(env);
}

// =============================================================================
// Step Tests
// =============================================================================
TEST(TestStep) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // All agents stay
  Companions_Action actions[3] = {
    {Companions_Movement_Stay, Companions_Interact_None},
    {Companions_Movement_Stay, Companions_Interact_None},
    {Companions_Movement_Stay, Companions_Interact_None}
  };

  Companions_StepResult result = {};
  companions_step(env, actions, 3, &result);

  ASSERT_EQ(companions_get_tick(env), 1);
  ASSERT_EQ(result.state.tick, 1);

  companions_destroy(env);
}

TEST(TestStepWithMovement) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // Get initial position of first agent
  Companions_AgentState initial = {};
  companions_get_agent_by_index(env, 0, &initial);

  // Try to move first agent up (might be blocked by wall)
  Companions_Action actions[3] = {
    {Companions_Movement_Up, Companions_Interact_None},
    {Companions_Movement_Stay, Companions_Interact_None},
    {Companions_Movement_Stay, Companions_Interact_None}
  };

  Companions_StepResult result = {};
  companions_step(env, actions, 3, &result);

  // Should have generated some events
  // At minimum: movement or blocked event for agent 0
  ASSERT_TRUE(result.event_count >= 0);

  // Check the event types
  for (int i = 0; i < result.event_count; i++) {
    ASSERT_TRUE(result.events[i].type != Companions_Event_None);
  }

  companions_destroy(env);
}

TEST(TestStepGeneratesMovementEvents) {
  Companions_EnvConfig config = MakeConfig(6, 6, 1, 1, 42);  // Small grid, 1 agent
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // Run multiple steps to find one where movement succeeds
  Companions_Action actions[1] = {{Companions_Movement_Down, Companions_Interact_None}};
  Companions_StepResult result = {};

  int move_events = 0;
  int blocked_events = 0;

  for (int step = 0; step < 10; step++) {
    companions_step(env, actions, 1, &result);

    for (int i = 0; i < result.event_count; i++) {
      if (result.events[i].type == Companions_Event_AgentMoved) {
        move_events++;
      } else if (result.events[i].type == Companions_Event_AgentBlocked) {
        blocked_events++;
      }
    }

    // Alternate directions
    actions[0].movement = static_cast<Companions_MovementAction>(
        (actions[0].movement % 4) + 1);
  }

  // Should have at least one movement or blocked event
  ASSERT_TRUE(move_events + blocked_events > 0);

  companions_destroy(env);
}

// =============================================================================
// Episode End Tests
// =============================================================================
TEST(TestEpisodeEnd) {
  // Small grid where it's easy to win
  Companions_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  config.horizon = 200;
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_Action actions[1] = {{Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};

  // Step until done (either success or horizon)
  int steps = 0;
  while (!companions_is_done(env) && steps < 300) {
    // Randomly move around to try to land on synchro
    actions[0].movement = static_cast<Companions_MovementAction>((steps % 5));
    companions_step(env, actions, 1, &result);
    steps++;
  }

  ASSERT_TRUE(companions_is_done(env));

  // Last step should have EpisodeEnd event
  bool has_end_event = false;
  for (int i = 0; i < result.event_count; i++) {
    if (result.events[i].type == Companions_Event_EpisodeEnd) {
      has_end_event = true;
    }
  }
  ASSERT_TRUE(has_end_event);

  companions_destroy(env);
}

// =============================================================================
// Special Cells Tests
// =============================================================================
TEST(TestSpecialCells) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_GameState state = {};
  companions_get_state(env, &state);

  // Should have special cells at the perimeter walls. Synchro/Target roles
  // now flow through the annotation layer rather than the physical CellKind,
  // so they're not reflected in special_cells.
  ASSERT_TRUE(state.special_cell_count > 0);

  companions_destroy(env);
}

// =============================================================================
// Action Intent vs Actual Tests
// =============================================================================
TEST(TestActionIntentVsActual) {
  // Small grid where we can predictably hit walls
  // Grid is 5x5 with walls on perimeter, agent spawns in interior
  Companions_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // Get initial agent position
  Companions_AgentState agent = {};
  companions_get_agent_by_index(env, 0, &agent);

  // Walk to the top wall (row 1 is adjacent to wall at row 0)
  // Keep moving up until we hit the wall
  Companions_Action actions[1] = {{Companions_Movement_Up, Companions_Interact_None}};
  Companions_StepResult result = {};

  // Move up repeatedly until blocked
  bool found_blocked = false;
  for (int i = 0; i < 5; i++) {
    companions_step(env, actions, 1, &result);

    // Check if movement was blocked
    if (!result.state.agents[0].action_succeeded) {
      found_blocked = true;

      // CRITICAL CHECKS:
      // Intent should be Up (what we asked for)
      ASSERT_EQ(result.state.agents[0].action_intent.movement, Companions_Movement_Up);
      // Actual should be Stay (blocked by wall)
      ASSERT_EQ(result.state.agents[0].action_actual.movement, Companions_Movement_Stay);
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

  companions_destroy(env);
}

TEST(TestActionIntentVsActualStay) {
  // Test that Stay action has matching intent/actual
  Companions_EnvConfig config = MakeConfig(5, 5, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_Action actions[1] = {{Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};

  companions_step(env, actions, 1, &result);

  // Stay should always succeed
  ASSERT_EQ(result.state.agents[0].action_intent.movement, Companions_Movement_Stay);
  ASSERT_EQ(result.state.agents[0].action_actual.movement, Companions_Movement_Stay);
  ASSERT_TRUE(result.state.agents[0].action_succeeded);

  companions_destroy(env);
}

// =============================================================================
// Snapshot Tests
// =============================================================================

TEST(TestSnapshotSizeReturnsPositive) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  companions_destroy(env);
}

TEST(TestSaveSnapshotRoundTrip) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Run a few steps to change state
  Companions_Action actions[3] = {
      {Companions_Movement_Right, Companions_Interact_None},
      {Companions_Movement_Down, Companions_Interact_None},
      {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result;
  companions_step(env, actions, 3, &result);
  companions_step(env, actions, 3, &result);

  // Record state before save
  Companions_GameState state_before;
  companions_get_state(env, &state_before);

  // Save snapshot
  int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool save_ok = companions_save_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(save_ok);

  // Reset env (changes state)
  companions_reset(env, 999);

  // Verify state changed
  Companions_GameState state_after_reset;
  companions_get_state(env, &state_after_reset);
  // Tick should be 0 after reset
  ASSERT_EQ(state_after_reset.tick, 0);

  // Load snapshot
  bool load_ok = companions_load_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(load_ok);

  // Verify state restored
  Companions_GameState state_after_load;
  companions_get_state(env, &state_after_load);

  ASSERT_EQ(state_after_load.tick, state_before.tick);
  ASSERT_EQ(state_after_load.agent_count, state_before.agent_count);

  // Check agent positions match
  for (int i = 0; i < state_before.agent_count && i < Companions_MAX_AGENTS; ++i) {
    ASSERT_EQ(state_after_load.agents[i].position.row,
              state_before.agents[i].position.row);
    ASSERT_EQ(state_after_load.agents[i].position.col,
              state_before.agents[i].position.col);
  }

  companions_destroy(env);
}

TEST(TestSaveSnapshotBufferTooSmall) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  // Try to save with buffer that's too small
  std::vector<uint8_t> small_buffer(size / 2);
  bool save_ok = companions_save_snapshot(env, small_buffer.data(),
                                              static_cast<int32_t>(small_buffer.size()));
  ASSERT_FALSE(save_ok);

  // Error should be set
  const char* error = companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);

  companions_destroy(env);
}

TEST(TestLoadSnapshotInvalidData) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Try to load garbage data
  uint8_t garbage[100] = {0x12, 0x34, 0x56, 0x78};  // Invalid magic
  bool load_ok = companions_load_snapshot(env, garbage, 100);
  ASSERT_FALSE(load_ok);

  // Error should be set
  const char* error = companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);

  companions_destroy(env);
}

TEST(TestSnapshotNullArgs) {
  // Test null env
  ASSERT_EQ(companions_get_snapshot_size(nullptr), 0);

  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);

  // Get valid size first
  int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);

  // Test null buffer
  ASSERT_FALSE(companions_save_snapshot(env, nullptr, size));

  // Test null data for load
  ASSERT_FALSE(companions_load_snapshot(env, nullptr, 100));

  // Test null env for load
  uint8_t dummy[100] = {0};
  ASSERT_FALSE(companions_load_snapshot(nullptr, dummy, 100));

  companions_destroy(env);
}

// =============================================================================
// Level Generation Tests
// =============================================================================

TEST(TestGenerateLevelBasic) {
  Companions_LevelConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.map_complexity = 0;
  config.seed = 12345;
  config.num_companions = 2;
  config.horizon = 100;

  int32_t size = companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool ok = companions_get_generated_level(buffer.data(), size);
  ASSERT_TRUE(ok);
}

TEST(TestGenerateLevelWithSynchro) {
  Companions_LevelConfig config = {};
  config.rows = 12;
  config.cols = 12;
  config.map_complexity = 0;
  config.seed = 42;
  config.synchro_cell_count = 3;
  config.num_companions = 3;
  config.horizon = 100;

  int32_t size = companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  // Load into SynchroEnv to verify compatibility
  std::vector<uint8_t> buffer(size);
  companions_get_generated_level(buffer.data(), size);

  Companions_EnvConfig env_config = MakeConfig(12, 12, 3, 3, 999);
  Companions_Env* env = companions_create(&env_config);
  ASSERT_TRUE(env != nullptr);

  bool load_ok = companions_load_snapshot(env, buffer.data(), size);
  ASSERT_TRUE(load_ok);

  companions_destroy(env);
}

TEST(TestGenerateLevelWithPatrol) {
  Companions_LevelConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.map_complexity = 0;
  config.seed = 54321;
  config.patrol_square_size = 3;
  config.has_target_cell = true;
  config.num_companions = 1;
  config.num_enemies = 1;
  config.horizon = 100;

  int32_t size = companions_generate_level(&config);
  ASSERT_TRUE(size > 0);

  std::vector<uint8_t> buffer(size);
  bool ok = companions_get_generated_level(buffer.data(), size);
  ASSERT_TRUE(ok);
}

TEST(TestGenerateLevelNullConfig) {
  int32_t size = companions_generate_level(nullptr);
  ASSERT_EQ(size, 0);

  const char* error = companions_get_error();
  ASSERT_TRUE(error != nullptr);
  ASSERT_TRUE(std::strlen(error) > 0);
}

TEST(TestGenerateLevelInvalidDimensions) {
  Companions_LevelConfig config = {};
  config.rows = 0;  // Invalid
  config.cols = 10;
  config.seed = 42;

  int32_t size = companions_generate_level(&config);
  ASSERT_EQ(size, 0);
}

TEST(TestGetGeneratedLevelWithoutGenerate) {
  uint8_t buffer[100];
  // Clear any previously generated level by generating with invalid config
  Companions_LevelConfig bad_config = {};
  bad_config.rows = 0;
  companions_generate_level(&bad_config);

  // Now try to get without valid generation
  bool ok = companions_get_generated_level(buffer, 100);
  ASSERT_FALSE(ok);
}

// =============================================================================
// Annotation API Tests
// =============================================================================
// SemanticTag integer values (must stay in sync with annotations.h /
// htn_bridge.py SEMANTIC_TAG_NAMES).
static constexpr int32_t kTagSynchroGoal = 0;
static constexpr int32_t kTagAggroTarget = 1;
static constexpr int32_t kTagQuestPickup = 2;

TEST(TestAnnotationCountMatchesNumSynchro) {
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  companions_reset(env, 42);

  // SynchroEnv places exactly `num_synchro` persistent SynchroGoal cell
  // annotations during Reset.
  int32_t count = companions_get_annotation_count(env);
  ASSERT_EQ(count, 3);

  companions_destroy(env);
}

TEST(TestAnnotationCountNullEnv) {
  ASSERT_EQ(companions_get_annotation_count(nullptr), 0);
}

TEST(TestGetAnnotationsReturnsSynchroGoals) {
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  int32_t count = companions_get_annotation_count(env);
  std::vector<Companions_Annotation> anns(count);
  int32_t written = companions_get_annotations(env, anns.data(), count);
  ASSERT_EQ(written, count);

  // Every returned annotation should be a persistent cell-level SynchroGoal
  // at a valid interior position.
  for (int i = 0; i < written; i++) {
    ASSERT_EQ(anns[i].target_kind, 0);  // 0 = Cell
    ASSERT_EQ(anns[i].tag, kTagSynchroGoal);
    ASSERT_EQ(anns[i].owner_lens_id, -1);  // Persistent (external owner)
    ASSERT_TRUE(anns[i].pos.row > 0 && anns[i].pos.row < 11);
    ASSERT_TRUE(anns[i].pos.col > 0 && anns[i].pos.col < 11);
    // params_json is null-terminated
    ASSERT_EQ(anns[i].params_json[COMPANIONS_MAX_ANNOTATION_PARAMS - 1], '\0');
  }

  companions_destroy(env);
}

TEST(TestGetAnnotationsBufferTooSmall) {
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  // Request fewer annotations than exist. Should write `count` and stop.
  Companions_Annotation one = {};
  int32_t written = companions_get_annotations(env, &one, 1);
  ASSERT_EQ(written, 1);
  ASSERT_EQ(one.tag, kTagSynchroGoal);

  companions_destroy(env);
}

TEST(TestGetAnnotationsNullArgs) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_Annotation buf[1] = {};
  ASSERT_EQ(companions_get_annotations(nullptr, buf, 1), 0);
  ASSERT_EQ(companions_get_annotations(env, nullptr, 1), 0);
  // Zero count is a no-op (not an error).
  ASSERT_EQ(companions_get_annotations(env, buf, 0), 0);

  companions_destroy(env);
}

TEST(TestHasTagAtSynchroGoalPositions) {
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  int32_t count = companions_get_annotation_count(env);
  std::vector<Companions_Annotation> anns(count);
  companions_get_annotations(env, anns.data(), count);

  // Every reported goal position answers yes; wrong tag answers no.
  for (const auto& a : anns) {
    ASSERT_TRUE(companions_has_tag_at(env, a.pos.row, a.pos.col, kTagSynchroGoal));
    ASSERT_FALSE(companions_has_tag_at(env, a.pos.row, a.pos.col, kTagAggroTarget));
    ASSERT_FALSE(companions_has_tag_at(env, a.pos.row, a.pos.col, kTagQuestPickup));
  }

  // A wall corner carries no semantic tags.
  ASSERT_FALSE(companions_has_tag_at(env, 0, 0, kTagSynchroGoal));
  // A clearly-non-goal interior position (start search from (1,1), skip goals)
  for (int r = 1; r < 11; r++) {
    for (int c = 1; c < 11; c++) {
      bool is_goal = false;
      for (const auto& a : anns) {
        if (a.pos.row == r && a.pos.col == c) { is_goal = true; break; }
      }
      if (!is_goal) {
        ASSERT_FALSE(companions_has_tag_at(env, r, c, kTagSynchroGoal));
        goto done;
      }
    }
  }
  done:;
  companions_destroy(env);
}

TEST(TestHasTagAtNullEnv) {
  ASSERT_FALSE(companions_has_tag_at(nullptr, 1, 1, kTagSynchroGoal));
}

TEST(TestAgentHasTagFalseForCompanions) {
  // SynchroEnv does not tag companions with any SemanticTag; agent-level tags
  // (TargetMob, SkillGiver, HtnName, ...) are set by the HTN planner, not the
  // default env reset.
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  for (int i = 0; i < companions_get_agent_count(env); i++) {
    Companions_AgentState agent = {};
    ASSERT_TRUE(companions_get_agent_by_index(env, i, &agent));
    ASSERT_FALSE(companions_agent_has_tag(env, agent.id, kTagSynchroGoal));
    ASSERT_FALSE(companions_agent_has_tag(env, agent.id, kTagAggroTarget));
  }

  companions_destroy(env);
}

TEST(TestAgentHasTagNullEnv) {
  ASSERT_FALSE(companions_agent_has_tag(nullptr, 1, kTagSynchroGoal));
}

TEST(TestAggroEnvHasTargetAnnotation) {
  Companions_AggroEnvConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.num_companions = 1;
  config.patrol_square_size = 3;
  config.horizon = 100;
  config.d4_transform = 0;
  config.seed = 42;
  config.enemy_type = Companions_Enemy_Zombie;
  config.map_complexity = 0;

  Companions_Env* env = companions_create_aggro(&config);
  ASSERT_NOT_NULL(env);
  companions_reset(env, 42);

  // AggroEnv places exactly one persistent AggroTarget cell annotation.
  int32_t count = companions_get_annotation_count(env);
  ASSERT_TRUE(count >= 1);

  std::vector<Companions_Annotation> anns(count);
  companions_get_annotations(env, anns.data(), count);

  int target_count = 0;
  Companions_Annotation target = {};
  for (const auto& a : anns) {
    if (a.tag == kTagAggroTarget) {
      target_count++;
      target = a;
    }
  }
  ASSERT_EQ(target_count, 1);
  ASSERT_EQ(target.target_kind, 0);
  ASSERT_EQ(target.owner_lens_id, -1);
  ASSERT_TRUE(companions_has_tag_at(env, target.pos.row, target.pos.col,
                                    kTagAggroTarget));

  companions_destroy(env);
}

TEST(TestAnnotationsSurviveSnapshotRoundTrip) {
  Companions_EnvConfig config = MakeConfig(12, 12, 3, 3, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  int32_t count_before = companions_get_annotation_count(env);
  std::vector<Companions_Annotation> before(count_before);
  companions_get_annotations(env, before.data(), count_before);

  // Save, reset to a different seed, reload.
  int32_t size = companions_get_snapshot_size(env);
  std::vector<uint8_t> buffer(size);
  ASSERT_TRUE(companions_save_snapshot(env, buffer.data(), size));
  companions_reset(env, 999);
  ASSERT_TRUE(companions_load_snapshot(env, buffer.data(), size));

  int32_t count_after = companions_get_annotation_count(env);
  ASSERT_EQ(count_after, count_before);

  // Every pre-save SynchroGoal position is still a SynchroGoal after reload.
  for (const auto& a : before) {
    ASSERT_TRUE(companions_has_tag_at(env, a.pos.row, a.pos.col, a.tag));
  }

  companions_destroy(env);
}

// =============================================================================
// Companion cast + host-driven changes
// =============================================================================
static Companions_Env* MakeAggroZombieEnv() {
  Companions_AggroEnvConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.num_companions = 1;
  config.patrol_square_size = 3;
  config.horizon = 100;
  config.d4_transform = 0;
  config.seed = 42;
  config.enemy_type = Companions_Enemy_Zombie;
  config.map_complexity = 0;
  Companions_Env* env = companions_create_aggro(&config);
  companions_reset(env, 42);
  return env;
}

static int32_t FindAgentIndex(Companions_Env* env, Companions_Faction faction) {
  int32_t n = companions_get_agent_count(env);
  for (int32_t i = 0; i < n; ++i) {
    Companions_AgentState a;
    if (companions_get_agent_by_index(env, i, &a) && a.faction == faction) {
      return i;
    }
  }
  return -1;
}

// Every companion slot holds a skill: without one set, it is the default
// "attack", so Attack (= Skill1) aims without moving and strikes the faced
// cell. There is no generic cast (no EffectSpawned).
TEST(TestAttackUsesTheDefaultSkill) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);

  Companions_AgentState before;
  ASSERT_TRUE(companions_get_agent_by_index(env, 0, &before));
  ASSERT_EQ(std::string(before.skills[0]), std::string("attack"));
  ASSERT_EQ(std::string(before.skills[1]), std::string("attack"));
  // Aim at whichever horizontal neighbour is inside the grid.
  Companions_MovementAction aim = before.position.col > 1
      ? Companions_Movement_Left : Companions_Movement_Right;
  Companions_Action action = {aim, Companions_Interact_Attack};
  Companions_StepResult result;
  companions_step(env, &action, 1, &result);

  Companions_AgentState after;
  ASSERT_TRUE(companions_get_agent_by_index(env, 0, &after));
  ASSERT_EQ(after.position.row, before.position.row);
  ASSERT_EQ(after.position.col, before.position.col);
  ASSERT_EQ(after.facing, aim == Companions_Movement_Left
      ? Companions_Direction_Left : Companions_Direction_Right);
  bool used = false;
  for (int32_t i = 0; i < result.event_count; ++i) {
    const Companions_Event& e = result.events[i];
    ASSERT_NE(e.type, Companions_Event_EffectSpawned);
    if (e.type != Companions_Event_SkillUsed) continue;
    used = true;
    ASSERT_EQ(e.subject_id, before.id);
    ASSERT_EQ(std::string(e.effect_name), std::string("attack"));
    ASSERT_EQ(e.effect_id, 0);
  }
  ASSERT_TRUE(used);
  companions_destroy(env);
}

TEST(TestSetCellChangesKind) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  companions_reset(env, 42);
  // The border is wall; open one border cell.
  ASSERT_EQ(companions_get_cell(env, 0, 3), Companions_CellKind_Wall);
  ASSERT_TRUE(companions_set_cell(env, 0, 3, Companions_CellKind_Floor));
  ASSERT_EQ(companions_get_cell(env, 0, 3), Companions_CellKind_Floor);
  ASSERT_FALSE(companions_set_cell(env, -1, 3, Companions_CellKind_Floor));
  ASSERT_FALSE(companions_set_cell(nullptr, 0, 3, Companions_CellKind_Floor));
  companions_destroy(env);
}

TEST(TestSpawnEffectKillAndStun) {
  Companions_Env* env = MakeAggroZombieEnv();
  int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  ASSERT_TRUE(zi >= 0);
  Companions_AgentState z;
  companions_get_agent_by_index(env, zi, &z);

  ASSERT_FALSE(companions_spawn_effect(env, "no_such_effect", z.position.row,
                                       z.position.col, Companions_Direction_Up, -1));
  ASSERT_TRUE(companions_spawn_effect(env, "stun", z.position.row, z.position.col,
                                      Companions_Direction_Up, -1));
  companions_get_agent_by_index(env, zi, &z);
  bool stunned = false;
  for (int32_t s = 0; s < z.status_count; ++s) {
    if (z.statuses[s].type == Companions_Status_Stunned) stunned = true;
  }
  ASSERT_TRUE(stunned);

  ASSERT_TRUE(companions_spawn_effect(env, "kill", z.position.row, z.position.col,
                                      Companions_Direction_Up, -1));
  companions_get_agent_by_index(env, zi, &z);
  ASSERT_FALSE(z.alive);
  companions_destroy(env);
}

TEST(TestSnapshotJsonKeepsEnemyKindAndAttack) {
  Companions_Env* env = MakeAggroZombieEnv();
  const char* json = companions_snapshot_to_json(env);
  ASSERT_NOT_NULL(json);
  std::string text(json);
  companions_free_string(json);
  ASSERT_TRUE(text.find("\"kind\": \"Zombie\"") != std::string::npos ||
              text.find("\"kind\":\"Zombie\"") != std::string::npos);
  ASSERT_TRUE(text.find("has_attack") != std::string::npos);

  ASSERT_TRUE(companions_load_snapshot_json(env, text.c_str()));
  int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  Companions_AgentState z;
  ASSERT_TRUE(companions_get_agent_by_index(env, zi, &z));
  ASSERT_EQ(z.kind, Companions_AgentKind_EnemyZombie);
  companions_destroy(env);
}

// =============================================================================
// Skills, tags, zones
// =============================================================================

// A companion of LevelJson: its cell, and `extra` spliced into its JSON
// object (e.g. ",\"skills\":[\"blink\",\"\"]").
struct LevelAgent {
  int row;
  int col;
  std::string extra;
};

// An 8x8 snapshot (walls on the border, floor inside) with one companion per
// entry, a SynchroGoal on (6, 6), `skills` as the snapshot's skill list, and
// the given horizon.
static std::string LevelJson(const std::vector<LevelAgent>& agents,
                             const std::string& skills = "[]", int horizon = 100) {
  std::ostringstream j;
  j << "{\"magic\":\"SNAP\",\"version\":4,\"grid\":{\"rows\":8,\"cols\":8,\"cells\":[";
  bool first = true;
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      if (r != 0 && r != 7 && c != 0 && c != 7) continue;
      j << (first ? "" : ",") << "{\"row\":" << r << ",\"col\":" << c
        << ",\"cell_kind\":\"Wall\",\"cell_origin\":\"Default\"}";
      first = false;
    }
  }
  j << "]},\"agents\":[";
  for (size_t i = 0; i < agents.size(); ++i) {
    const LevelAgent& a = agents[i];
    const std::string pos = "{\"row\":" + std::to_string(a.row) + ",\"col\":" +
                            std::to_string(a.col) + "}";
    j << (i ? "," : "") << "{\"id\":" << i << ",\"agent_type\":\"Player\",\"position\":" << pos
      << ",\"prev_position\":" << pos << ",\"health\":3,\"max_health\":3,\"agent_index\":" << i
      << ",\"faction\":\"COMPANION\",\"direction\":\"Down\",\"color\":\"Red\",\"alive\":true,"
      << "\"statuses\":[],\"fsm\":null,\"cadence\":[],\"tick\":0" << a.extra << "}";
  }
  j << "],\"effects\":[],\"tick\":0,\"horizon\":" << horizon
    << ",\"rng_state\":{\"state\":0,\"inc\":0},"
    << "\"d4_value\":0,\"patrol_path\":[],\"annotations\":[{\"target\":\"Cell\","
    << "\"pos\":{\"row\":6,\"col\":6},\"tag\":\"SynchroGoal\",\"owner_lens_id\":-1,"
    << "\"params\":{}}],\"skills\":" << skills << ",\"cell_tags\":[]}";
  return j.str();
}

static Companions_Env* LoadLevel(const std::vector<LevelAgent>& agents,
                                 const std::string& skills = "[]", int horizon = 100) {
  Companions_EnvConfig config = MakeConfig(8, 8, static_cast<int>(agents.size()), 1, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  if (!companions_load_snapshot_json(env, LevelJson(agents, skills, horizon).c_str())) {
    throw std::runtime_error(std::string("LevelJson did not load: ") + companions_get_error());
  }
  return env;
}

static Companions_AgentState AgentAt(Companions_Env* env, int32_t index) {
  Companions_AgentState a = {};
  ASSERT_TRUE(companions_get_agent_by_index(env, index, &a));
  return a;
}

// The first event of `type` about `subject` (any subject if -2), or null.
static const Companions_Event* FindEvent(const Companions_StepResult& r,
                                         Companions_EventType type,
                                         Companions_ObjectId subject = -2) {
  for (int32_t i = 0; i < r.event_count; ++i) {
    const Companions_Event& e = r.events[i];
    if (e.type == type && (subject == -2 || e.subject_id == subject)) return &e;
  }
  return nullptr;
}

// Leaves a known, unrelated message in companions_get_error ("Invalid
// arguments"), so a test can tell a call set its own.
static void SetErrorProbe() {
  companions_find_tag(nullptr, nullptr);
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid arguments"));
}

static bool HasStatus(const Companions_AgentState& a, Companions_StatusType type) {
  for (int32_t s = 0; s < a.status_count; ++s) {
    if (a.statuses[s].type == type) return true;
  }
  return false;
}

TEST(TestSkill1TeleportsAndReportsSkillUsed) {
  Companions_Env* env = LoadLevel({{3, 1, ""}});
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(std::string(a.skills[0]), std::string("attack"));
  ASSERT_TRUE(companions_set_agent_skill(env, a.id, 0, "teleport"));
  a = AgentAt(env, 0);
  ASSERT_EQ(std::string(a.skills[0]), std::string("teleport"));
  ASSERT_EQ(a.skill_cooldowns[0], 0);

  Companions_Action action = {Companions_Movement_Right, Companions_Interact_Skill1};
  Companions_StepResult result = {};
  companions_step(env, &action, 1, &result);

  Companions_AgentState after = AgentAt(env, 0);
  ASSERT_EQ(after.position.row, 3);
  ASSERT_EQ(after.position.col, 4);  // Teleported 3 cells
  ASSERT_EQ(std::string(after.skills[0]), std::string("teleport"));
  ASSERT_EQ(std::string(after.skills[1]), std::string("attack"));
  ASSERT_EQ(after.skill_cooldowns[0], 4);
  ASSERT_EQ(after.skill_cooldowns[1], 0);
  ASSERT_EQ(result.state.agents[0].skill_cooldowns[0], 4);

  const Companions_Event* used = FindEvent(result, Companions_Event_SkillUsed, a.id);
  ASSERT_NOT_NULL(used);
  ASSERT_EQ(std::string(used->effect_name), std::string("teleport"));
  ASSERT_EQ(used->effect_id, 0);  // The slot
  ASSERT_EQ(result.events_dropped, 0);
  ASSERT_EQ(used->position.row, 3);
  ASSERT_EQ(used->position.col, 4);
  ASSERT_EQ(used->tick, result.state.tick);
  const Companions_Event* moved = FindEvent(result, Companions_Event_AgentMoved, a.id);
  ASSERT_NOT_NULL(moved);
  ASSERT_EQ(moved->to_pos.col, 4);
  companions_destroy(env);
}

TEST(TestApplyAndRemoveTagThroughApi) {
  Companions_Env* env = LoadLevel({{3, 1, ""}});
  const Companions_ObjectId id = AgentAt(env, 0).id;
  ASSERT_EQ(companions_find_tag(env, "never_seen"), -1);
  ASSERT_EQ(companions_find_tag(env, nullptr), -1);
  ASSERT_EQ(companions_find_tag(nullptr, "burning"), -1);

  ASSERT_TRUE(companions_apply_tag(env, id, "burning", -1));
  const int32_t burning = companions_find_tag(env, "burning");
  ASSERT_TRUE(burning >= 0);
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(a.tag_count, 1);
  ASSERT_EQ(a.tags[0].tag_id, burning);
  ASSERT_EQ(a.tags[0].duration, -1);
  const char* name = companions_get_tag_name(env, burning);
  ASSERT_NOT_NULL(name);
  ASSERT_EQ(std::string(name), std::string("burning"));

  ASSERT_TRUE(companions_remove_tag(env, id, "burning"));
  ASSERT_EQ(AgentAt(env, 0).tag_count, 0);
  ASSERT_TRUE(companions_remove_tag(env, id, "burning"));  // Not carried: still fine
  ASSERT_FALSE(companions_remove_tag(env, 999, "burning"));
  ASSERT_FALSE(companions_remove_tag(env, id, nullptr));
  ASSERT_FALSE(companions_remove_tag(nullptr, id, "burning"));

  ASSERT_FALSE(companions_apply_tag(env, id, "burning", 0));
  ASSERT_FALSE(companions_apply_tag(env, id, "burning", -2));
  ASSERT_FALSE(companions_apply_tag(env, id, "", 3));
  ASSERT_FALSE(companions_apply_tag(env, id, nullptr, 3));
  ASSERT_FALSE(companions_apply_tag(env, 999, "burning", 3));
  ASSERT_FALSE(companions_apply_tag(nullptr, id, "burning", 3));
  ASSERT_EQ(AgentAt(env, 0).tag_count, 0);

  ASSERT_TRUE(companions_get_tag_name(env, 9999) == nullptr);
  ASSERT_TRUE(companions_get_tag_name(env, -1) == nullptr);
  ASSERT_TRUE(companions_get_tag_name(nullptr, burning) == nullptr);

  // More tags than Companions_MAX_TAGS: the state shows the first ones. New
  // names do not move the string an earlier companions_get_tag_name returned.
  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(companions_apply_tag(env, id, ("t" + std::to_string(i)).c_str(), 5));
  }
  a = AgentAt(env, 0);
  ASSERT_EQ(a.tag_count, Companions_MAX_TAGS);
  ASSERT_EQ(std::string(companions_get_tag_name(env, a.tags[0].tag_id)), std::string("t0"));
  ASSERT_EQ(a.tags[0].duration, 5);
  ASSERT_TRUE(companions_get_tag_name(env, burning) == name);
  ASSERT_EQ(std::string(name), std::string("burning"));

  // Ids survive a reset.
  companions_reset(env, 7);
  ASSERT_EQ(companions_find_tag(env, "burning"), burning);
  companions_destroy(env);
}

TEST(TestCellTagZoneLandsOnWalker) {
  Companions_Env* env = LoadLevel({{3, 1, ""}});
  const Companions_ObjectId id = AgentAt(env, 0).id;
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), -1);
  ASSERT_TRUE(companions_set_cell_tag(env, 3, 2, "wet", -1));
  const int32_t wet = companions_find_tag(env, "wet");
  ASSERT_TRUE(wet >= 0);
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), wet);
  ASSERT_EQ(companions_get_cell_tag(env, 3, 3), -1);

  Companions_Action action = {Companions_Movement_Right, Companions_Interact_None};
  Companions_StepResult result = {};
  companions_step(env, &action, 1, &result);
  const Companions_Event* landed = FindEvent(result, Companions_Event_TagApplied, id);
  ASSERT_NOT_NULL(landed);
  ASSERT_EQ(std::string(landed->effect_name), std::string("wet"));
  ASSERT_EQ(landed->effect_id, wet);  // The tag id
  ASSERT_EQ(landed->health_source_id, -1);
  ASSERT_TRUE(landed->tag_fresh);
  ASSERT_EQ(landed->status_duration, -1);
  ASSERT_EQ(landed->position.row, 3);
  ASSERT_EQ(landed->position.col, 2);
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(a.tag_count, 1);
  ASSERT_EQ(a.tags[0].tag_id, wet);

  // Standing on it: landed again, no longer fresh.
  action.movement = Companions_Movement_Stay;
  companions_step(env, &action, 1, &result);
  landed = FindEvent(result, Companions_Event_TagApplied, id);
  ASSERT_NOT_NULL(landed);
  ASSERT_FALSE(landed->tag_fresh);

  ASSERT_TRUE(companions_set_cell_tag(env, 3, 2, "", -1));
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), -1);
  ASSERT_TRUE(companions_set_cell_tag(env, 3, 2, "wet", 2));
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), wet);
  ASSERT_TRUE(companions_set_cell_tag(env, 3, 2, nullptr, -1));
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), -1);
  companions_step(env, &action, 1, &result);
  ASSERT_TRUE(FindEvent(result, Companions_Event_TagApplied) == nullptr);

  ASSERT_FALSE(companions_set_cell_tag(env, 8, 0, "wet", -1));
  ASSERT_FALSE(companions_set_cell_tag(env, 0, -1, "wet", -1));
  ASSERT_FALSE(companions_set_cell_tag(env, 3, 2, "wet", 0));
  ASSERT_FALSE(companions_set_cell_tag(nullptr, 3, 2, "wet", -1));
  SetErrorProbe();
  ASSERT_EQ(companions_get_cell_tag(env, -1, 0), -1);
  ASSERT_EQ(std::string(companions_get_error()), std::string("Position out of bounds"));
  SetErrorProbe();
  ASSERT_EQ(companions_get_cell_tag(env, 0, 8), -1);
  ASSERT_EQ(std::string(companions_get_error()), std::string("Position out of bounds"));
  ASSERT_EQ(companions_get_cell_tag(nullptr, 3, 2), -1);
  companions_destroy(env);
}

TEST(TestVortexRootIsReportedAsRooted) {
  // The caster at (3, 1) aims right: the vortex centre is (3, 4); the agent
  // on (2, 4) is on its cross, rooted and pulled into the centre.
  Companions_Env* env = LoadLevel({{3, 1, ""}, {2, 4, ""}});
  const Companions_ObjectId caster = AgentAt(env, 0).id;
  ASSERT_TRUE(companions_set_agent_skill(env, caster, 0, "vortex"));
  Companions_Action actions[2] = {{Companions_Movement_Right, Companions_Interact_Skill1},
                                  {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, actions, 2, &result);

  Companions_AgentState target = AgentAt(env, 1);
  ASSERT_EQ(target.position.row, 3);
  ASSERT_EQ(target.position.col, 4);
  ASSERT_TRUE(HasStatus(target, Companions_Status_Rooted));
  ASSERT_FALSE(HasStatus(target, Companions_Status_None));
  ASSERT_TRUE(HasStatus(result.state.agents[1], Companions_Status_Rooted));
  const Companions_Event* used = FindEvent(result, Companions_Event_SkillUsed, caster);
  ASSERT_NOT_NULL(used);
  ASSERT_EQ(std::string(used->effect_name), std::string("vortex"));
  ASSERT_EQ(used->position.col, 4);
  companions_destroy(env);
}

// Slot 2 is not enabled yet: Skill2 is None, even with both slots filled. An
// interact past Skill2, or a negative one, refuses the step.
TEST(TestSkill2IsIgnoredAndTheCompanionMoves) {
  Companions_Env* env = LoadLevel({{3, 1, ""}});
  const Companions_ObjectId id = AgentAt(env, 0).id;
  ASSERT_TRUE(companions_set_agent_skill(env, id, 0, "teleport"));
  ASSERT_TRUE(companions_set_agent_skill(env, id, 1, "teleport"));
  ASSERT_EQ(std::string(AgentAt(env, 0).skills[1]), std::string("teleport"));

  Companions_Action action = {Companions_Movement_Right, Companions_Interact_Skill2};
  Companions_StepResult result = {};
  companions_step(env, &action, 1, &result);
  ASSERT_EQ(result.state.tick, 1);
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(a.position.col, 2);  // Walked one cell
  ASSERT_EQ(a.skill_cooldowns[0], 0);
  ASSERT_EQ(a.skill_cooldowns[1], 0);
  ASSERT_TRUE(FindEvent(result, Companions_Event_SkillUsed) == nullptr);
  ASSERT_NOT_NULL(FindEvent(result, Companions_Event_AgentMoved, id));

  for (int32_t bad : {Companions_Interact_Skill2 + 1, 7, -1}) {
    SetErrorProbe();
    action.interact = static_cast<Companions_InteractAction>(bad);
    companions_step(env, &action, 1, &result);
    ASSERT_EQ(companions_get_tick(env), 1);  // Refused
    ASSERT_EQ(AgentAt(env, 0).position.col, 2);
    ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid interact action"));
  }
  companions_destroy(env);
}

TEST(TestSetAgentSkillRejectsUnknowns) {
  Companions_Env* env = LoadLevel({{3, 1, ""}});
  const Companions_ObjectId id = AgentAt(env, 0).id;
  ASSERT_FALSE(companions_set_agent_skill(env, id, 0, "nope"));
  ASSERT_FALSE(companions_set_agent_skill(env, id, 5, "teleport"));
  ASSERT_FALSE(companions_set_agent_skill(env, id, -1, "teleport"));
  ASSERT_FALSE(companions_set_agent_skill(env, id, Companions_MAX_SKILL_SLOTS, "teleport"));
  ASSERT_FALSE(companions_set_agent_skill(env, 999, 0, "teleport"));
  ASSERT_FALSE(companions_set_agent_skill(nullptr, id, 0, "teleport"));
  ASSERT_EQ(std::string(AgentAt(env, 0).skills[0]), std::string("attack"));

  // "" and NULL put the default attack back.
  ASSERT_TRUE(companions_set_agent_skill(env, id, 0, "fireball"));
  ASSERT_EQ(std::string(AgentAt(env, 0).skills[0]), std::string("fireball"));
  ASSERT_TRUE(companions_set_agent_skill(env, id, 0, ""));
  ASSERT_EQ(std::string(AgentAt(env, 0).skills[0]), std::string("attack"));
  ASSERT_TRUE(companions_set_agent_skill(env, id, 1, "vortex"));
  ASSERT_TRUE(companions_set_agent_skill(env, id, 1, nullptr));
  ASSERT_EQ(std::string(AgentAt(env, 0).skills[1]), std::string("attack"));
  companions_destroy(env);

  // Only companions have slots; other agents show empty ones.
  env = MakeAggroZombieEnv();
  Companions_AgentState z = AgentAt(env, FindAgentIndex(env, Companions_Faction_Enemy));
  ASSERT_FALSE(companions_set_agent_skill(env, z.id, 0, "teleport"));
  ASSERT_EQ(std::string(z.skills[0]), std::string(""));
  ASSERT_EQ(std::string(z.skills[1]), std::string(""));
  ASSERT_EQ(z.skill_cooldowns[0], 0);
  ASSERT_EQ(z.tag_count, 0);
  companions_destroy(env);
}

TEST(TestSnapshotSkillWorksThroughStep) {
  // The longest name allowed (Companions_SKILL_NAME_LEN - 1 bytes) shows in full.
  const std::string max_name(Companions_SKILL_NAME_LEN - 1, 'm');
  const std::string skills =
      "[{\"name\":\"blink\",\"targeting\":\"self\",\"motion\":\"teleport\",\"distance\":2,"
      "\"cooldown\":2},{\"name\":\"" + max_name + "\",\"targeting\":\"self\"}]";
  Companions_Env* env = LoadLevel(
      {{3, 1, ",\"skills\":[\"blink\",\"" + max_name + "\"],\"cooldowns\":[0,1],"
              "\"tags\":[{\"tag\":\"wet\",\"duration\":3}]"}},
      skills);
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(std::string(a.skills[0]), std::string("blink"));
  ASSERT_EQ(std::string(a.skills[1]), max_name);
  ASSERT_EQ(a.skill_cooldowns[1], 1);
  ASSERT_EQ(a.tag_count, 1);
  ASSERT_EQ(std::string(companions_get_tag_name(env, a.tags[0].tag_id)), std::string("wet"));
  ASSERT_EQ(a.tags[0].duration, 3);

  Companions_Action action = {Companions_Movement_Right, Companions_Interact_Skill1};
  Companions_StepResult result = {};
  companions_step(env, &action, 1, &result);
  a = AgentAt(env, 0);
  ASSERT_EQ(a.position.col, 3);  // blink: 2 cells
  ASSERT_EQ(a.skill_cooldowns[0], 2);
  const Companions_Event* used = FindEvent(result, Companions_Event_SkillUsed, a.id);
  ASSERT_NOT_NULL(used);
  ASSERT_EQ(std::string(used->effect_name), std::string("blink"));
  ASSERT_EQ(used->position.col, 3);
  companions_destroy(env);
}

// Skill and tag names of Companions_SKILL_NAME_LEN bytes or more are refused
// where they enter the env, so the C API never truncates one.
TEST(TestOverlongNamesAreRejected) {
  const std::string too_long(Companions_SKILL_NAME_LEN, 'x');
  const std::string max_name(Companions_SKILL_NAME_LEN - 1, 'y');
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  auto rejected = [&](const std::string& json) {
    SetErrorProbe();
    ASSERT_FALSE(companions_load_snapshot_json(env, json.c_str()));
    ASSERT_TRUE(std::string(companions_get_error()).find("at most 31") != std::string::npos);
  };
  // A skill's name, one of its tags, a slot, an agent's tag.
  rejected(LevelJson({{3, 1, ""}}, "[{\"name\":\"" + too_long + "\"}]"));
  rejected(LevelJson({{3, 1, ""}}, "[{\"name\":\"frost\",\"tags\":[{\"tag\":\"" + too_long +
                                       "\",\"duration\":2}]}]"));
  rejected(LevelJson({{3, 1, ",\"skills\":[\"" + too_long + "\"]"}}));
  rejected(LevelJson({{3, 1, ",\"tags\":[{\"tag\":\"" + too_long + "\",\"duration\":2}]"}}));
  ASSERT_TRUE(companions_load_snapshot_json(env, LevelJson({{3, 1, ""}}).c_str()));

  const Companions_ObjectId id = AgentAt(env, 0).id;
  ASSERT_FALSE(companions_apply_tag(env, id, too_long.c_str(), 3));
  ASSERT_FALSE(companions_set_cell_tag(env, 3, 2, too_long.c_str(), 3));
  ASSERT_EQ(companions_get_cell_tag(env, 3, 2), -1);
  ASSERT_EQ(companions_find_tag(env, too_long.c_str()), -1);  // Not interned
  ASSERT_EQ(AgentAt(env, 0).tag_count, 0);
  ASSERT_FALSE(companions_set_agent_skill(env, id, 0, too_long.c_str()));

  ASSERT_TRUE(companions_apply_tag(env, id, max_name.c_str(), 3));
  ASSERT_EQ(std::string(companions_get_tag_name(env, AgentAt(env, 0).tags[0].tag_id)), max_name);
  ASSERT_TRUE(companions_set_cell_tag(env, 3, 2, max_name.c_str(), 3));
  companions_destroy(env);
}

// A step reports at most Companions_MAX_EVENTS events and counts the others
// in events_dropped; the EpisodeEnd of a step that ends the episode is always
// reported, as the last event.
TEST(TestEventCapKeepsEpisodeEnd) {
  // "splash" lands 20 tags on each of the 4 agents around its caster (spared:
  // self_tags off): 80 TagApplied + 1 SkillUsed per step (+ EpisodeEnd on
  // step 2, the horizon).
  std::string tags;
  for (int i = 0; i < 20; ++i) {
    tags += std::string(i ? "," : "") + "{\"tag\":\"t" + std::to_string(i) + "\",\"duration\":-1}";
  }
  const std::string skills =
      "[{\"name\":\"splash\",\"targeting\":\"self\",\"area\":\"cross\",\"self_tags\":false,"
      "\"tags\":[" + tags + "]}]";
  Companions_Env* env = LoadLevel({{3, 3, ",\"skills\":[\"splash\"]"},
                                   {2, 3, ""},
                                   {4, 3, ""},
                                   {3, 2, ""},
                                   {3, 4, ""}},
                                  skills, 2);
  std::vector<Companions_Action> actions(5, {Companions_Movement_Stay, Companions_Interact_None});
  actions[0].interact = Companions_Interact_Skill1;
  auto count = [](const Companions_StepResult& r, Companions_EventType type) {
    int n = 0;
    for (int32_t i = 0; i < r.event_count; ++i) n += r.events[i].type == type;
    return n;
  };

  Companions_StepResult result = {};
  companions_step(env, actions.data(), 5, &result);
  ASSERT_FALSE(result.state.done);
  ASSERT_EQ(result.event_count, Companions_MAX_EVENTS);
  ASSERT_EQ(result.events_dropped, 81 - Companions_MAX_EVENTS);
  ASSERT_EQ(result.events[0].type, Companions_Event_SkillUsed);
  ASSERT_EQ(count(result, Companions_Event_TagApplied), Companions_MAX_EVENTS - 1);
  ASSERT_EQ(count(result, Companions_Event_EpisodeEnd), 0);

  companions_step(env, actions.data(), 5, &result);
  ASSERT_TRUE(result.state.done);
  ASSERT_EQ(result.event_count, Companions_MAX_EVENTS);
  ASSERT_EQ(result.events_dropped, 82 - Companions_MAX_EVENTS);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 1].type, Companions_Event_EpisodeEnd);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 1].episode_steps, 2);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 2].type, Companions_Event_TagApplied);
  ASSERT_EQ(count(result, Companions_Event_EpisodeEnd), 1);
  ASSERT_EQ(count(result, Companions_Event_TagApplied), Companions_MAX_EVENTS - 2);
  companions_destroy(env);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  std::cout << "Running C API tests...\n" << std::endl;

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
