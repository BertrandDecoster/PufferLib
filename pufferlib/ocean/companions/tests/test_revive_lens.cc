// Copyright 2024
// Unit tests for ReviveLens (Companions_Lens_Revive, C API 1.6): get the
// downed allies up. Success only once nobody is down; a down while it runs
// joins its goal, pays the down cost and interrupts nothing; only the team
// down or the horizon fails it. Its goal cells are the walkable orthogonal
// neighbours of the goal bodies. The C++ env first, then the C API.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "companions_api.h"
#include "../src/core/annotations.h"
#include "../src/core/object.h"
#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/env/revive_lens.h"
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

#define ASSERT_NEAR(a, b)                                                \
  if (std::abs((a) - (b)) > 1e-6) {                                      \
    std::ostringstream oss;                                             \
    oss << "ASSERT_NEAR failed: " << #a << " (" << (a) << ") != " << #b \
        << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__;       \
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
static Action Walk(MovementAction dir) { return EncodeAction(dir); }
static const Action kStay = EncodeAction(MovementAction::Stay);

static std::vector<Action> Stays(const BaseEnv& env) {
  return std::vector<Action>(static_cast<size_t>(env.NumAgents()), kStay);
}

static bool SetRevive(BaseEnv& env, const std::vector<Position>& cells) {
  LensParams params;
  params.positions = cells;
  return env.SetTaskLensWithParams(std::make_unique<ReviveLens>(), params);
}

static std::vector<Position> Sorted(std::vector<Position> cells) {
  std::sort(cells.begin(), cells.end(), [](Position a, Position b) {
    return a.row != b.row ? a.row < b.row : a.col < b.col;
  });
  return cells;
}

static std::vector<Position> GoalCells(const BaseEnv& env) {
  return Sorted(env.GetTaskLens()->GetGoalCells(env));
}

// The four orthogonal neighbours of an inner cell of the arena
static std::vector<Position> Around(Position p) {
  return Sorted({{p.row - 1, p.col}, {p.row + 1, p.col}, {p.row, p.col - 1}, {p.row, p.col + 1}});
}

static std::vector<Position> Union(std::vector<Position> a, const std::vector<Position>& b) {
  a.insert(a.end(), b.begin(), b.end());
  a = Sorted(a);
  a.erase(std::unique(a.begin(), a.end()), a.end());
  return a;
}

static const double kCost = BaseEnv::kDefaultDownCost;

// =============================================================================
// Kind, validation, activation
// =============================================================================

static_assert(TaskLens::kRevive == 4, "ReviveLens's kind is 4");

TEST(TestKindAndNotInterruptible) {
  ReviveLens lens;
  ASSERT_TRUE(lens.GetKind() == TaskLens::kRevive);
  ASSERT_FALSE(lens.IsInterruptible());
}

// Nobody down: nothing to revive, refused (plain and with params); the
// previous lens stays.
TEST(TestRefusedWhenNobodyIsDown) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  ASSERT_FALSE(env.SetTaskLens(std::make_unique<ReviveLens>()));
  ASSERT_FALSE(SetRevive(env, {}));
  ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kSynchro);
}

// Every cell must hold a downed companion: an empty cell, a standing
// companion's or an out-of-grid one refuses the lens.
TEST(TestEveryCellMustHoldADownedAlly) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 2});
  Place(env, 1, {3, 5});
  Place(env, 2, {3, 8});
  DownCompanion(env, 1);
  ASSERT_FALSE(SetRevive(env, {{4, 4}}));          // Empty floor
  ASSERT_FALSE(SetRevive(env, {{3, 2}}));          // Standing
  ASSERT_FALSE(SetRevive(env, {{3, 5}, {6, 6}}));  // One of two
  ASSERT_FALSE(SetRevive(env, {{-1, 5}}));         // Off the grid
  ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kSynchro);
  ASSERT_TRUE(SetRevive(env, {{3, 5}}));
  ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kRevive);
  const auto* lens = static_cast<const ReviveLens*>(env.GetTaskLens());
  ASSERT_EQ(lens->GetTargets().size(), 1u);
  ASSERT_EQ(lens->GetTargets()[0], AgentAt(env, 1)->GetId());
}

// A refused lens keeps the previous one as it was: a SynchroLens activated
// with params keeps the goal cells it stamped (re-stamped after the refused
// swap deactivated it), and still succeeds.
TEST(TestARefusedReviveKeepsThePreviousLensWorking) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  // Only the lens's own goals: the level's persistent one removed
  AnnotationStore& annotations = env.GetMutableAnnotations();
  for (Position p : annotations.FindCellsWithTag(SemanticTag::SynchroGoal)) {
    annotations.RemoveByKey(AnnotationKey{AnnotationTarget::Cell, p, kInvalidObjectId},
                            SemanticTag::SynchroGoal);
  }
  Place(env, 0, {3, 2});
  Place(env, 1, {5, 5});
  Place(env, 2, {3, 7});
  LensParams synchro;
  synchro.positions = {{3, 3}, {3, 6}};
  ASSERT_TRUE(env.SetTaskLensWithParams(std::make_unique<SynchroLens>(), synchro));
  ASSERT_EQ(GoalCells(env).size(), 2u);

  ASSERT_FALSE(SetRevive(env, {{5, 5}}));  // Nobody down
  ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kSynchro);
  ASSERT_TRUE(GoalCells(env) == Sorted({{3, 3}, {3, 6}}));
  ASSERT_FALSE(env.GetTaskLens()->IsSuccess(env));

  StepResult result =
      env.Step({Walk(MovementAction::Right), kStay, Walk(MovementAction::Left)});
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
}

// With params: the targets are the bodies on the cells, the other downed
// allies are not goals (while a target is down). Without params (plain or
// empty): every downed ally.
TEST(TestTargetsWithAndWithoutParams) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {2, 2});
  Place(env, 1, {4, 4});
  Place(env, 2, {6, 6});
  Place(env, 3, {2, 7});
  DownCompanion(env, 1);
  DownCompanion(env, 2);

  ASSERT_TRUE(SetRevive(env, {{4, 4}}));
  ASSERT_TRUE(GoalCells(env) == Around({4, 4}));
  const auto* lens = static_cast<const ReviveLens*>(env.GetTaskLens());
  ASSERT_TRUE(lens->GetGoalBodies(env) == std::vector<ObjectId>{AgentAt(env, 1)->GetId()});

  const std::vector<Position> both = Union(Around({4, 4}), Around({6, 6}));
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<ReviveLens>()));
  ASSERT_TRUE(GoalCells(env) == both);
  ASSERT_TRUE(SetRevive(env, {}));
  ASSERT_TRUE(GoalCells(env) == both);
  // Both cells: both targets
  ASSERT_TRUE(SetRevive(env, {{4, 4}, {6, 6}}));
  ASSERT_TRUE(GoalCells(env) == both);
}

// =============================================================================
// Success, reward, verdict
// =============================================================================

// Success only when nobody is down: with two down, reviving one is not a
// success; reviving the other is. Rewards: the time penalty each step, plus
// the bonus on the success.
TEST(TestSuccessOnlyWhenNobodyIsDown) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 3});
  Agent* first = Place(env, 1, {3, 4});
  Agent* second = Place(env, 2, {3, 6});
  Place(env, 3, {3, 7});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(SetRevive(env, {{3, 4}, {3, 6}}));

  StepResult one = env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_FALSE(first->IsDowned());
  ASSERT_TRUE(second->IsDowned());
  ASSERT_FALSE(one.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_FALSE(env.GetTaskLens()->IsSuccess(env));
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (double reward : one.rewards) ASSERT_NEAR(reward, ReviveLens::kTimePenalty);
  ASSERT_TRUE(GoalCells(env) == Around({3, 6}));

  StepResult two = env.Step({kStay, kStay, kStay, Use(MovementAction::Left)});
  ASSERT_FALSE(second->IsDowned());
  ASSERT_TRUE(two.done);
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Success);
  for (double reward : two.rewards) {
    ASSERT_NEAR(reward, ReviveLens::kTimePenalty + ReviveLens::kSuccessBonus);
  }
  ASSERT_TRUE(env.GetTaskLens()->GetGoalCells(env).empty());
}

// A down while the lens runs pays the down cost, joins the goal, and
// interrupts nothing (not done, no Interrupted).
TEST(TestADownDuringTheLensJoinsTheGoal) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {2, 2});
  Place(env, 1, {4, 4});
  Agent* late = Place(env, 2, {6, 6});
  Place(env, 3, {2, 7});
  DownCompanion(env, 1);
  ASSERT_TRUE(SetRevive(env, {{4, 4}}));
  ASSERT_TRUE(GoalCells(env) == Around({4, 4}));

  SpawnKillNextStep(env, {6, 6});
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(late->IsDowned());
  ASSERT_FALSE(result.done);
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsInterrupted());
  ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  for (double reward : result.rewards) ASSERT_NEAR(reward, ReviveLens::kTimePenalty + kCost);
  ASSERT_TRUE(GoalCells(env) == Union(Around({4, 4}), Around({6, 6})));

  // The next step pays the time penalty alone
  StepResult next = env.Step(Stays(env));
  ASSERT_FALSE(next.done);
  for (double reward : next.rewards) ASSERT_NEAR(reward, ReviveLens::kTimePenalty);
}

// A target revived, then downed again, is still a goal (it has gone down
// since the activation).
TEST(TestARevivedTargetDownedAgainIsAGoal) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 3});
  Agent* target = Place(env, 1, {3, 4});
  Place(env, 2, {6, 6});
  Place(env, 3, {6, 2});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(SetRevive(env, {{3, 4}}));
  env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_FALSE(target->IsDowned());
  // Companion 2 was down before the activation, not a target: the fallback
  ASSERT_TRUE(GoalCells(env) == Around({6, 6}));
  SpawnKillNextStep(env, {3, 4});
  env.Step(Stays(env));
  ASSERT_TRUE(target->IsDowned());
  // The target again, not the fallback
  ASSERT_TRUE(GoalCells(env) == Around({3, 4}));
}

// The fallback: once the goal set is empty while someone is still down (a
// non-target down before the activation), every downed ally is a goal.
TEST(TestTheFallbackToEveryDownedAlly) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {3, 3});
  Agent* target = Place(env, 1, {3, 4});
  Agent* other = Place(env, 2, {6, 6});
  Place(env, 3, {6, 2});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(SetRevive(env, {{3, 4}}));
  ASSERT_TRUE(GoalCells(env) == Around({3, 4}));
  StepResult result = env.Step({Use(MovementAction::Right), kStay, kStay, kStay});
  ASSERT_FALSE(target->IsDowned());
  ASSERT_TRUE(other->IsDowned());
  ASSERT_FALSE(result.done);
  const auto* lens = static_cast<const ReviveLens*>(env.GetTaskLens());
  ASSERT_TRUE(lens->GetGoalBodies(env) == std::vector<ObjectId>{other->GetId()});
  ASSERT_TRUE(GoalCells(env) == Around({6, 6}));
}

// It fails only by the horizon (Horizon, not a success)...
TEST(TestItFailsByTheHorizon) {
  SynchroEnv env(10, 10, 3, 1, 0, 42, 0, 3);
  MakeArena(env);
  Place(env, 0, {3, 2});
  Place(env, 1, {3, 5});
  Place(env, 2, {3, 8});
  DownCompanion(env, 1);
  ASSERT_TRUE(SetRevive(env, {{3, 5}}));
  for (int i = 0; i < 2; ++i) {
    StepResult result = env.Step(Stays(env));
    ASSERT_FALSE(result.done);
    ASSERT_TRUE(env.GetEndReason() == EndReason::None);
  }
  StepResult last = env.Step(Stays(env));
  ASSERT_TRUE(last.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::Horizon);
}

// ...or by the team down (the 3rd down during the lens): TeamDown
TEST(TestItFailsByTheTeamDown) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  ASSERT_EQ(env.GetMaxDowns(), 3);
  Place(env, 0, {2, 2});
  Place(env, 1, {4, 4});
  Place(env, 2, {6, 6});
  Place(env, 3, {2, 7});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(SetRevive(env, {}));
  ASSERT_FALSE(env.IsDone());
  SpawnKillNextStep(env, {2, 7});
  StepResult result = env.Step(Stays(env));
  ASSERT_TRUE(result.done);
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_TRUE(env.GetEndReason() == EndReason::TeamDown);
  for (double reward : result.rewards) ASSERT_NEAR(reward, ReviveLens::kTimePenalty + kCost);
}

// =============================================================================
// Goal cells
// =============================================================================

// The walkable orthogonal neighbours of the goal bodies holding no downed
// body: no wall, no hazard, no body; a standing agent's cell still counts
// (walkable terrain). The tensor's plane 2 marks the same cells.
TEST(TestGoalCellsExcludeWallsHazardsAndBodies) {
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {2, 1});  // Standing, on a goal cell
  Place(env, 1, {1, 1});  // Corner body
  Place(env, 2, {1, 2});  // Its neighbour, a body too
  Place(env, 3, {5, 5});
  env.GetMutableGrid().SetCell({1, 3}, CellKind::Hazard);
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(env.SetTaskLens(std::make_unique<ReviveLens>()));
  const std::vector<Position> expected = Sorted({{2, 1}, {2, 2}});
  ASSERT_TRUE(GoalCells(env) == expected);

  std::vector<float> tensor;
  env.ObservationTensor(tensor, 3);
  const int rows = env.GetRows();
  const int cols = env.GetCols();
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const bool goal = std::find(expected.begin(), expected.end(), Position{r, c}) != expected.end();
      ASSERT_EQ(env.GetTaskLens()->IsGoalCell(env, {r, c}), goal);
      ASSERT_EQ(tensor[static_cast<size_t>(2 * rows * cols + r * cols + c)], goal ? 1.0f : 0.0f);
    }
  }
}

// The goal-distance feature: the Manhattan distance to the nearest goal cell
// (a free neighbour of the body), normalized by twice the largest dimension.
TEST(TestTheGoalDistanceFeature) {
  SynchroEnv env(10, 10, 3, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {5, 1});
  Place(env, 1, {5, 5});
  Place(env, 2, {1, 8});
  DownCompanion(env, 1);
  ASSERT_TRUE(SetRevive(env, {{5, 5}}));
  std::vector<float> obs(static_cast<size_t>(env.VectorObservationSize()));
  env.WriteVectorObservation(obs.data(), 0);
  // (5, 1) to (5, 4): 3
  ASSERT_NEAR(obs[BaseEnv::kVectorObsGoalDistance], 3.0f / 20.0f);
  env.WriteVectorObservation(obs.data(), 2);
  // (1, 8) to (4, 5): 6
  ASSERT_NEAR(obs[BaseEnv::kVectorObsGoalDistance], 6.0f / 20.0f);
}

// =============================================================================
// Clone
// =============================================================================

// Clone keeps the targets (and the downs at the activation): a clone targets
// the one body, not every downed ally, and so does a copied env's lens.
TEST(TestCloneKeepsTheTargets) {
  ScopedEffectRegistry scoped_registry;
  SynchroEnv env(10, 10, 4, 1, 0, 42);
  MakeArena(env);
  Place(env, 0, {2, 2});
  Place(env, 1, {4, 4});
  Place(env, 2, {6, 6});
  Place(env, 3, {2, 7});
  DownCompanion(env, 1);
  DownCompanion(env, 2);
  ASSERT_TRUE(SetRevive(env, {{4, 4}}));

  std::unique_ptr<TaskLens> clone = env.GetTaskLens()->Clone();
  ASSERT_TRUE(clone->GetKind() == TaskLens::kRevive);
  const auto* revive = static_cast<const ReviveLens*>(clone.get());
  ASSERT_TRUE(revive->GetTargets() == static_cast<const ReviveLens*>(env.GetTaskLens())->GetTargets());
  ASSERT_TRUE(Sorted(clone->GetGoalCells(env)) == Around({4, 4}));

  std::unique_ptr<BaseEnv> copy = env.Clone();
  ASSERT_TRUE(GoalCells(*copy) == Around({4, 4}));
  // The downs at the activation too: a down in the copy joins its goal
  SpawnKillNextStep(*copy, {2, 7});
  copy->Step(Stays(*copy));
  ASSERT_TRUE(AgentAt(*copy, 3)->IsDowned());
  ASSERT_TRUE(GoalCells(*copy) == Union(Around({4, 4}), Around({2, 7})));
}

// =============================================================================
// C API
// =============================================================================

// A 10x10 arena with 3 companions at `positions`, loaded through the C API.
// The level's persistent goal replaced by one at `goal`.
static Companions_Env* LoadArena(const std::vector<Position>& positions, Position goal) {
  SynchroEnv cpp(10, 10, 3, 1, 0, 42);
  MakeArena(cpp);
  AnnotationStore& annotations = cpp.GetMutableAnnotations();
  for (Position p : annotations.FindCellsWithTag(SemanticTag::SynchroGoal)) {
    annotations.RemoveByKey(AnnotationKey{AnnotationTarget::Cell, p, kInvalidObjectId},
                            SemanticTag::SynchroGoal);
  }
  annotations.Add(AnnotationKey{AnnotationTarget::Cell, goal, kInvalidObjectId},
                  Annotation{SemanticTag::SynchroGoal, {}, -1});
  for (size_t i = 0; i < positions.size(); ++i) Place(cpp, static_cast<int>(i), positions[i]);
  const std::string json = SnapshotToJson(cpp.SaveSnapshot());

  Companions_EnvConfig config = {};
  config.rows = 10;
  config.cols = 10;
  config.num_companions = 3;
  config.num_synchro = 1;
  config.horizon = 100;
  config.seed = 42;
  Companions_Env* env = companions_create(&config);
  ASSERT_TRUE(env != nullptr);
  if (!companions_load_snapshot_json(env, json.c_str())) {
    throw std::runtime_error(std::string("the arena did not load: ") + companions_get_error());
  }
  return env;
}

static void ApiKill(Companions_Env* env, Position p) {
  ASSERT_TRUE(companions_spawn_effect(env, "kill", p.row, p.col, Companions_Direction_Up, -1));
}

static Companions_AgentState ApiAgent(Companions_Env* env, int32_t index) {
  Companions_AgentState a = {};
  ASSERT_TRUE(companions_get_agent_by_index(env, index, &a));
  return a;
}

static int CountEpisodeEnds(const Companions_StepResult& r) {
  int n = 0;
  for (int32_t i = 0; i < r.event_count; ++i) n += r.events[i].type == Companions_Event_EpisodeEnd;
  return n;
}

static bool ApiIsGoal(Companions_Env* env, Position p) {
  return companions_has_tag_at(env, p.row, p.col, static_cast<int32_t>(SemanticTag::SynchroGoal));
}

static_assert(Companions_Lens_Revive == 4 &&
                  static_cast<int>(Companions_Lens_Revive) == static_cast<int>(TaskLens::kRevive),
              "Companions_Lens_Revive is 4, TaskLens::kRevive");

// Set with and without cells, read back; refused while nobody is down.
TEST(TestApiSetAndGetTheReviveLens) {
  Companions_Env* env = LoadArena({{3, 2}, {3, 5}, {3, 8}}, {6, 6});
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Synchro);
  ASSERT_FALSE(companions_set_task_lens(env, Companions_Lens_Revive));
  ASSERT_EQ(std::string(companions_get_error()),
            std::string("Lens incompatible with current environment state"));
  ASSERT_FALSE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, nullptr, 0));
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Synchro);

  ApiKill(env, {3, 5});
  ASSERT_TRUE(ApiAgent(env, 1).downed);
  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Revive));
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Revive);
  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Synchro));
  ASSERT_TRUE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, nullptr, 0));
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Revive);
  ASSERT_TRUE(companions_set_task_lens(env, Companions_Lens_Synchro));
  const Companions_Position cell = {3, 5};
  ASSERT_TRUE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, &cell, 1));
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Revive);
  ASSERT_FALSE(companions_is_done(env));
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_None);
  companions_destroy(env);
}

// A cell without a downed ally is refused: the previous lens stays, with the
// goal cell it stamped, and still works (a success).
TEST(TestApiARefusedCellKeepsThePreviousLens) {
  // Companion 0 one step left of the persistent goal (2, 2); companion 2 one
  // step left of the stamped goal (5, 8)
  Companions_Env* env = LoadArena({{2, 1}, {7, 4}, {5, 7}}, {2, 2});
  const Companions_Position stamped = {5, 8};
  ASSERT_TRUE(companions_set_task_lens_with_params(env, Companions_Lens_Synchro, &stamped, 1));
  ASSERT_TRUE(ApiIsGoal(env, {5, 8}));

  ApiKill(env, {7, 4});
  const Companions_Position empty = {4, 4};
  ASSERT_FALSE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, &empty, 1));
  const Companions_Position standing = {2, 1};
  ASSERT_FALSE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, &standing, 1));
  const Companions_Position mixed[2] = {{7, 4}, {4, 4}};
  ASSERT_FALSE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, mixed, 2));
  ASSERT_TRUE(companions_get_task_lens(env) == Companions_Lens_Synchro);
  ASSERT_TRUE(ApiIsGoal(env, {5, 8}));
  ASSERT_TRUE(ApiIsGoal(env, {2, 2}));

  // Still works: the downed one is no objective; the two standing cover both
  // goals. (The down, seen by this step, interrupts the task: a success wins.)
  Companions_Action actions[3] = {{Companions_Movement_Right, Companions_Interact_None},
                                  {Companions_Movement_Stay, Companions_Interact_None},
                                  {Companions_Movement_Right, Companions_Interact_None}};
  Companions_StepResult result = {};
  ASSERT_TRUE(companions_step(env, actions, 3, &result));
  ASSERT_TRUE(result.state.done);
  ASSERT_TRUE(result.state.success);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Success);
  companions_destroy(env);
}

// A whole episode: one companion down, the two others walk to it, one
// revives it (slot 0 is revive next to a downed ally), Success with the bonus,
// and EpisodeEnd(Success).
TEST(TestApiAWholeReviveEpisode) {
  Companions_Env* env = LoadArena({{5, 2}, {5, 5}, {5, 8}}, {1, 1});
  ApiKill(env, {5, 5});
  ASSERT_TRUE(ApiAgent(env, 1).downed);
  const Companions_Position body = {5, 5};
  ASSERT_TRUE(companions_set_task_lens_with_params(env, Companions_Lens_Revive, &body, 1));

  const Companions_Action stay = {Companions_Movement_Stay, Companions_Interact_None};
  const Companions_Action right = {Companions_Movement_Right, Companions_Interact_None};
  const Companions_Action left = {Companions_Movement_Left, Companions_Interact_None};
  Companions_StepResult result = {};
  const std::vector<std::vector<Companions_Action>> walks = {{right, stay, left}, {right, stay, left}};
  for (const auto& actions : walks) {
    ASSERT_TRUE(companions_step(env, actions.data(), 3, &result));
    ASSERT_FALSE(result.state.done);
    ASSERT_EQ(CountEpisodeEnds(result), 0);
    for (int i = 0; i < 3; ++i) ASSERT_NEAR(result.state.rewards[i], static_cast<float>(ReviveLens::kTimePenalty));
  }
  ASSERT_TRUE(ApiAgent(env, 0).position.col == 4);
  ASSERT_TRUE(ApiAgent(env, 2).position.col == 6);
  ASSERT_TRUE(ApiAgent(env, 1).downed);

  const Companions_Action revive[3] = {{Companions_Movement_Right, Companions_Interact_Skill1}, stay, stay};
  ASSERT_TRUE(companions_step(env, revive, 3, &result));
  ASSERT_FALSE(ApiAgent(env, 1).downed);
  ASSERT_TRUE(result.state.done);
  ASSERT_TRUE(result.state.success);
  ASSERT_EQ(companions_get_end_reason(env), Companions_End_Success);
  ASSERT_EQ(CountEpisodeEnds(result), 1);
  bool revived = false;
  for (int32_t i = 0; i < result.event_count; ++i) {
    const Companions_Event& e = result.events[i];
    if (e.type == Companions_Event_AgentRevived) revived = true;
    if (e.type == Companions_Event_EpisodeEnd) {
      ASSERT_EQ(e.effect_id, static_cast<int32_t>(Companions_End_Success));
      ASSERT_TRUE(e.episode_success);
    }
  }
  ASSERT_TRUE(revived);
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(result.state.rewards[i],
                static_cast<float>(ReviveLens::kTimePenalty + ReviveLens::kSuccessBonus));
  }
  companions_destroy(env);
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

  std::cout << "Running " << tests.size() << " revive lens tests...\n\n";

  int passed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << "\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << ": " << e.what() << "\n";
    }
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
