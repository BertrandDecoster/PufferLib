// Copyright 2024
// Unit tests for the RL binding's C wrapper (synchro.h / synchro_wrapper.cc),
// driven as binding.c drives it: a zeroed Synchro struct with its own
// buffers, synchro_init, c_reset, c_step. An interruption (a down: C API 1.6
// EndReason::Interrupted) is a truncation, not a terminal; a success or the
// horizon is a terminal. The revive task (task 1) starts every episode with
// one companion down, under the ReviveLens. The down cost reaches the rewards;
// a bad one is refused by synchro_init.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "synchro.h"
#include "../src/core/object.h"
#include "../src/env/revive_lens.h"
#include "../src/env/synchro_env.h"

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

#define ASSERT_FALSE(cond)                                              \
  if (cond) {                                                           \
    std::ostringstream oss;                                             \
    oss << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__       \
        << ":" << __LINE__;                                             \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_EQ(a, b)                                                 \
  if ((a) != (b)) {                                                     \
    std::ostringstream oss;                                             \
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b   \
        << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__;       \
    throw std::runtime_error(oss.str());                                \
  }

#define ASSERT_NEAR(a, b)                                               \
  if (std::abs((a) - (b)) > 1e-5) {                                     \
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

struct Options {
  int num_agents = 3;
  int rows = 8;
  int cols = 8;
  int map_complexity = 2;
  int horizon = 50;
  int task = SYNCHRO_TASK_SYNCHRO;
  float down_cost = -0.5f;
  unsigned int seed = 7;
};

// A Synchro struct with its own buffers, as binding.c gives it (zeroed, the
// buffers pointed at, the kwargs copied in)
class WrappedEnv {
 public:
  explicit WrappedEnv(const Options& o) {
    std::memset(&env_, 0, sizeof(env_));
    env_.num_agents = o.num_agents;
    env_.rows = o.rows;
    env_.cols = o.cols;
    env_.num_synchro = o.num_agents;
    env_.map_complexity = o.map_complexity;
    env_.horizon = o.horizon;
    env_.task = o.task;
    env_.down_cost = o.down_cost;
    env_.seed = o.seed;
    stride_ = 5 * o.rows * o.cols + BaseEnv::kVectorObsBaseSize;
    // The whole buffer is poisoned: any float the wrapper leaves unwritten
    // shows up
    observations_.assign(static_cast<size_t>(o.num_agents * stride_),
                         std::numeric_limits<float>::quiet_NaN());
    actions_.assign(static_cast<size_t>(o.num_agents * 2), 0);
    rewards_.assign(static_cast<size_t>(o.num_agents), 0.0f);
    terminals_.assign(static_cast<size_t>(o.num_agents), 0);
    truncations_.assign(static_cast<size_t>(o.num_agents), 0);
    env_.observations = observations_.data();
    env_.actions = actions_.data();
    env_.rewards = rewards_.data();
    env_.terminals = terminals_.data();
    env_.truncations = truncations_.data();
    init_result_ = synchro_init(&env_);
  }
  ~WrappedEnv() { c_close(&env_); }
  WrappedEnv(const WrappedEnv&) = delete;
  WrappedEnv& operator=(const WrappedEnv&) = delete;

  int InitResult() const { return init_result_; }
  Synchro& Raw() { return env_; }
  SynchroEnv& Cpp() { return *static_cast<SynchroEnv*>(env_.cpp_env); }
  int Stride() const { return stride_; }
  int TensorSize() const { return 5 * env_.rows * env_.cols; }

  void Reset() { c_reset(&env_); }
  // Every agent stays, but for `agent` (if >= 0) doing (movement, interact)
  void Step(int agent = -1, int movement = 0, int interact = 0) {
    std::fill(actions_.begin(), actions_.end(), 0);
    if (agent >= 0) {
      actions_[static_cast<size_t>(agent * 2)] = movement;
      actions_[static_cast<size_t>(agent * 2 + 1)] = interact;
    }
    c_step(&env_);
  }

  const std::vector<float>& Obs() const { return observations_; }
  float Reward(int i) const { return rewards_[static_cast<size_t>(i)]; }
  unsigned char Terminal(int i) const { return terminals_[static_cast<size_t>(i)]; }
  unsigned char Truncation(int i) const { return truncations_[static_cast<size_t>(i)]; }
  bool AllTerminal() const {
    return std::all_of(terminals_.begin(), terminals_.end(), [](unsigned char t) { return t == 1; });
  }
  bool NoTerminal() const {
    return std::all_of(terminals_.begin(), terminals_.end(), [](unsigned char t) { return t == 0; });
  }
  bool AllTruncated() const {
    return std::all_of(truncations_.begin(), truncations_.end(), [](unsigned char t) { return t == 1; });
  }
  bool NoTruncation() const {
    return std::all_of(truncations_.begin(), truncations_.end(), [](unsigned char t) { return t == 0; });
  }

 private:
  Synchro env_;
  int stride_ = 0;
  int init_result_ = -1;
  std::vector<float> observations_;
  std::vector<int> actions_;
  std::vector<float> rewards_;
  std::vector<unsigned char> terminals_;
  std::vector<unsigned char> truncations_;
};

static Companion* CompanionAt(BaseEnv& env, int index) {
  for (Companion* c : env.GetMutableObjectManager().GetAllCompanions()) {
    if (c->GetAgentIndex() == index) return c;
  }
  throw std::runtime_error("no companion " + std::to_string(index));
}

static std::vector<int> DownedIndices(BaseEnv& env) {
  std::vector<int> out;
  for (Companion* c : env.GetMutableObjectManager().GetAllCompanions()) {
    if (c->IsAlive() && c->IsDowned()) out.push_back(c->GetAgentIndex());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Down between steps (the host): the next step reads the down
static void DownCompanion(BaseEnv& env, int index) {
  Companion* c = CompanionAt(env, index);
  c->TakeDamage(c->GetHealth());
  ASSERT_TRUE(c->IsDowned());
}

static bool IsOnCell(const BaseEnv& env, Position p) { return env.GetObjectManager().IsOccupied(p); }

// Every companion onto a synchro cell (between steps): those already on one
// stay, the others take the free ones
static void PlaceAllOnSynchro(SynchroEnv& env) {
  const std::vector<Position>& cells = env.GetSynchroPositions();
  std::vector<Position> free_cells;
  for (Position p : cells) {
    if (!IsOnCell(env, p)) free_cells.push_back(p);
  }
  for (Companion* c : env.GetMutableObjectManager().GetAllCompanions()) {
    if (std::find(cells.begin(), cells.end(), c->GetPosition()) != cells.end()) continue;
    ASSERT_FALSE(free_cells.empty());
    env.GetMutableObjectManager().UpdatePosition(c->GetId(), free_cells.back());
    free_cells.pop_back();
  }
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), static_cast<int>(cells.size()));
}

static int MovementToward(Position from, Position to) {
  if (to.row < from.row) return 1;  // Up
  if (to.row > from.row) return 2;  // Down
  if (to.col < from.col) return 3;  // Left
  return 4;                         // Right
}

// The wrapper's observation of every agent is the env's tensor + vector
// observation, at a stride of tensor + 12 floats
static void AssertObservationsMatch(WrappedEnv& w) {
  SynchroEnv& env = w.Cpp();
  for (int i = 0; i < w.Raw().num_agents; ++i) {
    std::vector<float> tensor;
    std::vector<float> vec;
    env.ObservationTensor(tensor, i);
    env.VectorObservation(vec, i);
    ASSERT_EQ(static_cast<int>(tensor.size()), w.TensorSize());
    ASSERT_EQ(static_cast<int>(vec.size()), 12);
    const float* row = w.Obs().data() + static_cast<size_t>(i * w.Stride());
    for (size_t k = 0; k < tensor.size(); ++k) ASSERT_EQ(row[k], tensor[k]);
    for (size_t k = 0; k < vec.size(); ++k) ASSERT_EQ(row[tensor.size() + k], vec[k]);
  }
}

// =============================================================================
// Tests
// =============================================================================

// The vector obs is 12 floats (BaseEnv::kVectorObsBaseSize), and the
// wrapper writes each agent at a stride of tensor + 12
TEST(TestTheObservationStrideIs12) {
  WrappedEnv w(Options{});
  ASSERT_EQ(w.InitResult(), 0);
  ASSERT_EQ(w.Raw().vector_obs_size, 12);
  w.Reset();
  AssertObservationsMatch(w);
  w.Step();
  AssertObservationsMatch(w);
}

// A down interrupts the Synchro task: done as Interrupted, which the binding
// reports as a truncation (every agent), not a terminal. The episode then
// auto-resets (nobody down in the new one).
TEST(TestADownIsATruncationNotATerminal) {
  WrappedEnv w(Options{});
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  w.Step();
  ASSERT_TRUE(w.NoTerminal());
  ASSERT_TRUE(w.NoTruncation());
  DownCompanion(w.Cpp(), 1);
  const float log_n = w.Raw().log.n;
  w.Step();
  ASSERT_TRUE(w.AllTruncated());
  ASSERT_TRUE(w.NoTerminal());
  ASSERT_EQ(w.Raw().log.n, log_n + 1.0f);  // An episode ended
  // Auto-reset: a new episode, nobody down, its observations written
  ASSERT_TRUE(DownedIndices(w.Cpp()).empty());
  ASSERT_EQ(w.Cpp().GetTick(), 0);
  AssertObservationsMatch(w);
  // The next step clears the flags
  w.Step();
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_TRUE(w.NoTerminal());
}

// A success is a terminal, not a truncation
TEST(TestASuccessIsATerminal) {
  WrappedEnv w(Options{});
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  PlaceAllOnSynchro(w.Cpp());
  w.Step();
  ASSERT_TRUE(w.AllTerminal());
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_EQ(w.Raw().log.success_rate, 1.0f);
  ASSERT_EQ(w.Raw().log.n, 1.0f);
  ASSERT_EQ(w.Cpp().GetTick(), 0);  // Auto-reset
}

// The horizon is a terminal, not a truncation
TEST(TestTheHorizonIsATerminal) {
  Options o;
  o.horizon = 3;
  WrappedEnv w(o);
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  // Keep anyone from succeeding by chance: nobody moves
  for (int t = 1; t < 3; ++t) {
    w.Step();
    ASSERT_TRUE(w.NoTerminal());
    ASSERT_TRUE(w.NoTruncation());
  }
  w.Step();
  ASSERT_TRUE(w.AllTerminal());
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_EQ(w.Raw().log.success_rate, 0.0f);
  ASSERT_EQ(w.Raw().log.n, 1.0f);
}

// A reset clears the truncations (and terminals) left by the last step
TEST(TestAResetClearsTheTruncations) {
  WrappedEnv w(Options{});
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  DownCompanion(w.Cpp(), 0);
  w.Step();
  ASSERT_TRUE(w.AllTruncated());
  w.Reset();
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_TRUE(w.NoTerminal());
}

// The down cost set through the struct reaches the rewards: the interrupted
// step pays the lens's reward plus the cost, once per new down
TEST(TestTheDownCostReachesTheRewards) {
  for (float cost : {-0.25f, -0.5f, 0.0f}) {
    Options o;
    o.down_cost = cost;
    WrappedEnv w(o);
    ASSERT_EQ(w.InitResult(), 0);
    ASSERT_NEAR(w.Cpp().GetDownCost(), static_cast<double>(cost));
    w.Reset();
    DownCompanion(w.Cpp(), 0);
    DownCompanion(w.Cpp(), 2);
    // The same step on a copy, which does not auto-reset
    std::unique_ptr<BaseEnv> twin = w.Cpp().Clone();
    StepResult expected = twin->Step(std::vector<Action>(3, EncodeAction(MovementAction::Stay)));
    ASSERT_TRUE(twin->IsInterrupted());
    w.Step();
    ASSERT_TRUE(w.AllTruncated());
    for (int i = 0; i < 3; ++i) {
      const double lens = twin->GetTaskLens()->ComputeReward(*twin, i);
      ASSERT_NEAR(expected.rewards[static_cast<size_t>(i)], lens + 2.0 * cost);
      ASSERT_NEAR(static_cast<double>(w.Reward(i)), lens + 2.0 * cost);
    }
  }
}

// A bad down cost (positive, not finite, below BaseEnv::kMinDownCost) is
// refused: synchro_init fails (non-zero), says why in `error`, and leaves no
// env behind (c_close is still safe)
TEST(TestABadDownCostIsRefused) {
  for (float cost : {0.5f, std::numeric_limits<float>::quiet_NaN(),
                     std::numeric_limits<float>::infinity(), -2e6f}) {
    Options o;
    o.down_cost = cost;
    WrappedEnv w(o);
    ASSERT_TRUE(w.InitResult() != 0);
    ASSERT_TRUE(w.Raw().cpp_env == nullptr);
    ASSERT_TRUE(std::string(w.Raw().error).find("down_cost") != std::string::npos);
  }
}

// An unknown task, or the revive task without an ally to revive with, is
// refused the same way; so is a config the env rejects
TEST(TestABadConfigIsRefused) {
  {
    Options o;
    o.task = 2;
    WrappedEnv w(o);
    ASSERT_TRUE(w.InitResult() != 0);
    ASSERT_TRUE(w.Raw().cpp_env == nullptr);
    ASSERT_TRUE(std::string(w.Raw().error).find("task") != std::string::npos);
  }
  {
    Options o;
    o.task = SYNCHRO_TASK_REVIVE;
    o.num_agents = 1;
    WrappedEnv w(o);
    ASSERT_TRUE(w.InitResult() != 0);
    ASSERT_TRUE(w.Raw().cpp_env == nullptr);
    ASSERT_TRUE(std::string(w.Raw().error).find("revive") != std::string::npos);
  }
  {
    Options o;
    o.num_agents = 0;
    WrappedEnv w(o);
    ASSERT_TRUE(w.InitResult() != 0);
    ASSERT_TRUE(w.Raw().cpp_env == nullptr);
    ASSERT_TRUE(std::string(w.Raw().error).size() > 0);
  }
}

// The revive task: every episode starts with one companion down (one of the
// team's downs: 1 of 3), under the ReviveLens (every downed ally), not done;
// its horizon is a terminal, and the auto-reset starts another such episode
TEST(TestTheReviveTaskStartsWithOneDown) {
  Options o;
  o.task = SYNCHRO_TASK_REVIVE;
  o.horizon = 4;
  WrappedEnv w(o);
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  std::vector<std::vector<int>> downed_per_episode;
  for (int episode = 0; episode < 6; ++episode) {
    SynchroEnv& env = w.Cpp();
    ASSERT_TRUE(env.GetTaskLens()->GetKind() == TaskLens::kRevive);
    const auto* lens = static_cast<const ReviveLens*>(env.GetTaskLens());
    ASSERT_TRUE(lens->GetTargets().empty());
    const std::vector<int> downed = DownedIndices(env);
    ASSERT_EQ(downed.size(), 1u);
    ASSERT_EQ(env.GetDowns(), 1);
    ASSERT_EQ(env.GetMaxDowns(), 3);
    ASSERT_EQ(env.GetTick(), 0);
    ASSERT_FALSE(env.IsDone());
    downed_per_episode.push_back(downed);
    // The downed flag in its own observation, at the stride
    const float* self = w.Obs().data() + static_cast<size_t>(downed[0] * w.Stride());
    ASSERT_EQ(self[w.TensorSize() + BaseEnv::kVectorObsDowned], 1.0f);
    AssertObservationsMatch(w);
    for (int t = 1; t <= 4; ++t) {
      w.Step();
      for (int i = 0; i < 3; ++i) {
        ASSERT_NEAR(static_cast<double>(w.Reward(i)), ReviveLens::kTimePenalty);
      }
      ASSERT_TRUE(w.NoTruncation());
      if (t < 4) {
        ASSERT_TRUE(w.NoTerminal());
      } else {
        ASSERT_TRUE(w.AllTerminal());  // The horizon
      }
    }
  }
  // The seed picks each episode's downed companion: not always the same one
  bool varies = false;
  for (const auto& d : downed_per_episode) varies = varies || d != downed_per_episode[0];
  ASSERT_TRUE(varies);
}

// The revive task's success is a terminal: an ally beside the body uses its
// slot-0 skill (the context revive) toward it
TEST(TestAReviveIsATerminalSuccess) {
  Options o;
  o.task = SYNCHRO_TASK_REVIVE;
  WrappedEnv w(o);
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  SynchroEnv& env = w.Cpp();
  const int body_index = DownedIndices(env)[0];
  const Position body = CompanionAt(env, body_index)->GetPosition();
  const int ally_index = body_index == 0 ? 1 : 0;
  Companion* ally = CompanionAt(env, ally_index);
  // Beside the body (the host moves it between steps), on a free goal cell
  Position beside{-1, -1};
  for (Position p : env.GetTaskLens()->GetGoalCells(env)) {
    if (!IsOnCell(env, p) || p == ally->GetPosition()) {
      beside = p;
      break;
    }
  }
  ASSERT_TRUE(beside.row >= 0);
  env.GetMutableObjectManager().UpdatePosition(ally->GetId(), beside);
  w.Step(ally_index, MovementToward(beside, body), 1);
  ASSERT_TRUE(w.AllTerminal());
  ASSERT_TRUE(w.NoTruncation());
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(static_cast<double>(w.Reward(i)),
                ReviveLens::kTimePenalty + ReviveLens::kSuccessBonus);
  }
  ASSERT_EQ(w.Raw().log.success_rate, 1.0f);
  // The auto-reset: another revive episode
  ASSERT_EQ(DownedIndices(w.Cpp()).size(), 1u);
  ASSERT_TRUE(w.Cpp().GetTaskLens()->GetKind() == TaskLens::kRevive);
}

// A second down in the revive task interrupts nothing (the ReviveLens is not
// interruptible): no truncation, the down cost paid, the body joins the goal.
// The third down is the team's last: TeamDown, a terminal.
TEST(TestTheReviveTaskIsNotInterrupted) {
  Options o;
  o.task = SYNCHRO_TASK_REVIVE;
  WrappedEnv w(o);
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  SynchroEnv& env = w.Cpp();
  std::vector<int> standing;
  for (int i = 0; i < 3; ++i) {
    if (!CompanionAt(env, i)->IsDowned()) standing.push_back(i);
  }
  ASSERT_EQ(standing.size(), 2u);
  DownCompanion(env, standing[0]);
  w.Step();
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_TRUE(w.NoTerminal());
  for (int i = 0; i < 3; ++i) {
    ASSERT_NEAR(static_cast<double>(w.Reward(i)), ReviveLens::kTimePenalty - 0.5);
  }
  ASSERT_EQ(env.GetDowns(), 2);
  DownCompanion(env, standing[1]);
  w.Step();
  ASSERT_TRUE(w.AllTerminal());  // TeamDown
  ASSERT_TRUE(w.NoTruncation());
  ASSERT_EQ(w.Raw().log.success_rate, 0.0f);
}

// With 2 companions, the revive task starts with the only other one
// standing: its down leaves nobody standing (all down: TeamDown at downs 2
// of 3), a terminal
TEST(TestTheReviveTaskWithTwoCompanions) {
  Options o;
  o.task = SYNCHRO_TASK_REVIVE;
  o.num_agents = 2;
  WrappedEnv w(o);
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  SynchroEnv& env = w.Cpp();
  ASSERT_EQ(env.GetDowns(), 1);
  const int standing = DownedIndices(env)[0] == 0 ? 1 : 0;
  DownCompanion(env, standing);
  ASSERT_TRUE(env.IsTeamDown());
  ASSERT_EQ(env.GetDowns(), 2);
  w.Step();
  ASSERT_TRUE(w.AllTerminal());
  ASSERT_TRUE(w.NoTruncation());
}

// Friendly fire (reported, not changed): the generated companions' slot-0
// skill is the default attack, which spares allies. A standing companion's
// Skill1 aimed at an ally leaves it unhurt, so the standing companions cannot
// down each other in Synchro RL.
TEST(TestSkill1SparesAnAlly) {
  WrappedEnv w(Options{});
  ASSERT_EQ(w.InitResult(), 0);
  w.Reset();
  SynchroEnv& env = w.Cpp();
  Companion* a = CompanionAt(env, 0);
  Companion* b = CompanionAt(env, 1);
  ASSERT_EQ(a->GetSkill(0), std::string(kDefaultSkill));
  // Side by side on a free floor pair
  const Grid& grid = env.GetGrid();
  Position pa{-1, -1};
  for (int r = 1; r < grid.GetRows() - 1 && pa.row < 0; ++r) {
    for (int c = 1; c < grid.GetCols() - 2 && pa.row < 0; ++c) {
      const Position p{r, c};
      const Position q{r, c + 1};
      if (!grid.IsWalkable(p) || !grid.IsWalkable(q)) continue;
      const bool p_ok = !IsOnCell(env, p) || p == a->GetPosition();
      const bool q_ok = !IsOnCell(env, q) || q == b->GetPosition();
      if (p_ok && q_ok) pa = p;
    }
  }
  ASSERT_TRUE(pa.row >= 0);
  env.GetMutableObjectManager().UpdatePosition(b->GetId(), {pa.row, pa.col + 1});
  env.GetMutableObjectManager().UpdatePosition(a->GetId(), pa);
  const int health = b->GetHealth();
  for (int t = 0; t < 5; ++t) {
    w.Step(0, 4, 1);  // Skill1 aimed Right, at the ally
    ASSERT_EQ(b->GetHealth(), health);
    ASSERT_FALSE(b->IsDowned());
    ASSERT_TRUE(w.NoTruncation());
  }
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

  std::cout << "Running " << tests.size() << " synchro wrapper tests...\n\n";

  int passed = 0;
  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    try {
      test.func();
      std::cout << "[       OK ] " << test.name << "\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "[  FAILED  ] " << test.name << ": " << e.what() << "\n";
    } catch (...) {
      std::cout << "[  FAILED  ] " << test.name << ": unknown exception\n";
    }
  }

  std::cout << "\n[==========] " << passed << "/" << tests.size()
            << " tests passed.\n";

  return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
