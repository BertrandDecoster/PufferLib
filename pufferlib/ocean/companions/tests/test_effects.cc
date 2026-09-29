// Copyright 2024
// Unit tests for Effect System

#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/agent_config.h"
#include "../src/core/effect_config.h"
#include "../src/core/grid.h"
#include "../src/core/level_builder.h"
#include "../src/core/object_manager.h"
#include "../src/env/synchro_env.h"
#include "effect_registry_guard.h"

using namespace companions;

// =============================================================================
// Test macros
// =============================================================================
#define TEST(name)                                        \
  void name();                                            \
  struct name##_registrar {                               \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance;                                      \
  void name()

#define ASSERT_TRUE(cond)                                               \
  if (!(cond)) {                                                        \
    std::ostringstream oss;                                             \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__        \
        << ":" << __LINE__;                                             \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_FALSE(cond)                                               \
  if (cond) {                                                            \
    std::ostringstream oss;                                             \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__       \
        << ":" << __LINE__;                                             \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_EQ(a, b)                                                  \
  if ((a) != (b)) {                                                      \
    std::ostringstream oss;                                             \
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at "         \
        << __FILE__ << ":" << __LINE__;                                 \
    throw std::runtime_error(oss.str());                                \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// EffectConfig Tests
// =============================================================================

TEST(TestEffectConfigAreaSize) {
  EffectConfig cfg;

  // 1x1 area (single cell)
  cfg.area = {1};
  ASSERT_EQ(cfg.GetAreaSize(), 1);

  // 3x3 area
  cfg.area = {0, 1, 0, 1, 1, 1, 0, 1, 0};
  ASSERT_EQ(cfg.GetAreaSize(), 3);

  // 5x5 area
  cfg.area = std::vector<int>(25, 1);
  ASSERT_EQ(cfg.GetAreaSize(), 5);
}

TEST(TestEffectConfigIsPositionAffectedNorth) {
  EffectConfig cfg;
  // 3x3 cross pattern (center and orthogonal neighbors)
  //   0 1 0
  //   1 1 1
  //   0 1 0
  cfg.area = {0, 1, 0, 1, 1, 1, 0, 1, 0};

  // Direction::Up = NORTH (no rotation)
  // Center (0, 0) should be affected
  ASSERT_TRUE(cfg.IsPositionAffected(0, 0, Direction::Up));

  // North (-1, 0) should be affected
  ASSERT_TRUE(cfg.IsPositionAffected(-1, 0, Direction::Up));

  // South (1, 0) should be affected
  ASSERT_TRUE(cfg.IsPositionAffected(1, 0, Direction::Up));

  // Diagonal (-1, -1) should NOT be affected
  ASSERT_FALSE(cfg.IsPositionAffected(-1, -1, Direction::Up));

  // Out of range (2, 0) should NOT be affected
  ASSERT_FALSE(cfg.IsPositionAffected(2, 0, Direction::Up));
}

TEST(TestEffectConfigIsPositionAffectedRotated) {
  EffectConfig cfg;
  // Line pattern facing NORTH (up): center and one cell above
  //   0 1 0    <- row -1 (relative to center)
  //   0 1 0    <- row 0 (center)
  //   0 0 0    <- row +1
  cfg.area = {0, 1, 0, 0, 1, 0, 0, 0, 0};

  // NORTH (no rotation) - affects (0,0) and (-1,0)
  ASSERT_TRUE(cfg.IsPositionAffected(0, 0, Direction::Up));
  ASSERT_TRUE(cfg.IsPositionAffected(-1, 0, Direction::Up));
  ASSERT_FALSE(cfg.IsPositionAffected(0, 1, Direction::Up));
  ASSERT_FALSE(cfg.IsPositionAffected(1, 0, Direction::Up));

  // The pattern turns with the effect: the "ahead" cell follows the facing.
  ASSERT_TRUE(cfg.IsPositionAffected(0, 0, Direction::Right));
  ASSERT_TRUE(cfg.IsPositionAffected(0, 1, Direction::Right));
  ASSERT_FALSE(cfg.IsPositionAffected(0, -1, Direction::Right));
  ASSERT_FALSE(cfg.IsPositionAffected(-1, 0, Direction::Right));

  ASSERT_TRUE(cfg.IsPositionAffected(0, 0, Direction::Down));
  ASSERT_TRUE(cfg.IsPositionAffected(1, 0, Direction::Down));
  ASSERT_FALSE(cfg.IsPositionAffected(-1, 0, Direction::Down));

  ASSERT_TRUE(cfg.IsPositionAffected(0, 0, Direction::Left));
  ASSERT_TRUE(cfg.IsPositionAffected(0, -1, Direction::Left));
  ASSERT_FALSE(cfg.IsPositionAffected(0, 1, Direction::Left));
  ASSERT_FALSE(cfg.IsPositionAffected(1, 0, Direction::Left));
}

TEST(TestEffectConfigRotatedPush) {
  EffectConfig cfg;
  cfg.push_dx = 0;
  cfg.push_dy = -1;  // Push NORTH
  cfg.push_distance = 1;

  int dx, dy;

  // NORTH - push remains (0, -1)
  cfg.GetRotatedPush(Direction::Up, dx, dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, -1);

  // EAST - push becomes (1, 0)
  cfg.GetRotatedPush(Direction::Right, dx, dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, 0);

  // SOUTH - push becomes (0, 1)
  cfg.GetRotatedPush(Direction::Down, dx, dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 1);

  // WEST - push becomes (-1, 0)
  cfg.GetRotatedPush(Direction::Left, dx, dy);
  ASSERT_EQ(dx, 1);
  ASSERT_EQ(dy, 0);
}

// =============================================================================
// EffectConfigRegistry Tests
// =============================================================================

TEST(TestEffectConfigRegistryRegister) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "test_effect";
  cfg.damage = 5;
  registry.RegisterConfig(cfg);

  const EffectConfig* found = registry.GetConfig("test_effect");
  ASSERT_TRUE(found != nullptr);
  ASSERT_EQ(found->name, "test_effect");
  ASSERT_EQ(found->damage, 5);

  // Not found
  ASSERT_TRUE(registry.GetConfig("nonexistent") == nullptr);
}

TEST(TestEffectConfigRegistryUpdate) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "update_test";
  cfg.damage = 1;
  registry.RegisterConfig(cfg);

  // Update same name
  cfg.damage = 10;
  registry.RegisterConfig(cfg);

  const EffectConfig* found = registry.GetConfig("update_test");
  ASSERT_TRUE(found != nullptr);
  ASSERT_EQ(found->damage, 10);
}

// =============================================================================
// EffectTarget Tests
// =============================================================================

TEST(TestEffectTargetFactoryMethods) {
  // Cell target
  EffectTarget cell = EffectTarget::AtCell({3, 4});
  ASSERT_EQ(cell.type, EffectTarget::Type::Cell);
  ASSERT_EQ(cell.cell.row, 3);
  ASSERT_EQ(cell.cell.col, 4);

  // Actor target
  EffectTarget actor = EffectTarget::OnActor(42);
  ASSERT_EQ(actor.type, EffectTarget::Type::Actor);
  ASSERT_EQ(actor.actor_id, 42);

  // ActorList target
  EffectTarget list = EffectTarget::OnActors({1, 2, 3});
  ASSERT_EQ(list.type, EffectTarget::Type::ActorList);
  ASSERT_EQ(list.actors.size(), 3u);
}

// =============================================================================
// ActiveEffect Tests
// =============================================================================

TEST(TestActiveEffectIsFinished) {
  EffectConfig cfg;
  cfg.name = "test";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.loop = 0;

  ActiveEffect effect;
  effect.config = &cfg;
  effect.in_telegraph = true;
  effect.ticks_remaining = 2;
  effect.loops_remaining = 0;

  // Not finished - still telegraphing
  ASSERT_FALSE(effect.IsFinished());

  // Transition to active phase
  effect.in_telegraph = false;
  effect.ticks_remaining = 1;
  ASSERT_FALSE(effect.IsFinished());

  // Finish active phase
  effect.ticks_remaining = 0;
  ASSERT_TRUE(effect.IsFinished());
}

TEST(TestActiveEffectIsFinishedWithLoops) {
  EffectConfig cfg;
  cfg.name = "test_loop";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.loop = 2;  // Execute 3 times total

  ActiveEffect effect;
  effect.config = &cfg;
  effect.in_telegraph = false;
  effect.ticks_remaining = 0;
  effect.loops_remaining = 1;  // One more loop

  // Not finished - loops remaining
  ASSERT_FALSE(effect.IsFinished());

  effect.loops_remaining = 0;
  ASSERT_TRUE(effect.IsFinished());
}

TEST(TestActiveEffectIsFinishedInfiniteLoop) {
  EffectConfig cfg;
  cfg.name = "infinite";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.loop = -1;  // Infinite

  ActiveEffect effect;
  effect.config = &cfg;
  effect.in_telegraph = false;
  effect.ticks_remaining = 0;
  effect.loops_remaining = 0;

  // Never finished for infinite loops
  ASSERT_FALSE(effect.IsFinished());
}

// =============================================================================
// BaseEnv Effect System Integration Tests
// =============================================================================

TEST(TestSpawnEffectAndTick) {
  // Register a test effect
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "test_damage";
  cfg.telegraph_ticks = 1;
  cfg.active_ticks = 1;
  cfg.damage = 2;
  cfg.area = {1};  // 1x1
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  // Create environment
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Get player position
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_TRUE(!agents.empty());
  Agent* player = agents[0];
  Position player_pos = player->GetPosition();
  int initial_health = player->GetHealth();

  // Spawn effect at player position
  env.SpawnEffect("test_damage", EffectTarget::AtCell(player_pos));

  // Check effect was added
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 1);

  // Step to process telegraph phase
  env.Step({EncodeAction(MovementAction::Stay)});

  // Effect should now be in active phase and applied damage
  // (After the step, the effect transitions and applies damage)
  ASSERT_EQ(player->GetHealth(), initial_health - 2);
}

TEST(TestEffectPush) {
  // Register a push effect
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "wind_push";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 0;
  cfg.push_dy = 1;  // Push south (down)
  cfg.push_distance = 2;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  // Create environment
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Get player and put them in a known position
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position start_pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), start_pos);

  // Verify player faction is COMPANION
  ASSERT_EQ(player->GetFaction(), Faction::COMPANION);

  // Spawn effect at player position
  env.SpawnEffect("wind_push", EffectTarget::AtCell(start_pos));
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);

  // With telegraph=0, effect starts in active phase
  const auto& effect = env.GetActiveEffects()[0];
  ASSERT_FALSE(effect.in_telegraph);  // Should be in active phase
  ASSERT_EQ(effect.ticks_remaining, 1);

  // Step to apply effect
  env.Step({EncodeAction(MovementAction::Stay)});

  // Player should be pushed south by 2
  Position new_pos = player->GetPosition();
  // Note: effect is applied when ticks_remaining goes to 0
  // Push dy=1 means row increases
  ASSERT_EQ(new_pos.row, 5);  // 3 + 2 = 5
  ASSERT_EQ(new_pos.col, 3);
}

TEST(TestEffectPushBlockedByWall) {
  // Register a push effect
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "wind_push_wall";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 0;
  cfg.push_dy = 1;  // Push south
  cfg.push_distance = 5;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  // Create environment with wall
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Add a wall at row 4
  Grid& grid = env.GetMutableGrid();
  grid.SetCell({4, 3}, CellKind::Wall);

  // Get player and position them
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position start_pos = {2, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), start_pos);

  // Spawn effect
  env.SpawnEffect("wind_push_wall", EffectTarget::AtCell(start_pos));
  env.Step({EncodeAction(MovementAction::Stay)});

  // Player should be pushed but stopped by wall
  Position new_pos = player->GetPosition();
  ASSERT_EQ(new_pos.row, 3);  // Pushed 1 cell, stopped at wall
  ASSERT_EQ(new_pos.col, 3);
}

TEST(TestEffectClearedOnReset) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "persist_test";
  cfg.telegraph_ticks = 5;
  cfg.active_ticks = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::All;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Spawn some effects
  env.SpawnEffect("persist_test", EffectTarget::AtCell({3, 3}));
  env.SpawnEffect("persist_test", EffectTarget::AtCell({4, 4}));
  ASSERT_EQ(env.GetActiveEffects().size(), 2u);

  // Clear effects (would be called by env Reset)
  env.ClearEffects();
  ASSERT_EQ(env.GetActiveEffects().size(), 0u);
}

// =============================================================================
// SCENARIO-BASED DAMAGE EFFECT TESTS
// =============================================================================

// Scenario: Single-cell damage effect with telegraph phase
// - Tick 0: Effect spawned at cell (3,3), player at (3,3), telegraph starts (2 ticks)
// - Tick 1: Telegraph continues (1 tick remaining)
// - Tick 2: Telegraph ends, active phase starts, damage applied
// - Tick 3: Effect finished and removed
TEST(TestDamageScenario_SingleCell_HitOnTelegraphEnd) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_damage_1";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position target_pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), target_pos);
  int initial_hp = player->GetHealth();

  // Tick 0: Spawn effect
  env.SpawnEffect("sc_damage_1", EffectTarget::AtCell(target_pos));
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 2);
  ASSERT_EQ(player->GetHealth(), initial_hp);  // No damage yet

  // Tick 1: Telegraph continues
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 1);
  ASSERT_EQ(player->GetHealth(), initial_hp);  // Still no damage

  // Tick 2: Telegraph ends, damage applied
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp - 1);  // Damage applied!
  // Effect should have transitioned to active phase and then removed
  // (active_ticks=1, so after applying it's finished)
}

// Scenario: Player dodges damage by moving out
// - Tick 0: Effect at (3,3), player at (3,3), telegraph starts
// - Tick 1: Player moves to (3,4)
// - Tick 2: Damage applied at (3,3) but player is at (3,4) - MISS!
TEST(TestDamageScenario_PlayerDodges) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_dodge_dmg";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 5;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position effect_pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), effect_pos);
  int initial_hp = player->GetHealth();

  // Tick 0: Spawn effect at player's current position
  env.SpawnEffect("sc_dodge_dmg", EffectTarget::AtCell(effect_pos));

  // Tick 1: Player moves right (away from danger zone)
  env.Step({EncodeAction(MovementAction::Right)});
  ASSERT_EQ(player->GetPosition().row, 3);
  ASSERT_EQ(player->GetPosition().col, 4);  // Moved to (3,4)
  ASSERT_EQ(player->GetHealth(), initial_hp);  // Still no damage

  // Tick 2: Damage applies at (3,3) but player is at (3,4)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);  // Still no damage - dodged!
}

// Scenario: 3x3 AoE damage effect
// - Effect centered at (3,3), affects 3x3 area
// - Player at (3,4) is within area - should take damage
// - Another test: Player at (3,5) is outside area - should NOT take damage
TEST(TestDamageScenario_AoE_HitsAdjacentCell) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_aoe_3x3";
  cfg.telegraph_ticks = 2;  // 2 ticks telegraph so we can observe one step without damage
  cfg.active_ticks = 1;
  cfg.damage = 2;
  cfg.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // Full 3x3
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];

  // Place player one cell to the right of effect center
  Position effect_center = {3, 3};
  Position player_pos = {3, 4};  // Adjacent, within 3x3 area
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), player_pos);
  int initial_hp = player->GetHealth();

  // Spawn AoE at (3,3)
  env.SpawnEffect("sc_aoe_3x3", EffectTarget::AtCell(effect_center));

  // Tick 1: Telegraph phase (ticks_remaining goes from 2 to 1)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);

  // Tick 2: Telegraph phase ends (ticks_remaining goes from 1 to 0, transitions to active)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp - 2);  // Damage applied on transition!
}

TEST(TestDamageScenario_AoE_MissesOutsideArea) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_aoe_miss";
  cfg.telegraph_ticks = 2;  // 2 ticks telegraph for consistency
  cfg.active_ticks = 1;
  cfg.damage = 2;
  cfg.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // Full 3x3
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];

  // Place player 2 cells away from effect center (outside 3x3)
  Position effect_center = {3, 3};
  Position player_pos = {3, 5};  // 2 cells right, outside 3x3
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), player_pos);
  int initial_hp = player->GetHealth();

  env.SpawnEffect("sc_aoe_miss", EffectTarget::AtCell(effect_center));

  // Tick 1: Telegraph phase (no damage)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);

  // Tick 2: Damage activates but player is outside 3x3 area
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);  // No damage - outside area
}

// Scenario: Filter test - Effect targets Enemy but player is Companion
// - Effect should NOT damage player (wrong faction filter)
TEST(TestDamageScenario_FilterMismatch) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_enemy_only";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = 10;
  cfg.area = {1};
  cfg.filter = TargetFilter::Enemy;  // Only enemies!
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);
  ASSERT_EQ(player->GetFaction(), Faction::COMPANION);  // Confirm faction
  int initial_hp = player->GetHealth();

  // Spawn effect that only targets enemies
  env.SpawnEffect("sc_enemy_only", EffectTarget::AtCell(pos));
  env.Step({EncodeAction(MovementAction::Stay)});

  // Player should NOT take damage (wrong faction)
  ASSERT_EQ(player->GetHealth(), initial_hp);
}

// =============================================================================
// SCENARIO-BASED PUSH EFFECT TESTS
// =============================================================================

// Scenario: Push in all 4 directions
// - Verify direction rotation works correctly
TEST(TestPushScenario_AllDirections) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  // Push north (dy=-1 in CSV, which is up = negative row)
  EffectConfig cfg;
  cfg.name = "sc_push_north";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 0;
  cfg.push_dy = -1;  // North
  cfg.push_distance = 2;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  // Test North push
  {
    SynchroEnv env(8, 8, 1, 1, 0, 42);
    env.Reset();
    auto agents = env.GetMutableObjectManager().GetAllAgents();
    Agent* player = agents[0];
    Position start = {4, 4};
    env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

    env.SpawnEffect("sc_push_north", EffectTarget::AtCell(start), Direction::Up);
    env.Step({EncodeAction(MovementAction::Stay)});

    ASSERT_EQ(player->GetPosition().row, 2);  // 4 - 2 = 2
    ASSERT_EQ(player->GetPosition().col, 4);
  }

  // Test South push (rotate 180)
  {
    SynchroEnv env(8, 8, 1, 1, 0, 42);
    env.Reset();
    auto agents = env.GetMutableObjectManager().GetAllAgents();
    Agent* player = agents[0];
    Position start = {4, 4};
    env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

    env.SpawnEffect("sc_push_north", EffectTarget::AtCell(start), Direction::Down);
    env.Step({EncodeAction(MovementAction::Stay)});

    ASSERT_EQ(player->GetPosition().row, 6);  // 4 + 2 = 6
    ASSERT_EQ(player->GetPosition().col, 4);
  }

  // Test East push (rotate 90 CW)
  {
    SynchroEnv env(8, 8, 1, 1, 0, 42);
    env.Reset();
    auto agents = env.GetMutableObjectManager().GetAllAgents();
    Agent* player = agents[0];
    Position start = {4, 4};
    env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

    env.SpawnEffect("sc_push_north", EffectTarget::AtCell(start), Direction::Right);
    env.Step({EncodeAction(MovementAction::Stay)});

    // North (-1,0) rotated 90 CW becomes (0,1) wait no...
    // Actually the rotation formula: out_dx = dy, out_dy = -dx
    // For (0,-1): out_dx = -1, out_dy = 0 -> push left
    ASSERT_EQ(player->GetPosition().row, 4);
    ASSERT_EQ(player->GetPosition().col, 2);  // 4 - 2 = 2
  }
}

// Scenario: Push blocked by wall at distance 1
TEST(TestPushScenario_BlockedByWall) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_push_wall2";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 1;  // East
  cfg.push_dy = 0;
  cfg.push_distance = 3;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  // Create wall 2 cells to the right of start
  Grid& grid = env.GetMutableGrid();
  grid.SetCell({3, 5}, CellKind::Wall);

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position start = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

  env.SpawnEffect("sc_push_wall2", EffectTarget::AtCell(start), Direction::Up);
  env.Step({EncodeAction(MovementAction::Stay)});

  // Should stop at (3,4) - one before the wall
  ASSERT_EQ(player->GetPosition().row, 3);
  ASSERT_EQ(player->GetPosition().col, 4);
}

// Scenario: Push into world boundary
TEST(TestPushScenario_BlockedByBoundary) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_push_boundary";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 0;
  cfg.push_dy = -1;  // North (up, decreasing row)
  cfg.push_distance = 10;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position start = {2, 3};  // Row 2, near top (row 0 is wall)
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

  env.SpawnEffect("sc_push_boundary", EffectTarget::AtCell(start), Direction::Up);
  env.Step({EncodeAction(MovementAction::Stay)});

  // Should stop at row 1 (wall at row 0)
  ASSERT_EQ(player->GetPosition().row, 1);
  ASSERT_EQ(player->GetPosition().col, 3);
}

// Scenario: Push with 0 distance = no movement
TEST(TestPushScenario_ZeroDistance) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_push_zero";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.push_dx = 1;
  cfg.push_dy = 0;
  cfg.push_distance = 0;  // No push
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position start = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), start);

  env.SpawnEffect("sc_push_zero", EffectTarget::AtCell(start), Direction::Up);
  env.Step({EncodeAction(MovementAction::Stay)});

  // No movement
  ASSERT_EQ(player->GetPosition().row, 3);
  ASSERT_EQ(player->GetPosition().col, 3);
}

// =============================================================================
// SCENARIO-BASED LOOPING EFFECT TESTS
// =============================================================================

// Scenario: Effect with loop=2 fires 3 times total
// - Tick 0: First activation
// - Tick 1: Second activation
// - Tick 2: Third activation
// - Tick 3: Effect removed
TEST(TestLoopScenario_ThreeActivations) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_loop_3x";
  cfg.telegraph_ticks = 0;  // No telegraph for simplicity
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.loop = 2;  // 0 + 2 loops = 3 activations
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(10);  // Enough HP for multiple hits
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);

  // Spawn effect - applies immediately (no telegraph)
  env.SpawnEffect("sc_loop_3x", EffectTarget::AtCell(pos));
  ASSERT_EQ(player->GetHealth(), 9);  // First damage on spawn

  // Tick 1: Second activation
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 8);  // Second damage

  // Tick 2: Third activation
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 7);  // Third damage

  // Tick 3: Effect should be removed
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 7);  // No more damage
  ASSERT_EQ(env.GetActiveEffects().size(), 0u);  // Effect removed
}

// Scenario: Effect with telegraph + loop
// Each loop goes through telegraph -> active
TEST(TestLoopScenario_WithTelegraph) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_loop_tele";
  cfg.telegraph_ticks = 1;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.loop = 1;  // 2 activations total
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(10);
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);

  env.SpawnEffect("sc_loop_tele", EffectTarget::AtCell(pos));
  ASSERT_EQ(player->GetHealth(), 10);  // Telegraph, no damage yet
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);

  // Tick 1: First active
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 9);  // First damage

  // Tick 2: Second telegraph (loop restarted)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 9);  // Still 9, in telegraph
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);

  // Tick 3: Second active
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), 8);  // Second damage

  // Tick 4: Effect finished
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(env.GetActiveEffects().size(), 0u);
}

// =============================================================================
// SCENARIO-BASED MULTIPLE EFFECTS TESTS
// =============================================================================

// Scenario: Two effects hitting same target
TEST(TestMultipleEffects_BothHitSameTarget) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg1;
  cfg1.name = "sc_multi_1";
  cfg1.telegraph_ticks = 0;
  cfg1.active_ticks = 1;
  cfg1.damage = 2;
  cfg1.area = {1};
  cfg1.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg1);

  EffectConfig cfg2;
  cfg2.name = "sc_multi_2";
  cfg2.telegraph_ticks = 0;
  cfg2.active_ticks = 1;
  cfg2.damage = 3;
  cfg2.area = {1};
  cfg2.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg2);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(10);
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);

  // Spawn both effects at same position
  env.SpawnEffect("sc_multi_1", EffectTarget::AtCell(pos));
  env.SpawnEffect("sc_multi_2", EffectTarget::AtCell(pos));

  // Both apply immediately (no telegraph)
  ASSERT_EQ(player->GetHealth(), 10 - 2 - 3);  // 5 HP remaining
}

// Scenario: Effect at different positions, player walks into one
TEST(TestMultipleEffects_PlayerWalksIntoEffect) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_walk_into";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position player_start = {3, 2};
  Position effect_pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), player_start);
  int initial_hp = player->GetHealth();

  // Spawn effect one cell to the right
  env.SpawnEffect("sc_walk_into", EffectTarget::AtCell(effect_pos));

  // Tick 1: Player stays, telegraph continues
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);

  // Tick 2: Player moves RIGHT into the danger zone!
  env.Step({EncodeAction(MovementAction::Right)});
  ASSERT_EQ(player->GetPosition().col, 3);  // Now at effect position
  ASSERT_EQ(player->GetHealth(), initial_hp - 1);  // Damage applied!
}

// =============================================================================
// TELEGRAPH PHASE PREVENTS DAMAGE TESTS
// =============================================================================

TEST(TestTelegraphPreventsDirectDamage) {
  // Critical test: Verify telegraph phase does NOT apply damage
  // Damage should ONLY apply when transitioning from telegraph to active
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "telegraph_test";
  cfg.telegraph_ticks = 2;  // 2 ticks telegraph
  cfg.active_ticks = 1;     // 1 tick active (like the working test)
  cfg.damage = 2;           // Reasonable damage (default HP is 3)
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);
  int initial_hp = player->GetHealth();

  // Spawn effect at player position - starts in telegraph
  env.SpawnEffect("telegraph_test", EffectTarget::AtCell(pos));
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 2);

  // Player should NOT take damage during telegraph spawn
  ASSERT_EQ(player->GetHealth(), initial_hp);

  // Tick 1: Still telegraphing (ticks_remaining: 2 -> 1)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 1);
  ASSERT_EQ(player->GetHealth(), initial_hp);  // NO DAMAGE

  // Tick 2: Telegraph transitions to active, damage is applied
  // Effect completes immediately since active_ticks=1
  env.Step({EncodeAction(MovementAction::Stay)});

  ASSERT_EQ(player->GetHealth(), initial_hp - 2);  // Damage applied!
  // Effect should be removed after completing
}

TEST(TestTelegraphAllowsDodge) {
  // Verify that player can dodge during telegraph and avoid damage
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "dodge_test";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 10;
  cfg.area = {1};  // Single cell
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position danger_pos = {3, 3};
  Position safe_pos = {3, 4};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), danger_pos);
  int initial_hp = player->GetHealth();

  // Spawn effect at danger zone
  env.SpawnEffect("dodge_test", EffectTarget::AtCell(danger_pos));

  // Tick 1: Telegraph, player still in danger zone - no damage
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);

  // Tick 2: Player dodges to safe position during last telegraph tick
  env.Step({EncodeAction(MovementAction::Right)});
  ASSERT_EQ(player->GetPosition(), safe_pos);
  ASSERT_EQ(player->GetHealth(), initial_hp);  // Still no damage - dodged!

  // Effect is now in active phase but player is outside the area
  // It will complete after active_ticks=1 finishes (one more step)
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_FALSE(env.GetActiveEffects()[0].in_telegraph);

  // Tick 3: Active phase completes, effect removed
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp);  // Still no damage
  ASSERT_TRUE(env.GetActiveEffects().empty());
}

// =============================================================================
// EFFECT STACKING TESTS
// =============================================================================

TEST(TestEffectStacking_TwoEffectsSameTarget) {
  // Critical test: Multiple effects hitting the same target should all apply
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg1;
  cfg1.name = "stack_effect_1";
  cfg1.telegraph_ticks = 0;
  cfg1.active_ticks = 1;
  cfg1.damage = 2;
  cfg1.area = {1};
  cfg1.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg1);

  EffectConfig cfg2;
  cfg2.name = "stack_effect_2";
  cfg2.telegraph_ticks = 0;
  cfg2.active_ticks = 1;
  cfg2.damage = 3;
  cfg2.area = {1};
  cfg2.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg2);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(20);
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);
  int initial_hp = player->GetHealth();

  // Spawn BOTH effects at same position at same time
  env.SpawnEffect("stack_effect_1", EffectTarget::AtCell(pos));
  env.SpawnEffect("stack_effect_2", EffectTarget::AtCell(pos));

  // Both effects have telegraph_ticks=0, so damage applies immediately
  // Player should take BOTH damages: 2 + 3 = 5 total
  ASSERT_EQ(player->GetHealth(), initial_hp - 5);
}

TEST(TestEffectStacking_ThreeEffectsDifferentTimings) {
  // Test stacking with different telegraph timings
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig instant;
  instant.name = "instant_dmg";
  instant.telegraph_ticks = 0;
  instant.active_ticks = 1;
  instant.damage = 1;
  instant.area = {1};
  instant.filter = TargetFilter::Companion;
  registry.RegisterConfig(instant);

  EffectConfig delayed1;
  delayed1.name = "delayed_1";
  delayed1.telegraph_ticks = 1;
  delayed1.active_ticks = 1;
  delayed1.damage = 2;
  delayed1.area = {1};
  delayed1.filter = TargetFilter::Companion;
  registry.RegisterConfig(delayed1);

  EffectConfig delayed2;
  delayed2.name = "delayed_2";
  delayed2.telegraph_ticks = 2;
  delayed2.active_ticks = 1;
  delayed2.damage = 3;
  delayed2.area = {1};
  delayed2.filter = TargetFilter::Companion;
  registry.RegisterConfig(delayed2);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(20);
  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);
  int initial_hp = player->GetHealth();

  // Spawn all three effects at t=0
  env.SpawnEffect("instant_dmg", EffectTarget::AtCell(pos));
  env.SpawnEffect("delayed_1", EffectTarget::AtCell(pos));
  env.SpawnEffect("delayed_2", EffectTarget::AtCell(pos));

  // t=0: instant damage applies (1)
  ASSERT_EQ(player->GetHealth(), initial_hp - 1);

  // Tick 1: delayed_1 activates (2 more damage)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp - 1 - 2);

  // Tick 2: delayed_2 activates (3 more damage)
  env.Step({EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(player->GetHealth(), initial_hp - 1 - 2 - 3);
}

// =============================================================================
// MULTI-TARGET AOE TESTS
// =============================================================================

TEST(TestMultiTargetAoE_ThreeCompanions) {
  // Critical test: Effect should hit ALL companions in AoE
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "multi_aoe";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // 3x3 full area
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  // Create environment with 3 companions
  SynchroEnv env(8, 8, 3, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 3u);

  // Position all 3 companions in 3x3 area around (4,4)
  env.GetMutableObjectManager().UpdatePosition(agents[0]->GetId(), {4, 4});  // Center
  env.GetMutableObjectManager().UpdatePosition(agents[1]->GetId(), {4, 5});  // Right
  env.GetMutableObjectManager().UpdatePosition(agents[2]->GetId(), {5, 4});  // Below

  int hp0 = agents[0]->GetHealth();
  int hp1 = agents[1]->GetHealth();
  int hp2 = agents[2]->GetHealth();

  // Spawn AoE centered at (4,4)
  env.SpawnEffect("multi_aoe", EffectTarget::AtCell({4, 4}));

  // ALL three companions should take damage
  ASSERT_EQ(agents[0]->GetHealth(), hp0 - 1);
  ASSERT_EQ(agents[1]->GetHealth(), hp1 - 1);
  ASSERT_EQ(agents[2]->GetHealth(), hp2 - 1);
}

TEST(TestMultiTargetAoE_PartialHit) {
  // Test that only companions WITHIN area are hit
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "partial_aoe";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = 2;
  cfg.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // 3x3
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 3, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 3u);

  // Place 2 companions inside AoE, 1 outside
  env.GetMutableObjectManager().UpdatePosition(agents[0]->GetId(), {4, 4});  // Center - HIT
  env.GetMutableObjectManager().UpdatePosition(agents[1]->GetId(), {4, 5});  // Right - HIT
  env.GetMutableObjectManager().UpdatePosition(agents[2]->GetId(), {7, 7});  // Far away - MISS

  int hp0 = agents[0]->GetHealth();
  int hp1 = agents[1]->GetHealth();
  int hp2 = agents[2]->GetHealth();

  // Spawn AoE at (4,4)
  env.SpawnEffect("partial_aoe", EffectTarget::AtCell({4, 4}));

  // Only agents 0 and 1 should take damage
  ASSERT_EQ(agents[0]->GetHealth(), hp0 - 2);
  ASSERT_EQ(agents[1]->GetHealth(), hp1 - 2);
  ASSERT_EQ(agents[2]->GetHealth(), hp2);  // NO DAMAGE - outside area
}

TEST(TestMultiTargetAoE_ExactDamageValues) {
  // Verify exact damage amounts, not just "decreased"
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "exact_dmg_aoe";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = 7;  // Specific damage amount
  cfg.area = {1, 1, 1, 1, 1, 1, 1, 1, 1};  // 3x3
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 2, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 2u);

  // Set up agents with known health
  agents[0]->SetMaxHealth(20);
  agents[1]->SetMaxHealth(15);

  env.GetMutableObjectManager().UpdatePosition(agents[0]->GetId(), {4, 4});
  env.GetMutableObjectManager().UpdatePosition(agents[1]->GetId(), {4, 5});

  // Spawn effect
  env.SpawnEffect("exact_dmg_aoe", EffectTarget::AtCell({4, 4}));

  // Verify EXACT damage values
  ASSERT_EQ(agents[0]->GetHealth(), 20 - 7);  // 13, not "less than 20"
  ASSERT_EQ(agents[1]->GetHealth(), 15 - 7);  // 8, not "less than 15"
}

// =============================================================================
// SCENARIO-BASED HEAL EFFECT TESTS
// =============================================================================

TEST(TestHealScenario_NegativeDamageHeals) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig cfg;
  cfg.name = "sc_heal";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = -2;  // Negative = heal
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  player->SetMaxHealth(10);
  player->TakeDamage(5);  // Now at 5/10 HP
  ASSERT_EQ(player->GetHealth(), 5);

  Position pos = {3, 3};
  env.GetMutableObjectManager().UpdatePosition(player->GetId(), pos);

  env.SpawnEffect("sc_heal", EffectTarget::AtCell(pos));
  ASSERT_EQ(player->GetHealth(), 7);  // 5 + 2 = 7
}

TEST(TestEffectOnDownedCompanionIsNoOp) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  // Register a damage effect
  EffectConfig cfg;
  cfg.name = "test_damage";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.damage = 50;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 2, 1, 0, 42);
  env.Reset();

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* target = agents[0];
  int initial_hp = target->GetHealth();

  // Down the companion
  target->TakeDamage(initial_hp + 100);
  ASSERT_TRUE(target->IsDowned());
  int downed_hp = target->GetHealth();

  // Spawn damage effect targeting the downed companion's position
  Position downed_pos = target->GetPosition();
  env.SpawnEffect("test_damage", EffectTarget::AtCell(downed_pos), Direction::Up, 0);

  // Tick effect system via step (need 2 actions for 2 agents)
  env.Step({EncodeAction(MovementAction::Stay), EncodeAction(MovementAction::Stay)});

  // Health should remain unchanged (a downed agent takes no further damage)
  ASSERT_EQ(target->GetHealth(), downed_hp);
  ASSERT_TRUE(target->IsDowned());
}

// The effect system skips the dead: nothing of an effect lands on them (the
// status would, ApplyStatus alone does not refuse the dead).
TEST(TestEffectOnDeadEnemyIsNoOp) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfig cfg;
  cfg.name = "test_mark";
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Enemy;
  cfg.status_applied = "marked";
  cfg.status_duration = 3;
  EffectConfigRegistry::Instance().RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  Agent* enemy = env.GetMutableObjectManager().CreateActor<Agent>({3, 4});
  enemy->SetFaction(Faction::ENEMY);
  enemy->SetMaxHealth(1);
  enemy->TakeDamage(1);
  ASSERT_FALSE(enemy->IsAlive());

  env.SpawnEffect("test_mark", EffectTarget::AtCell({3, 4}), Direction::Up, 0);
  env.Step({EncodeAction(MovementAction::Stay), EncodeAction(MovementAction::Stay)});
  ASSERT_EQ(enemy->GetHealth(), 0);
  ASSERT_FALSE(enemy->IsAlive());
  ASSERT_TRUE(enemy->GetStatuses().empty());
}

// =============================================================================
// A dead attacker's pending attacks are cancelled
// =============================================================================

// Companion 0 on (3,3) facing a 1-HP enemy agent on (3,4); an effect from the
// enemy (`telegraph` ticks, then `active` ticks of 1 damage each) is aimed at
// the companion. Returns the enemy. The caller holds a ScopedEffectRegistry
// declared before `env` (the effect points into the registry).
static Agent* PendingStrikeScene(SynchroEnv& env, int telegraph, int active) {
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig cfg;
  cfg.name = "wind_up";
  cfg.telegraph_ticks = telegraph;
  cfg.active_ticks = active;
  cfg.apply_every_tick = active > 1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  env.Reset();
  ObjectManager& om = env.GetMutableObjectManager();
  Agent* companion = om.GetAllAgents()[0];
  om.UpdatePosition(companion->GetId(), {3, 3});
  Agent* enemy = om.CreateActor<Agent>({3, 4});
  enemy->SetFaction(Faction::ENEMY);
  enemy->SetMaxHealth(1);
  env.SpawnEffect("wind_up", EffectTarget::AtCell({3, 3}), Direction::Up, enemy->GetId());
  return enemy;
}

static const Action kStrikeRight = EncodeAction(MovementAction::Right, InteractAction::Skill1);
static const Action kIdle = EncodeAction(MovementAction::Stay);

TEST(TestDeadAttackersTelegraphedEffectIsCancelled) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* enemy = PendingStrikeScene(env, 2, 1);
  Agent* companion = env.GetMutableObjectManager().GetAllAgents()[0];
  const int hp = companion->GetHealth();
  // Kills it while its strike winds up: it dies at the end of the turn, its
  // strike still winding up then is cancelled
  env.Step({kStrikeRight, kIdle});
  ASSERT_FALSE(enemy->IsAlive());
  ASSERT_TRUE(env.GetActiveEffects().empty());
  env.Step({kIdle, kIdle});
  env.Step({kIdle, kIdle});
  ASSERT_EQ(companion->GetHealth(), hp);
}

TEST(TestLivingAttackersTelegraphedEffectLands) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* enemy = PendingStrikeScene(env, 2, 1);
  Agent* companion = env.GetMutableObjectManager().GetAllAgents()[0];
  const int hp = companion->GetHealth();
  env.Step({kIdle, kIdle});
  env.Step({kIdle, kIdle});
  ASSERT_TRUE(enemy->IsAlive());
  ASSERT_EQ(companion->GetHealth(), hp - 1);
}

// Only pending (telegraphed) phases are cancelled: an effect already active
// when its source dies runs its course.
TEST(TestDeadAttackersActiveEffectRunsItsCourse) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  Agent* enemy = PendingStrikeScene(env, 0, 3);  // Active at once: 1 damage
  Agent* companion = env.GetMutableObjectManager().GetAllAgents()[0];
  const int hp = companion->GetHealth();
  ASSERT_EQ(hp, companion->GetMaxHealth() - 1);  // Active at spawn: it hit already
  env.Step({kStrikeRight, kIdle});               // Kills the source, the effect ticks on
  ASSERT_FALSE(enemy->IsAlive());
  ASSERT_FALSE(env.GetActiveEffects().empty());
  ASSERT_EQ(companion->GetHealth(), hp - 1);
  env.Step({kIdle, kIdle});
  ASSERT_EQ(companion->GetHealth(), hp - 2);
}

// A looping effect whose source died is cancelled when it winds up again.
TEST(TestDeadAttackersLoopIsCancelledAtItsNextWindUp) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig cfg;
  cfg.name = "looping_strike";
  cfg.telegraph_ticks = 1;
  cfg.active_ticks = 1;
  cfg.loop = -1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  ObjectManager& om = env.GetMutableObjectManager();
  Agent* companion = om.GetAllAgents()[0];
  om.UpdatePosition(companion->GetId(), {3, 3});
  Agent* tower = om.CreateActor<Agent>({3, 4});
  tower->SetFaction(Faction::ENEMY);
  env.SpawnEffect("looping_strike", EffectTarget::AtCell({3, 3}), Direction::Up, tower->GetId());
  const int hp = companion->GetHealth();
  env.Step({kIdle, kIdle});  // Telegraph -> active: hit
  ASSERT_EQ(companion->GetHealth(), hp - 1);
  tower->TakeDamage(tower->GetHealth());
  for (int i = 0; i < 4; ++i) env.Step({kIdle, kIdle});
  ASSERT_EQ(companion->GetHealth(), hp - 1);
  ASSERT_TRUE(env.GetActiveEffects().empty());
}

// A looping effect with no wind-up (telegraph_ticks 0) restarts straight into
// its active phase: a dead source's loop stops at that restart too. `loop` is
// -1 (forever) or a count. Returns the companion's health lost over 3 steps
// after the spawn-time hit, the tower killed right after the spawn or not.
static int NoWindUpLoopDamage(int loop, bool kill_tower) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig cfg;
  cfg.name = "pulse";
  cfg.telegraph_ticks = 0;
  cfg.active_ticks = 1;
  cfg.loop = loop;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  ObjectManager& om = env.GetMutableObjectManager();
  Agent* companion = om.GetAllAgents()[0];
  companion->SetMaxHealth(10);
  om.UpdatePosition(companion->GetId(), {3, 3});
  Agent* tower = om.CreateActor<Agent>({3, 4});
  tower->SetFaction(Faction::ENEMY);
  env.SpawnEffect("pulse", EffectTarget::AtCell({3, 3}), Direction::Up, tower->GetId());
  const int hp = companion->GetHealth();  // After the spawn-time hit
  if (kill_tower) tower->TakeDamage(tower->GetHealth());
  for (int i = 0; i < 3; ++i) env.Step({kIdle, kIdle});
  const bool gone = env.GetActiveEffects().empty();
  if (kill_tower && !gone) throw std::runtime_error("the dead tower's loop is still active");
  return hp - companion->GetHealth();
}

TEST(TestDeadSourcesNoWindUpLoopStops) {
  ASSERT_EQ(NoWindUpLoopDamage(-1, true), 0);
  ASSERT_EQ(NoWindUpLoopDamage(5, true), 0);
}

TEST(TestLivingSourcesNoWindUpLoopKeepsHitting) {
  ASSERT_EQ(NoWindUpLoopDamage(-1, false), 3);
  ASSERT_EQ(NoWindUpLoopDamage(5, false), 3);
}

// LoadSnapshot re-issues agent ids (0, 1, ... in the saved order): a loaded
// effect's source follows its agent even when the saved ids had gaps, and a
// source naming no saved agent loads as no source.
TEST(TestLoadedEffectKeepsItsSource) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig cfg;
  cfg.name = "wind_up";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  ObjectManager& om = env.GetMutableObjectManager();
  om.UpdatePosition(om.GetAllAgents()[0]->GetId(), {3, 3});
  const ObjectId gap = om.CreateActor<Agent>({5, 5})->GetId();
  Agent* tower = om.CreateActor<Agent>({3, 4});
  tower->SetFaction(Faction::ENEMY);
  om.RemoveObject(gap);
  env.SpawnEffect("wind_up", EffectTarget::AtCell({3, 3}), Direction::Up, tower->GetId());
  Snapshot snap = env.SaveSnapshot();
  ASSERT_EQ(snap.effects.size(), 1u);
  ASSERT_EQ(snap.effects[0].source_id, tower->GetId());

  SynchroEnv loaded(8, 8, 1, 1, 0, 42);
  loaded.LoadSnapshot(snap);
  Agent* loaded_tower = dynamic_cast<Agent*>(loaded.GetMutableObjectManager().GetActorAt({3, 4}));
  ASSERT_TRUE(loaded_tower != nullptr);
  ASSERT_TRUE(loaded_tower->GetId() != tower->GetId());  // Re-issued without the gap
  ASSERT_EQ(loaded.GetActiveEffects()[0].source_id, loaded_tower->GetId());
  // So killing it cancels its pending strike
  Agent* companion = loaded.GetMutableObjectManager().GetAllAgents()[0];
  const int hp = companion->GetHealth();
  loaded_tower->TakeDamage(loaded_tower->GetHealth());
  loaded.Step({kIdle, kIdle});
  loaded.Step({kIdle, kIdle});
  ASSERT_TRUE(loaded.GetActiveEffects().empty());
  ASSERT_EQ(companion->GetHealth(), hp);

  snap.effects[0].source_id = 42;  // No saved agent
  loaded.LoadSnapshot(snap);
  ASSERT_EQ(loaded.GetActiveEffects()[0].source_id, kInvalidObjectId);
}

// Actor targets follow their agents through the saved ids too: an Actor
// target naming no saved agent loads as none (the effect falls back to its
// cell), and an ActorList drops such entries.
TEST(TestLoadedEffectKeepsItsActorTargets) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  EffectConfig cfg;
  cfg.name = "wind_up";
  cfg.telegraph_ticks = 2;
  cfg.active_ticks = 1;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();
  ObjectManager& om = env.GetMutableObjectManager();
  const ObjectId companion = om.GetAllAgents()[0]->GetId();
  om.UpdatePosition(companion, {3, 3});
  const ObjectId gap = om.CreateActor<Agent>({5, 5})->GetId();
  const ObjectId tower = om.CreateActor<Agent>({3, 4})->GetId();
  om.RemoveObject(gap);
  env.SpawnEffect("wind_up", EffectTarget::OnActor(tower));
  env.SpawnEffect("wind_up", EffectTarget::OnActors({tower, companion}));
  Snapshot snap = env.SaveSnapshot();
  ASSERT_EQ(snap.effects.size(), 2u);
  ASSERT_EQ(snap.effects[0].target_actor_id, tower);
  ASSERT_TRUE(snap.effects[1].target_actors == (std::vector<int>{tower, companion}));
  snap.effects[1].target_actors.insert(snap.effects[1].target_actors.begin(), 42);

  SynchroEnv loaded(8, 8, 1, 1, 0, 42);
  loaded.LoadSnapshot(snap);
  const ObjectManager& lom = loaded.GetObjectManager();
  const ObjectId loaded_tower = lom.GetActorAt({3, 4})->GetId();
  const ObjectId loaded_companion = lom.GetActorAt({3, 3})->GetId();
  ASSERT_TRUE(loaded_tower != tower);  // Re-issued without the gap
  const auto& effects = loaded.GetActiveEffects();
  ASSERT_EQ(effects.size(), 2u);
  ASSERT_TRUE(effects[0].target.type == EffectTarget::Type::Actor);
  ASSERT_EQ(effects[0].target.actor_id, loaded_tower);
  ASSERT_TRUE(effects[1].target.type == EffectTarget::Type::ActorList);
  ASSERT_TRUE(effects[1].target.actors ==
              (std::vector<ObjectId>{loaded_tower, loaded_companion}));
  ASSERT_TRUE(effects[1].GetCenter(lom) == (Position{3, 4}));  // Its first actor's cell

  snap.effects[0].target_actor_id = 42;  // No saved agent
  snap.effects[0].target_cell = {6, 6};
  loaded.LoadSnapshot(snap);
  const ActiveEffect& orphan = loaded.GetActiveEffects()[0];
  ASSERT_TRUE(orphan.target.type == EffectTarget::Type::Actor);
  ASSERT_EQ(orphan.target.actor_id, kInvalidObjectId);
  ASSERT_TRUE(orphan.GetCenter(loaded.GetObjectManager()) == (Position{6, 6}));  // Its cell
}

// =============================================================================
// Main
// =============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  // Disable Windows error dialogs (crash reports, assert dialogs)
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " effect system tests...\n\n";

  int passed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    test.func();
    std::cout << "[       OK ] " << test.name << "\n";
    passed++;
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return 0;
}
