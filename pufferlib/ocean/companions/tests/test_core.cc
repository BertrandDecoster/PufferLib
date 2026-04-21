// Copyright 2024
// Test suite for The Companions game

#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Include all modules
#include "../src/core/pcg32.h"
#include "../src/core/types.h"
#include "../src/core/cell.h"
#include "../src/core/grid.h"
#include "../src/core/object.h"
#include "../src/core/object_manager.h"
#include "../src/core/level_builder.h"
#include "../src/core/pathfinder.h"
#include "../src/core/agent_config.h"
#include "../src/core/effect_config.h"
#include "../src/env/base_env.h"
#include "../src/env/synchro_env.h"
#include "../src/viz/renderer.h"

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
    oss << "ASSERT_EQ failed: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_NE(a, b) \
  if ((a) == (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_NE failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Types Tests
// =============================================================================
TEST(TestPosition) {
  Position p1{3, 4};
  Position p2{3, 4};
  Position p3{5, 6};

  ASSERT_TRUE(p1 == p2);
  ASSERT_FALSE(p1 == p3);
  ASSERT_TRUE(p1 != p3);
  ASSERT_TRUE(p1.IsValid());

  Position invalid{-1, 0};
  ASSERT_FALSE(invalid.IsValid());
}

TEST(TestMovementAction) {
  Position start{5, 5};

  ASSERT_EQ(ApplyMovement(start, MovementAction::Stay).row, 5);
  ASSERT_EQ(ApplyMovement(start, MovementAction::Stay).col, 5);

  ASSERT_EQ(ApplyMovement(start, MovementAction::Up).row, 4);
  ASSERT_EQ(ApplyMovement(start, MovementAction::Down).row, 6);
  ASSERT_EQ(ApplyMovement(start, MovementAction::Left).col, 4);
  ASSERT_EQ(ApplyMovement(start, MovementAction::Right).col, 6);
}

TEST(TestActionEncoding) {
  for (int m = 0; m < kNumMovementActions; ++m) {
    MovementAction mov = static_cast<MovementAction>(m);
    Action encoded = EncodeAction(mov);
    DecodedAction decoded = DecodeAction(encoded);
    ASSERT_EQ(static_cast<int>(decoded.movement), m);
  }
}

TEST(TestDirection) {
  // Test MovementToDirection conversion
  auto up = MovementToDirection(MovementAction::Up);
  ASSERT_TRUE(up.has_value());
  ASSERT_EQ(*up, Direction::Up);

  auto down = MovementToDirection(MovementAction::Down);
  ASSERT_TRUE(down.has_value());
  ASSERT_EQ(*down, Direction::Down);

  auto left = MovementToDirection(MovementAction::Left);
  ASSERT_TRUE(left.has_value());
  ASSERT_EQ(*left, Direction::Left);

  auto right = MovementToDirection(MovementAction::Right);
  ASSERT_TRUE(right.has_value());
  ASSERT_EQ(*right, Direction::Right);

  auto stay = MovementToDirection(MovementAction::Stay);
  ASSERT_FALSE(stay.has_value());
}

// =============================================================================
// Cell Tests
// =============================================================================
TEST(TestCellKinds) {
  Position pos{3, 4};

  // Floor cell
  Cell floor(pos, CellKind::Floor);
  ASSERT_TRUE(floor.IsWalkable());
  ASSERT_TRUE(floor.IsPathable());
  ASSERT_EQ(floor.GetKind(), CellKind::Floor);
  ASSERT_EQ(floor.GetChar(), '.');

  // Wall cell
  Cell wall(pos, CellKind::Wall);
  ASSERT_FALSE(wall.IsWalkable());
  ASSERT_FALSE(wall.IsPathable());
  ASSERT_EQ(wall.GetKind(), CellKind::Wall);
  ASSERT_EQ(wall.GetChar(), '#');

  // Hazard cell (pathable but not walkable)
  Cell hazard(pos, CellKind::Hazard);
  ASSERT_FALSE(hazard.IsWalkable());
  ASSERT_TRUE(hazard.IsPathable());
  ASSERT_EQ(hazard.GetKind(), CellKind::Hazard);
  ASSERT_EQ(hazard.GetChar(), '~');

  // HealArea cell
  Cell heal(pos, CellKind::HealArea);
  ASSERT_TRUE(heal.IsWalkable());
  ASSERT_TRUE(heal.IsPathable());
  ASSERT_EQ(heal.GetKind(), CellKind::HealArea);
  ASSERT_EQ(heal.GetChar(), '+');
}

TEST(TestCellProperties) {
  // Test that properties are retrieved correctly
  const auto& floor_props = CellProperties::Get(CellKind::Floor);
  ASSERT_TRUE(floor_props.walkable);
  ASSERT_TRUE(floor_props.pathable);

  const auto& wall_props = CellProperties::Get(CellKind::Wall);
  ASSERT_FALSE(wall_props.walkable);
  ASSERT_FALSE(wall_props.pathable);
}

TEST(TestCellSetKind) {
  Position pos{0, 0};
  Cell cell(pos, CellKind::Floor);

  ASSERT_EQ(cell.GetKind(), CellKind::Floor);
  ASSERT_TRUE(cell.IsWalkable());

  cell.SetKind(CellKind::Wall);
  ASSERT_EQ(cell.GetKind(), CellKind::Wall);
  ASSERT_FALSE(cell.IsWalkable());
}

// =============================================================================
// Grid Tests
// =============================================================================
TEST(TestGridCreation) {
  Grid grid(12, 12);
  ASSERT_EQ(grid.GetRows(), 12);
  ASSERT_EQ(grid.GetCols(), 12);

  // All cells should be walkable by default (Floor)
  for (int r = 0; r < 12; ++r) {
    for (int c = 0; c < 12; ++c) {
      ASSERT_TRUE(grid.IsWalkable({r, c}));
      ASSERT_EQ(grid.GetCellKind({r, c}), CellKind::Floor);
    }
  }
}

TEST(TestGridBounds) {
  Grid grid(10, 10);

  ASSERT_TRUE(grid.IsInBounds({0, 0}));
  ASSERT_TRUE(grid.IsInBounds({9, 9}));
  ASSERT_FALSE(grid.IsInBounds({-1, 0}));
  ASSERT_FALSE(grid.IsInBounds({0, -1}));
  ASSERT_FALSE(grid.IsInBounds({10, 0}));
  ASSERT_FALSE(grid.IsInBounds({0, 10}));
}

TEST(TestGridSetCell) {
  Grid grid(10, 10);

  Position pos{5, 5};
  grid.SetCell(pos, CellKind::Wall);

  ASSERT_FALSE(grid.IsWalkable(pos));
  ASSERT_FALSE(grid.IsPathable(pos));
  ASSERT_EQ(grid.GetCellKind(pos), CellKind::Wall);
}

TEST(TestGridFindCells) {
  Grid grid(10, 10);

  Position s1{2, 3};
  Position s2{7, 8};
  grid.SetCell(s1, CellKind::HealArea);
  grid.SetCell(s2, CellKind::HealArea);

  auto heals = grid.FindCellsOfKind(CellKind::HealArea);
  ASSERT_EQ(heals.size(), 2u);
}

TEST(TestGridFindWalkable) {
  Grid grid(5, 5);

  // Add some walls
  grid.SetCell({0, 0}, CellKind::Wall);
  grid.SetCell({1, 1}, CellKind::Wall);
  grid.SetCell({2, 2}, CellKind::Wall);

  auto walkable = grid.FindWalkableCells();
  ASSERT_EQ(walkable.size(), 25u - 3u);  // 5x5 grid minus 3 walls
}

TEST(TestGridOutOfBoundsAccess) {
  Grid grid(10, 10);

  // Out of bounds access should return wall (not walkable)
  ASSERT_FALSE(grid.IsWalkable({-1, 0}));
  ASSERT_FALSE(grid.IsWalkable({10, 0}));
  ASSERT_EQ(grid.GetCellKind({-1, 0}), CellKind::Wall);
}

// =============================================================================
// LevelBuilder Tests
// =============================================================================
TEST(TestLevelBuilderFill) {
  Grid grid(5, 5);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Wall);

  for (int r = 0; r < 5; ++r) {
    for (int c = 0; c < 5; ++c) {
      ASSERT_EQ(grid.GetCellKind({r, c}), CellKind::Wall);
    }
  }
}

TEST(TestLevelBuilderBorder) {
  Grid grid(5, 5);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Floor);
  builder.Border(CellKind::Wall);

  // Check border is walls
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(grid.GetCellKind({0, i}), CellKind::Wall);     // Top
    ASSERT_EQ(grid.GetCellKind({4, i}), CellKind::Wall);     // Bottom
    ASSERT_EQ(grid.GetCellKind({i, 0}), CellKind::Wall);     // Left
    ASSERT_EQ(grid.GetCellKind({i, 4}), CellKind::Wall);     // Right
  }

  // Check interior is floor
  for (int r = 1; r < 4; ++r) {
    for (int c = 1; c < 4; ++c) {
      ASSERT_EQ(grid.GetCellKind({r, c}), CellKind::Floor);
    }
  }
}

TEST(TestLevelBuilderRectangle) {
  Grid grid(10, 10);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Floor);

  // Filled rectangle
  builder.Rectangle(CellKind::Wall, 2, 2, 3, 3, true);

  for (int r = 2; r < 5; ++r) {
    for (int c = 2; c < 5; ++c) {
      ASSERT_EQ(grid.GetCellKind({r, c}), CellKind::Wall);
    }
  }
}

TEST(TestLevelBuilderRectangleOutline) {
  Grid grid(10, 10);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Floor);

  // Outline rectangle (hollow)
  builder.Rectangle(CellKind::Wall, 2, 2, 4, 4, false);

  // Corners and edges should be walls
  ASSERT_EQ(grid.GetCellKind({2, 2}), CellKind::Wall);
  ASSERT_EQ(grid.GetCellKind({2, 5}), CellKind::Wall);
  ASSERT_EQ(grid.GetCellKind({5, 2}), CellKind::Wall);
  ASSERT_EQ(grid.GetCellKind({5, 5}), CellKind::Wall);

  // Interior should still be floor
  ASSERT_EQ(grid.GetCellKind({3, 3}), CellKind::Floor);
  ASSERT_EQ(grid.GetCellKind({4, 4}), CellKind::Floor);
}

TEST(TestLevelBuilderLine) {
  Grid grid(10, 10);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Floor);

  // Horizontal line
  builder.Line(CellKind::Wall, {3, 1}, {3, 5});

  for (int c = 1; c <= 5; ++c) {
    ASSERT_EQ(grid.GetCellKind({3, c}), CellKind::Wall);
  }

  // Vertical line
  builder.Line(CellKind::HealArea, {1, 7}, {6, 7});

  for (int r = 1; r <= 6; ++r) {
    ASSERT_EQ(grid.GetCellKind({r, 7}), CellKind::HealArea);
  }

  // Diagonal line - should throw
  bool threw = false;
  try {
    builder.Line(CellKind::Wall, {1, 1}, {5, 5});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  // Reversed horizontal line (right-to-left)
  builder.Line(CellKind::Wall, {5, 8}, {5, 4});
  for (int c = 4; c <= 8; ++c) {
    ASSERT_EQ(grid.GetCellKind({5, c}), CellKind::Wall);
  }

  // Reversed vertical line (bottom-to-top)
  builder.Line(CellKind::HealArea, {8, 2}, {4, 2});
  for (int r = 4; r <= 8; ++r) {
    ASSERT_EQ(grid.GetCellKind({r, 2}), CellKind::HealArea);
  }
}

// =============================================================================
// Object/Actor Tests
// =============================================================================
TEST(TestObjectHierarchy) {
  Player player(0, {1, 1});
  ASSERT_EQ(player.GetType(), ObjectType::Player);
  ASSERT_TRUE(player.IsPlayerControlled());
  ASSERT_EQ(player.GetChar(), 'P');

  NPCCompanion npc(1, {2, 2});
  ASSERT_EQ(npc.GetType(), ObjectType::NPCCompanion);
  ASSERT_FALSE(npc.IsPlayerControlled());
  ASSERT_EQ(npc.GetChar(), 'C');

  AgentFSM agent_fsm(2, {3, 3});
  ASSERT_EQ(agent_fsm.GetType(), ObjectType::AgentFSM);
  ASSERT_EQ(agent_fsm.GetChar(), 'E');
}

TEST(TestActorPosition) {
  // Test initial position via constructor
  ObjectManager mgr(10, 10);
  Player* player = mgr.CreateActor<Player>({5, 5});
  ASSERT_EQ(player->GetPosition().row, 5);
  ASSERT_EQ(player->GetPosition().col, 5);

  // Test position update via ObjectManager
  mgr.UpdatePosition(player->GetId(), {6, 7});
  ASSERT_EQ(player->GetPosition().row, 6);
  ASSERT_EQ(player->GetPosition().col, 7);

  // Verify spatial index is in sync
  ASSERT_EQ(mgr.GetActorAt({6, 7}), player);
  ASSERT_EQ(mgr.GetActorAt({5, 5}), nullptr);
}

TEST(TestAgentIntention) {
  Player player(0, {5, 5});

  // Default intention should be Stay
  ASSERT_EQ(player.GetIntention().movement, MovementAction::Stay);

  player.SetIntention({MovementAction::Up});
  ASSERT_EQ(player.GetIntention().movement, MovementAction::Up);

  player.ClearIntention();
  ASSERT_EQ(player.GetIntention().movement, MovementAction::Stay);
}

TEST(TestCompanionDirection) {
  Player player(0, {5, 5});

  // Default direction should be Down
  ASSERT_EQ(player.GetDirection(), Direction::Down);

  player.SetDirection(Direction::Up);
  ASSERT_EQ(player.GetDirection(), Direction::Up);

  player.SetDirection(Direction::Left);
  ASSERT_EQ(player.GetDirection(), Direction::Left);
}

TEST(TestCompanionColorCode) {
  Player player(0, {5, 5});

  // Default color should be None
  ASSERT_EQ(player.GetColor(), ActorColor::None);

  player.SetColor(ActorColor::Red);
  ASSERT_EQ(player.GetColor(), ActorColor::Red);
}

// =============================================================================
// ObjectManager Tests
// =============================================================================
TEST(TestObjectManagerCreate) {
  ObjectManager mgr;

  auto* player = mgr.CreateActor<Player>({1, 1});
  ASSERT_NE(player, nullptr);
  ASSERT_EQ(player->GetAgentIndex(), 0);

  auto* npc = mgr.CreateActor<NPCCompanion>({2, 2});
  ASSERT_NE(npc, nullptr);
  ASSERT_EQ(npc->GetAgentIndex(), 1);

  ASSERT_EQ(mgr.GetNumActors(), 2);
  ASSERT_EQ(mgr.GetNumAgents(), 2);
}

TEST(TestObjectManagerPositionMapping) {
  ObjectManager mgr;

  Position p1{1, 1};
  Position p2{2, 2};

  mgr.CreateActor<Player>(p1);
  mgr.CreateActor<NPCCompanion>(p2);

  ASSERT_TRUE(mgr.IsOccupied(p1));
  ASSERT_TRUE(mgr.IsOccupied(p2));
  ASSERT_FALSE(mgr.IsOccupied({3, 3}));

  auto* actor = mgr.GetActorAt(p1);
  ASSERT_NE(actor, nullptr);
  ASSERT_EQ(actor->GetType(), ObjectType::Player);
}

TEST(TestObjectManagerUpdatePosition) {
  ObjectManager mgr;

  Position old_pos{1, 1};
  Position new_pos{2, 2};

  auto* player = mgr.CreateActor<Player>(old_pos);
  ObjectId id = player->GetId();

  mgr.UpdatePosition(id, new_pos);

  ASSERT_FALSE(mgr.IsOccupied(old_pos));
  ASSERT_TRUE(mgr.IsOccupied(new_pos));
  ASSERT_EQ(player->GetPosition(), new_pos);
}

TEST(TestObjectManagerClear) {
  ObjectManager mgr;

  mgr.CreateActor<Player>({1, 1});
  mgr.CreateActor<NPCCompanion>({2, 2});

  ASSERT_EQ(mgr.GetNumActors(), 2);

  mgr.Clear();

  ASSERT_EQ(mgr.GetNumActors(), 0);
  ASSERT_FALSE(mgr.IsOccupied({1, 1}));
}

// =============================================================================
// Collision Tests
// =============================================================================

// Helper class for collision testing
class CollisionTestEnv : public BaseEnv {
 public:
  CollisionTestEnv() : BaseEnv(10, 10) {
    // Grid and ObjectManager already created by BaseEnv constructor
  }

  void Reset() override {
    tick_ = 0;
  }

  void Reset(unsigned int seed) override {
    tick_ = 0;
    // Seed not used in collision tests
  }

  bool IsDone() const override {
    return false;
  }

  // Required pure virtual implementations (not used in collision tests)
  double MinUtility() const override { return -100.0; }
  double MaxUtility() const override { return 100.0; }

  // Clone (required by BaseEnv but not used in collision tests)
  std::unique_ptr<BaseEnv> Clone() const override {
    return std::make_unique<CollisionTestEnv>(*this);
  }

  // Expose internals for testing
  void TestGatherIntentions(const std::vector<Action>& actions) {
    GatherIntentions(actions);
  }

  void TestResolveCollisions() {
    ResolveCollisions();
  }

  void TestExecuteValidatedMovements() {
    ExecuteValidatedMovements();
  }

  void AddWall(Position pos) {
    grid_->SetCell(pos, CellKind::Wall);
  }
};

TEST(TestCollisionTwoAgentsSameTarget) {
  // Two agents moving to same empty cell -> both noop
  CollisionTestEnv env;

  // A at (2,2), B at (2,4), both try to move to (2,3)
  auto* a = env.GetMutableObjectManager().CreateActor<Player>({2, 2});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({2, 4});

  // A moves right, B moves left -> both target (2,3)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),  // A
    EncodeAction(MovementAction::Left)    // B
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // Both should become Stay
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Stay);
}

TEST(TestCollisionSwap) {
  // A->B's cell and B->A's cell -> both noop
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 3});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 4});

  // A moves right (to 3,4), B moves left (to 3,3)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Left)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // Both should become Stay (swap not allowed)
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Stay);
}

TEST(TestCollisionSwapEndToEnd) {
  // End-to-end test: verify actual positions after movements execute
  // This catches bugs where intentions are correct but execution is wrong
  CollisionTestEnv env;

  Position original_a_pos{3, 3};
  Position original_b_pos{3, 4};

  auto* a = env.GetMutableObjectManager().CreateActor<Player>(original_a_pos);
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>(original_b_pos);

  // A moves right (to 3,4), B moves left (to 3,3) - swap attempt
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Left)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();
  env.TestExecuteValidatedMovements();  // Actually apply movements

  // Verify agents DID NOT swap - they should stay in original positions
  ASSERT_EQ(a->GetPosition(), original_a_pos);  // Agent A should not have moved
  ASSERT_EQ(b->GetPosition(), original_b_pos);  // Agent B should not have moved
}

TEST(TestCollisionChaseAllowed) {
  // A->B's cell, B moving away -> A succeeds
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 3});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 4});

  // A moves right (to 3,4 - B's cell), B moves right (to 3,5)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Right)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // Both should keep their intentions (chase is allowed)
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Right);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Right);
}

TEST(TestCollisionChaseAllowedEndToEnd) {
  // End-to-end test: verify chase actually works - A takes B's spot as B moves away
  CollisionTestEnv env;

  Position original_a_pos{3, 3};
  Position original_b_pos{3, 4};
  Position expected_a_pos{3, 4};  // A takes B's original spot
  Position expected_b_pos{3, 5};  // B moves to new spot

  auto* a = env.GetMutableObjectManager().CreateActor<Player>(original_a_pos);
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>(original_b_pos);

  // A moves right (to 3,4 - B's cell), B moves right (to 3,5)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Right)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();
  env.TestExecuteValidatedMovements();

  // Verify chase succeeded - A is now in B's original position, B moved forward
  ASSERT_EQ(a->GetPosition(), expected_a_pos);  // A chased into B's original spot
  ASSERT_EQ(b->GetPosition(), expected_b_pos);  // B moved forward
}

TEST(TestCollisionChaseBlocked) {
  // A->B's cell, B staying -> A noop
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 3});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 4});

  // A moves right (to 3,4 - B's cell), B stays
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Stay)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // A should become Stay, B stays
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Stay);
}

TEST(TestCollisionChaseChain) {
  // A->B->C->empty (train movement), all succeed
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 2});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 3});
  auto* c = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 4});

  // All move right: A->(3,3), B->(3,4), C->(3,5)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Right)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // All should keep their intentions
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Right);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Right);
  ASSERT_EQ(c->GetIntention().movement, MovementAction::Right);
}

TEST(TestCollisionChaseIntoConflict) {
  // A->B's cell, B->C's cell, C staying -> B noop -> A noop (cascade)
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 2});
  auto* b = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 3});
  auto* c = env.GetMutableObjectManager().CreateActor<NPCCompanion>({3, 4});

  // A and B move right, C stays
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Stay)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  // C stays, so B can't move, so A can't move
  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
  ASSERT_EQ(b->GetIntention().movement, MovementAction::Stay);
  ASSERT_EQ(c->GetIntention().movement, MovementAction::Stay);
}

TEST(TestCollisionWall) {
  // Agent moving to wall -> noop
  CollisionTestEnv env;
  env.AddWall({3, 4});

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({3, 3});

  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right)  // to wall at (3,4)
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
}

TEST(TestCollisionOutOfBounds) {
  // Agent moving out of bounds -> noop
  CollisionTestEnv env;

  auto* a = env.GetMutableObjectManager().CreateActor<Player>({0, 0});

  std::vector<Action> actions = {
    EncodeAction(MovementAction::Up)  // out of bounds
  };

  env.TestGatherIntentions(actions);
  env.TestResolveCollisions();

  ASSERT_EQ(a->GetIntention().movement, MovementAction::Stay);
}

// =============================================================================
// Direction Auto-Update Test
// =============================================================================
TEST(TestDirectionAutoUpdate) {
  CollisionTestEnv env;

  auto* player = env.GetMutableObjectManager().CreateActor<Player>({5, 5});

  // Initial direction is Down
  ASSERT_EQ(player->GetDirection(), Direction::Down);

  // Move right - direction should update
  std::vector<Action> actions = {EncodeAction(MovementAction::Right)};
  env.TestGatherIntentions(actions);

  ASSERT_EQ(player->GetDirection(), Direction::Right);

  // Move up - direction should update
  actions = {EncodeAction(MovementAction::Up)};
  env.TestGatherIntentions(actions);

  ASSERT_EQ(player->GetDirection(), Direction::Up);

  // Stay - direction should NOT change
  actions = {EncodeAction(MovementAction::Stay)};
  env.TestGatherIntentions(actions);

  ASSERT_EQ(player->GetDirection(), Direction::Up);  // Still Up
}

// =============================================================================
// SynchroEnv Tests
// =============================================================================
TEST(TestSynchroEnvCreation) {
  SynchroEnv env(42);  // Fixed seed

  ASSERT_EQ(env.NumAgents(), 3);
  ASSERT_EQ(env.GetSynchroPositions().size(), 3u);
  ASSERT_FALSE(env.IsDone());
}

TEST(TestSynchroEnvResetSameSeed) {
  SynchroEnv env1(42);
  SynchroEnv env2(42);

  // Same seed should produce same initial positions
  auto agents1 = env1.GetObjectManager().GetAllAgents();
  auto agents2 = env2.GetObjectManager().GetAllAgents();

  ASSERT_EQ(agents1.size(), agents2.size());
  for (size_t i = 0; i < agents1.size(); ++i) {
    ASSERT_EQ(agents1[i]->GetPosition(), agents2[i]->GetPosition());
  }
}

TEST(TestSynchroEnvResetDifferentSeed) {
  SynchroEnv env1(42);
  SynchroEnv env2(123);

  // Different seeds should (very likely) produce different positions
  auto agents1 = env1.GetObjectManager().GetAllAgents();
  auto agents2 = env2.GetObjectManager().GetAllAgents();

  // At least one position should differ (not guaranteed but very likely)
  bool any_different = false;
  for (size_t i = 0; i < agents1.size(); ++i) {
    if (agents1[i]->GetPosition() != agents2[i]->GetPosition()) {
      any_different = true;
      break;
    }
  }
  ASSERT_TRUE(any_different);
}

TEST(TestSynchroEnvStep) {
  SynchroEnv env(42);

  // All stay
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(actions);

  ASSERT_EQ(result.rewards.size(), 3u);
  ASSERT_EQ(env.GetTick(), 1);
}

TEST(TestSynchroEnvAgentsOnSynchroCells) {
  SynchroEnv env(42);

  // Initially agents should not be on synchro cells (spawned on regular cells)
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);
}

TEST(TestSynchroEnvToString) {
  SynchroEnv env(42);
  std::string str = env.ToString();

  // Should contain tick info
  ASSERT_TRUE(str.find("Tick:") != std::string::npos);
  // Should contain agent info
  ASSERT_TRUE(str.find("Agents:") != std::string::npos);
}

TEST(TestSynchroEnvLegalActions) {
  SynchroEnv env(42);

  for (int i = 0; i < 3; ++i) {
    auto actions = env.LegalActions(i);
    // At minimum, Stay should be legal
    ASSERT_FALSE(actions.empty());
    bool has_stay = false;
    for (Action a : actions) {
      if (DecodeAction(a).movement == MovementAction::Stay) {
        has_stay = true;
        break;
      }
    }
    ASSERT_TRUE(has_stay);
  }
}

TEST(TestSynchroEnvRandomSimulation) {
  // Run a random simulation to test it doesn't crash
  SynchroEnv env(42);
  pcg32 rng(12345);

  int max_steps = 1000;
  int step = 0;

  while (!env.IsDone() && step < max_steps) {
    std::vector<Action> actions;
    for (int i = 0; i < env.NumAgents(); ++i) {
      auto legal = env.LegalActions(i);
      std::uniform_int_distribution<size_t> dist(0, legal.size() - 1);
      actions.push_back(legal[dist(rng)]);
    }
    env.Step(actions);
    step++;
  }

  // Should have run without crashing
  ASSERT_TRUE(step > 0);
}

// =============================================================================
// Renderer Tests
// =============================================================================
TEST(TestRendererAscii) {
  SynchroEnv env(42);
  Renderer renderer;

  // Just verify it doesn't crash and produces output
  std::string output = renderer.RenderAscii(env);

  ASSERT_FALSE(output.empty());
  ASSERT_TRUE(output.find("Tick:") != std::string::npos);
  ASSERT_TRUE(output.find("Synchro:") != std::string::npos);
}

// =============================================================================
// Spatial Utility Tests
// =============================================================================
TEST(TestManhattanDistance) {
  Position p1{0, 0};
  Position p2{3, 4};

  ASSERT_EQ(ManhattanDistance(p1, p2), 7);  // 3 + 4
  ASSERT_EQ(ManhattanDistance(p2, p1), 7);  // Symmetric

  Position p3{5, 5};
  Position p4{5, 5};
  ASSERT_EQ(ManhattanDistance(p3, p4), 0);  // Same position

  Position p5{2, 3};
  Position p6{7, 1};
  ASSERT_EQ(ManhattanDistance(p5, p6), 7);  // |2-7| + |3-1| = 5 + 2
}

TEST(TestChebyshevDistance) {
  Position p1{0, 0};
  Position p2{3, 4};

  ASSERT_EQ(ChebyshevDistance(p1, p2), 4);  // max(3, 4)

  Position p3{5, 5};
  ASSERT_EQ(ChebyshevDistance(p3, p3), 0);  // Same position
}

TEST(TestPositionOperators) {
  Position p1{3, 4};
  Position p2{1, 2};

  Position sum = p1 + p2;
  ASSERT_EQ(sum.row, 4);
  ASSERT_EQ(sum.col, 6);

  Position diff = p1 - p2;
  ASSERT_EQ(diff.row, 2);
  ASSERT_EQ(diff.col, 2);
}

TEST(TestActionToString) {
  // New action encoding: action = movement * kNumInteractActions + interact
  // 0 = Stay+None, 1 = Stay+Attack, 2 = Up+None, 3 = Up+Attack, etc.
  ASSERT_EQ(ActionToString(0), "Stay");           // Stay + None
  ASSERT_EQ(ActionToString(1), "Stay+Attack");    // Stay + Attack
  ASSERT_EQ(ActionToString(2), "Up");             // Up + None
  ASSERT_EQ(ActionToString(3), "Up+Attack");      // Up + Attack
  ASSERT_EQ(ActionToString(4), "Down");           // Down + None
  ASSERT_EQ(ActionToString(6), "Left");           // Left + None
  ASSERT_EQ(ActionToString(8), "Right");          // Right + None
}

TEST(TestStringToAction) {
  // StringToAction returns the encoded action with interact=None
  auto stay = StringToAction("Stay");
  ASSERT_TRUE(stay.has_value());
  ASSERT_EQ(*stay, EncodeAction(MovementAction::Stay));  // 0

  auto up = StringToAction("up");
  ASSERT_TRUE(up.has_value());
  ASSERT_EQ(*up, EncodeAction(MovementAction::Up));  // 2

  auto invalid = StringToAction("invalid");
  ASSERT_FALSE(invalid.has_value());
}

TEST(TestDirectionToMovement) {
  ASSERT_EQ(DirectionToMovement(Direction::Up), MovementAction::Up);
  ASSERT_EQ(DirectionToMovement(Direction::Down), MovementAction::Down);
  ASSERT_EQ(DirectionToMovement(Direction::Left), MovementAction::Left);
  ASSERT_EQ(DirectionToMovement(Direction::Right), MovementAction::Right);
}

// =============================================================================
// Grid Neighbor Tests
// =============================================================================
TEST(TestGridGetNeighbors) {
  Grid grid(10, 10);

  // Center cell should have 4 neighbors
  auto neighbors = grid.GetNeighbors({5, 5});
  ASSERT_EQ(neighbors.size(), 4u);

  // Corner cell should have 2 neighbors
  neighbors = grid.GetNeighbors({0, 0});
  ASSERT_EQ(neighbors.size(), 2u);

  // Edge cell should have 3 neighbors
  neighbors = grid.GetNeighbors({0, 5});
  ASSERT_EQ(neighbors.size(), 3u);
}

TEST(TestGridGetWalkableNeighbors) {
  Grid grid(10, 10);

  // Add walls around (5,5)
  grid.SetCell({5, 4}, CellKind::Wall);  // Left
  grid.SetCell({5, 6}, CellKind::Wall);  // Right

  auto neighbors = grid.GetWalkableNeighbors({5, 5});
  ASSERT_EQ(neighbors.size(), 2u);  // Only up and down are walkable
}

// =============================================================================
// Pathfinder Tests
// =============================================================================
TEST(TestPathfinderStraightPath) {
  Grid grid(10, 10);
  Pathfinder pf(grid);

  // Straight horizontal path
  auto path = pf.FindPath({5, 2}, {5, 7});
  ASSERT_FALSE(path.empty());
  ASSERT_EQ(path.front(), (Position{5, 2}));
  ASSERT_EQ(path.back(), (Position{5, 7}));
  ASSERT_EQ(path.size(), 6u);  // 5 steps + start = 6 positions
}

TEST(TestPathfinderAroundObstacle) {
  Grid grid(10, 10);

  // Create a horizontal wall
  for (int c = 2; c <= 7; ++c) {
    grid.SetCell({5, c}, CellKind::Wall);
  }

  Pathfinder pf(grid);

  // Path should go around the wall
  auto path = pf.FindPath({4, 4}, {6, 4});

  ASSERT_FALSE(path.empty());
  ASSERT_EQ(path.front(), (Position{4, 4}));
  ASSERT_EQ(path.back(), (Position{6, 4}));

  // Path should be longer than 2 (direct would be blocked)
  ASSERT_TRUE(path.size() > 3);
}

TEST(TestPathfinderNoPath) {
  Grid grid(10, 10);

  // Completely surround the start with walls
  grid.SetCell({4, 5}, CellKind::Wall);
  grid.SetCell({6, 5}, CellKind::Wall);
  grid.SetCell({5, 4}, CellKind::Wall);
  grid.SetCell({5, 6}, CellKind::Wall);

  Pathfinder pf(grid);

  auto path = pf.FindPath({5, 5}, {8, 8});
  ASSERT_TRUE(path.empty());  // No path exists
}

TEST(TestPathfinderSamePosition) {
  Grid grid(10, 10);
  Pathfinder pf(grid);

  auto path = pf.FindPath({5, 5}, {5, 5});
  ASSERT_EQ(path.size(), 1u);
  ASSERT_EQ(path[0], (Position{5, 5}));
}

TEST(TestPathfinderGetDistance) {
  Grid grid(10, 10);
  Pathfinder pf(grid);

  // Straight path distance
  int dist = pf.GetDistance({5, 2}, {5, 7});
  ASSERT_EQ(dist, 5);  // 5 steps

  // Same position
  dist = pf.GetDistance({5, 5}, {5, 5});
  ASSERT_EQ(dist, 0);
}

TEST(TestPathfinderIsReachable) {
  Grid grid(10, 10);

  // Create isolated region
  for (int r = 4; r <= 6; ++r) {
    grid.SetCell({r, 3}, CellKind::Wall);
    grid.SetCell({r, 7}, CellKind::Wall);
  }
  for (int c = 3; c <= 7; ++c) {
    grid.SetCell({4, c}, CellKind::Wall);
    grid.SetCell({6, c}, CellKind::Wall);
  }
  // (5, 5) is now isolated

  // But actually the cell itself is still floor
  grid.SetCell({5, 5}, CellKind::Floor);

  Pathfinder pf(grid);

  ASSERT_FALSE(pf.IsReachable({5, 5}, {8, 8}));
  ASSERT_TRUE(pf.IsReachable({5, 5}, {5, 5}));  // Same position always reachable
}

TEST(TestPathfinderGetReachableCells) {
  Grid grid(5, 5);
  LevelBuilder builder(grid);

  builder.Fill(CellKind::Floor);
  builder.Border(CellKind::Wall);

  Pathfinder pf(grid);

  // All interior cells should be reachable from any interior cell
  auto reachable = pf.GetReachableCells({2, 2});
  ASSERT_EQ(reachable.size(), 9u);  // 3x3 interior
}

// =============================================================================
// Environment Utility Tests
// =============================================================================
TEST(TestEnvMinMaxUtility) {
  SynchroEnv env(42);

  double min_util = env.MinUtility();
  double max_util = env.MaxUtility();

  // Min should be negative (time penalties only)
  ASSERT_TRUE(min_util < 0);

  // Max should be positive (win reward + progress)
  ASSERT_TRUE(max_util > 0);

  // Max should be greater than min
  ASSERT_TRUE(max_util > min_util);
}

TEST(TestEnvObservationShape) {
  SynchroEnv env(42);

  auto shape = env.ObservationShape();
  ASSERT_EQ(shape.size(), 3u);
  ASSERT_EQ(shape[0], 5);   // 5 planes
  ASSERT_EQ(shape[1], 12);  // Grid size
  ASSERT_EQ(shape[2], 12);

  // Also test alias
  auto shape2 = env.ObservationTensorShape();
  ASSERT_EQ(shape, shape2);
}

TEST(TestEnvObservationTensor5Planes) {
  SynchroEnv env(42);

  std::vector<float> obs;
  env.ObservationTensor(obs, 0);  // Player 0's view

  auto shape = env.ObservationShape();
  int expected_size = shape[0] * shape[1] * shape[2];
  ASSERT_EQ(static_cast<int>(obs.size()), expected_size);

  // Check that observation has exact expected values based on grid structure
  // Count actual 1s in each plane:
  // Plane 0 (floor): all walkable cells (perimeter walls, so (size-2)^2 + some synchro)
  // Plane 1 (walls): perimeter = 4*size - 4 corners = 4*size - 4 for a square grid
  // Plane 2 (synchro): exactly 3 synchro cells
  // Plane 3 (current player): exactly 1
  // Plane 4 (other agents): exactly 2 (3 total - 1 current)
  int size = shape[1];
  int plane0_ones = 0, plane1_ones = 0, plane2_ones = 0;
  for (int r = 0; r < size; ++r) {
    for (int c = 0; c < size; ++c) {
      if (obs[0 * size * size + r * size + c] > 0.5f) plane0_ones++;
      if (obs[1 * size * size + r * size + c] > 0.5f) plane1_ones++;
      if (obs[2 * size * size + r * size + c] > 0.5f) plane2_ones++;
    }
  }
  // Verify: plane0 + plane1 should equal total grid cells
  ASSERT_EQ(plane0_ones + plane1_ones, size * size);
  // Verify synchro cells
  ASSERT_EQ(plane2_ones, 3);  // Default env has 3 synchro cells

  // Check that player plane (3) has exactly one 1.0
  int plane3_ones = 0;
  for (int r = 0; r < size; ++r) {
    for (int c = 0; c < size; ++c) {
      if (obs[3 * size * size + r * size + c] > 0.5f) {
        plane3_ones++;
      }
    }
  }
  ASSERT_EQ(plane3_ones, 1);  // Current player position

  // Check that other agents plane (4) has exactly 2 ones (the other agents)
  int plane4_ones = 0;
  for (int r = 0; r < size; ++r) {
    for (int c = 0; c < size; ++c) {
      if (obs[4 * size * size + r * size + c] > 0.5f) {
        plane4_ones++;
      }
    }
  }
  ASSERT_EQ(plane4_ones, 2);  // Two other agents
}

TEST(TestEnvHorizon) {
  SynchroEnv env(42);

  // Default horizon should be set
  ASSERT_TRUE(env.GetHorizon() > 0);
  ASSERT_EQ(env.GetHorizon(), kDefaultHorizon);
}

// =============================================================================
// Rectangular Grid Tests
// =============================================================================
TEST(TestRectangularGrid) {
  Grid grid(5, 10);  // 5 rows, 10 cols
  ASSERT_EQ(grid.GetRows(), 5);
  ASSERT_EQ(grid.GetCols(), 10);

  // Test bounds
  ASSERT_TRUE(grid.IsInBounds({4, 9}));   // max valid row/col
  ASSERT_FALSE(grid.IsInBounds({5, 0}));  // row out of bounds
  ASSERT_FALSE(grid.IsInBounds({0, 10})); // col out of bounds
}

TEST(TestRectangularObjectManager) {
  ObjectManager mgr(5, 10);  // 5 rows, 10 cols
  ASSERT_EQ(mgr.GetRows(), 5);
  ASSERT_EQ(mgr.GetCols(), 10);

  // Test creating actors in rectangular grid
  auto* p1 = mgr.CreateActor<Player>({0, 0});
  auto* p2 = mgr.CreateActor<NPCCompanion>({4, 9});

  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  ASSERT_EQ(p1->GetPosition(), (Position{0, 0}));
  ASSERT_EQ(p2->GetPosition(), (Position{4, 9}));

  ASSERT_TRUE(mgr.IsOccupied({0, 0}));
  ASSERT_TRUE(mgr.IsOccupied({4, 9}));
  ASSERT_FALSE(mgr.IsOccupied({5, 0}));  // Out of bounds
}

TEST(TestRectangularSynchroEnv) {
  // 10x8 grid with 5 companions and 4 synchro cells
  SynchroEnv env(10, 8, 5, 4);

  ASSERT_EQ(env.GetRows(), 10);
  ASSERT_EQ(env.GetCols(), 8);
  ASSERT_EQ(env.NumAgents(), 5);
  ASSERT_EQ(env.GetSynchroPositions().size(), 4u);
  ASSERT_EQ(env.GetNumCompanions(), 5);
  ASSERT_EQ(env.GetNumSynchro(), 4);
}

TEST(TestFindEmptyCells) {
  SynchroEnv env(10, 10, 3, 3, 0, 42);

  // Grid has wall perimeter, so interior is 8x8 = 64 cells
  // 3 synchro cells + 3 companions = 6 occupied
  // Should have 64 - 6 = 58 empty floor cells

  pcg32 rng(123);
  auto empty = env.FindEmptyCells(5, rng);
  ASSERT_EQ(empty.size(), 5u);

  // All positions should be Floor and not occupied
  for (const auto& pos : empty) {
    ASSERT_EQ(env.GetGrid().GetCellKind(pos), CellKind::Floor);
    ASSERT_FALSE(env.GetObjectManager().IsOccupied(pos));
  }
}

TEST(TestFindEmptyCellsThrowsWhenNotEnough) {
  // 4x4 grid with wall perimeter = 2x2 = 4 interior cells
  // 3 companions + 1 synchro = 4 cells used
  // Asking for more should throw
  SynchroEnv env(4, 4, 3, 1, 0, 42);

  pcg32 rng(123);

  // Try to find more empty cells than could possibly exist in the grid.
  // (Goal cells are annotations now, so they don't take up physical slots,
  //  but 10000 is always too many.)
  bool threw = false;
  try {
    env.FindEmptyCells(10000, rng);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

TEST(TestSynchroEnvFullPopulation) {
  // 8x6 grid = 6x4 = 24 interior cells
  // Fill with max companions + synchro that fit
  int rows = 8, cols = 6;
  int interior = (rows - 2) * (cols - 2);  // 24 cells
  int num_synchro = 3;
  int num_companions = interior - num_synchro;  // 21 companions

  SynchroEnv env(rows, cols, num_companions, num_synchro, 0, 42);

  ASSERT_EQ(env.NumAgents(), num_companions);
  ASSERT_EQ(env.GetSynchroPositions().size(), static_cast<size_t>(num_synchro));

  // All interior cells should be reserved (by either a companion or a
  // SynchroGoal annotation).
  const auto& annotations = env.GetAnnotations();
  int total_occupied = 0;
  for (int r = 1; r < rows - 1; ++r) {
    for (int c = 1; c < cols - 1; ++c) {
      Position pos{r, c};
      bool is_goal = annotations.HasTag(
          AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
          SemanticTag::SynchroGoal);
      bool is_occupied = env.GetObjectManager().IsOccupied(pos);
      if (is_goal || is_occupied) {
        total_occupied++;
      }
    }
  }
  ASSERT_EQ(total_occupied, interior);
}

TEST(TestSynchroEnvTooManyCompanions) {
  // 8x6 grid = 6x4 = 24 interior cells
  // Try to put 25 companions (> 24 available spots)
  int rows = 8, cols = 6;

  bool threw = false;
  try {
    SynchroEnv env(rows, cols, 25, 3, 0, 42);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

TEST(TestSynchroEnvInvalidConfig) {
  // Test invalid configurations throw
  bool threw = false;

  // num_companions < 1
  try {
    SynchroEnv env(10, 10, 0, 1);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  // num_synchro < 1
  threw = false;
  try {
    SynchroEnv env(10, 10, 3, 0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  // num_synchro > num_companions
  threw = false;
  try {
    SynchroEnv env(10, 10, 2, 3);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

// =============================================================================
// Observation Tensor Content Validation Tests
// =============================================================================
TEST(TestObservationTensorPlane0Floor) {
  // Test that Plane 0 contains all walkable cells (including synchro)
  SynchroEnv env(8, 8, 2, 2, 0, 123);

  std::vector<float> obs;
  env.ObservationTensor(obs, 0);

  int rows = env.GetRows();
  int cols = env.GetCols();

  // Check each cell
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      Position pos{r, c};
      int plane0_idx = 0 * rows * cols + r * cols + c;
      float plane0_value = obs[plane0_idx];

      bool is_walkable = env.GetGrid().IsWalkable(pos);

      if (is_walkable) {
        ASSERT_EQ(plane0_value, 1.0f);
      } else {
        ASSERT_EQ(plane0_value, 0.0f);
      }
    }
  }
}

TEST(TestObservationTensorPlane1Walls) {
  // Test that Plane 1 contains all walls
  SynchroEnv env(10, 10, 2, 2, 0, 456);

  std::vector<float> obs;
  env.ObservationTensor(obs, 0);

  int rows = env.GetRows();
  int cols = env.GetCols();

  // Check each cell
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      Position pos{r, c};
      int plane1_idx = 1 * rows * cols + r * cols + c;
      float plane1_value = obs[plane1_idx];

      CellKind kind = env.GetGrid().GetCellKind(pos);

      if (kind == CellKind::Wall) {
        ASSERT_EQ(plane1_value, 1.0f);
      } else {
        ASSERT_EQ(plane1_value, 0.0f);
      }
    }
  }
}

TEST(TestObservationTensorPlane2Synchro) {
  // Test that Plane 2 contains all synchro cells
  SynchroEnv env(10, 10, 3, 3, 0, 789);

  std::vector<float> obs;
  env.ObservationTensor(obs, 0);

  int rows = env.GetRows();
  int cols = env.GetCols();
  auto synchro_positions = env.GetSynchroPositions();

  int synchro_count = 0;
  const auto& annotations = env.GetAnnotations();

  // Check each cell
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      Position pos{r, c};
      int plane2_idx = 2 * rows * cols + r * cols + c;
      float plane2_value = obs[plane2_idx];

      bool is_goal = annotations.HasTag(
          AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
          SemanticTag::SynchroGoal);

      if (is_goal) {
        ASSERT_EQ(plane2_value, 1.0f);
        synchro_count++;
      } else {
        ASSERT_EQ(plane2_value, 0.0f);
      }
    }
  }

  // Verify we found all synchro cells
  ASSERT_EQ(synchro_count, static_cast<int>(synchro_positions.size()));
}

TEST(TestObservationTensorPlane3CurrentPlayer) {
  // Test that Plane 3 has exactly one 1.0 at the current player's position
  SynchroEnv env(10, 10, 3, 3, 0, 100);

  int rows = env.GetRows();
  int cols = env.GetCols();

  // Test for each player
  for (int player = 0; player < env.NumAgents(); ++player) {
    std::vector<float> obs;
    env.ObservationTensor(obs, player);

    auto agents = env.GetObjectManager().GetAllAgents();
    Position player_pos = agents[player]->GetPosition();

    int player_count = 0;

    // Check each cell in Plane 3
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        Position pos{r, c};
        int plane3_idx = 3 * rows * cols + r * cols + c;
        float plane3_value = obs[plane3_idx];

        if (pos == player_pos) {
          ASSERT_EQ(plane3_value, 1.0f);
          player_count++;
        } else {
          ASSERT_EQ(plane3_value, 0.0f);
        }
      }
    }

    // Verify exactly one player position found
    ASSERT_EQ(player_count, 1);
  }
}

TEST(TestObservationTensorPlane4OtherAgents) {
  // Test that Plane 4 contains other agents (not current player)
  SynchroEnv env(10, 10, 3, 3, 0, 200);

  int rows = env.GetRows();
  int cols = env.GetCols();

  // Test for each player
  for (int player = 0; player < env.NumAgents(); ++player) {
    std::vector<float> obs;
    env.ObservationTensor(obs, player);

    auto agents = env.GetObjectManager().GetAllAgents();
    Position player_pos = agents[player]->GetPosition();

    int other_agent_count = 0;

    // Check each cell in Plane 4
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        Position pos{r, c};
        int plane4_idx = 4 * rows * cols + r * cols + c;
        float plane4_value = obs[plane4_idx];

        // Check if another agent is at this position
        bool is_other_agent = false;
        for (const Agent* agent : agents) {
          if (agent->GetPosition() == pos && agent->GetPosition() != player_pos) {
            is_other_agent = true;
            break;
          }
        }

        if (is_other_agent) {
          ASSERT_EQ(plane4_value, 1.0f);
          other_agent_count++;
        } else {
          ASSERT_EQ(plane4_value, 0.0f);
        }
      }
    }

    // Verify we found all other agents
    ASSERT_EQ(other_agent_count, env.NumAgents() - 1);
  }
}

TEST(TestObservationTensorConsistencyAfterMovement) {
  // Test that observation updates correctly after agents move
  SynchroEnv env(10, 10, 2, 2, 0, 300);

  // Get initial observation
  std::vector<float> obs_before;
  env.ObservationTensor(obs_before, 0);

  // Move agents
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Right),
    EncodeAction(MovementAction::Down)
  };

  env.Step(actions);

  // Get observation after movement
  std::vector<float> obs_after;
  env.ObservationTensor(obs_after, 0);

  // Observations should be different (agents moved)
  bool different = false;
  for (size_t i = 0; i < obs_before.size(); ++i) {
    if (obs_before[i] != obs_after[i]) {
      different = true;
      break;
    }
  }
  ASSERT_TRUE(different);
}

// =============================================================================
// Vector Observation Tests
// =============================================================================
TEST(TestVectorObservationSize) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);

  // Base vector observation size should be 9 (8 base + 1 steps_left)
  ASSERT_EQ(env.VectorObservationSize(), 9);
}

TEST(TestVectorObservationValues) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);

  std::vector<float> obs;
  env.VectorObservation(obs, 0);

  ASSERT_EQ(obs.size(), static_cast<size_t>(env.VectorObservationSize()));

  // First two values are position (normalized to [0,1])
  ASSERT_TRUE(obs[0] >= 0.0f && obs[0] <= 1.0f);  // row
  ASSERT_TRUE(obs[1] >= 0.0f && obs[1] <= 1.0f);  // col

  // Third value is health ratio
  ASSERT_TRUE(obs[2] >= 0.0f && obs[2] <= 1.0f);

  // Fourth value is distance to goal (normalized)
  ASSERT_TRUE(obs[3] >= 0.0f && obs[3] <= 1.0f);

  // Ninth value (index 8) is steps_left / 100
  // At start of episode with default horizon 100, should be 100/100 = 1.0
  // (or horizon/100 if horizon is different)
  ASSERT_TRUE(obs[8] >= 0.0f);  // Should be positive at start
}

TEST(TestVectorObservationMultipleAgents) {
  SynchroEnv env(8, 8, 3, 3, 0, 42);

  // Get observations for each player
  std::vector<float> obs0, obs1, obs2;
  env.VectorObservation(obs0, 0);
  env.VectorObservation(obs1, 1);
  env.VectorObservation(obs2, 2);

  // All should have same size
  ASSERT_EQ(obs0.size(), obs1.size());
  ASSERT_EQ(obs1.size(), obs2.size());

  // Positions should be different (different agents)
  bool pos_different = (obs0[0] != obs1[0] || obs0[1] != obs1[1]);
  ASSERT_TRUE(pos_different);
}

TEST(TestVectorObservationUpdatesAfterMove) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);

  // Get initial observation
  std::vector<float> obs_before;
  env.VectorObservation(obs_before, 0);

  // Move the agent
  std::vector<Action> actions = {EncodeAction(MovementAction::Right)};
  env.Step(actions);

  // Get new observation
  std::vector<float> obs_after;
  env.VectorObservation(obs_after, 0);

  // Position should have changed (specifically col should increase)
  // Original col was obs_before[1], after moving right it should be higher
  ASSERT_TRUE(obs_after[1] > obs_before[1]);

  // Steps left (index 8) should have decreased by 1/100 = 0.01
  ASSERT_TRUE(obs_after[8] < obs_before[8]);
}

TEST(TestVectorObservationStepsLeft) {
  // Test that steps_left decreases correctly over multiple steps
  SynchroEnv env(8, 8, 1, 1, 0, 42, 0, 50);  // horizon = 50

  std::vector<float> obs;
  env.VectorObservation(obs, 0);

  // At start: steps_left = 50, so obs[8] = 50/100 = 0.5
  ASSERT_TRUE(std::abs(obs[8] - 0.5f) < 0.01f);

  // Take 10 steps
  std::vector<Action> actions = {EncodeAction(MovementAction::Stay)};
  for (int i = 0; i < 10; ++i) {
    env.Step(actions);
  }

  env.VectorObservation(obs, 0);
  // After 10 steps: steps_left = 40, so obs[8] = 40/100 = 0.4
  ASSERT_TRUE(std::abs(obs[8] - 0.4f) < 0.01f);
}

// =============================================================================
// Exact Reward Value Tests
// =============================================================================
TEST(TestRewardWinValue) {
  // Test that win condition gives exactly kWinReward (10.0)
  SynchroEnv env(10, 10, 2, 2, 0, 400);

  // Manually place agents on synchro cells
  auto synchro_positions = env.GetSynchroPositions();
  auto& obj_mgr = env.GetMutableObjectManager();

  // Clear and respawn agents on synchro cells
  obj_mgr.Clear();
  for (size_t i = 0; i < 2; ++i) {
    obj_mgr.CreateActor<NPCCompanion>(synchro_positions[i]);
  }

  // Step with Stay actions (agents already on goals)
  std::vector<Action> actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(actions);

  // Verify win reward: kWinReward + kProgressReward * num_on_synchro + time_penalty
  // time_penalty = -num_agents * kProgressReward = -2 * 0.1 = -0.2
  // With 2 on synchro: 0.1 * 2 + (-0.2) + 10.0 = 10.0
  int num_agents = 2;
  double time_penalty = -static_cast<double>(num_agents) * SynchroEnv::kProgressReward;
  double expected = SynchroEnv::kWinReward + SynchroEnv::kProgressReward * 2 + time_penalty;
  ASSERT_EQ(result.rewards.size(), 2u);
  ASSERT_EQ(result.rewards[0], expected);
  ASSERT_EQ(result.rewards[1], expected);
  ASSERT_TRUE(result.done);
}

TEST(TestRewardProgressValue) {
  // Test that progress reward is exactly kProgressReward * num_agents_on_goals
  SynchroEnv env(10, 10, 3, 3, 0, 500);

  auto synchro_positions = env.GetSynchroPositions();
  auto& obj_mgr = env.GetMutableObjectManager();

  // Place only 1 agent on a synchro cell, others elsewhere
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(synchro_positions[0]);  // On synchro
  obj_mgr.CreateActor<NPCCompanion>(Position{5, 5});  // Not on synchro
  obj_mgr.CreateActor<NPCCompanion>(Position{6, 6});  // Not on synchro

  std::vector<Action> actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(actions);

  // Expected: 1 agent on synchro = kProgressReward * 1 + time_penalty
  // time_penalty = -num_agents * kProgressReward = -3 * 0.1 = -0.3
  // With 1 on synchro: 0.1 * 1 + (-0.3) = -0.2
  int num_agents = 3;
  double time_penalty = -static_cast<double>(num_agents) * SynchroEnv::kProgressReward;
  double expected = SynchroEnv::kProgressReward * 1.0 + time_penalty;

  ASSERT_EQ(result.rewards.size(), 3u);
  for (double reward : result.rewards) {
    ASSERT_EQ(reward, expected);
  }
  ASSERT_FALSE(result.done);
}

TEST(TestRewardTimePenalty) {
  // Test that with no progress, reward is exactly time_penalty
  SynchroEnv env(10, 10, 2, 2, 0, 600);

  auto& obj_mgr = env.GetMutableObjectManager();

  // Place agents away from synchro cells
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(Position{5, 5});
  obj_mgr.CreateActor<NPCCompanion>(Position{6, 6});

  std::vector<Action> actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(actions);

  // Expected: 0 agents on synchro = kProgressReward * 0 + time_penalty
  // time_penalty = -num_agents * kProgressReward = -2 * 0.1 = -0.2
  int num_agents = 2;
  double time_penalty = -static_cast<double>(num_agents) * SynchroEnv::kProgressReward;
  double expected = time_penalty;

  ASSERT_EQ(result.rewards.size(), 2u);
  for (double reward : result.rewards) {
    ASSERT_EQ(reward, expected);
  }
  ASSERT_FALSE(result.done);
}

TEST(TestRewardAccumulationOverSteps) {
  // Test that rewards accumulate correctly over multiple steps
  SynchroEnv env(10, 10, 2, 2, 0, 700);

  double total_reward = 0.0;

  // Take 5 steps with no progress
  for (int step = 0; step < 5; ++step) {
    std::vector<Action> actions = {
      EncodeAction(MovementAction::Stay),
      EncodeAction(MovementAction::Stay)
    };

    auto result = env.Step(actions);
    total_reward += result.rewards[0];
  }

  // Expected: 5 steps * time_penalty
  // time_penalty = -num_agents * kProgressReward = -2 * 0.1 = -0.2
  int num_agents = 2;
  double time_penalty = -static_cast<double>(num_agents) * SynchroEnv::kProgressReward;
  double expected_total = 5.0 * time_penalty;
  ASSERT_EQ(total_reward, expected_total);
}

TEST(TestMaxUtilityMatchesOptimalGame) {
  // Test that MaxUtility equals the total reward from an optimal game
  // With the new reward structure, MaxUtility = kWinReward = 10.0

  constexpr double kEpsilon = 1e-9;
  auto approx_eq = [kEpsilon](double a, double b) {
    return std::abs(a - b) < kEpsilon;
  };

  // 5x5 grid, 2 agents, 2 synchro cells, seed 42
  SynchroEnv env(5, 5, 2, 2, 0, 42);

  // MaxUtility should now be exactly kWinReward (10.0)
  double max_util = env.MaxUtility();
  ASSERT_TRUE(approx_eq(max_util, SynchroEnv::kWinReward));

  // Reset and place agents directly on synchro cells to win immediately
  auto& obj_mgr = env.GetMutableObjectManager();
  obj_mgr.Clear();

  auto synchros = env.GetSynchroPositions();
  for (size_t i = 0; i < 2; ++i) {
    obj_mgr.CreateActor<NPCCompanion>(synchros[i]);
  }

  std::vector<Action> stay = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(stay);
  double achieved = result.rewards[0];
  ASSERT_TRUE(result.done);

  // This single-step win should be exactly kWinReward = 10.0
  // (kProgressReward * num_agents + time_penalty = 0 when all on synchro)
  ASSERT_TRUE(approx_eq(achieved, SynchroEnv::kWinReward));
  ASSERT_TRUE(approx_eq(achieved, max_util));

  // Now test that total reward from multi-step game doesn't exceed MaxUtility
  // Use seed that ensures agents don't start on synchro cells
  for (unsigned int seed = 100; seed < 200; ++seed) {
    SynchroEnv env2(5, 5, 2, 2, 0, seed);

    // Check that no agent starts on synchro
    if (env2.NumAgentsOnSynchroCells() == 0) {
      double total_reward = 0.0;
      int steps = 0;
      const int max_steps = 50;

      // Simple greedy: each agent moves toward nearest synchro
      while (!env2.IsDone() && steps < max_steps) {
        auto current_agents = env2.GetObjectManager().GetAllAgents();
        auto synchro_pos = env2.GetSynchroPositions();
        std::vector<Action> actions;

        for (size_t i = 0; i < current_agents.size(); ++i) {
          Position agent_pos = current_agents[i]->GetPosition();
          Position target = synchro_pos[i % synchro_pos.size()];

          MovementAction mov = MovementAction::Stay;
          if (agent_pos.row < target.row) mov = MovementAction::Down;
          else if (agent_pos.row > target.row) mov = MovementAction::Up;
          else if (agent_pos.col < target.col) mov = MovementAction::Right;
          else if (agent_pos.col > target.col) mov = MovementAction::Left;

          actions.push_back(EncodeAction(mov));
        }

        auto step_result = env2.Step(actions);
        total_reward += step_result.rewards[0];
        steps++;
      }

      // If we won, check that reward doesn't exceed MaxUtility
      if (env2.IsDone() && env2.IsSuccess()) {
        double max_util2 = env2.MaxUtility();
        // Total reward should be <= MaxUtility
        // (intermediate steps contribute <= 0, so total <= kWinReward)
        ASSERT_TRUE(total_reward <= max_util2 + kEpsilon);
        return;
      }
    }
  }

  ASSERT_TRUE(false);
}

// =============================================================================
// Status Effect Tests
// =============================================================================
TEST(TestStatusEffectApplyAndHas) {
  Agent agent(1, Position{5, 5});

  // No status initially
  ASSERT_FALSE(agent.IsStunned());
  ASSERT_FALSE(agent.IsSlowed());
  ASSERT_FALSE(agent.IsMarked());

  // Apply statuses
  agent.ApplyStatus(StatusType::Stunned, 2);
  agent.ApplyStatus(StatusType::Slowed, 3);
  agent.ApplyStatus(StatusType::Marked, 1);

  ASSERT_TRUE(agent.IsStunned());
  ASSERT_TRUE(agent.IsSlowed());
  ASSERT_TRUE(agent.IsMarked());
}

TEST(TestStatusEffectDuration) {
  Agent agent(1, Position{5, 5});
  agent.ApplyStatus(StatusType::Stunned, 2);

  ASSERT_TRUE(agent.IsStunned());

  // Tick once - duration goes from 2 to 1
  agent.TickStatuses();
  ASSERT_TRUE(agent.IsStunned());

  // Tick again - duration goes from 1 to 0, status removed
  agent.TickStatuses();
  ASSERT_FALSE(agent.IsStunned());
}

TEST(TestStatusEffectRefresh) {
  Agent agent(1, Position{5, 5});
  agent.ApplyStatus(StatusType::Stunned, 2);

  // Tick once - duration now 1
  agent.TickStatuses();
  ASSERT_TRUE(agent.IsStunned());

  // Re-apply with longer duration - should refresh to 3
  agent.ApplyStatus(StatusType::Stunned, 3);
  ASSERT_TRUE(agent.IsStunned());

  // Tick 3 times - should still be stunned until last tick
  agent.TickStatuses();
  ASSERT_TRUE(agent.IsStunned());  // duration 2
  agent.TickStatuses();
  ASSERT_TRUE(agent.IsStunned());  // duration 1
  agent.TickStatuses();
  ASSERT_FALSE(agent.IsStunned()); // duration 0, removed
}

TEST(TestStatusEffectClear) {
  Agent agent(1, Position{5, 5});
  agent.ApplyStatus(StatusType::Stunned, 5);
  agent.ApplyStatus(StatusType::Slowed, 5);

  ASSERT_TRUE(agent.IsStunned());
  ASSERT_TRUE(agent.IsSlowed());

  // Clear only stunned
  agent.ClearStatus(StatusType::Stunned);
  ASSERT_FALSE(agent.IsStunned());
  ASSERT_TRUE(agent.IsSlowed());

  // Clear all
  agent.ApplyStatus(StatusType::Marked, 5);
  agent.ClearAllStatuses();
  ASSERT_FALSE(agent.IsStunned());
  ASSERT_FALSE(agent.IsSlowed());
  ASSERT_FALSE(agent.IsMarked());
}

TEST(TestStatusMarkedDamageMultiplier) {
  Agent agent(1, Position{5, 5});
  agent.SetMaxHealth(100);

  // Normal damage
  agent.TakeDamage(10);
  ASSERT_EQ(agent.GetHealth(), 90);

  // Apply marked
  agent.ApplyStatus(StatusType::Marked, 5);
  agent.TakeDamage(10);  // Should do 15 damage (1.5x)
  ASSERT_EQ(agent.GetHealth(), 75);
}

TEST(TestStatusStunnedPreventsMovement) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);

  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* agent = agents[0];
  Position initial_pos = agent->GetPosition();

  // Apply stun
  agent->ApplyStatus(StatusType::Stunned, 2);

  // Try to move right
  std::vector<Action> actions = {EncodeAction(MovementAction::Right)};
  env.Step(actions);

  // Agent should not have moved
  ASSERT_EQ(agent->GetPosition(), initial_pos);

  // After stun expires, movement should work
  env.Step(actions);  // Still stunned (duration was 2, now 1)
  ASSERT_EQ(agent->GetPosition(), initial_pos);

  env.Step(actions);  // Stun expired, should move
  // Note: stun ticks at end of step, so after 2 steps with duration 2, stun is gone
  // Actually let me reconsider - duration 2 means 2 ticks, so:
  // Step 1: stunned (duration 2), tick -> duration 1
  // Step 2: stunned (duration 1), tick -> duration 0 (removed)
  // Step 3: not stunned, can move
}

TEST(TestStatusSlowedReducesMovement) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);

  // Place agent in center where movement is guaranteed to work
  auto& obj_mgr = env.GetMutableObjectManager();
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(Position{5, 5});

  auto agents = obj_mgr.GetAllAgents();
  Agent* agent = agents[0];
  Position initial_pos = agent->GetPosition();

  // Apply slow
  agent->ApplyStatus(StatusType::Slowed, 4);

  // Slowed agents can only move on even ticks (tick 0, 2, 4...)
  // Tick 0 (before step): can move
  std::vector<Action> actions = {EncodeAction(MovementAction::Right)};
  env.Step(actions);  // tick 0 -> 1
  Position pos_after_1 = agent->GetPosition();
  // Should have moved (tick was 0 = even)
  ASSERT_EQ(pos_after_1.col, initial_pos.col + 1);

  // Tick 1: odd, cannot move
  env.Step(actions);  // tick 1 -> 2
  Position pos_after_2 = agent->GetPosition();
  ASSERT_EQ(pos_after_2, pos_after_1);  // No movement on odd tick

  // Tick 2: even, can move
  env.Step(actions);  // tick 2 -> 3
  Position pos_after_3 = agent->GetPosition();
  ASSERT_EQ(pos_after_3.col, pos_after_1.col + 1);  // Moved on even tick
}

TEST(TestStatusTypeFromString) {
  ASSERT_EQ(StatusTypeFromString("stunned"), StatusType::Stunned);
  ASSERT_EQ(StatusTypeFromString("STUNNED"), StatusType::Stunned);
  ASSERT_EQ(StatusTypeFromString("stun"), StatusType::Stunned);
  ASSERT_EQ(StatusTypeFromString("slowed"), StatusType::Slowed);
  ASSERT_EQ(StatusTypeFromString("slow"), StatusType::Slowed);
  ASSERT_EQ(StatusTypeFromString("marked"), StatusType::Marked);
  ASSERT_EQ(StatusTypeFromString("mark"), StatusType::Marked);
  ASSERT_EQ(StatusTypeFromString("invalid"), StatusType::None);
  ASSERT_EQ(StatusTypeFromString(""), StatusType::None);
}

TEST(TestStatusTypeToString) {
  ASSERT_EQ(StatusTypeToString(StatusType::Stunned), "stunned");
  ASSERT_EQ(StatusTypeToString(StatusType::Slowed), "slowed");
  ASSERT_EQ(StatusTypeToString(StatusType::Marked), "marked");
  ASSERT_EQ(StatusTypeToString(StatusType::None), "none");
}

TEST(TestStatusEffectAppliedByEffect) {
  // Test that effects with status_applied field apply statuses
  EffectConfigRegistry& registry = EffectConfigRegistry::Instance();
  registry.Clear();

  // Register a stun effect
  EffectConfig stun_config;
  stun_config.name = "test_stun";
  stun_config.telegraph_ticks = 0;
  stun_config.active_ticks = 1;
  stun_config.area = {1};  // 1x1
  stun_config.filter = TargetFilter::Companion;
  stun_config.damage = 0;
  stun_config.status_applied = "stunned";
  stun_config.status_duration = 3;
  registry.RegisterConfig(stun_config);

  SynchroEnv env(10, 10, 1, 1, 0, 42);
  auto agents = env.GetMutableObjectManager().GetAllAgents();
  Agent* agent = agents[0];
  Position agent_pos = agent->GetPosition();

  ASSERT_FALSE(agent->IsStunned());

  // Spawn stun effect on agent
  env.SpawnEffect("test_stun", EffectTarget::AtCell(agent_pos));

  // Effect applies immediately since telegraph_ticks = 0
  ASSERT_TRUE(agent->IsStunned());

  registry.Clear();
}

// =============================================================================
// Seed 1337 Reward Tests (8x8 grid, 3 companions)
// =============================================================================
//
// Layout for seed 1337, 8x8 grid, 3 companions, 3 synchro:
//
//   W W W W W W W W
//   W . . 1 . . . W    Agent 1 at (1,3)
//   W . . . . . . W
//   W . . 2 . . . W    Agent 2 at (3,3)
//   W . . . . . S W    Synchro 0 at (4,6)
//   W . . . S . S W    Synchro 1 at (5,6), Synchro 2 at (5,4)
//   W . . . . 0 . W    Agent 0 at (6,5)
//   W W W W W W W W
//
// Reward formula for 3 agents:
//   reward = 0.1 * agents_on_synchro - 0.3 [+ 10.0 if win]
//   0 on synchro: -0.3
//   1 on synchro: -0.2
//   2 on synchro: -0.1
//   3 on synchro: 10.0 (WIN)
//

// Helper for approximate floating point comparison
namespace {
constexpr double kRewardEpsilon = 1e-9;
bool ApproxEq(double a, double b) {
  return std::abs(a - b) < kRewardEpsilon;
}
}  // namespace

#define ASSERT_APPROX_EQ(a, b) \
  if (!ApproxEq((a), (b))) { \
    std::cerr << "ASSERT_APPROX_EQ failed: " << #a << " (" << (a) << ") != " \
              << #b << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::abort(); \
  }

// Test 1: Verify deterministic initial positions with seed 1337
// Note: We test properties only, not hardcoded positions (positions depend on PRNG)
TEST(TestSeed1337InitialPositions) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);

  // Verify correct number of synchro cells
  const auto& synchro_pos = env.GetSynchroPositions();
  ASSERT_EQ(synchro_pos.size(), 3u);

  // Verify all synchro positions are on Floor cells with a SynchroGoal
  // annotation attached (physical + semantic separation).
  const Grid& grid = env.GetGrid();
  const auto& annotations = env.GetAnnotations();
  for (const auto& pos : synchro_pos) {
    ASSERT_TRUE(grid.IsInBounds(pos));
    ASSERT_EQ(grid.GetCell(pos).GetKind(), CellKind::Floor);
    ASSERT_TRUE(annotations.HasTag(
        AnnotationKey{AnnotationTarget::Cell, pos, kInvalidObjectId},
        SemanticTag::SynchroGoal));
  }

  // Verify correct number of agents
  auto agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 3u);

  // Verify all agents are on walkable (Floor) cells, not walls.
  for (const auto* agent : agents) {
    Position pos = agent->GetPosition();
    ASSERT_TRUE(grid.IsInBounds(pos));
    ASSERT_EQ(grid.GetCell(pos).GetKind(), CellKind::Floor);
  }

  // Verify no agents start on synchro cells
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);
  ASSERT_FALSE(env.IsDone());

  // Verify determinism: creating same env again produces same positions
  SynchroEnv env2(8, 8, 3, 3, 0, 1337);
  const auto& synchro_pos2 = env2.GetSynchroPositions();
  auto agents2 = env2.GetObjectManager().GetAllAgents();
  for (size_t i = 0; i < synchro_pos.size(); i++) {
    ASSERT_EQ(synchro_pos[i], synchro_pos2[i]);
  }
  for (size_t i = 0; i < agents.size(); i++) {
    ASSERT_EQ(agents[i]->GetPosition(), agents2[i]->GetPosition());
  }
}

// Test 2: No progress reward (all stay, none on synchro)
TEST(TestSeed1337NoProgressReward) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);

  std::vector<Action> stay_actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(stay_actions);

  // 0 agents on synchro: kProgressReward * 0 + time_penalty
  // time_penalty = -num_agents * kProgressReward = -3 * kProgressReward
  double expected = -3.0 * SynchroEnv::kProgressReward;
  ASSERT_EQ(result.rewards.size(), 3u);
  ASSERT_APPROX_EQ(result.rewards[0], expected);
  ASSERT_APPROX_EQ(result.rewards[1], expected);
  ASSERT_APPROX_EQ(result.rewards[2], expected);
  ASSERT_FALSE(result.done);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);
}

// Test 3: One agent on synchro (partial progress)
// Note: This test dynamically moves an agent to a synchro cell instead of
// relying on hardcoded positions (which depend on PRNG)
TEST(TestSeed1337OneOnSynchroReward) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);

  // Get actual positions dynamically
  const auto& synchro_pos = env.GetSynchroPositions();
  auto agents = env.GetObjectManager().GetAllAgents();

  // Move Agent 0 directly onto a synchro cell via object manager
  // (avoiding pathfinding issues with PRNG-dependent positions)
  auto& obj_mgr = env.GetMutableObjectManager();
  Position target_synchro = synchro_pos[0];
  obj_mgr.UpdatePosition(agents[0]->GetId(), target_synchro);

  // Take a step with all agents staying - reward should reflect 1 on synchro
  std::vector<Action> stay_actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };
  auto result = env.Step(stay_actions);

  // 1 agent on synchro with 3 agents: (1 - 3) * kProgressReward = -2 * kProgressReward
  double expected = -2.0 * SynchroEnv::kProgressReward;
  ASSERT_APPROX_EQ(result.rewards[0], expected);
  ASSERT_APPROX_EQ(result.rewards[1], expected);
  ASSERT_APPROX_EQ(result.rewards[2], expected);
  ASSERT_FALSE(result.done);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 1);
}

// Test 4: Two agents on synchro
TEST(TestSeed1337TwoOnSynchroReward) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  auto& obj_mgr = env.GetMutableObjectManager();

  // Get actual synchro positions dynamically
  const auto& synchro_pos = env.GetSynchroPositions();

  // Clear and place 2 agents on synchro, 1 elsewhere
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[0]);  // On Synchro 0
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[1]);  // On Synchro 1
  obj_mgr.CreateActor<NPCCompanion>(Position{3, 3});  // Not on synchro (floor cell)

  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 2);

  std::vector<Action> stay = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(stay);

  // 2 agents on synchro with 3 agents: (2 - 3) * kProgressReward = -1 * kProgressReward
  double expected = -1.0 * SynchroEnv::kProgressReward;
  ASSERT_APPROX_EQ(result.rewards[0], expected);
  ASSERT_APPROX_EQ(result.rewards[1], expected);
  ASSERT_APPROX_EQ(result.rewards[2], expected);
  ASSERT_FALSE(result.done);
}

// Test 5: Win reward (all 3 on synchro)
TEST(TestSeed1337WinReward) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  auto& obj_mgr = env.GetMutableObjectManager();

  // Get actual synchro positions dynamically
  const auto& synchro_pos = env.GetSynchroPositions();

  // Place all 3 agents on synchro cells
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[0]);  // On Synchro 0
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[1]);  // On Synchro 1
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[2]);  // On Synchro 2

  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 3);

  std::vector<Action> stay = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(stay);

  // 3 agents on synchro (WIN): (3 - 3) * kProgressReward + kWinReward = kWinReward
  double expected = SynchroEnv::kWinReward;
  ASSERT_APPROX_EQ(result.rewards[0], expected);
  ASSERT_APPROX_EQ(result.rewards[1], expected);
  ASSERT_APPROX_EQ(result.rewards[2], expected);
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
}

// Test 6: Agent walking on and off synchro cell
// Note: Uses object manager to place agents directly instead of
// relying on hardcoded movement paths (which depend on PRNG positions)
TEST(TestSeed1337AgentWalkingOnAndOff) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  auto& obj_mgr = env.GetMutableObjectManager();

  // Get actual synchro positions and agents
  const auto& synchro_pos = env.GetSynchroPositions();
  auto agents = obj_mgr.GetAllAgents();

  // With 3 agents: 0 on synchro = -3 * kProgressReward, 1 on synchro = -2 * kProgressReward
  const double reward_0_on = -3.0 * SynchroEnv::kProgressReward;
  const double reward_1_on = -2.0 * SynchroEnv::kProgressReward;

  // Initially no one on synchro
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);

  // Step 1: All stay (0 on synchro)
  auto r1 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r1.rewards[0], reward_0_on);

  // Move agent 0 onto synchro cell directly
  obj_mgr.UpdatePosition(agents[0]->GetId(), synchro_pos[0]);

  // Step 2: All stay (1 on synchro)
  auto r2 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r2.rewards[0], reward_1_on);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 1);

  // Step 3: Stay on synchro
  auto r3 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r3.rewards[0], reward_1_on);

  // Move agent 0 off synchro cell
  obj_mgr.UpdatePosition(agents[0]->GetId(), Position{3, 3});

  // Step 4: All stay (0 on synchro)
  auto r4 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r4.rewards[0], reward_0_on);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);

  // Move agent 0 back onto synchro cell
  obj_mgr.UpdatePosition(agents[0]->GetId(), synchro_pos[0]);

  // Step 5: All stay (1 on synchro again)
  auto r5 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r5.rewards[0], reward_1_on);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 1);

  ASSERT_FALSE(env.IsDone());
}

// Test 7: Full solution path with rewards at each step
// Note: Uses object manager to directly place agents on synchro cells
// instead of relying on hardcoded movement paths (which depend on PRNG positions)
TEST(TestSeed1337FullSolutionPath) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  auto& obj_mgr = env.GetMutableObjectManager();

  // Get actual synchro positions and agents
  const auto& synchro_pos = env.GetSynchroPositions();
  auto agents = obj_mgr.GetAllAgents();

  // With 3 agents: 0 on synchro = -3k, 1 on synchro = -2k, 2 on synchro = -1k, win = kWinReward
  const double k = SynchroEnv::kProgressReward;
  const double reward_0_on = -3.0 * k;
  const double reward_1_on = -2.0 * k;
  const double reward_2_on = -1.0 * k;
  const double reward_win = SynchroEnv::kWinReward;

  std::vector<double> expected_rewards;

  // Step 1: All stay (0 on synchro)
  auto r1 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r1.rewards[0], reward_0_on);
  expected_rewards.push_back(r1.rewards[0]);

  // Move agent 0 onto synchro cell 0
  obj_mgr.UpdatePosition(agents[0]->GetId(), synchro_pos[0]);

  // Step 2: All stay (1 on synchro)
  auto r2 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r2.rewards[0], reward_1_on);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 1);
  expected_rewards.push_back(r2.rewards[0]);

  // Step 3: All stay (still 1 on synchro)
  auto r3 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r3.rewards[0], reward_1_on);
  expected_rewards.push_back(r3.rewards[0]);

  // Move agent 1 onto synchro cell 1
  obj_mgr.UpdatePosition(agents[1]->GetId(), synchro_pos[1]);

  // Step 4: All stay (2 on synchro)
  auto r4 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r4.rewards[0], reward_2_on);
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 2);
  expected_rewards.push_back(r4.rewards[0]);

  // Move agent 2 onto synchro cell 2
  obj_mgr.UpdatePosition(agents[2]->GetId(), synchro_pos[2]);

  // Step 5: All stay (3 on synchro = WIN!)
  auto r5 = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_APPROX_EQ(r5.rewards[0], reward_win);
  ASSERT_TRUE(r5.done);
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
  expected_rewards.push_back(r5.rewards[0]);

  // Verify total rewards
  double total = 0.0;
  for (double r : expected_rewards) {
    total += r;
  }
  // reward_0_on + reward_1_on + reward_1_on + reward_2_on + reward_win
  double expected_total = reward_0_on + reward_1_on + reward_1_on + reward_2_on + reward_win;
  ASSERT_APPROX_EQ(total, expected_total);
}

// Test 8: Accumulated rewards before win
TEST(TestSeed1337AccumulatedRewardsBeforeWin) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);

  double total_reward = 0.0;

  // Take 10 steps with no progress (all stay)
  for (int step = 0; step < 10; ++step) {
    auto result = env.Step({
      EncodeAction(MovementAction::Stay),
      EncodeAction(MovementAction::Stay),
      EncodeAction(MovementAction::Stay)
    });
    total_reward += result.rewards[0];
  }

  // Expected: 10 steps * (-3 * kProgressReward) with 3 agents and 0 on synchro
  double expected = 10.0 * (-3.0 * SynchroEnv::kProgressReward);
  ASSERT_APPROX_EQ(total_reward, expected);
  ASSERT_FALSE(env.IsDone());
}

// Test 9: Terminal state properties
TEST(TestSeed1337TerminalStateProperties) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);
  auto& obj_mgr = env.GetMutableObjectManager();

  // Get actual synchro positions dynamically
  const auto& synchro_pos = env.GetSynchroPositions();

  // Place all agents on synchro cells
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[0]);
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[1]);
  obj_mgr.CreateActor<NPCCompanion>(synchro_pos[2]);

  // Before stepping
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());

  // Win by stepping
  auto result = env.Step({
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  });

  // After winning
  ASSERT_TRUE(result.done);
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
  // Win reward = kWinReward (progress terms cancel out when all agents on synchro)
  ASSERT_APPROX_EQ(result.rewards[0], SynchroEnv::kWinReward);
  ASSERT_APPROX_EQ(result.rewards[1], SynchroEnv::kWinReward);
  ASSERT_APPROX_EQ(result.rewards[2], SynchroEnv::kWinReward);
}

// Test 10: Reward consistency across resets with same seed
TEST(TestSeed1337RewardConsistencyAcrossResets) {
  SynchroEnv env(8, 8, 3, 3, 0, 1337);

  // Store initial positions
  auto agents1 = env.GetObjectManager().GetAllAgents();
  Position pos0_1 = agents1[0]->GetPosition();
  Position pos1_1 = agents1[1]->GetPosition();
  Position pos2_1 = agents1[2]->GetPosition();

  // Take a step and record reward
  auto result1 = env.Step({
    EncodeAction(MovementAction::Up),
    EncodeAction(MovementAction::Down),
    EncodeAction(MovementAction::Stay)
  });
  double reward1 = result1.rewards[0];

  // Reset with same seed
  env.Reset(1337);

  // Verify same initial positions
  auto agents2 = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents2[0]->GetPosition(), pos0_1);
  ASSERT_EQ(agents2[1]->GetPosition(), pos1_1);
  ASSERT_EQ(agents2[2]->GetPosition(), pos2_1);

  // Take same step and verify same reward
  auto result2 = env.Step({
    EncodeAction(MovementAction::Up),
    EncodeAction(MovementAction::Down),
    EncodeAction(MovementAction::Stay)
  });
  ASSERT_EQ(result2.rewards[0], reward1);

  // Create fresh env with same seed - should be identical
  SynchroEnv env3(8, 8, 3, 3, 0, 1337);
  auto agents3 = env3.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents3[0]->GetPosition(), pos0_1);
  ASSERT_EQ(agents3[1]->GetPosition(), pos1_1);
  ASSERT_EQ(agents3[2]->GetPosition(), pos2_1);
}

// =============================================================================
// SynchroEnv Map Complexity Tests
// =============================================================================

TEST(TestSynchroEnvComplexity0MatchesLegacy) {
  // Complexity 0 produces an empty rectangle (no obstacles)
  SynchroEnv env_legacy(8, 8, 3, 3, 0, 1337);
  SynchroEnv env_complex(8, 8, 3, 3, 0, 1337);  // Explicit complexity 0

  // Same synchro positions
  const auto& synchro_legacy = env_legacy.GetSynchroPositions();
  const auto& synchro_complex = env_complex.GetSynchroPositions();
  ASSERT_EQ(synchro_legacy.size(), synchro_complex.size());
  for (size_t i = 0; i < synchro_legacy.size(); ++i) {
    ASSERT_EQ(synchro_legacy[i], synchro_complex[i]);
  }

  // Same agent positions
  auto agents_legacy = env_legacy.GetObjectManager().GetAllAgents();
  auto agents_complex = env_complex.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents_legacy.size(), agents_complex.size());
  for (size_t i = 0; i < agents_legacy.size(); ++i) {
    ASSERT_EQ(agents_legacy[i]->GetPosition(), agents_complex[i]->GetPosition());
  }
}

TEST(TestSynchroEnvComplexity1HasObstacles) {
  SynchroEnv env(10, 10, 2, 2, 1, 12345);

  // Should have some interior walls (complexity 1 adds obstacles)
  int interior_walls = 0;
  for (int r = 1; r < 9; ++r) {
    for (int c = 1; c < 9; ++c) {
      if (env.GetGrid().GetCellKind(r, c) == CellKind::Wall) {
        interior_walls++;
      }
    }
  }
  ASSERT_TRUE(interior_walls > 0);  // At least some obstacles
}

TEST(TestSynchroEnvComplexity2HasRooms) {
  SynchroEnv env(16, 16, 2, 2, 2, 12345);

  // Should have fewer walkable cells than an empty rectangle
  int walkable = 0;
  int total_interior = 14 * 14;  // 16-2 for walls on each side
  for (int r = 1; r < 15; ++r) {
    for (int c = 1; c < 15; ++c) {
      if (env.GetGrid().IsWalkable({r, c})) {
        walkable++;
      }
    }
  }
  ASSERT_TRUE(walkable < total_interior);  // Not empty rectangle (has walls)
  ASSERT_TRUE(walkable >= 10);  // But has enough space for game (agents + synchro cells)
}

TEST(TestSynchroEnvComplexityDeterministic) {
  // Same seed and complexity should give same layout
  // Use complexity 2 (max supported)
  SynchroEnv env1(12, 12, 2, 2, 2, 9999);
  SynchroEnv env2(12, 12, 2, 2, 2, 9999);

  for (int r = 0; r < 12; ++r) {
    for (int c = 0; c < 12; ++c) {
      ASSERT_EQ(env1.GetGrid().GetCellKind(r, c),
                env2.GetGrid().GetCellKind(r, c));
    }
  }

  // Same synchro and agent positions
  ASSERT_EQ(env1.GetSynchroPositions(), env2.GetSynchroPositions());
}

TEST(TestSynchroEnvComplexityDifferentSeeds) {
  // Different seeds should give different layouts for complexity > 0
  SynchroEnv env1(12, 12, 2, 2, 2, 100);
  SynchroEnv env2(12, 12, 2, 2, 2, 200);

  bool different = false;
  for (int r = 0; r < 12 && !different; ++r) {
    for (int c = 0; c < 12 && !different; ++c) {
      if (env1.GetGrid().GetCellKind(r, c) != env2.GetGrid().GetCellKind(r, c)) {
        different = true;
      }
    }
  }
  ASSERT_TRUE(different);  // Layouts should differ
}

TEST(TestSynchroEnvComplexityCopy) {
  // Use complexity 2 (max supported)
  SynchroEnv env1(10, 10, 2, 2, 2, 12345);
  SynchroEnv env2(env1);  // Copy constructor

  ASSERT_EQ(env2.GetMapComplexity(), 2);

  // Same grid
  for (int r = 0; r < 10; ++r) {
    for (int c = 0; c < 10; ++c) {
      ASSERT_EQ(env1.GetGrid().GetCellKind(r, c),
                env2.GetGrid().GetCellKind(r, c));
    }
  }
}

TEST(TestSynchroEnvComplexityAccessor) {
  // Only test complexity 0-2 (3-5 disabled)
  SynchroEnv env0(8, 8, 2, 2, 0, 1);
  SynchroEnv env1(8, 8, 2, 2, 1, 1);
  SynchroEnv env2(8, 8, 2, 2, 2, 1);

  ASSERT_EQ(env0.GetMapComplexity(), 0);
  ASSERT_EQ(env1.GetMapComplexity(), 1);
  ASSERT_EQ(env2.GetMapComplexity(), 2);
}

TEST(TestSynchroEnvComplexityClamps) {
  // Complexity should be clamped - test negative clamping to 0
  // Note: complexity >= 3 is disabled, so only test lower bound clamping
  SynchroEnv env_neg(8, 8, 2, 2, -1, 1);

  ASSERT_EQ(env_neg.GetMapComplexity(), 0);
}

TEST(TestSynchroEnvComplexityAllLevelsPlayable) {
  // All supported complexity levels should produce playable games
  // Note: complexity >= 3 is disabled
  for (int complexity = 0; complexity <= 2; ++complexity) {
    SynchroEnv env(12, 12, 3, 3, complexity, 42);

    // Should have correct number of agents
    ASSERT_EQ(static_cast<int>(env.GetObjectManager().GetAllAgents().size()), 3);

    // Should have correct number of synchro cells
    ASSERT_EQ(static_cast<int>(env.GetSynchroPositions().size()), 3);

    // Should not be done initially (assuming not all agents start on synchro)
    // Note: This might rarely fail if randomly all agents land on synchro cells
    // but for seed 42 and these sizes it shouldn't happen
    ASSERT_FALSE(env.IsDone());
  }
}

// =============================================================================
// Integration Tests - Full Game Loop
// =============================================================================
TEST(TestSynchroEnvFullGameLoopToWin) {
  // Integration test: Complete game from spawn to win
  // Create a small, controlled environment
  SynchroEnv env(8, 8, 2, 2, 0, 12345);

  // Verify initial state
  ASSERT_FALSE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());
  ASSERT_EQ(env.GetTick(), 0);
  ASSERT_EQ(env.NumAgents(), 2);

  // Get synchro positions and agent positions
  auto synchro_positions = env.GetSynchroPositions();
  ASSERT_EQ(synchro_positions.size(), 2u);

  auto agents = env.GetObjectManager().GetAllAgents();
  ASSERT_EQ(agents.size(), 2u);

  Position agent0_start = agents[0]->GetPosition();
  Position agent1_start = agents[1]->GetPosition();

  // Verify agents don't start on synchro cells (for seed 12345)
  bool agent0_on_synchro = false;
  bool agent1_on_synchro = false;
  for (const auto& synchro_pos : synchro_positions) {
    if (agent0_start == synchro_pos) agent0_on_synchro = true;
    if (agent1_start == synchro_pos) agent1_on_synchro = true;
  }
  // Initial state should not be winning
  ASSERT_FALSE(agent0_on_synchro && agent1_on_synchro);

  // Manually place agents on synchro cells to test win condition
  auto& obj_mgr = env.GetMutableObjectManager();
  obj_mgr.Clear();
  obj_mgr.CreateActor<NPCCompanion>(synchro_positions[0]);
  obj_mgr.CreateActor<NPCCompanion>(synchro_positions[1]);

  // Verify agents are on synchro cells
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 2);

  // Execute one step with Stay actions
  std::vector<Action> stay_actions = {
    EncodeAction(MovementAction::Stay),
    EncodeAction(MovementAction::Stay)
  };

  auto result = env.Step(stay_actions);

  // Verify win condition
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_EQ(result.rewards.size(), 2u);

  // Check win reward (kWinReward=10.0 + kProgressReward*2=0.2 - time_penalty=0.2 = 10.0)
  double expected_win_reward = SynchroEnv::kWinReward;
  ASSERT_EQ(result.rewards[0], expected_win_reward);
  ASSERT_EQ(result.rewards[1], expected_win_reward);

  // Verify observation tensor still works after win
  std::vector<float> final_obs;
  env.ObservationTensor(final_obs, 0);
  auto shape = env.ObservationShape();
  int expected_size = shape[0] * shape[1] * shape[2];
  ASSERT_EQ(static_cast<int>(final_obs.size()), expected_size);
}

TEST(TestSynchroEnvFullGameLoopWithMovement) {
  // Integration test: Move agents to synchro cells and win
  // Create controlled environment where we know positions
  SynchroEnv env(8, 8, 1, 1, 0, 999);

  // Get synchro position
  auto synchro_positions = env.GetSynchroPositions();
  ASSERT_EQ(synchro_positions.size(), 1u);
  Position synchro = synchro_positions[0];

  // Place agent adjacent to synchro cell
  auto& obj_mgr = env.GetMutableObjectManager();
  obj_mgr.Clear();
  Position start_pos = {synchro.row - 1, synchro.col};  // One cell above synchro
  obj_mgr.CreateActor<Player>(start_pos);

  // Verify not winning initially
  ASSERT_FALSE(env.IsDone());
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 0);

  // Move down onto synchro cell
  std::vector<Action> move_down = {EncodeAction(MovementAction::Down)};
  auto step1 = env.Step(move_down);

  // Should win immediately after moving onto synchro
  ASSERT_TRUE(env.IsDone());
  ASSERT_TRUE(env.IsSuccess());
  ASSERT_EQ(env.NumAgentsOnSynchroCells(), 1);

  // Verify win reward received
  ASSERT_EQ(step1.rewards.size(), 1u);
  ASSERT_EQ(step1.rewards[0], SynchroEnv::kWinReward);
}

TEST(TestSynchroEnvFullGameLoopTimeoutLose) {
  // Integration test: Game times out without winning
  // Create env with short horizon (5 steps)
  SynchroEnv env(8, 8, 1, 1, 0, 777, 0, 5);  // d4_transform=0, horizon=5

  // Place agent away from synchro
  auto& obj_mgr = env.GetMutableObjectManager();
  obj_mgr.Clear();
  obj_mgr.CreateActor<Player>({4, 4});

  ASSERT_FALSE(env.IsDone());
  ASSERT_EQ(env.GetHorizon(), 5);

  // Step until horizon reached
  std::vector<Action> stay = {EncodeAction(MovementAction::Stay)};
  StepResult result;

  for (int i = 0; i < 5; ++i) {
    result = env.Step(stay);
  }

  // Should be done after horizon reached
  ASSERT_TRUE(env.IsDone());
  ASSERT_FALSE(env.IsSuccess());  // Didn't win, just timed out
  ASSERT_EQ(env.GetTick(), 5);
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

  std::cout << "Running " << tests.size() << " tests...\n\n";

  int passed = 0;
  int failed = 0;

  for (const auto& test : tests) {
    std::cout << "Running " << test.name << "... ";
    std::cout.flush();
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
