// Copyright 2024
// Test suite for D4 symmetry transformations

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/core/d4_transform.h"
#include "../src/core/grid.h"
#include "../src/core/types.h"
#include "../src/env/synchro_env.h"
#include "../src/env/aggro_env.h"

using namespace companions;

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
    std::cerr << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::abort(); \
  }

#define ASSERT_FALSE(cond) \
  if (cond) { \
    std::cerr << "ASSERT_FALSE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::abort(); \
  }

#define ASSERT_EQ(a, b) \
  if ((a) != (b)) { \
    std::cerr << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::abort(); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// D4 Transform Validation Tests
// =============================================================================

TEST(TestD4TransformValidation) {
  ASSERT_TRUE(IsValidD4Transform(0));
  ASSERT_TRUE(IsValidD4Transform(7));
  ASSERT_FALSE(IsValidD4Transform(-1));
  ASSERT_FALSE(IsValidD4Transform(8));
}

TEST(TestD4TransformSwapsDimensions) {
  // Identity, Rot180, FlipH, FlipV don't swap
  ASSERT_FALSE(SwapsDimensions(D4Transform::Identity));
  ASSERT_FALSE(SwapsDimensions(D4Transform::Rot180));
  ASSERT_FALSE(SwapsDimensions(D4Transform::FlipH));
  ASSERT_FALSE(SwapsDimensions(D4Transform::FlipV));

  // Rot90, Rot270, FlipD, FlipA do swap
  ASSERT_TRUE(SwapsDimensions(D4Transform::Rot90));
  ASSERT_TRUE(SwapsDimensions(D4Transform::Rot270));
  ASSERT_TRUE(SwapsDimensions(D4Transform::FlipD));
  ASSERT_TRUE(SwapsDimensions(D4Transform::FlipA));
}

TEST(TestD4GetTransformedDimensions_Square) {
  // Square grid: dimensions never change
  for (int t = 0; t < 8; ++t) {
    auto [rows, cols] = GetTransformedDimensions(6, 6, ToD4Transform(t));
    ASSERT_EQ(rows, 6);
    ASSERT_EQ(cols, 6);
  }
}

TEST(TestD4GetTransformedDimensions_Rectangular) {
  // 4 rows, 6 cols
  auto [r0, c0] = GetTransformedDimensions(4, 6, D4Transform::Identity);
  ASSERT_EQ(r0, 4);
  ASSERT_EQ(c0, 6);

  auto [r1, c1] = GetTransformedDimensions(4, 6, D4Transform::Rot90);
  ASSERT_EQ(r1, 6);  // Swapped
  ASSERT_EQ(c1, 4);

  auto [r2, c2] = GetTransformedDimensions(4, 6, D4Transform::Rot180);
  ASSERT_EQ(r2, 4);  // Not swapped
  ASSERT_EQ(c2, 6);

  auto [r3, c3] = GetTransformedDimensions(4, 6, D4Transform::FlipD);
  ASSERT_EQ(r3, 6);  // Swapped
  ASSERT_EQ(c3, 4);
}

// =============================================================================
// Position Transform Tests
// =============================================================================

TEST(TestD4TransformPosition_Identity) {
  Position pos{2, 3};
  Position result = TransformPosition(pos, 5, 5, D4Transform::Identity);
  ASSERT_EQ(result.row, 2);
  ASSERT_EQ(result.col, 3);
}

TEST(TestD4TransformPosition_Rot90_Square) {
  // In a 5x5 grid (N=5), Rot90: (r, c) -> (c, N-1-r)
  // (0, 0) -> (0, 4)
  // (0, 4) -> (4, 4)
  // (4, 4) -> (4, 0)
  // (4, 0) -> (0, 0)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p00.row, 0);
  ASSERT_EQ(p00.col, 4);

  Position p04 = TransformPosition({0, 4}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p04.row, 4);
  ASSERT_EQ(p04.col, 4);

  Position p44 = TransformPosition({4, 4}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p44.row, 4);
  ASSERT_EQ(p44.col, 0);

  Position p40 = TransformPosition({4, 0}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p40.row, 0);
  ASSERT_EQ(p40.col, 0);
}

TEST(TestD4TransformPosition_Rot180_Square) {
  // Rot180: (r, c) -> (N-1-r, N-1-c)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::Rot180);
  ASSERT_EQ(p00.row, 4);
  ASSERT_EQ(p00.col, 4);

  Position p22 = TransformPosition({2, 2}, N, N, D4Transform::Rot180);
  ASSERT_EQ(p22.row, 2);  // Center stays at center
  ASSERT_EQ(p22.col, 2);
}

TEST(TestD4TransformPosition_Rot270_Square) {
  // Rot270: (r, c) -> (N-1-c, r)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::Rot270);
  ASSERT_EQ(p00.row, 4);
  ASSERT_EQ(p00.col, 0);
}

TEST(TestD4TransformPosition_FlipH_Square) {
  // FlipH: (r, c) -> (r, N-1-c)
  int N = 5;

  Position p02 = TransformPosition({0, 2}, N, N, D4Transform::FlipH);
  ASSERT_EQ(p02.row, 0);
  ASSERT_EQ(p02.col, 2);  // Center col unchanged

  Position p01 = TransformPosition({0, 1}, N, N, D4Transform::FlipH);
  ASSERT_EQ(p01.row, 0);
  ASSERT_EQ(p01.col, 3);
}

TEST(TestD4TransformPosition_FlipV_Square) {
  // FlipV: (r, c) -> (N-1-r, c)
  int N = 5;

  Position p10 = TransformPosition({1, 0}, N, N, D4Transform::FlipV);
  ASSERT_EQ(p10.row, 3);
  ASSERT_EQ(p10.col, 0);
}

TEST(TestD4TransformPosition_FlipD_Square) {
  // FlipD (transpose): (r, c) -> (c, r)
  int N = 5;

  Position p13 = TransformPosition({1, 3}, N, N, D4Transform::FlipD);
  ASSERT_EQ(p13.row, 3);
  ASSERT_EQ(p13.col, 1);
}

TEST(TestD4TransformPosition_FlipA_Square) {
  // FlipA: (r, c) -> (N-1-c, N-1-r)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::FlipA);
  ASSERT_EQ(p00.row, 4);
  ASSERT_EQ(p00.col, 4);

  Position p13 = TransformPosition({1, 3}, N, N, D4Transform::FlipA);
  ASSERT_EQ(p13.row, 1);
  ASSERT_EQ(p13.col, 3);
}

TEST(TestD4TransformPosition_Rectangular) {
  // 4 rows, 6 cols
  // Rot90: (r, c) -> (c, rows-1-r)
  // (0, 0) -> (0, 3)  (new grid is 6x4)
  Position p00 = TransformPosition({0, 0}, 4, 6, D4Transform::Rot90);
  ASSERT_EQ(p00.row, 0);
  ASSERT_EQ(p00.col, 3);

  // (3, 5) -> (5, 0)
  Position p35 = TransformPosition({3, 5}, 4, 6, D4Transform::Rot90);
  ASSERT_EQ(p35.row, 5);
  ASSERT_EQ(p35.col, 0);
}

// =============================================================================
// Grid Transform Tests
// =============================================================================

TEST(TestD4TransformGrid_Identity) {
  Grid grid(5, 5);
  grid.SetCell({1, 2}, CellKind::Wall);
  grid.SetCell({3, 4}, CellKind::Synchro);

  auto result = TransformGrid(grid, D4Transform::Identity);

  ASSERT_EQ(result->GetRows(), 5);
  ASSERT_EQ(result->GetCols(), 5);
  ASSERT_TRUE(result->GetCellKind({1, 2}) == CellKind::Wall);
  ASSERT_TRUE(result->GetCellKind({3, 4}) == CellKind::Synchro);
}

TEST(TestD4TransformGrid_Rot90) {
  Grid grid(5, 5);
  grid.SetCell({0, 0}, CellKind::Wall);
  grid.SetCell({1, 2}, CellKind::Synchro);

  auto result = TransformGrid(grid, D4Transform::Rot90);

  // Rot90: (r, c) -> (c, rows-1-r)
  // (0, 0) -> (0, 4)
  ASSERT_TRUE(result->GetCellKind({0, 4}) == CellKind::Wall);
  // (1, 2) -> (2, 5-1-1) = (2, 3)
  ASSERT_TRUE(result->GetCellKind({2, 3}) == CellKind::Synchro);
}

TEST(TestD4TransformGrid_Rectangular) {
  // 3 rows, 5 cols
  Grid grid(3, 5);
  grid.SetCell({0, 4}, CellKind::Wall);

  auto result = TransformGrid(grid, D4Transform::Rot90);

  // Dimensions should swap: 5 rows, 3 cols
  ASSERT_EQ(result->GetRows(), 5);
  ASSERT_EQ(result->GetCols(), 3);

  // (0, 4) -> (4, 2) ... Rot90: (r, c) -> (c, rows-1-r) = (4, 2)
  ASSERT_TRUE(result->GetCellKind({4, 2}) == CellKind::Wall);
}

TEST(TestD4TransformGrid_PreservesOrigin) {
  Grid grid(5, 5);
  grid.SetCell({1, 2}, CellKind::Wall, CellOrigin::Obstacle);

  auto result = TransformGrid(grid, D4Transform::Rot180);

  Position transformed = TransformPosition({1, 2}, 5, 5, D4Transform::Rot180);
  ASSERT_TRUE(result->GetCell(transformed).GetOrigin() == CellOrigin::Obstacle);
}

// =============================================================================
// SynchroEnv Integration Tests
// =============================================================================

TEST(TestSynchroEnvD4Transform_Identity) {
  SynchroEnv env(6, 6, 2, 2, 0, 42, 0);  // d4_transform=0

  ASSERT_EQ(env.GetD4Transform(), 0);
  ASSERT_EQ(env.GetRows(), 6);
  ASSERT_EQ(env.GetCols(), 6);
  ASSERT_EQ(env.NumAgents(), 2);
}

TEST(TestSynchroEnvD4Transform_Rot90) {
  SynchroEnv env0(6, 6, 2, 2, 0, 42, 0);
  SynchroEnv env1(6, 6, 2, 2, 0, 42, 1);  // Rot90

  // Square grid: dimensions unchanged
  ASSERT_EQ(env1.GetRows(), 6);
  ASSERT_EQ(env1.GetCols(), 6);

  // Synchro positions should be transformed
  auto pos0 = env0.GetSynchroPositions();
  auto pos1 = env1.GetSynchroPositions();

  ASSERT_EQ(pos0.size(), pos1.size());

  // Check transformation is applied
  for (size_t i = 0; i < pos0.size(); ++i) {
    Position expected = TransformPosition(pos0[i], 6, 6, D4Transform::Rot90);
    ASSERT_EQ(pos1[i].row, expected.row);
    ASSERT_EQ(pos1[i].col, expected.col);
  }
}

TEST(TestSynchroEnvD4Transform_ActorPositions) {
  SynchroEnv env0(6, 6, 2, 2, 0, 42, 0);
  SynchroEnv env1(6, 6, 2, 2, 0, 42, 2);  // Rot180

  auto agents0 = env0.GetObjectManager().GetAllAgents();
  auto agents1 = env1.GetObjectManager().GetAllAgents();

  ASSERT_EQ(agents0.size(), agents1.size());

  for (size_t i = 0; i < agents0.size(); ++i) {
    Position p0 = agents0[i]->GetPosition();
    Position p1 = agents1[i]->GetPosition();
    Position expected = TransformPosition(p0, 6, 6, D4Transform::Rot180);
    ASSERT_EQ(p1.row, expected.row);
    ASSERT_EQ(p1.col, expected.col);
  }
}

TEST(TestSynchroEnvD4Transform_AllTransforms) {
  // Test all 8 transforms work without crash
  for (int t = 0; t < 8; ++t) {
    SynchroEnv env(6, 6, 2, 2, 0, 42, t);
    ASSERT_EQ(env.GetD4Transform(), t);
    ASSERT_EQ(env.NumAgents(), 2);
    ASSERT_FALSE(env.IsDone());
  }
}

TEST(TestSynchroEnvD4Transform_CanStep) {
  // Test that transformed env can execute steps
  for (int t = 0; t < 8; ++t) {
    SynchroEnv env(6, 6, 2, 2, 0, 42, t);
    std::vector<Action> actions = {0, 0};  // Both stay
    auto result = env.Step(actions);
    ASSERT_FALSE(result.done);
  }
}

// =============================================================================
// AggroEnv Integration Tests
// =============================================================================

TEST(TestAggroEnvD4Transform_AllTransforms) {
  // Test all 8 transforms work without crash
  for (int t = 0; t < 8; ++t) {
    AggroEnv env(8, 1, EnemyType::Zombie, 42, t);
    ASSERT_EQ(env.GetD4Transform(), t);
    ASSERT_EQ(env.NumAgents(), 2);  // 1 companion + 1 enemy
    ASSERT_FALSE(env.IsDone());
  }
}

TEST(TestAggroEnvD4Transform_PatrolPathTransformed) {
  AggroEnv env0(8, 1, EnemyType::Zombie, 42, 0);
  AggroEnv env1(8, 1, EnemyType::Zombie, 42, 2);  // Rot180

  auto path0 = env0.GetPatrolPath();
  auto path1 = env1.GetPatrolPath();

  ASSERT_EQ(path0.size(), path1.size());
  ASSERT_EQ(path0.size(), 8u);  // 3x3 perimeter = 8 cells

  for (size_t i = 0; i < path0.size(); ++i) {
    Position expected = TransformPosition(path0[i], 8, 8, D4Transform::Rot180);
    ASSERT_EQ(path1[i].row, expected.row);
    ASSERT_EQ(path1[i].col, expected.col);
  }
}

TEST(TestAggroEnvD4Transform_TargetPosTransformed) {
  AggroEnv env0(8, 1, EnemyType::Zombie, 42, 0);
  AggroEnv env1(8, 1, EnemyType::Zombie, 42, 4);  // FlipH

  Position t0 = env0.GetTargetPosition();
  Position t1 = env1.GetTargetPosition();

  Position expected = TransformPosition(t0, 8, 8, D4Transform::FlipH);
  ASSERT_EQ(t1.row, expected.row);
  ASSERT_EQ(t1.col, expected.col);
}

// =============================================================================
// Clone Consistency Tests
// =============================================================================

TEST(TestD4Transform_ClonePreservesTransform) {
  SynchroEnv env(6, 6, 2, 2, 0, 42, 3);  // Rot270
  auto cloned = env.Clone();

  ASSERT_EQ(cloned->GetD4Transform(), 3);
  ASSERT_EQ(cloned->GetRows(), 6);
  ASSERT_EQ(cloned->GetCols(), 6);
}

// =============================================================================
// Main
// =============================================================================
int main() {
  std::cout << "Running " << tests.size() << " D4 transform tests...\n\n";

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "Running " << test.name << "... ";
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "FAILED: " << e.what() << "\n";
      failed++;
    } catch (...) {
      std::cout << "FAILED (unknown exception)\n";
      failed++;
    }
  }

  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
  return (failed > 0) ? 1 : 0;
}
