// Copyright 2024
// Unit tests for DodgeEnv

#include <cstdlib>
#include <iostream>
#include <vector>

#include "../src/core/agent_config.h"
#include "../src/env/dodge_env.h"

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
    std::cerr << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ \
              << ":" << __LINE__ << "\n";                              \
    std::abort();                                                       \
  }

#define ASSERT_FALSE(cond)                                               \
  if (cond) {                                                            \
    std::cerr << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ \
              << ":" << __LINE__ << "\n";                               \
    std::abort();                                                        \
  }

#define ASSERT_EQ(a, b)                                                  \
  if ((a) != (b)) {                                                      \
    std::cerr << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != "   \
              << #b << " (" << (b) << ") at " << __FILE__ << ":"        \
              << __LINE__ << "\n";                                      \
    std::abort();                                                        \
  }

#define ASSERT_GE(a, b)                                                  \
  if ((a) < (b)) {                                                       \
    std::cerr << "ASSERT_GE failed: " << #a << " (" << (a) << ") < "    \
              << #b << " (" << (b) << ") at " << __FILE__ << ":"        \
              << __LINE__ << "\n";                                      \
    std::abort();                                                        \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// DodgeEnv Tests
// =============================================================================

TEST(TestDodgeEnvConstruction) {
  DodgeEnv env(7, 1, 3, 50, 42);
  ASSERT_EQ(env.GetNumCompanions(), 1);
  ASSERT_EQ(env.GetHazardInterval(), 3);
  ASSERT_EQ(env.GetHorizon(), 50);
}

TEST(TestDodgeEnvReset) {
  DodgeEnv env(7, 2, 5, 30, 42);

  // Check initial state
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_EQ(env.GetTick(), 0);

  // Agents should be spawned
  auto agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 2u);
}

TEST(TestDodgeEnvObservationShape) {
  DodgeEnv env(7, 1, 3, 50, 42);

  auto shape = env.ObservationShape();
  ASSERT_EQ(shape.size(), 3u);
  ASSERT_EQ(shape[0], 7);  // 7 planes
  ASSERT_EQ(shape[1], 7);  // rows
  ASSERT_EQ(shape[2], 7);  // cols
}

TEST(TestDodgeEnvSurvivalWin) {
  // Create env with short survival requirement
  DodgeEnv env(7, 1, 100, 3, 42);  // Hazard interval 100 = no hazards during test

  std::vector<Action> actions = {EncodeAction(MovementAction::Stay)};

  // Step until survival complete
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(env.IsDone());
    env.Step(actions);
  }

  // Should be done and successful
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
}

TEST(TestDodgeEnvDeath) {
  // Register a lethal effect
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

  EffectConfig lethal;
  lethal.name = "instant_death";
  lethal.telegraph_ticks = 0;
  lethal.active_ticks = 1;
  lethal.area = {1};
  lethal.filter = TargetFilter::Companion;
  lethal.damage = 100;  // Lethal
  registry.RegisterConfig(lethal);

  DodgeEnv env(7, 1, 100, 50, 42);

  // Get player position
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position player_pos = player->GetPosition();

  // Spawn lethal effect on player
  env.SpawnEffect("instant_death", EffectTarget::AtCell(player_pos));

  // Step once - player should die
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay)};
  env.Step(actions);

  // Should be done but not successful
  ASSERT_TRUE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
}

TEST(TestDodgeEnvCopy) {
  DodgeEnv env(7, 1, 3, 50, 42);

  // Step a few times
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay)};
  env.Step(actions);
  env.Step(actions);

  // Copy the environment
  DodgeEnv copy(env);

  // Check copy has same state
  ASSERT_EQ(copy.GetTick(), env.GetTick());
  ASSERT_EQ(copy.GetNumCompanions(), env.GetNumCompanions());
  ASSERT_EQ(copy.IsSuccess(), env.IsSuccess());
}

TEST(TestDodgeEnvMultipleCompanions) {
  DodgeEnv env(7, 3, 5, 30, 42);

  auto agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 3u);

  // Each agent should be at a different position
  std::vector<Position> positions;
  for (const auto* agent : agents) {
    positions.push_back(agent->GetPosition());
  }

  // Check all positions are unique
  for (size_t i = 0; i < positions.size(); ++i) {
    for (size_t j = i + 1; j < positions.size(); ++j) {
      ASSERT_FALSE(positions[i] == positions[j]);
    }
  }
}

TEST(TestDodgeEnvRewards) {
  // Short survival, no hazards
  DodgeEnv env(7, 1, 100, 2, 42);

  std::vector<Action> actions = {EncodeAction(MovementAction::Stay)};

  // Step once - should get survival bonus
  auto result = env.Step(actions);
  ASSERT_EQ(result.rewards.size(), 1u);
  ASSERT_EQ(result.rewards[0], DodgeEnv::kSurvivalBonus);

  // Step again - should win
  result = env.Step(actions);
  ASSERT_TRUE(env.IsSuccess());
  // Should get survival bonus + win reward
  double expected = DodgeEnv::kSurvivalBonus + DodgeEnv::kWinReward;
  ASSERT_EQ(result.rewards[0], expected);
}

// =============================================================================
// Vector Observation Tests
// =============================================================================
TEST(TestDodgeEnvVectorObservationSize) {
  DodgeEnv env(7, 1, 3, 50, 42);

  // DodgeEnv adds 10 features to base (9): total = 19
  ASSERT_EQ(env.VectorObservationSize(), 19);
}

TEST(TestDodgeEnvVectorObservationValues) {
  DodgeEnv env(7, 1, 3, 50, 42);

  std::vector<float> obs;
  env.VectorObservation(obs, 0);

  ASSERT_EQ(obs.size(), static_cast<size_t>(env.VectorObservationSize()));

  // All values should be in valid range [0, 1]
  for (size_t i = 0; i < obs.size(); ++i) {
    ASSERT_TRUE(obs[i] >= 0.0f && obs[i] <= 1.0f);
  }

  // Feature 8: steps_left (base env)
  // Feature 9: Survival progress (should be close to 1.0 at start)
  ASSERT_TRUE(obs[9] > 0.9f);  // 50/50 = 1.0 at tick 0

  // Feature 10: Number of effects (should be 0 at start)
  ASSERT_EQ(obs[10], 0.0f);
}

TEST(TestDodgeEnvVectorObservationDanger) {
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  registry.Clear();

  // Register a test effect
  EffectConfig cfg;
  cfg.name = "test_danger";
  cfg.telegraph_ticks = 0;  // Immediate active
  cfg.active_ticks = 5;
  cfg.damage = 1;
  cfg.area = {1};
  cfg.filter = TargetFilter::Companion;
  registry.RegisterConfig(cfg);

  DodgeEnv env(7, 1, 100, 50, 42);  // No auto hazards

  // Get initial observation (no effects)
  std::vector<float> obs_no_effect;
  env.VectorObservation(obs_no_effect, 0);

  // Danger features (indices 10-17) should all be 0 (no danger)
  for (int i = 10; i < 18; ++i) {
    ASSERT_EQ(obs_no_effect[i], 0.0f);
  }

  // Now spawn an effect and check danger increases
  auto agents = env.GetObjectManager().GetAllAgents();
  Position player_pos = agents[0]->GetPosition();
  Position effect_pos = {player_pos.row - 2, player_pos.col};  // 2 cells above
  env.SpawnEffect("test_danger", EffectTarget::AtCell(effect_pos));

  std::vector<float> obs_with_effect;
  env.VectorObservation(obs_with_effect, 0);

  // Feature 9 should now show 1 effect
  ASSERT_TRUE(obs_with_effect[9] > 0.0f);

  // Danger in "up" direction should be non-zero
  // Features 10-13 are active danger (up, down, left, right)
  ASSERT_TRUE(obs_with_effect[10] > 0.0f);  // Danger up

  registry.Clear();
}

TEST(TestDodgeEnvHazardDamage) {
  // Test that companions take damage from hazards
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  registry.Clear();

  // Register a damaging effect
  EffectConfig damage_cfg;
  damage_cfg.name = "damage_hazard";
  damage_cfg.telegraph_ticks = 0;  // Immediately active
  damage_cfg.active_ticks = 1;
  damage_cfg.area = {1};  // Single cell
  damage_cfg.filter = TargetFilter::Companion;
  damage_cfg.damage = 1;  // 1 damage (companion has 3 HP by default)
  registry.RegisterConfig(damage_cfg);

  DodgeEnv env(7, 1, 100, 50, 42);  // No auto hazards

  // Get player position and health
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 1u);
  Agent* player = agents[0];
  Position player_pos = player->GetPosition();
  int initial_health = player->GetHealth();
  ASSERT_EQ(initial_health, 3);  // Default health

  // Spawn damaging effect directly on player
  env.SpawnEffect("damage_hazard", EffectTarget::AtCell(player_pos));

  // Step once - hazard should damage player
  std::vector<Action> stay = {EncodeAction(MovementAction::Stay)};
  env.Step(stay);

  // Verify player took damage
  int health_after_damage = player->GetHealth();
  ASSERT_EQ(health_after_damage, 2);  // 3 - 1 = 2

  // Verify game is not done (player still alive)
  ASSERT_FALSE(env.IsDone());

  registry.Clear();
}

TEST(TestDodgeEnvHazardKillsCompanion) {
  // Test that lethal damage ends the game
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  registry.Clear();

  // Register a lethal effect
  EffectConfig lethal_cfg;
  lethal_cfg.name = "lethal_hazard";
  lethal_cfg.telegraph_ticks = 0;
  lethal_cfg.active_ticks = 1;
  lethal_cfg.area = {1};
  lethal_cfg.filter = TargetFilter::Companion;
  lethal_cfg.damage = 10;  // More than max HP (3)
  registry.RegisterConfig(lethal_cfg);

  DodgeEnv env(7, 1, 100, 50, 99);

  // Get player
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* player = agents[0];
  Position player_pos = player->GetPosition();

  ASSERT_FALSE(env.IsDone());
  ASSERT_EQ(player->GetHealth(), 3);

  // Spawn lethal effect on player
  env.SpawnEffect("lethal_hazard", EffectTarget::AtCell(player_pos));

  // Step - should kill player and end game
  std::vector<Action> stay = {EncodeAction(MovementAction::Stay)};
  env.Step(stay);

  // Verify player died and game ended
  ASSERT_TRUE(player->GetHealth() <= 0);
  ASSERT_TRUE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());  // Death is not success

  registry.Clear();
}

TEST(TestDodgeEnvSurvivalIntegration) {
  // Integration test: Survive to horizon without hazards
  DodgeEnv env(7, 1, 1000, 10, 12345);  // Hazard interval 1000 = no hazards

  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());

  // Step until horizon
  std::vector<Action> stay = {EncodeAction(MovementAction::Stay)};
  for (int i = 0; i < 10; ++i) {
    ASSERT_FALSE(env.IsDone());
    auto result = env.Step(stay);

    // Should get survival bonus each step
    ASSERT_EQ(result.rewards.size(), 1u);
    if (i < 9) {
      ASSERT_EQ(result.rewards[0], DodgeEnv::kSurvivalBonus);
    } else {
      // Last step should give survival bonus + win reward
      double expected = DodgeEnv::kSurvivalBonus + DodgeEnv::kWinReward;
      ASSERT_EQ(result.rewards[0], expected);
    }
  }

  // Should win after surviving to horizon
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "Running " << tests.size() << " DodgeEnv tests...\n\n";

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
