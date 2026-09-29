// Copyright 2024
// Unit tests for interruptions: a down interrupts the task (EndReason::Interrupted),
// pays the down cost, and pauses the lens until nobody is down

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/object.h"
#include "../src/core/snapshot.h"
#include "../src/env/dodge_env.h"
#include "../src/env/synchro_env.h"
#include "../src/env/synchro_lens.h"
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
// Helpers
// =============================================================================

// An open arena: a wall border, floor inside, agents parked on the last inner
// row until a test places them.
static void MakeArena(BaseEnv& env) {
  env.Reset();
  Grid& g = env.GetMutableGrid();
  const int rows = g.GetRows();
  const int cols = g.GetCols();
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      bool border = r == 0 || c == 0 || r == rows - 1 || c == cols - 1;
      g.SetCell({r, c}, border ? CellKind::Wall : CellKind::Floor);
    }
  }
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  for (size_t i = 0; i < agents.size(); ++i) {
    env.GetMutableObjectManager().UpdatePosition(agents[i]->GetId(),
                                                 {rows - 2, 1 + static_cast<int>(i)});
  }
}

static Agent* AgentAt(BaseEnv& env, int index) {
  return env.GetMutableObjectManager().GetAllAgents()[static_cast<size_t>(index)];
}

static Agent* Place(BaseEnv& env, int index, Position p) {
  Agent* a = AgentAt(env, index);
  env.GetMutableObjectManager().UpdatePosition(a->GetId(), p);
  return a;
}

// Down between steps (the host)
static Agent* DownCompanion(BaseEnv& env, int index) {
  Agent* a = AgentAt(env, index);
  a->TakeDamage(a->GetHealth());
  return a;
}

// A lethal effect on `cell` that strikes during the next step. Call under a
// ScopedEffectRegistry.
static void SpawnKillNextStep(BaseEnv& env, Position cell) {
  EffectConfig kill;
  kill.name = "kill_next_step";
  kill.telegraph_ticks = 1;
  kill.active_ticks = 1;
  kill.area = {1};
  kill.filter = TargetFilter::Companion;
  kill.damage = 999;
  EffectConfigRegistry::Instance().RegisterConfig(kill);
  env.SpawnEffect("kill_next_step", EffectTarget::AtCell(cell));
}

static Action Use(MovementAction aim) { return EncodeAction(aim, InteractAction::Skill1); }
static const Action kStay = EncodeAction(MovementAction::Stay);

static std::vector<Action> Stays(const BaseEnv& env) {
  return std::vector<Action>(static_cast<size_t>(env.NumAgents()), kStay);
}

// A 10x10 SynchroEnv arena with one goal. Row() is an inner row off the goal:
// companions placed on it never cover the goal.
static int Row(const SynchroEnv& env) {
  const Position goal = env.GetSynchroPositions()[0];
  return goal.row == 3 ? 5 : 3;
}

static Position Goal(const SynchroEnv& env) { return env.GetSynchroPositions()[0]; }

// The lens's reward for agent `i` on the env as it is (after a step)
static double LensReward(BaseEnv& env, int i) { return env.GetTaskLens()->ComputeReward(env, i); }

static const double kCost = BaseEnv::kDefaultDownCost;

// =============================================================================
// A down interrupts the task
// =============================================================================

// A down during a step: done, Interrupted, each reward the lens's plus the
// down cost.
TEST(TestADownDuringAStepInterrupts) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Agent* victim = Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  SpawnKillNextStep(env, {r, 4});
  ASSERT_TRUE(env.GetDownCost() == -0.5);
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(victim->IsDowned());
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsInterrupted());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + kCost);
  }
}

// A down the host caused between steps: nothing until the next step, which
// reads Interrupted and pays the cost.
TEST(TestAHostDownInterruptsAtTheNextStep) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + kCost);
  }
}

// While paused: no reward, the reason stays Interrupted; a second down pays
// nothing, then or later. Companion 0 revives both (two steps: still paused
// after the first); the step after the second revive pays the lens's reward
// alone.
TEST(TestWhilePausedNothingIsPaid) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 3});
  Agent* right = Place(env, 1, {r, 4});
  Agent* left = Place(env, 2, {r, 6});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);

  StepResult paused = env.Step(Stays(env));
  ASSERT_TRUE(paused.done);
  for (double reward : paused.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);

  // A second down while paused (during the step)
  SpawnKillNextStep(env, {r, 6});
  StepResult second = env.Step(Stays(env));
  ASSERT_TRUE(left->IsDowned());
  ASSERT_EQ(env.GetDowns(), 2);
  for (double reward : second.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  // Moved beside companion 0 by the test (the body is inert)
  Place(env, 2, {r, 2});

  StepResult first_revive = env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_FALSE(right->IsDowned());
  ASSERT_TRUE(left->IsDowned());
  ASSERT_TRUE(first_revive.done);  // Still paused: companion 2 is down
  for (double reward : first_revive.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);

  StepResult second_revive = env.Step({Use(MovementAction::Left), kStay, kStay});
  ASSERT_FALSE(left->IsDowned());
  ASSERT_FALSE(second_revive.done);
  for (double reward : second_revive.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);

  StepResult resumed = env.Step(Stays(env));
  ASSERT_FALSE(resumed.done);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(resumed.rewards[static_cast<size_t>(i)] == LensReward(env, i));  // No cost
  }
}

// A revive clears the pause. The revive step is still paused (no reward, no
// success though the goal holds); the next step rewards again, and a success
// then ends as Success.
TEST(TestAReviveResumesTheTaskFromTheNextStep) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 3});
  Agent* downed = Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());

  // The goal holds while paused: no success
  Place(env, 2, Goal(env));
  ASSERT_TRUE(env.GetTaskLens()->IsSuccess(env));
  StepResult paused = env.Step(Stays(env));
  ASSERT_TRUE(paused.done);
  ASSERT_FALSE(env.IsSuccess());
  for (double reward : paused.rewards) ASSERT_TRUE(reward == 0.0);

  StepResult revive = env.Step({Use(MovementAction::Right), kStay, kStay});
  ASSERT_FALSE(downed->IsDowned());
  ASSERT_FALSE(revive.done);
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_FALSE(env.IsSuccess());  // The revive step is still paused
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (double reward : revive.rewards) ASSERT_TRUE(reward == 0.0);

  StepResult win = env.Step(Stays(env));
  ASSERT_TRUE(win.done);
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(win.rewards[static_cast<size_t>(i)] == LensReward(env, i));
    ASSERT_TRUE(win.rewards[static_cast<size_t>(i)] > SynchroLens::kWinReward / 2);
  }
}

// =============================================================================
// Upgrades: Success > TeamDown > Horizon > Interrupted
// =============================================================================

// Interrupted, then the team goes down while paused: TeamDown (live between
// steps, latched by the next step).
TEST(TestATeamDownWhilePausedIsTeamDown) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  ASSERT_EQ(env.GetMaxDowns(), 3);
  const int r = Row(env);
  for (int i = 0; i < 4; ++i) Place(env, i, {r, 1 + 2 * i});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  SpawnKillNextStep(env, {r, 5});  // Companion 2, while paused
  StepResult second = env.Step(Stays(env));
  ASSERT_TRUE(AgentAt(env, 2)->IsDowned());
  for (double reward : second.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);  // 2 downs of 3
  DownCompanion(env, 3);  // The 3rd down, between steps
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);  // Live
  StepResult last = env.Step(Stays(env));
  ASSERT_TRUE(last.done);
  for (double reward : last.rewards) ASSERT_TRUE(reward == 0.0);
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
}

// Interrupted, then the pause reaches the horizon: Horizon.
TEST(TestAPauseReachingTheHorizonIsHorizon) {
  SynchroEnv env(10, 10, 2, 1, 0, 42, 0, 3);
  MakeArena(env);
  ASSERT_EQ(env.GetHorizon(), 3);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  DownCompanion(env, 1);
  env.Step(Stays(env));  // Tick 1
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  env.Step(Stays(env));  // Tick 2
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  StepResult last = env.Step(Stays(env));  // Tick 3: the horizon
  ASSERT_TRUE(last.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  env.Step(Stays(env));  // Played on: the reason stays
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

// The horizon never pauses: a down on the horizon step ends as Horizon (the
// down is still paid).
TEST(TestADownOnTheHorizonStepIsHorizon) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 2, 1, 0, 42, 0, 2);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  StepResult first = env.Step(Stays(env));
  ASSERT_FALSE(first.done);
  SpawnKillNextStep(env, {r, 4});
  StepResult last = env.Step(Stays(env));
  ASSERT_TRUE(AgentAt(env, 1)->IsDowned());
  ASSERT_TRUE(last.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(last.rewards[static_cast<size_t>(i)] == LensReward(env, i) + kCost);
  }
}

// A Dodge companion revived exactly on the horizon step: the step is still
// paused, so no success (everyone up at the horizon would be one): Horizon.
TEST(TestADodgeReviveOnTheHorizonStepIsHorizon) {
  DodgeEnv env(10, 2, 1000, 3, 42);  // No hazard before the horizon
  MakeArena(env);
  Place(env, 0, {4, 3});
  Agent* downed = Place(env, 1, {4, 4});
  DownCompanion(env, 1);
  env.Step(Stays(env));  // Tick 1
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  env.Step(Stays(env));  // Tick 2
  StepResult last = env.Step({Use(MovementAction::Right), kStay});  // Tick 3: the horizon
  ASSERT_FALSE(downed->IsDowned());
  ASSERT_TRUE(last.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
  for (double reward : last.rewards) ASSERT_TRUE(reward == 0.0);
}

// A success and a down on the same step: Success (not paused), the cost paid.
// A down after the latched success does not pause either.
TEST(TestASuccessAndADownOnOneStepIsASuccess) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, Goal(env));
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  SpawnKillNextStep(env, {r, 4});
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(AgentAt(env, 1)->IsDowned());
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + kCost);
  }

  DownCompanion(env, 2);
  env.Step(Stays(env));
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
}

// A team the host downed between steps is not turned into a success by the
// next step, the goal held or not: TeamDown, no win reward.
TEST(TestAHostTeamDownIsNotASuccess) {
  SynchroEnv env(10, 10, 2, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetMaxDowns(1));
  const int r = Row(env);
  Place(env, 0, Goal(env));
  Place(env, 1, {r, 4});
  DownCompanion(env, 1);
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_TRUE(env.GetTaskLens()->IsSuccess(env));  // Companion 0 stands on the goal
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(result.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
  for (double reward : result.rewards) ASSERT_TRUE(reward < SynchroLens::kWinReward / 2);
}

// =============================================================================
// What clears the pause
// =============================================================================

// A lens change resumes; a down before the change never interrupts the new lens.
TEST(TestALensChangeClearsThePause) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_FALSE(env.IsDone());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  StepResult resumed = env.Step(Stays(env));
  ASSERT_FALSE(resumed.done);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(resumed.rewards[static_cast<size_t>(i)] == LensReward(env, i));
  }

  // A down between steps, then a lens change: the new lens is not interrupted
  DownCompanion(env, 2);
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));
  StepResult after = env.Step(Stays(env));
  ASSERT_FALSE(after.done);
  ASSERT_FALSE(env.IsInterrupted());
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(after.rewards[static_cast<size_t>(i)] == LensReward(env, i));
  }
}

// Reset and LoadSnapshot clear the pause; a snapshot saved with someone down
// loads not interrupted, and its downs are not new to the next step.
TEST(TestResetAndLoadSnapshotClearThePause) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());
  const Snapshot snap = env.SaveSnapshot();

  env.Reset(42);
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_FALSE(env.IsDone());

  MakeArena(env);
  DownCompanion(env, 0);
  env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());
  env.LoadSnapshot(snap);  // Companion 1 down in it
  ASSERT_EQ(env.GetDowns(), 1);
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_FALSE(env.IsDone());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  StepResult next = env.Step(Stays(env));
  ASSERT_FALSE(next.done);
  ASSERT_FALSE(env.IsInterrupted());
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(next.rewards[static_cast<size_t>(i)] == LensReward(env, i));
  }

  SynchroEnv fresh(10, 10, 3, 1, 0, 7);  // A load into another env: the same
  fresh.LoadSnapshot(snap);
  ASSERT_FALSE(fresh.IsInterrupted());
  StepResult fresh_next = fresh.Step(Stays(fresh));
  ASSERT_FALSE(fresh_next.done);
}

// =============================================================================
// Copies, several downs, the cost
// =============================================================================

// A copy keeps the pause, the downs it has seen and the cost.
TEST(TestACopyKeepsThePauseAndTheDownsSeen) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetDownCost(-0.25));
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());
  auto clone = env.Clone();
  ASSERT_TRUE(clone->IsInterrupted());
  ASSERT_TRUE(clone->GetEndReason() == EndReason::Interrupted);
  ASSERT_TRUE(clone->GetDownCost() == -0.25);
  StepResult paused = clone->Step(Stays(*clone));
  ASSERT_TRUE(paused.done);
  for (double reward : paused.rewards) ASSERT_TRUE(reward == 0.0);

  // Seen downs, not paused (the lens changed after the down): a copy's next
  // step finds no new down
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<SynchroLens>()));
  SynchroEnv assigned(10, 10, 3, 1, 0, 7);
  assigned = env;
  auto cloned = env.Clone();
  for (BaseEnv* copy : {cloned.get(), static_cast<BaseEnv*>(&assigned)}) {
    ASSERT_TRUE(copy->GetDownCost() == -0.25);
    StepResult next = copy->Step(Stays(*copy));
    ASSERT_FALSE(next.done);
    ASSERT_FALSE(copy->IsInterrupted());
  }
}

// Two downs in one step pay the cost twice.
TEST(TestTwoDownsInOneStepPayTwice) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_FALSE(env.IsTeamDown());  // 2 of 3, companion 0 stands
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + 2 * kCost);
  }
}

// A custom cost is paid. SetDownCost refuses a non-finite or positive cost
// (keeping the old one) and accepts 0; the cost is runtime data that survives
// Reset and LoadSnapshot.
TEST(TestTheDownCostIsSettable) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetDownCost(-0.2));
  ASSERT_FALSE(env.SetDownCost(std::numeric_limits<double>::quiet_NaN()));
  ASSERT_FALSE(env.SetDownCost(std::numeric_limits<double>::infinity()));
  ASSERT_FALSE(env.SetDownCost(-std::numeric_limits<double>::infinity()));
  ASSERT_FALSE(env.SetDownCost(0.1));
  ASSERT_TRUE(env.GetDownCost() == -0.2);
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  const Snapshot snap = env.SaveSnapshot();
  DownCompanion(env, 1);
  StepResult result = env.Step(Stays(env));
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + -0.2);
  }

  env.Reset(7);
  ASSERT_TRUE(env.GetDownCost() == -0.2);
  env.LoadSnapshot(snap);
  ASSERT_TRUE(env.GetDownCost() == -0.2);

  ASSERT_TRUE(env.SetDownCost(0.0));  // Free downs: still an interruption
  DownCompanion(env, 1);
  StepResult free_down = env.Step(Stays(env));
  ASSERT_TRUE(env.IsInterrupted());
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(free_down.rewards[static_cast<size_t>(i)] == LensReward(env, i));
  }
}

// =============================================================================
// Other envs and lenses
// =============================================================================

// DodgeEnv: a down reads Interrupted (Dodge pays 0 with someone down: the
// step pays the cost alone).
TEST(TestADodgeDownInterrupts) {
  DodgeEnv env(10, 2, 1000, 50, 42);
  MakeArena(env);
  Place(env, 0, {4, 3});
  Place(env, 1, {4, 6});
  DownCompanion(env, 1);
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Interrupted);
  for (double reward : result.rewards) ASSERT_TRUE(reward == kCost);
}

// A lens that opts out of interruptions: a down pays the cost, nothing pauses.
class UninterruptibleLens : public SynchroLens {
 public:
  std::unique_ptr<TaskLens> Clone() const override {
    return std::make_unique<UninterruptibleLens>(*this);
  }
  bool IsInterruptible() const override { return false; }
};

TEST(TestAnUninterruptibleLensPaysButDoesNotPause) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.GetTaskLens()->IsInterruptible());
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<UninterruptibleLens>()));
  const int r = Row(env);
  Place(env, 0, {r, 1});
  Place(env, 1, {r, 4});
  Place(env, 2, {r, 7});
  DownCompanion(env, 1);
  StepResult result = env.Step(Stays(env));
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(result.rewards[static_cast<size_t>(i)] == LensReward(env, i) + kCost);
  }
  StepResult next = env.Step(Stays(env));  // Not new any more
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(next.rewards[static_cast<size_t>(i)] == LensReward(env, i));
  }
}

// Without a lens: rewards 0, no cost, nothing pauses.
TEST(TestWithoutALensNothingPauses) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  ASSERT_TRUE(env.SetTaskLens(nullptr));
  DownCompanion(env, 1);
  StepResult result = env.Step(Stays(env));
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsInterrupted());
  for (double reward : result.rewards) ASSERT_TRUE(reward == 0.0);
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

  std::cout << "Running " << tests.size() << " interrupts tests...\n\n";

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
