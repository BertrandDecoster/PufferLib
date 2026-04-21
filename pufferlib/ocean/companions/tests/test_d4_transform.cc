// Copyright 2024
// Test suite for D4 symmetry transformations

#include <iostream>
#include <sstream>
#include <stdexcept>
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
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
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
  // In a 5x5 grid (N=5), Rot90 CCW: (r, c) -> (N-1-c, r)
  // (0, 0) -> (4, 0)
  // (0, 4) -> (0, 0)
  // (4, 4) -> (0, 4)
  // (4, 0) -> (4, 4)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p00.row, 4);
  ASSERT_EQ(p00.col, 0);

  Position p04 = TransformPosition({0, 4}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p04.row, 0);
  ASSERT_EQ(p04.col, 0);

  Position p44 = TransformPosition({4, 4}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p44.row, 0);
  ASSERT_EQ(p44.col, 4);

  Position p40 = TransformPosition({4, 0}, N, N, D4Transform::Rot90);
  ASSERT_EQ(p40.row, 4);
  ASSERT_EQ(p40.col, 4);
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
  // Rot270 CCW (= 90 CW): (r, c) -> (c, N-1-r)
  int N = 5;

  Position p00 = TransformPosition({0, 0}, N, N, D4Transform::Rot270);
  ASSERT_EQ(p00.row, 0);
  ASSERT_EQ(p00.col, 4);
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
  // Rot90 CCW: (r, c) -> (cols-1-c, r)
  // (0, 0) -> (5, 0)  (new grid is 6x4)
  Position p00 = TransformPosition({0, 0}, 4, 6, D4Transform::Rot90);
  ASSERT_EQ(p00.row, 5);
  ASSERT_EQ(p00.col, 0);

  // (3, 5) -> (0, 3)
  Position p35 = TransformPosition({3, 5}, 4, 6, D4Transform::Rot90);
  ASSERT_EQ(p35.row, 0);
  ASSERT_EQ(p35.col, 3);
}

// =============================================================================
// Grid Transform Tests
// =============================================================================

TEST(TestD4TransformGrid_Identity) {
  Grid grid(5, 5);
  grid.SetCell({1, 2}, CellKind::Wall);
  grid.SetCell({3, 4}, CellKind::HealArea);

  auto result = TransformGrid(grid, D4Transform::Identity);

  ASSERT_EQ(result->GetRows(), 5);
  ASSERT_EQ(result->GetCols(), 5);
  ASSERT_TRUE(result->GetCellKind({1, 2}) == CellKind::Wall);
  ASSERT_TRUE(result->GetCellKind({3, 4}) == CellKind::HealArea);
}

TEST(TestD4TransformGrid_Rot90) {
  Grid grid(5, 5);
  grid.SetCell({0, 0}, CellKind::Wall);
  grid.SetCell({1, 2}, CellKind::HealArea);

  auto result = TransformGrid(grid, D4Transform::Rot90);

  // Rot90 CCW: (r, c) -> (cols-1-c, r)
  // (0, 0) -> (4, 0)
  ASSERT_TRUE(result->GetCellKind({4, 0}) == CellKind::Wall);
  // (1, 2) -> (5-1-2, 1) = (2, 1)
  ASSERT_TRUE(result->GetCellKind({2, 1}) == CellKind::HealArea);
}

TEST(TestD4TransformGrid_Rectangular) {
  // 3 rows, 5 cols
  Grid grid(3, 5);
  grid.SetCell({0, 4}, CellKind::Wall);

  auto result = TransformGrid(grid, D4Transform::Rot90);

  // Dimensions should swap: 5 rows, 3 cols
  ASSERT_EQ(result->GetRows(), 5);
  ASSERT_EQ(result->GetCols(), 3);

  // (0, 4) -> (0, 0) ... Rot90 CCW: (r, c) -> (cols-1-c, r) = (5-1-4, 0) = (0, 0)
  ASSERT_TRUE(result->GetCellKind({0, 0}) == CellKind::Wall);
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
// D4 Group Mathematical Property Tests
// =============================================================================

// Helper to get inverse transform
D4Transform GetInverseTransform(D4Transform t) {
  // D4 inverses:
  // Identity^-1 = Identity
  // Rot90^-1 = Rot270
  // Rot180^-1 = Rot180 (self-inverse)
  // Rot270^-1 = Rot90
  // FlipH^-1 = FlipH (self-inverse)
  // FlipV^-1 = FlipV (self-inverse)
  // FlipD^-1 = FlipD (self-inverse)
  // FlipA^-1 = FlipA (self-inverse)
  switch (t) {
    case D4Transform::Identity: return D4Transform::Identity;
    case D4Transform::Rot90: return D4Transform::Rot270;
    case D4Transform::Rot180: return D4Transform::Rot180;
    case D4Transform::Rot270: return D4Transform::Rot90;
    case D4Transform::FlipH: return D4Transform::FlipH;
    case D4Transform::FlipV: return D4Transform::FlipV;
    case D4Transform::FlipD: return D4Transform::FlipD;
    case D4Transform::FlipA: return D4Transform::FlipA;
  }
  return D4Transform::Identity;
}

TEST(TestD4RotationCycle) {
  // Applying Rot90 four times should return to the original position
  int N = 7;
  std::vector<Position> test_positions = {{0, 0}, {0, 6}, {6, 0}, {6, 6}, {3, 3}, {2, 5}};

  for (const auto& start : test_positions) {
    Position current = start;
    for (int i = 0; i < 4; ++i) {
      current = TransformPosition(current, N, N, D4Transform::Rot90);
    }
    ASSERT_EQ(current.row, start.row);
    ASSERT_EQ(current.col, start.col);
  }
}

TEST(TestD4InverseCorrectness) {
  // For all 8 transforms: transform(inverse(transform(pos))) = pos
  int N = 6;
  std::vector<Position> test_positions = {{0, 0}, {0, 5}, {5, 0}, {5, 5}, {2, 3}, {4, 1}};

  for (int t = 0; t < 8; ++t) {
    D4Transform transform = ToD4Transform(t);
    D4Transform inverse = GetInverseTransform(transform);

    for (const auto& pos : test_positions) {
      // Apply transform
      Position transformed = TransformPosition(pos, N, N, transform);
      // Get transformed dimensions
      auto [tRows, tCols] = GetTransformedDimensions(N, N, transform);
      // Apply inverse
      Position restored = TransformPosition(transformed, tRows, tCols, inverse);

      ASSERT_EQ(restored.row, pos.row);
      ASSERT_EQ(restored.col, pos.col);
    }
  }
}

TEST(TestD4ReflectionsSelfInverse) {
  // All 4 reflections are self-inverse: applying twice = identity
  int N = 5;
  std::vector<D4Transform> reflections = {
    D4Transform::FlipH, D4Transform::FlipV,
    D4Transform::FlipD, D4Transform::FlipA
  };
  std::vector<Position> test_positions = {{0, 0}, {1, 3}, {4, 2}};

  for (auto flip : reflections) {
    for (const auto& pos : test_positions) {
      Position once = TransformPosition(pos, N, N, flip);
      // For FlipD/FlipA, dimensions swap, so use swapped dims
      auto [r1, c1] = GetTransformedDimensions(N, N, flip);
      Position twice = TransformPosition(once, r1, c1, flip);

      ASSERT_EQ(twice.row, pos.row);
      ASSERT_EQ(twice.col, pos.col);
    }
  }
}

TEST(TestD4Rot180SelfInverse) {
  // Rot180 applied twice = identity
  int N = 5;
  std::vector<Position> test_positions = {{0, 0}, {1, 3}, {2, 2}};

  for (const auto& pos : test_positions) {
    Position once = TransformPosition(pos, N, N, D4Transform::Rot180);
    Position twice = TransformPosition(once, N, N, D4Transform::Rot180);

    ASSERT_EQ(twice.row, pos.row);
    ASSERT_EQ(twice.col, pos.col);
  }
}

TEST(TestD4GroupComposition) {
  // Test some key group compositions:
  // Rot90 ∘ Rot90 = Rot180
  // FlipH ∘ FlipV = Rot180
  // FlipD ∘ Rot90 = FlipH (in the sense of equivalent positions)
  int N = 5;
  std::vector<Position> test_positions = {{0, 0}, {1, 2}, {3, 4}};

  for (const auto& pos : test_positions) {
    // Rot90 ∘ Rot90 = Rot180
    Position via_two_rot90 = TransformPosition(
        TransformPosition(pos, N, N, D4Transform::Rot90),
        N, N, D4Transform::Rot90);
    Position via_rot180 = TransformPosition(pos, N, N, D4Transform::Rot180);

    ASSERT_EQ(via_two_rot90.row, via_rot180.row);
    ASSERT_EQ(via_two_rot90.col, via_rot180.col);

    // FlipH ∘ FlipV = Rot180
    Position via_flips = TransformPosition(
        TransformPosition(pos, N, N, D4Transform::FlipH),
        N, N, D4Transform::FlipV);

    ASSERT_EQ(via_flips.row, via_rot180.row);
    ASSERT_EQ(via_flips.col, via_rot180.col);
  }
}

TEST(TestD4GridTransformCycle) {
  // Rotating a grid 4 times should restore original
  Grid grid(5, 5);
  grid.SetCell({0, 0}, CellKind::Wall);
  grid.SetCell({1, 3}, CellKind::HealArea);
  grid.SetCell({4, 2}, CellKind::Hazard);

  std::unique_ptr<Grid> current = std::make_unique<Grid>(grid);
  for (int i = 0; i < 4; ++i) {
    current = TransformGrid(*current, D4Transform::Rot90);
  }

  // Check same dimensions
  ASSERT_EQ(current->GetRows(), 5);
  ASSERT_EQ(current->GetCols(), 5);

  // Check cells restored
  ASSERT_TRUE(current->GetCellKind({0, 0}) == CellKind::Wall);
  ASSERT_TRUE(current->GetCellKind({1, 3}) == CellKind::HealArea);
  ASSERT_TRUE(current->GetCellKind({4, 2}) == CellKind::Hazard);
}

TEST(TestD4AllTransformsPreserveGridContent) {
  // All 8 transforms should preserve the total count of each cell type
  Grid grid(6, 6);
  grid.SetCell({0, 0}, CellKind::Wall);
  grid.SetCell({0, 1}, CellKind::Wall);
  grid.SetCell({2, 3}, CellKind::HealArea);
  grid.SetCell({4, 5}, CellKind::Hazard);

  for (int t = 0; t < 8; ++t) {
    auto transformed = TransformGrid(grid, ToD4Transform(t));

    // Count cell types
    int walls = 0, synchros = 0, targets = 0;
    auto [rows, cols] = GetTransformedDimensions(6, 6, ToD4Transform(t));

    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        CellKind kind = transformed->GetCellKind({r, c});
        if (kind == CellKind::Wall) walls++;
        else if (kind == CellKind::HealArea) synchros++;
        else if (kind == CellKind::Hazard) targets++;
      }
    }

    ASSERT_EQ(walls, 2);
    ASSERT_EQ(synchros, 1);
    ASSERT_EQ(targets, 1);
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
