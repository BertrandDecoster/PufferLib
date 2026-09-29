// Copyright 2024
// Test suite for the C API

#include <cstdio>
#include <cstring>
#include <fstream>
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
  // 1.2 removed the companion cast; 1.2.1 added companions_get_end_reason;
  // 1.2.2 made every timer tick at the end of a step; 1.3 added downs
  // (struct layouts changed); 1.4 added equipped_skills and AgentRevived
  // (struct layouts changed); 1.5 the rules as data; 1.6: only a team down
  // or the horizon fails a task
  ASSERT_EQ(std::string(version), std::string("1.6.0"));
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
static Companions_Env* MakeAggroZombieEnv(int horizon = 100) {
  Companions_AggroEnvConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.num_companions = 1;
  config.patrol_square_size = 3;
  config.horizon = horizon;
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

// A slot naming a skill neither builtin nor in the level is refused, with a
// message naming the agent and the skill.
TEST(TestSnapshotUnknownSlotSkillIsRejected) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  SetErrorProbe();
  ASSERT_FALSE(companions_load_snapshot_json(
      env, LevelJson({{3, 1, ",\"skills\":[\"meteor\"]"}}).c_str()));
  const std::string error = companions_get_error();
  ASSERT_TRUE(error.find("agent #0") != std::string::npos);
  ASSERT_TRUE(error.find("meteor") != std::string::npos);
  ASSERT_TRUE(companions_load_snapshot_json(
      env, LevelJson({{3, 1, ",\"skills\":[\"meteor\"]"}}, "[{\"name\":\"meteor\"}]").c_str()));
  companions_destroy(env);
}

static int CountEvents(const Companions_StepResult& r, Companions_EventType type) {
  int n = 0;
  for (int32_t i = 0; i < r.event_count; ++i) n += r.events[i].type == type;
  return n;
}

// A step reports at most Companions_MAX_EVENTS events and counts the others
// in events_dropped; the EpisodeEnd of a step that ends the episode is always
// reported, as the last event, and an AgentDowned comes before the skill and
// tag events, so a down survives the cap too.
TEST(TestEventCapKeepsEpisodeEnd) {
  // "splash" lands 20 tags on each of the 4 agents around its caster (spared:
  // self_tags off): 80 TagApplied + 1 SkillUsed per step (+ on step 2, the
  // horizon, the down of the 6th companion, away in a corner, and EpisodeEnd).
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
                                   {3, 4, ""},
                                   {1, 1, ""}},
                                  skills, 2);
  std::vector<Companions_Action> actions(6, {Companions_Movement_Stay, Companions_Interact_None});
  actions[0].interact = Companions_Interact_Skill1;

  Companions_StepResult result = {};
  companions_step(env, actions.data(), 6, &result);
  ASSERT_FALSE(result.state.done);
  ASSERT_EQ(result.event_count, Companions_MAX_EVENTS);
  ASSERT_EQ(result.events_dropped, 81 - Companions_MAX_EVENTS);
  ASSERT_EQ(result.events[0].type, Companions_Event_SkillUsed);
  ASSERT_EQ(CountEvents(result, Companions_Event_TagApplied), Companions_MAX_EVENTS - 1);
  ASSERT_EQ(CountEvents(result, Companions_Event_EpisodeEnd), 0);

  // The 6th companion goes down between the steps: step 2 reports it
  const Companions_AgentState corner = AgentAt(env, 5);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", corner.position.row, corner.position.col,
                                      Companions_Direction_Up, -1));
  companions_step(env, actions.data(), 6, &result);
  ASSERT_TRUE(result.state.done);
  ASSERT_FALSE(result.state.team_down);  // 1 down of 3
  ASSERT_EQ(result.event_count, Companions_MAX_EVENTS);
  ASSERT_EQ(result.events_dropped, 83 - Companions_MAX_EVENTS);
  ASSERT_EQ(result.events[0].type, Companions_Event_AgentDowned);
  ASSERT_EQ(result.events[0].subject_id, corner.id);
  ASSERT_EQ(result.events[1].type, Companions_Event_SkillUsed);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 1].type, Companions_Event_EpisodeEnd);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 1].episode_steps, 2);
  ASSERT_EQ(result.events[Companions_MAX_EVENTS - 2].type, Companions_Event_TagApplied);
  ASSERT_EQ(CountEvents(result, Companions_Event_EpisodeEnd), 1);
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentDowned), 1);
  ASSERT_EQ(CountEvents(result, Companions_Event_TagApplied), Companions_MAX_EVENTS - 3);
  companions_destroy(env);
}

static int CountEpisodeEnds(const Companions_StepResult& r) {
  int n = 0;
  for (int32_t i = 0; i < r.event_count; ++i) n += r.events[i].type == Companions_Event_EpisodeEnd;
  return n;
}

// EpisodeEnd is reported once, on the step where the episode became done; the
// steps a host keeps playing afterwards do not repeat it. Reset and loading a
// snapshot start a new episode that reports its own end.
TEST(TestEpisodeEndIsReportedOnce) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  config.horizon = 2;
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  Companions_StepResult result = {};
  auto play_to_horizon = [&]() {
    companions_step(env, &stay, 1, &result);
    ASSERT_FALSE(result.state.done);
    ASSERT_EQ(CountEpisodeEnds(result), 0);
    companions_step(env, &stay, 1, &result);
    ASSERT_TRUE(result.state.done);
    ASSERT_EQ(CountEpisodeEnds(result), 1);
    for (int i = 0; i < 2; ++i) {  // Playing on past the end
      companions_step(env, &stay, 1, &result);
      ASSERT_TRUE(result.state.done);
      ASSERT_EQ(CountEpisodeEnds(result), 0);
    }
  };
  play_to_horizon();
  companions_reset(env, 42);
  play_to_horizon();
  companions_destroy(env);

  env = LoadLevel({{3, 1, ""}}, "[]", 2);
  play_to_horizon();
  ASSERT_TRUE(companions_load_snapshot_json(env, LevelJson({{3, 1, ""}}, "[]", 2).c_str()));
  play_to_horizon();
  companions_destroy(env);
}

// A lens change starts a new episode (as reset does), which reports its own
// end: here it is done at once (still at the horizon), so the next step does.
TEST(TestLensChangeReportsTheNextEpisodeEnd) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  config.horizon = 2;
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  Companions_StepResult result = {};
  companions_step(env, &stay, 1, &result);
  companions_step(env, &stay, 1, &result);
  ASSERT_EQ(CountEpisodeEnds(result), 1);
  companions_step(env, &stay, 1, &result);
  ASSERT_EQ(CountEpisodeEnds(result), 0);  // Playing on
  for (int round = 0; round < 2; ++round) {
    const bool with_params = round == 1;
    ASSERT_TRUE(with_params
                    ? companions_set_task_lens_with_params(env, Companions_Lens_Synchro, nullptr, 0)
                    : companions_set_task_lens(env, Companions_Lens_Synchro));
    companions_step(env, &stay, 1, &result);
    ASSERT_TRUE(result.state.done);
    ASSERT_EQ(CountEpisodeEnds(result), 1);
    companions_step(env, &stay, 1, &result);
    ASSERT_EQ(CountEpisodeEnds(result), 0);
  }
  companions_destroy(env);
}

// companions_get_end_reason says why the episode ended (None while it runs),
// and the EpisodeEnd event carries the same reason in effect_id.
static void ExpectEnd(Companions_Env* env, const Companions_StepResult& result,
                      Companions_EndReason reason) {
  ASSERT_TRUE(companions_is_done(env));
  ASSERT_EQ(companions_get_end_reason(env), reason);
  const Companions_Event* end = FindEvent(result, Companions_Event_EpisodeEnd);
  ASSERT_NOT_NULL(end);
  ASSERT_EQ(end->effect_id, static_cast<int32_t>(reason));
  ASSERT_EQ(end->episode_success, reason == Companions_End_Success);
}

TEST(TestEndReasonSuccess) {
  Companions_Env* env = LoadLevel({{6, 5, ""}});  // Next to the goal (6, 6)
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  Companions_Action right = {Companions_Movement_Right, Companions_Interact_None};
  Companions_StepResult result = {};
  companions_step(env, &right, 1, &result);
  ExpectEnd(env, result, Companions_End_Success);
  companions_destroy(env);
}

TEST(TestEndReasonHorizon) {
  Companions_Env* env = LoadLevel({{3, 1, ""}}, "[]", 2);
  Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  Companions_StepResult result = {};
  companions_step(env, &stay, 1, &result);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  companions_step(env, &stay, 1, &result);
  ExpectEnd(env, result, Companions_End_Horizon);
  companions_step(env, &stay, 1, &result);  // Playing on keeps the reason
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  ASSERT_TRUE(companions_load_snapshot_json(env, LevelJson({{3, 1, ""}}, "[]", 2).c_str()));
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  companions_destroy(env);
}

// A dead Aggro enemy fails nothing (since 1.6, only a team down or the
// horizon fails a task): the episode runs on to the horizon, which ends it.
TEST(TestEndReasonAggroEnemyDead) {
  Companions_Env* env = MakeAggroZombieEnv(3);
  const int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  ASSERT_TRUE(zi >= 0);
  Companions_AgentState z = AgentAt(env, zi);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", z.position.row, z.position.col,
                                      Companions_Direction_Up, -1));
  ASSERT_FALSE(companions_is_done(env));
  const int32_t n = companions_get_agent_count(env);
  std::vector<Companions_Action> stay(n, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  for (int i = 0; i < 2; ++i) {
    companions_step(env, stay.data(), n, &result);
    ASSERT_FALSE(AgentAt(env, zi).alive);
    ASSERT_FALSE(companions_is_done(env));
    ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
    ASSERT_EQ(CountEpisodeEnds(result), 0);
  }
  companions_step(env, stay.data(), n, &result);
  ExpectEnd(env, result, Companions_End_Horizon);
  companions_step(env, stay.data(), n, &result);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  companions_reset(env, 42);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  ASSERT_EQ(companions_get_end_reason(nullptr), Companions_End_None);
  companions_destroy(env);
}

// An Aggro reset starts with no end reason: the first step's end is its own
// (here the horizon), whatever the level load saw before the enemy spawned.
TEST(TestEndReasonAfterAnAggroResetIsTheStepsOwn) {
  Companions_Env* env = MakeAggroZombieEnv(1);  // Reset once already
  const int32_t n = companions_get_agent_count(env);
  std::vector<Companions_Action> stay(n, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  for (int i = 0; i < 2; ++i) {
    companions_reset(env, 42);
    ASSERT_FALSE(companions_is_done(env));
    ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
    companions_step(env, stay.data(), n, &result);
    ExpectEnd(env, result, Companions_End_Horizon);
  }
  companions_destroy(env);
}

// A snapshot loaded at the horizon is done at once, as Horizon (as the env
// says), on every load path; the next step reports its EpisodeEnd.
TEST(TestSnapshotLoadedAtTheHorizonIsDone) {
  Companions_Env* env = LoadLevel({{3, 1, ""}}, "[]", 2);
  std::string json = LevelJson({{3, 1, ""}}, "[]", 2);
  const std::string tick0 = "\"tick\":0,\"horizon\"";
  const size_t at = json.find(tick0);
  ASSERT_TRUE(at != std::string::npos);
  json.replace(at, tick0.size(), "\"tick\":2,\"horizon\"");
  Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  Companions_StepResult result = {};
  auto expect_done_at_horizon = [&]() {
    ASSERT_TRUE(companions_is_done(env));
    ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
    Companions_GameState state = {};
    companions_get_state(env, &state);
    ASSERT_TRUE(state.done);
    ASSERT_FALSE(state.success);
    companions_step(env, &stay, 1, &result);
    ExpectEnd(env, result, Companions_End_Horizon);
    ASSERT_EQ(CountEpisodeEnds(result), 1);
  };

  // JSON string
  ASSERT_TRUE(companions_load_snapshot_json(env, json.c_str()));
  expect_done_at_horizon();

  // Binary buffer (saved from a state loaded at the horizon)
  ASSERT_TRUE(companions_load_snapshot_json(env, json.c_str()));
  const int32_t size = companions_get_snapshot_size(env);
  ASSERT_TRUE(size > 0);
  std::vector<uint8_t> buffer(size);
  ASSERT_TRUE(companions_save_snapshot(env, buffer.data(), size));
  ASSERT_TRUE(companions_load_snapshot_json(env, LevelJson({{3, 1, ""}}, "[]", 2).c_str()));
  ASSERT_FALSE(companions_is_done(env));
  ASSERT_TRUE(companions_load_snapshot(env, buffer.data(), size));
  expect_done_at_horizon();

  // JSON file
  const std::string path = "test_api_snapshot_at_horizon.json";
  {
    std::ofstream out(path);
    out << json;
  }
  ASSERT_TRUE(companions_load_snapshot_json_file(env, path.c_str()));
  std::remove(path.c_str());
  expect_done_at_horizon();

  // A reset starts over
  companions_reset(env, 42);
  ASSERT_FALSE(companions_is_done(env));
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  companions_destroy(env);
}

// A lens change after a kill ends nothing (no lens fails on a dead enemy):
// the episode runs on to the horizon, whose step reports EpisodeEnd once.
TEST(TestALensChangeAfterAKillEndsNothing) {
  Companions_Env* env = MakeAggroZombieEnv(3);
  const int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  ASSERT_TRUE(zi >= 0);
  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Dodge));
  Companions_AgentState z = AgentAt(env, zi);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", z.position.row, z.position.col,
                                      Companions_Direction_Up, -1));
  const int32_t n = companions_get_agent_count(env);
  std::vector<Companions_Action> stay(n, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  companions_step(env, stay.data(), n, &result);
  ASSERT_FALSE(AgentAt(env, zi).alive);
  ASSERT_FALSE(companions_is_done(env));  // Dodge does not end on a dead enemy
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);

  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Aggro));
  ASSERT_FALSE(companions_is_done(env));
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  companions_step(env, stay.data(), n, &result);
  ASSERT_FALSE(companions_is_done(env));
  ASSERT_EQ(CountEpisodeEnds(result), 0);
  companions_step(env, stay.data(), n, &result);
  ExpectEnd(env, result, Companions_End_Horizon);
  ASSERT_EQ(CountEpisodeEnds(result), 1);
  companions_step(env, stay.data(), n, &result);
  ASSERT_EQ(CountEpisodeEnds(result), 0);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  companions_destroy(env);
}

// The reason is fixed when the episode ends: a kill after the horizon (a host
// playing on) leaves it Horizon.
TEST(TestEndReasonKeepsHorizonAfterAKill) {
  Companions_Env* env = MakeAggroZombieEnv(3);
  const int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  ASSERT_TRUE(zi >= 0);
  const int32_t n = companions_get_agent_count(env);
  std::vector<Companions_Action> stay(n, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  for (int i = 0; i < 2; ++i) {
    companions_step(env, stay.data(), n, &result);
    ASSERT_FALSE(companions_is_done(env));
  }
  companions_step(env, stay.data(), n, &result);
  ExpectEnd(env, result, Companions_End_Horizon);

  Companions_AgentState z = AgentAt(env, zi);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", z.position.row, z.position.col,
                                      Companions_Direction_Up, -1));
  for (int i = 0; i < 2; ++i) {
    companions_step(env, stay.data(), n, &result);
    ASSERT_FALSE(AgentAt(env, zi).alive);
    ASSERT_TRUE(companions_is_done(env));
    ASSERT_EQ(CountEpisodeEnds(result), 0);
    ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  }
  companions_destroy(env);
}

// A companion at 0 HP goes down: its state says so, the step reports it, and
// the team's downs count it. The level is lost at max_downs (TeamDown).
TEST(TestDownsThroughTheApi) {
  Companions_EnvConfig config = MakeConfig(8, 8, 2, 1, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  Companions_GameState state = {};
  companions_get_state(env, &state);
  ASSERT_EQ(state.downs, 0);
  ASSERT_EQ(state.max_downs, 3);
  ASSERT_FALSE(state.agents[0].downed);
  const Companions_ObjectId id = state.agents[0].id;
  const Companions_Position cell = state.agents[0].position;
  // "kill" on its cell (the host primitive): it goes down instead
  ASSERT_TRUE(companions_spawn_effect(env, "kill", cell.row, cell.col,
                                      Companions_Direction_Up, -1));
  companions_get_state(env, &state);  // Between steps, the state already says so
  ASSERT_TRUE(state.agents[0].downed);
  ASSERT_EQ(state.downs, 1);
  // No tag lands on the downed, not even the host's (and none is interned)
  ASSERT_FALSE(companions_apply_tag(env, id, "blessed", 2));
  ASSERT_EQ(companions_find_tag(env, "blessed"), -1);

  Companions_Action stay[2] = {{Companions_Movement_Stay, Companions_Interact_None},
                               {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, stay, 2, &result);
  ASSERT_TRUE(result.state.agents[0].downed);
  ASSERT_TRUE(result.state.agents[0].alive);
  ASSERT_FALSE(result.state.agents[1].downed);
  ASSERT_EQ(result.state.downs, 1);
  ASSERT_EQ(result.state.max_downs, 3);
  ASSERT_FALSE(result.state.team_down);  // One down of two, one of three
  ASSERT_FALSE(result.state.done);
  // The step after the down reports it, once
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentDowned), 1);
  const Companions_Event* down = FindEvent(result, Companions_Event_AgentDowned);
  ASSERT_NOT_NULL(down);
  ASSERT_EQ(down->subject_id, id);
  ASSERT_EQ(down->position.row, cell.row);
  ASSERT_EQ(down->position.col, cell.col);
  Companions_AgentState agent = {};
  ASSERT_TRUE(companions_get_agent(env, id, &agent));
  ASSERT_TRUE(agent.downed);

  companions_step(env, stay, 2, &result);
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentDowned), 0);
  ASSERT_EQ(result.state.downs, 1);
  companions_destroy(env);
}

// Every companion down at once loses the level: done, as TeamDown, and the
// AgentDowned event comes before the EpisodeEnd that carries the reason.
TEST(TestTeamDownThroughTheApi) {
  Companions_EnvConfig config = MakeConfig(8, 8, 1, 1, 42);
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", a.position.row, a.position.col,
                                      Companions_Direction_Up, -1));
  Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  Companions_StepResult result = {};
  companions_step(env, &stay, 1, &result);
  ASSERT_TRUE(result.state.done);
  ASSERT_FALSE(result.state.success);
  ASSERT_EQ(result.state.downs, 1);
  ExpectEnd(env, result, Companions_End_TeamDown);
  ASSERT_TRUE(result.event_count >= 2);
  ASSERT_EQ(result.events[result.event_count - 1].type, Companions_Event_EpisodeEnd);
  ASSERT_EQ(result.events[result.event_count - 2].type, Companions_Event_AgentDowned);
  ASSERT_EQ(result.events[result.event_count - 2].subject_id, a.id);
  ASSERT_TRUE(result.state.team_down);
  companions_destroy(env);
}

// team_down is the live verdict: a team that goes down after the episode
// ended (here at its horizon, a host playing on) says so, while the end
// reason keeps the first one (Horizon).
TEST(TestTeamDownIsLiveAfterTheHorizon) {
  Companions_Env* env = MakeAggroZombieEnv(2);
  const int32_t n = companions_get_agent_count(env);
  std::vector<Companions_Action> stay(n, {Companions_Movement_Stay, Companions_Interact_None});
  Companions_StepResult result = {};
  companions_step(env, stay.data(), n, &result);
  ASSERT_FALSE(result.state.team_down);
  companions_step(env, stay.data(), n, &result);
  ExpectEnd(env, result, Companions_End_Horizon);
  ASSERT_FALSE(result.state.team_down);

  int32_t companions = 0;
  for (int32_t i = 0; i < n; ++i) {
    Companions_AgentState a = AgentAt(env, i);
    if (a.faction != Companions_Faction_Companion) continue;
    ++companions;
    ASSERT_TRUE(companions_spawn_effect(env, "kill", a.position.row, a.position.col,
                                        Companions_Direction_Up, -1));
  }
  ASSERT_TRUE(companions > 0);
  Companions_GameState state = {};
  companions_get_state(env, &state);  // Between steps, already
  ASSERT_TRUE(state.team_down);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  for (int i = 0; i < 2; ++i) {
    companions_step(env, stay.data(), n, &result);
    ASSERT_TRUE(result.state.team_down);
    ASSERT_TRUE(result.state.done);
    ASSERT_EQ(CountEpisodeEnds(result), 0);
    ASSERT_EQ(companions_get_end_reason(env), Companions_End_Horizon);
  }
  companions_destroy(env);
}

// The index of the first event of `type` about `subject`, or -1.
static int EventIndex(const Companions_StepResult& r, Companions_EventType type,
                      Companions_ObjectId subject) {
  for (int32_t i = 0; i < r.event_count; ++i) {
    if (r.events[i].type == type && r.events[i].subject_id == subject) return i;
  }
  return -1;
}

// A slot reports its effective skill (skills: what Skill1 uses now) and its
// equipped one (equipped_skills: what companions_set_agent_skill wrote): next
// to a downed ally, slot 0 is revive; away from it, the equipped skill. The
// cooldowns stay the equipped skills'.
TEST(TestEffectiveAndEquippedSkills) {
  // A (3,3) next to B (3,4); C (6,1) away from both
  Companions_Env* env = LoadLevel({{3, 3, ",\"skills\":[\"fireball\",\"teleport\"]"},
                                   {3, 4, ""},
                                   {6, 1, ""}});
  Companions_AgentState a = AgentAt(env, 0);
  ASSERT_EQ(std::string(a.skills[0]), std::string("fireball"));
  ASSERT_EQ(std::string(a.equipped_skills[0]), std::string("fireball"));
  ASSERT_EQ(std::string(a.skills[1]), std::string("teleport"));
  ASSERT_EQ(std::string(a.equipped_skills[1]), std::string("teleport"));

  const Companions_AgentState b = AgentAt(env, 1);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", b.position.row, b.position.col,
                                      Companions_Direction_Up, -1));
  ASSERT_TRUE(AgentAt(env, 1).downed);
  // Between steps already: slot 0 is revive, slot 1 keeps its skill
  a = AgentAt(env, 0);
  ASSERT_EQ(std::string(a.skills[0]), std::string("revive"));
  ASSERT_EQ(std::string(a.equipped_skills[0]), std::string("fireball"));
  ASSERT_EQ(std::string(a.skills[1]), std::string("teleport"));
  ASSERT_EQ(std::string(a.equipped_skills[1]), std::string("teleport"));
  // Away from the downed: the equipped skill
  const Companions_AgentState c = AgentAt(env, 2);
  ASSERT_EQ(std::string(c.skills[0]), std::string("attack"));
  ASSERT_EQ(std::string(c.equipped_skills[0]), std::string("attack"));

  // The cooldown is the equipped skill's: A casts fireball away from B, then
  // comes back next to it; the cooldown shows while slot 0 is revive
  Companions_Action act[3] = {{Companions_Movement_Left, Companions_Interact_None},
                              {Companions_Movement_Stay, Companions_Interact_None},
                              {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, act, 3, &result);  // A walks away to (3,2)
  a = result.state.agents[0];
  ASSERT_EQ(a.position.col, 2);
  ASSERT_EQ(std::string(a.skills[0]), std::string("fireball"));
  act[0] = {Companions_Movement_Up, Companions_Interact_Skill1};
  companions_step(env, act, 3, &result);  // Fireball, up
  ASSERT_NOT_NULL(FindEvent(result, Companions_Event_SkillUsed, a.id));
  const int32_t cooldown = result.state.agents[0].skill_cooldowns[0];
  ASSERT_TRUE(cooldown > 1);
  act[0] = {Companions_Movement_Right, Companions_Interact_None};
  companions_step(env, act, 3, &result);  // Back next to B
  a = result.state.agents[0];
  ASSERT_EQ(a.position.col, 3);
  ASSERT_EQ(std::string(a.skills[0]), std::string("revive"));
  ASSERT_EQ(std::string(a.equipped_skills[0]), std::string("fireball"));
  ASSERT_EQ(a.skill_cooldowns[0], cooldown - 1);

  // Other agents: "" (no slots)
  Companions_Env* aggro = MakeAggroZombieEnv();
  Companions_AgentState z = {};
  ASSERT_TRUE(companions_get_agent_by_index(
      aggro, FindAgentIndex(aggro, Companions_Faction_Enemy), &z));
  ASSERT_EQ(std::string(z.skills[0]), std::string(""));
  ASSERT_EQ(std::string(z.equipped_skills[0]), std::string(""));
  companions_destroy(aggro);
  companions_destroy(env);
}

// Reviving through the API: Skill1 next to a downed ally uses revive. The
// step reports the ally's down (between steps), then AgentRevived (subject =
// the revived, health_source_id = the reviver, health_new = the HP it came
// back with, position = its cell), then the SkillUsed; the state has it
// standing, at half its max HP rounded up.
TEST(TestReviveThroughTheApi) {
  Companions_Env* env = LoadLevel({{3, 3, ",\"skills\":[\"fireball\"]"}, {3, 4, ""}});
  const Companions_AgentState a = AgentAt(env, 0);
  const Companions_AgentState b = AgentAt(env, 1);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", b.position.row, b.position.col,
                                      Companions_Direction_Up, -1));
  Companions_Action act[2] = {{Companions_Movement_Right, Companions_Interact_Skill1},
                              {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, act, 2, &result);

  const int downed = EventIndex(result, Companions_Event_AgentDowned, b.id);
  const int revived = EventIndex(result, Companions_Event_AgentRevived, b.id);
  const int used = EventIndex(result, Companions_Event_SkillUsed, a.id);
  ASSERT_TRUE(downed >= 0);
  ASSERT_TRUE(revived > downed);
  ASSERT_TRUE(used > revived);
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentRevived), 1);
  const Companions_Event& e = result.events[revived];
  ASSERT_EQ(e.health_source_id, a.id);
  ASSERT_EQ(e.health_new, 2);  // ceil(3 * 50%)
  ASSERT_EQ(e.health_amount, 2);  // Gained from 0
  ASSERT_EQ(e.position.row, 3);
  ASSERT_EQ(e.position.col, 4);
  ASSERT_EQ(e.tick, 1);
  ASSERT_EQ(std::string(result.events[used].effect_name), std::string("revive"));
  ASSERT_EQ(result.events[used].effect_id, 0);

  const Companions_AgentState& rb = result.state.agents[1];
  ASSERT_FALSE(rb.downed);
  ASSERT_TRUE(rb.alive);
  ASSERT_EQ(rb.health, 2);
  ASSERT_EQ(result.state.downs, 1);  // A revive does not undo the down
  // No downed ally left: A's slot 0 is its fireball again, still ready
  ASSERT_EQ(std::string(result.state.agents[0].skills[0]), std::string("fireball"));
  ASSERT_EQ(result.state.agents[0].skill_cooldowns[0], 0);

  act[0] = {Companions_Movement_Stay, Companions_Interact_None};
  companions_step(env, act, 2, &result);
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentRevived), 0);
  companions_destroy(env);
}

// A downed companion has no context skill: lying next to a downed ally, it
// shows its equipped skill.
TEST(TestADownedCompanionShowsItsEquippedSkill) {
  Companions_Env* env = LoadLevel({{3, 3, ",\"skills\":[\"fireball\"]"}, {3, 4, ""}});
  for (int32_t i = 1; i >= 0; --i) {
    const Companions_AgentState x = AgentAt(env, i);
    ASSERT_TRUE(companions_spawn_effect(env, "kill", x.position.row, x.position.col,
                                        Companions_Direction_Up, -1));
    if (i == 1) ASSERT_EQ(std::string(AgentAt(env, 0).skills[0]), std::string("revive"));
  }
  const Companions_AgentState a = AgentAt(env, 0);
  ASSERT_TRUE(a.downed);
  ASSERT_EQ(std::string(a.skills[0]), std::string("fireball"));
  ASSERT_EQ(std::string(a.equipped_skills[0]), std::string("fireball"));
  const Companions_AgentState b = AgentAt(env, 1);
  ASSERT_EQ(std::string(b.skills[0]), std::string("attack"));
  companions_destroy(env);
}

// Revived and downed again in the same step (two strikes wound up before the
// step land in its effect tick, after the revive): the step reports both
// downs, grouped before the AgentRevived (events are grouped by kind); the
// state has it down with both downs counted.
TEST(TestRevivedAndDownedAgainInOneStepThroughTheApi) {
  Companions_Env* env = LoadLevel({{3, 3, ""}, {3, 4, ""}});
  const Companions_AgentState a = AgentAt(env, 0);
  const Companions_AgentState b = AgentAt(env, 1);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", 3, 4, Companions_Direction_Up, -1));
  // goblin_attack: a 1-step wind-up, then 1 damage; two of them take the
  // revived companion's 2 HP
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(companions_spawn_effect(env, "goblin_attack", 3, 4, Companions_Direction_Up, -1));
  }
  Companions_Action act[2] = {{Companions_Movement_Right, Companions_Interact_Skill1},
                              {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, act, 2, &result);

  ASSERT_EQ(CountEvents(result, Companions_Event_AgentDowned), 2);
  ASSERT_EQ(CountEvents(result, Companions_Event_AgentRevived), 1);
  const int revived = EventIndex(result, Companions_Event_AgentRevived, b.id);
  ASSERT_TRUE(revived >= 0);
  ASSERT_EQ(result.events[revived].health_source_id, a.id);
  ASSERT_EQ(result.events[revived].health_new, 2);
  for (int32_t i = 0; i < result.event_count; ++i) {
    if (result.events[i].type != Companions_Event_AgentDowned) continue;
    ASSERT_EQ(result.events[i].subject_id, b.id);
    ASSERT_TRUE(i < revived);  // The second down too
  }
  const Companions_AgentState& rb = result.state.agents[1];
  ASSERT_TRUE(rb.downed);
  ASSERT_EQ(rb.health, 0);
  ASSERT_EQ(result.state.downs, 2);
  companions_destroy(env);
}

// =============================================================================
// The skill book (companions_get_skill_count / _get_skill / _find_skill)
// =============================================================================

// The skill named `name`, found through the API (fails the test if unknown).
static Companions_SkillInfo SkillNamed(const Companions_Env* env, const char* name) {
  Companions_SkillInfo info = {};
  if (!companions_find_skill(env, name, &info)) {
    throw std::runtime_error(std::string("skill not found: ") + name + ": " +
                             companions_get_error());
  }
  return info;
}

// A fresh env's book: the builtins, in the book's order, "attack" and
// "revive" included, with every field of their SkillConfig.
TEST(TestTheSkillBookHoldsTheBuiltins) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  const char* names[] = {"fireball", "lightningStep", "teleport", "vortex", "revive", "attack"};
  ASSERT_EQ(companions_get_skill_count(env), 6);
  for (int32_t i = 0; i < 6; ++i) {
    Companions_SkillInfo by_index = {};
    ASSERT_TRUE(companions_get_skill(env, i, &by_index));
    ASSERT_EQ(std::string(by_index.name), std::string(names[i]));
    const Companions_SkillInfo by_name = SkillNamed(env, names[i]);
    ASSERT_TRUE(std::memcmp(&by_index, &by_name, sizeof(by_name)) == 0);
  }

  const Companions_SkillInfo attack = SkillNamed(env, "attack");
  ASSERT_EQ(attack.targeting, Companions_SkillTargeting_Projectile);
  ASSERT_EQ(attack.range, 1);
  ASSERT_EQ(attack.filter, Companions_TargetFilter_All);
  ASSERT_EQ(attack.area, Companions_SkillArea_Single);
  ASSERT_EQ(attack.motion, Companions_SkillMotion_None);
  ASSERT_EQ(attack.tag_count, 0);
  ASSERT_EQ(attack.damage, 1);
  ASSERT_EQ(attack.cooldown, 0);
  ASSERT_FALSE(attack.friendly_fire);
  ASSERT_FALSE(attack.affects_downed);
  ASSERT_EQ(attack.revive_percent, 0);

  const Companions_SkillInfo revive = SkillNamed(env, "revive");
  ASSERT_EQ(revive.targeting, Companions_SkillTargeting_Projectile);
  ASSERT_EQ(revive.range, 1);
  ASSERT_EQ(revive.filter, Companions_TargetFilter_Companion);
  ASSERT_TRUE(revive.affects_downed);
  ASSERT_EQ(revive.revive_percent, 50);
  ASSERT_TRUE(revive.friendly_fire);
  ASSERT_EQ(revive.damage, 0);
  ASSERT_EQ(revive.cooldown, 0);

  const Companions_SkillInfo fireball = SkillNamed(env, "fireball");
  ASSERT_EQ(fireball.targeting, Companions_SkillTargeting_Ground);
  ASSERT_EQ(fireball.range, 3);
  ASSERT_EQ(fireball.area, Companions_SkillArea_Cross);
  ASSERT_EQ(fireball.motion, Companions_SkillMotion_PushOut);
  ASSERT_EQ(fireball.motion_distance, 1);
  ASSERT_EQ(fireball.tag_count, 1);
  ASSERT_EQ(std::string(fireball.tags[0].tag), std::string("burning"));
  ASSERT_EQ(fireball.tags[0].duration, -1);
  ASSERT_EQ(fireball.cooldown, 3);

  const Companions_SkillInfo dash = SkillNamed(env, "lightningStep");
  ASSERT_EQ(dash.targeting, Companions_SkillTargeting_Self);
  ASSERT_EQ(dash.motion, Companions_SkillMotion_Dash);
  ASSERT_EQ(dash.motion_distance, 4);
  ASSERT_TRUE(dash.tag_path);
  ASSERT_FALSE(dash.self_tags);
  ASSERT_TRUE(dash.self_motion);

  const Companions_SkillInfo vortex = SkillNamed(env, "vortex");
  ASSERT_EQ(vortex.motion, Companions_SkillMotion_PullIn);
  ASSERT_EQ(vortex.root_steps, 1);
  ASSERT_FALSE(vortex.self_root);
  ASSERT_TRUE(vortex.self_damage);

  ASSERT_EQ(SkillNamed(env, "teleport").motion, Companions_SkillMotion_Teleport);
  companions_destroy(env);
}

// A level's book: its own skills after the builtins, a retuned builtin in
// place with the level's values; the next load without skills is back to the
// builtins.
TEST(TestTheSkillBookFollowsTheLoadedLevel) {
  const std::string skills =
      "[{\"name\":\"frost\",\"range\":2,\"filter\":\"enemy\",\"tags\":[{\"tag\":\"chilled\","
      "\"duration\":2},{\"tag\":\"slowish\"}],\"damage\":1,\"cooldown\":2,\"friendly_fire\":false},"
      "{\"name\":\"revive\",\"range\":2,\"filter\":\"companion\",\"affects_downed\":true,"
      "\"revive_percent\":100}]";
  Companions_Env* env = LoadLevel({{3, 3, ""}}, skills);
  ASSERT_EQ(companions_get_skill_count(env), 7);
  Companions_SkillInfo last = {};
  ASSERT_TRUE(companions_get_skill(env, 6, &last));
  ASSERT_EQ(std::string(last.name), std::string("frost"));
  ASSERT_EQ(last.targeting, Companions_SkillTargeting_Projectile);
  ASSERT_EQ(last.range, 2);
  ASSERT_EQ(last.filter, Companions_TargetFilter_Enemy);
  ASSERT_EQ(last.tag_count, 2);
  ASSERT_EQ(std::string(last.tags[0].tag), std::string("chilled"));
  ASSERT_EQ(last.tags[0].duration, 2);
  ASSERT_EQ(std::string(last.tags[1].tag), std::string("slowish"));
  ASSERT_EQ(last.tags[1].duration, -1);
  ASSERT_EQ(last.damage, 1);
  ASSERT_EQ(last.cooldown, 2);
  ASSERT_FALSE(last.friendly_fire);

  // The retuned revive keeps its place in the book.
  Companions_SkillInfo fifth = {};
  ASSERT_TRUE(companions_get_skill(env, 4, &fifth));
  ASSERT_EQ(std::string(fifth.name), std::string("revive"));
  ASSERT_EQ(fifth.range, 2);
  ASSERT_EQ(fifth.revive_percent, 100);
  ASSERT_TRUE(fifth.affects_downed);

  ASSERT_TRUE(companions_load_snapshot_json(env, LevelJson({{3, 3, ""}}).c_str()));
  ASSERT_EQ(companions_get_skill_count(env), 6);
  ASSERT_EQ(SkillNamed(env, "revive").range, 1);
  ASSERT_EQ(SkillNamed(env, "revive").revive_percent, 50);
  Companions_SkillInfo gone = {};
  ASSERT_FALSE(companions_find_skill(env, "frost", &gone));
  companions_destroy(env);
}

// A skill lands at most Companions_MAX_SKILL_TAGS tags: the env refuses more,
// so Companions_SkillInfo.tags never truncates.
TEST(TestASkillHoldsAtMostMaxSkillTags) {
  auto skill_with = [](int n) {
    std::string tags;
    for (int i = 0; i < n; ++i) {
      tags += std::string(i ? "," : "") + "{\"tag\":\"t" + std::to_string(i) + "\"}";
    }
    return "[{\"name\":\"many\",\"tags\":[" + tags + "]}]";
  };
  Companions_Env* env = LoadLevel({{3, 3, ""}}, skill_with(Companions_MAX_SKILL_TAGS));
  const Companions_SkillInfo many = SkillNamed(env, "many");
  ASSERT_EQ(many.tag_count, Companions_MAX_SKILL_TAGS);
  ASSERT_EQ(std::string(many.tags[Companions_MAX_SKILL_TAGS - 1].tag),
            std::string("t") + std::to_string(Companions_MAX_SKILL_TAGS - 1));
  SetErrorProbe();
  ASSERT_FALSE(companions_load_snapshot_json(
      env, LevelJson({{3, 3, ""}}, skill_with(Companions_MAX_SKILL_TAGS + 1)).c_str()));
  const std::string cap = "at most " + std::to_string(Companions_MAX_SKILL_TAGS) + " tags";
  ASSERT_TRUE(std::string(companions_get_error()).find(cap) != std::string::npos);
  companions_destroy(env);
}

// Unknown names, indices out of range and NULLs: false (or 0), the error set,
// `out` untouched.
TEST(TestSkillQueriesRejectBadArguments) {
  Companions_EnvConfig config = MakeConfig();
  Companions_Env* env = companions_create(&config);
  ASSERT_NOT_NULL(env);
  Companions_SkillInfo out = {};
  std::memset(&out, 0x5A, sizeof(out));
  Companions_SkillInfo before = out;
  auto untouched = [&]() { return std::memcmp(&out, &before, sizeof(out)) == 0; };

  SetErrorProbe();
  ASSERT_EQ(companions_get_skill_count(nullptr), 0);
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid environment"));

  const int32_t count = companions_get_skill_count(env);
  for (int32_t bad : {-1, count, count + 5}) {
    SetErrorProbe();
    ASSERT_FALSE(companions_get_skill(env, bad, &out));
    ASSERT_EQ(std::string(companions_get_error()), std::string("Skill index out of range"));
    ASSERT_TRUE(untouched());
  }
  // (The last call left "Skill index out of range".)
  ASSERT_FALSE(companions_get_skill(nullptr, 0, &out));
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid arguments"));
  ASSERT_FALSE(companions_get_skill(env, 0, nullptr));
  ASSERT_TRUE(untouched());

  SetErrorProbe();
  ASSERT_FALSE(companions_find_skill(env, "no_such_skill", &out));
  ASSERT_EQ(std::string(companions_get_error()), std::string("Unknown skill: no_such_skill"));
  ASSERT_TRUE(untouched());
  ASSERT_FALSE(companions_find_skill(env, "", &out));
  ASSERT_TRUE(untouched());
  // (The last call left "Unknown skill: ".)
  ASSERT_FALSE(companions_find_skill(env, nullptr, &out));
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid arguments"));
  ASSERT_FALSE(companions_find_skill(nullptr, "attack", &out));
  ASSERT_FALSE(companions_find_skill(env, "attack", nullptr));
  ASSERT_TRUE(untouched());
  companions_destroy(env);
}

// =============================================================================
// Skill previews and the last step's skill uses (1.4)
// =============================================================================

// A fireball previewed, then used: the preview is what the step reports (its
// SkillUsed position, its skill use's affected agents), and the preview
// changes nothing.
TEST(TestPreviewSkillMatchesTheStepThroughTheApi) {
  Companions_Env* env = LoadLevel({{3, 1, ",\"skills\":[\"fireball\"]"}, {3, 4, ""}, {2, 4, ""}});
  const Companions_AgentState a = AgentAt(env, 0);
  const Companions_AgentState b = AgentAt(env, 1);
  const Companions_AgentState c = AgentAt(env, 2);
  std::vector<uint8_t> before(static_cast<size_t>(companions_get_snapshot_size(env)));
  ASSERT_TRUE(companions_save_snapshot(env, before.data(), static_cast<int32_t>(before.size())));

  Companions_SkillPreview p = {};
  ASSERT_TRUE(companions_preview_skill(env, a.id, 0, Companions_Direction_Right, &p));
  ASSERT_TRUE(p.usable);
  ASSERT_EQ(std::string(p.skill), std::string("fireball"));
  ASSERT_EQ(p.centre.row, 3);
  ASSERT_EQ(p.centre.col, 4);
  ASSERT_EQ(p.caster_landing.row, 3);
  ASSERT_EQ(p.caster_landing.col, 1);
  ASSERT_EQ(p.affected_count, 2);
  ASSERT_EQ(p.affected_total, 2);  // Nothing cut
  ASSERT_EQ(p.affected[0], b.id);  // The centre, then up
  ASSERT_EQ(p.affected[1], c.id);
  ASSERT_EQ(p.affected_effects[0], static_cast<uint32_t>(Companions_SkillEffect_Tags));
  ASSERT_EQ(p.affected_effects[1],
            static_cast<uint32_t>(Companions_SkillEffect_Tags | Companions_SkillEffect_Motion));

  std::vector<uint8_t> after(static_cast<size_t>(companions_get_snapshot_size(env)));
  ASSERT_TRUE(companions_save_snapshot(env, after.data(), static_cast<int32_t>(after.size())));
  ASSERT_TRUE(before == after);
  ASSERT_EQ(companions_get_last_skill_use_count(env), 0);

  Companions_Action act[3] = {{Companions_Movement_Right, Companions_Interact_Skill1},
                              {Companions_Movement_Stay, Companions_Interact_None},
                              {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, act, 3, &result);
  const Companions_Event* used = FindEvent(result, Companions_Event_SkillUsed, a.id);
  ASSERT_NOT_NULL(used);
  ASSERT_EQ(used->position.row, p.centre.row);
  ASSERT_EQ(used->position.col, p.centre.col);

  ASSERT_EQ(companions_get_last_skill_use_count(env), 1);
  Companions_SkillUseInfo use = {};
  ASSERT_TRUE(companions_get_last_skill_use(env, 0, &use));
  ASSERT_EQ(use.caster, a.id);
  ASSERT_EQ(std::string(use.skill), std::string("fireball"));
  ASSERT_EQ(use.slot, 0);
  ASSERT_EQ(use.centre.row, 3);
  ASSERT_EQ(use.centre.col, 4);
  ASSERT_EQ(use.affected_count, p.affected_count);
  ASSERT_EQ(use.affected_total, p.affected_total);
  for (int32_t i = 0; i < use.affected_count; ++i) {
    ASSERT_EQ(use.affected[i], p.affected[i]);
    ASSERT_EQ(use.affected_effects[i], p.affected_effects[i]);
  }

  // Cooling down: previewed, not usable. The next step empties the uses.
  ASSERT_TRUE(companions_preview_skill(env, a.id, 0, Companions_Direction_Right, &p));
  ASSERT_FALSE(p.usable);
  act[0] = {Companions_Movement_Stay, Companions_Interact_None};
  companions_step(env, act, 3, &result);
  ASSERT_EQ(companions_get_last_skill_use_count(env), 0);
  companions_destroy(env);
}

// The context revive, previewed and used: the downed ally is whom it affects.
TEST(TestPreviewTheContextReviveThroughTheApi) {
  Companions_Env* env = LoadLevel({{3, 3, ",\"skills\":[\"fireball\"]"}, {3, 4, ""}});
  const Companions_AgentState a = AgentAt(env, 0);
  const Companions_AgentState b = AgentAt(env, 1);
  ASSERT_TRUE(companions_spawn_effect(env, "kill", b.position.row, b.position.col,
                                      Companions_Direction_Up, -1));
  Companions_SkillPreview p = {};
  ASSERT_TRUE(companions_preview_skill(env, a.id, 0, Companions_Direction_Right, &p));
  ASSERT_TRUE(p.usable);
  ASSERT_EQ(std::string(p.skill), std::string("revive"));
  ASSERT_EQ(p.affected_count, 1);
  ASSERT_EQ(p.affected[0], b.id);
  ASSERT_EQ(p.affected_effects[0], static_cast<uint32_t>(Companions_SkillEffect_Revive));
  // The downed ally cannot use anything
  ASSERT_TRUE(companions_preview_skill(env, b.id, 0, Companions_Direction_Left, &p));
  ASSERT_FALSE(p.usable);

  Companions_Action act[2] = {{Companions_Movement_Right, Companions_Interact_Skill1},
                              {Companions_Movement_Stay, Companions_Interact_None}};
  Companions_StepResult result = {};
  companions_step(env, act, 2, &result);
  Companions_SkillUseInfo use = {};
  ASSERT_TRUE(companions_get_last_skill_use(env, 0, &use));
  ASSERT_EQ(std::string(use.skill), std::string("revive"));
  ASSERT_EQ(use.affected_count, 1);
  ASSERT_EQ(use.affected_total, 1);
  ASSERT_EQ(use.affected[0], b.id);
  ASSERT_EQ(use.affected_effects[0], static_cast<uint32_t>(Companions_SkillEffect_Revive));
  ASSERT_FALSE(result.state.agents[1].downed);
  companions_destroy(env);
}

TEST(TestSkillPreviewAndUseRejectBadArguments) {
  Companions_Env* env = MakeAggroZombieEnv();
  const int32_t zi = FindAgentIndex(env, Companions_Faction_Enemy);
  const int32_t ci = FindAgentIndex(env, Companions_Faction_Companion);
  ASSERT_TRUE(zi >= 0 && ci >= 0);
  const Companions_ObjectId zombie = AgentAt(env, zi).id;
  const Companions_ObjectId comp = AgentAt(env, ci).id;
  Companions_SkillPreview out = {};
  std::memset(&out, 0x5A, sizeof(out));
  const Companions_SkillPreview before = out;
  auto untouched = [&]() { return std::memcmp(&out, &before, sizeof(out)) == 0; };
  auto rejects = [&](const Companions_Env* e, Companions_ObjectId id, int32_t slot, int aim,
                     const char* error) {
    SetErrorProbe();
    ASSERT_FALSE(companions_preview_skill(e, id, slot, static_cast<Companions_Direction>(aim), &out));
    ASSERT_EQ(std::string(companions_get_error()), std::string(error));
    ASSERT_TRUE(untouched());
  };
  rejects(env, 999, 0, Companions_Direction_Up, "Agent not found");
  rejects(env, zombie, 0, Companions_Direction_Up, "Not a companion");
  rejects(env, comp, -1, Companions_Direction_Up, "Skill slot out of range");
  rejects(env, comp, Companions_MAX_SKILL_SLOTS, Companions_Direction_Up,
          "Skill slot out of range");
  rejects(env, comp, 0, 4, "Invalid direction");
  rejects(env, comp, 0, -1, "Invalid direction");
  rejects(nullptr, comp, 0, Companions_Direction_Up, "Invalid arguments");
  ASSERT_FALSE(companions_preview_skill(env, comp, 0, Companions_Direction_Up, nullptr));
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid arguments"));
  // Slot 1: previewed (its equipped skill), not usable yet
  ASSERT_TRUE(companions_preview_skill(env, comp, 1, Companions_Direction_Up, &out));
  ASSERT_FALSE(out.usable);
  ASSERT_EQ(std::string(out.skill), std::string("attack"));

  Companions_SkillUseInfo use = {};
  std::memset(&use, 0x5A, sizeof(use));
  const Companions_SkillUseInfo use_before = use;
  SetErrorProbe();
  ASSERT_EQ(companions_get_last_skill_use_count(nullptr), 0);
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid environment"));
  for (int32_t bad : {-1, 0, 3}) {
    SetErrorProbe();
    ASSERT_FALSE(companions_get_last_skill_use(env, bad, &use));
    ASSERT_EQ(std::string(companions_get_error()), std::string("Skill use index out of range"));
  }
  ASSERT_FALSE(companions_get_last_skill_use(nullptr, 0, &use));
  ASSERT_EQ(std::string(companions_get_error()), std::string("Invalid arguments"));
  ASSERT_FALSE(companions_get_last_skill_use(env, 0, nullptr));
  ASSERT_TRUE(std::memcmp(&use, &use_before, sizeof(use)) == 0);
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
