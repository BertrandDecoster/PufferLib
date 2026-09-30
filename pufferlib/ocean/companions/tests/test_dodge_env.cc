// Copyright 2024
// Unit tests for DodgeEnv

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../src/core/agent_config.h"
#include "../src/env/dodge_env.h"
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
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != "         \
        << #b << " (" << (b) << ") at " << __FILE__ << ":"              \
        << __LINE__;                                                    \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_GE(a, b)                                                  \
  if ((a) < (b)) {                                                       \
    std::ostringstream oss;                                             \
    oss << "ASSERT_GE failed: " << #a << " (" << (a) << ") < "          \
        << #b << " (" << (b) << ") at " << __FILE__ << ":"              \
        << __LINE__;                                                    \
    throw std::runtime_error(oss.str());                                \
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
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
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

  // Should be done but not successful: the lone companion is down, so the
  // whole team is (TeamDown)
  ASSERT_TRUE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
}

// A companion that goes down on the horizon step fails nothing: the horizon
// ends the episode, as Horizon (not Interrupted: the horizon never pauses),
// without the success (a companion is down). The step pays the down cost
// alone (Dodge pays 0 while someone is down), the steps played on after it 0
// (not paused: Dodge's 0). Two companions: one down is not the team down
// (TeamDown would name the end).
TEST(TestDodgeEnvDownOnTheHorizonStepEndsAsHorizon) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfig lethal;
  lethal.name = "delayed_death";
  lethal.telegraph_ticks = 1;  // Strikes during the next step
  lethal.active_ticks = 1;
  lethal.area = {1};
  lethal.filter = TargetFilter::Companion;
  lethal.damage = 100;
  EffectConfigRegistry::Instance().RegisterConfig(lethal);

  DodgeEnv env(7, 2, 100, 2, 42);  // Horizon 2, no hazards
  const std::vector<Action> stay(2, EncodeAction(MovementAction::Stay));
  StepResult first = env.Step(stay);
  ASSERT_FALSE(first.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  Agent* companion = env.GetMutableObjectManager().GetAllAgents()[0];
  env.SpawnEffect("delayed_death", EffectTarget::AtCell(companion->GetPosition()));

  StepResult last = env.Step(stay);
  ASSERT_TRUE(companion->IsDowned());
  ASSERT_FALSE(env.IsTeamDown());
  ASSERT_EQ(env.GetTick(), 2);
  ASSERT_TRUE(last.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  ASSERT_FALSE(env.IsInterrupted());
  for (double r : last.rewards) ASSERT_EQ(r, BaseEnv::kDefaultDownCost);
  StepResult after = env.Step(stay);  // Playing on changes nothing
  ASSERT_TRUE(after.done);
  for (double r : after.rewards) ASSERT_EQ(r, 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

// Two companions: a down is no failure (only a team down or the horizon
// fails a task): it interrupts the task (Interrupted; the step pays the down
// cost, Dodge paying 0 with someone down), and the pause (0) runs on to the
// horizon, which ends it as Horizon.
TEST(TestDodgeEnvADownIsNoFailure) {
  DodgeEnv env(7, 2, 100, 4, 42);  // Horizon 4, no hazards
  const std::vector<Action> stay(2, EncodeAction(MovementAction::Stay));
  Companion* companion = env.GetMutableObjectManager().GetAllCompanions()[0];
  companion->TakeDamage(companion->GetHealth());
  ASSERT_TRUE(companion->IsDowned());
  ASSERT_FALSE(env.IsTeamDown());
  ASSERT_FALSE(env.IsDone());
  for (int i = 0; i < 3; ++i) {
    StepResult result = env.Step(stay);
    ASSERT_TRUE(result.done);
    ASSERT_FALSE(env.IsTeamDown());
    ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
    for (double r : result.rewards) ASSERT_EQ(r, i == 0 ? BaseEnv::kDefaultDownCost : 0.0);
  }
  StepResult last = env.Step(stay);
  ASSERT_TRUE(last.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  for (double r : last.rewards) ASSERT_EQ(r, 0.0);
}

// A down is salvageable: revived before the horizon, the team that is all up
// at the horizon succeeds (Success, kWinReward paid). The down interrupts
// the task (the down cost); the step after the revive is still paused (0),
// the survival bonus comes back on the next.
TEST(TestDodgeEnvRevivedBeforeTheHorizonSucceeds) {
  DodgeEnv env(7, 2, 100, 3, 42);  // Horizon 3, no hazards
  const std::vector<Action> stay(2, EncodeAction(MovementAction::Stay));
  Companion* companion = env.GetMutableObjectManager().GetAllCompanions()[0];
  companion->TakeDamage(companion->GetHealth());
  ASSERT_TRUE(companion->IsDowned());
  StepResult first = env.Step(stay);
  ASSERT_TRUE(first.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  for (double r : first.rewards) ASSERT_EQ(r, BaseEnv::kDefaultDownCost);
  ASSERT_TRUE(companion->Revive(1));
  ASSERT_FALSE(companion->IsDowned());
  StepResult second = env.Step(stay);
  ASSERT_FALSE(second.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (double r : second.rewards) ASSERT_EQ(r, 0.0);
  StepResult last = env.Step(stay);
  ASSERT_TRUE(last.done);
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
  for (double r : last.rewards) {
    ASSERT_EQ(r, DodgeEnv::kSurvivalBonus + DodgeEnv::kWinReward);
  }
}

// The horizon ended the episode (a companion down there; the horizon ended
// the pause too): a revive after it succeeds no more. Nothing latches, no
// kWinReward; the survival bonus is paid again (nobody is down).
TEST(TestDodgeEnvNoSuccessAfterTheHorizon) {
  DodgeEnv env(7, 2, 100, 2, 42);  // Horizon 2, no hazards
  const std::vector<Action> stay(2, EncodeAction(MovementAction::Stay));
  Companion* companion = env.GetMutableObjectManager().GetAllCompanions()[0];
  companion->TakeDamage(companion->GetHealth());
  env.Step(stay);
  ASSERT_TRUE(env.Step(stay).done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(companion->Revive(1));
  for (int i = 0; i < 2; ++i) {
    StepResult after = env.Step(stay);
    ASSERT_TRUE(after.done);
    ASSERT_FALSE(env.IsSuccess());
    ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
    for (double r : after.rewards) ASSERT_EQ(r, DodgeEnv::kSurvivalBonus);
  }
}

// The worst return: every down's cost (no lens reward is negative), up to
// the team down: max_downs - 1 downs, then both companions at once.
TEST(TestDodgeEnvMinUtility) {
  DodgeEnv env(7, 2, 100, 3, 42);
  ASSERT_EQ(env.MinUtility(), BaseEnv::kDefaultDownCost * (BaseEnv::kDefaultMaxDowns - 1 + 2));
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
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
  // Should get survival bonus + win reward
  double expected = DodgeEnv::kSurvivalBonus + DodgeEnv::kWinReward;
  ASSERT_EQ(result.rewards[0], expected);
}

// =============================================================================
// Vector Observation Tests
// =============================================================================
// DodgeEnv's features follow the base layout (BaseEnv::kVectorObsBaseSize):
// survival progress (+0), active effects (+1), active danger up / down /
// left / right (+2..+5), telegraph danger up / down / left / right (+6..+9)
static constexpr int kDodgeObs = BaseEnv::kVectorObsBaseSize;

TEST(TestDodgeEnvVectorObservationSize) {
  DodgeEnv env(7, 1, 3, 50, 42);

  // DodgeEnv adds 10 features to base (12): total = 22
  ASSERT_EQ(env.VectorObservationSize(), 22);
  ASSERT_EQ(env.VectorObservationSize(), kDodgeObs + 10);
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

  // Feature 11: steps_left (base env); not downed (feature 3)
  ASSERT_EQ(obs[BaseEnv::kVectorObsDowned], 0.0f);
  ASSERT_EQ(obs[BaseEnv::kVectorObsStepsLeft], 0.5f);  // 50 / 100

  // Feature 12: Survival progress (should be close to 1.0 at start)
  static_assert(kDodgeObs == 12);
  ASSERT_TRUE(obs[12] > 0.9f);  // 50/50 = 1.0 at tick 0

  // Feature 13: Number of effects (should be 0 at start)
  ASSERT_EQ(obs[13], 0.0f);
}

TEST(TestDodgeEnvVectorObservationDanger) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

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

  // Effect count and danger features (indices 13-21) should all be 0
  for (int i = kDodgeObs + 1; i < kDodgeObs + 10; ++i) {
    ASSERT_EQ(obs_no_effect[i], 0.0f);
  }

  // Now spawn an effect and check danger increases
  auto agents = env.GetObjectManager().GetAllAgents();
  Position player_pos = agents[0]->GetPosition();
  Position effect_pos = {player_pos.row - 2, player_pos.col};  // 2 cells above
  env.SpawnEffect("test_danger", EffectTarget::AtCell(effect_pos));

  std::vector<float> obs_with_effect;
  env.VectorObservation(obs_with_effect, 0);

  // Feature 13 should now show 1 effect
  ASSERT_TRUE(obs_with_effect[kDodgeObs + 1] > 0.0f);

  // Danger in "up" direction should be non-zero
  // Features 14-17 are active danger (up, down, left, right)
  ASSERT_TRUE(obs_with_effect[kDodgeObs + 2] > 0.0f);  // Danger up
}

TEST(TestDodgeEnvHazardDamage) {
  // Test that companions take damage from hazards
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

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
}

TEST(TestDodgeEnvHazardKillsCompanion) {
  // Test that lethal damage ends the game
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();

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
}

// A hazard without a wind-up spawned after a step (PostStep) applies on the
// next turn, to whoever stands on its cells after that turn's motion (a
// warning meanwhile: a telegraph of 1 step, so it strikes on the same turn
// as a telegraph-1 hazard would): the two hazards are
// redefined without a wind-up, 1 damage over the whole grid, one per step.
TEST(TestATelegraphZeroHazardSpawnedAfterTheStepAppliesNextTurn) {
  ScopedEffectRegistry scoped_registry;  // Builtins only, until it goes
  for (const char* name : {"dodge_fire", "dodge_wind"}) {
    EffectConfig hazard;
    hazard.name = name;
    hazard.telegraph_ticks = 0;
    hazard.active_ticks = 1;
    hazard.area.assign(13 * 13, 1);  // Covers a 7x7 grid from any centre
    hazard.filter = TargetFilter::Companion;
    hazard.damage = 1;
    EffectConfigRegistry::Instance().RegisterConfig(hazard);  // DodgeEnv keeps them
  }
  DodgeEnv env(7, 1, /*hazard_interval=*/1, /*horizon=*/50, 42);
  Agent* player = env.GetMutableObjectManager().GetAllAgents()[0];
  const std::vector<Action> stay = {EncodeAction(MovementAction::Stay)};
  env.Step(stay);  // Spawns one after the step
  ASSERT_EQ(player->GetHealth(), 3);
  ASSERT_EQ(env.GetActiveEffects().size(), 1u);
  ASSERT_TRUE(env.GetActiveEffects()[0].in_telegraph);
  ASSERT_EQ(env.GetActiveEffects()[0].ticks_remaining, 1);
  env.Step(stay);  // It applies; another spawns
  ASSERT_EQ(player->GetHealth(), 2);
  env.Step(stay);
  ASSERT_EQ(player->GetHealth(), 1);
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
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  // Disable Windows error dialogs (crash reports, assert dialogs)
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

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
