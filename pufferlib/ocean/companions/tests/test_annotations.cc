// Copyright 2024
// Tests for the semantic annotation store.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

#include <memory>

#include "../src/core/annotations.h"
#include "../src/core/snapshot.h"
#include "../src/core/types.h"
#include "../src/env/aggro_env.h"
#include "../src/env/synchro_env.h"
#include "../src/env/synchro_lens.h"

using namespace companions;

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

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

namespace {

AnnotationKey CellKey(int row, int col) {
  return AnnotationKey{AnnotationTarget::Cell, Position{row, col}, kInvalidObjectId};
}

AnnotationKey AgentKey(ObjectId id) {
  return AnnotationKey{AnnotationTarget::Agent, Position{-1, -1}, id};
}

}  // namespace

// =============================================================================
// Basic add / query
// =============================================================================
TEST(AddsAndQueriesCellTag) {
  AnnotationStore store;
  store.Add(CellKey(2, 3), Annotation{SemanticTag::SynchroGoal, {}, -1});

  ASSERT_TRUE(store.HasTag(CellKey(2, 3), SemanticTag::SynchroGoal));
  ASSERT_FALSE(store.HasTag(CellKey(2, 3), SemanticTag::AggroTarget));
  ASSERT_FALSE(store.HasTag(CellKey(5, 5), SemanticTag::SynchroGoal));
}

TEST(AddsAndQueriesAgentTag) {
  AnnotationStore store;
  Annotation ann{SemanticTag::SkillGiver, {{"skill", "electric"}}, -1};
  store.Add(AgentKey(7), ann);

  ASSERT_TRUE(store.HasTag(AgentKey(7), SemanticTag::SkillGiver));
  auto found = store.Get(AgentKey(7));
  ASSERT_EQ(found.size(), 1u);
  ASSERT_EQ(found[0]->params.at("skill"), std::string("electric"));
}

TEST(AllowsMultipleTagsPerKey) {
  AnnotationStore store;
  store.Add(CellKey(1, 1), Annotation{SemanticTag::SynchroGoal, {}, -1});
  store.Add(CellKey(1, 1), Annotation{SemanticTag::Room, {{"room", "main"}}, -1});

  ASSERT_TRUE(store.HasTag(CellKey(1, 1), SemanticTag::SynchroGoal));
  ASSERT_TRUE(store.HasTag(CellKey(1, 1), SemanticTag::Room));
  ASSERT_EQ(store.Get(CellKey(1, 1)).size(), 2u);
}

// =============================================================================
// Tag-indexed lookups
// =============================================================================
TEST(FindsAllCellsWithTag) {
  AnnotationStore store;
  store.Add(CellKey(0, 0), Annotation{SemanticTag::SynchroGoal, {}, -1});
  store.Add(CellKey(3, 4), Annotation{SemanticTag::SynchroGoal, {}, -1});
  store.Add(CellKey(2, 2), Annotation{SemanticTag::AggroTarget, {}, -1});

  auto synchro_cells = store.FindCellsWithTag(SemanticTag::SynchroGoal);
  ASSERT_EQ(synchro_cells.size(), 2u);

  auto aggro_cells = store.FindCellsWithTag(SemanticTag::AggroTarget);
  ASSERT_EQ(aggro_cells.size(), 1u);
  ASSERT_EQ(aggro_cells[0], (Position{2, 2}));
}

TEST(FindsAllAgentsWithTag) {
  AnnotationStore store;
  store.Add(AgentKey(1), Annotation{SemanticTag::TargetMob, {}, -1});
  store.Add(AgentKey(2), Annotation{SemanticTag::SkillGiver, {}, -1});
  store.Add(AgentKey(3), Annotation{SemanticTag::TargetMob, {}, -1});

  auto mobs = store.FindAgentsWithTag(SemanticTag::TargetMob);
  ASSERT_EQ(mobs.size(), 2u);
}

// =============================================================================
// Ownership / cleanup
// =============================================================================
TEST(RemoveByOwnerOnlyRemovesThatOwner) {
  AnnotationStore store;
  store.Add(CellKey(0, 0), Annotation{SemanticTag::SynchroGoal, {}, 42});    // lens 42
  store.Add(CellKey(1, 1), Annotation{SemanticTag::SynchroGoal, {}, 42});    // lens 42
  store.Add(CellKey(2, 2), Annotation{SemanticTag::Room, {}, -1});           // persistent

  store.RemoveByOwner(42);

  ASSERT_EQ(store.FindCellsWithTag(SemanticTag::SynchroGoal).size(), 0u);
  ASSERT_EQ(store.FindCellsWithTag(SemanticTag::Room).size(), 1u);
}

TEST(RemoveByKeyRemovesOneTag) {
  AnnotationStore store;
  store.Add(CellKey(4, 5), Annotation{SemanticTag::SynchroGoal, {}, -1});
  store.Add(CellKey(4, 5), Annotation{SemanticTag::Room, {{"room", "hall"}}, -1});

  store.RemoveByKey(CellKey(4, 5), SemanticTag::SynchroGoal);

  ASSERT_FALSE(store.HasTag(CellKey(4, 5), SemanticTag::SynchroGoal));
  ASSERT_TRUE(store.HasTag(CellKey(4, 5), SemanticTag::Room));
}

// =============================================================================
// Snapshot round-trip
// =============================================================================
TEST(SerializeDeserializePreservesEverything) {
  AnnotationStore src;
  src.Add(CellKey(0, 0), Annotation{SemanticTag::SynchroGoal, {}, 1});
  src.Add(CellKey(3, 3), Annotation{SemanticTag::Room, {{"room", "main"}}, -1});
  src.Add(AgentKey(5), Annotation{SemanticTag::SkillGiver, {{"skill", "fire"}}, -1});
  src.Add(AgentKey(5), Annotation{SemanticTag::HtnName, {{"name", "goblin_boss"}}, -1});

  auto serialized = src.Serialize();
  AnnotationStore dst;
  dst.Deserialize(serialized);

  ASSERT_TRUE(dst.HasTag(CellKey(0, 0), SemanticTag::SynchroGoal));
  ASSERT_TRUE(dst.HasTag(CellKey(3, 3), SemanticTag::Room));
  ASSERT_TRUE(dst.HasTag(AgentKey(5), SemanticTag::SkillGiver));
  ASSERT_TRUE(dst.HasTag(AgentKey(5), SemanticTag::HtnName));

  auto goblin = dst.Get(AgentKey(5));
  ASSERT_EQ(goblin.size(), 2u);

  // Params survive
  auto room_anns = dst.Get(CellKey(3, 3));
  ASSERT_EQ(room_anns.size(), 1u);
  ASSERT_EQ(room_anns[0]->params.at("room"), std::string("main"));

  // Ownership survives (so RemoveByOwner still works post-load)
  dst.RemoveByOwner(1);
  ASSERT_FALSE(dst.HasTag(CellKey(0, 0), SemanticTag::SynchroGoal));
  ASSERT_TRUE(dst.HasTag(CellKey(3, 3), SemanticTag::Room));  // persistent unaffected
}

// =============================================================================
// Snapshot binary round-trip (includes annotations)
// =============================================================================
TEST(SnapshotBinaryRoundTripPreservesAnnotations) {
  Snapshot src;
  src.rows = 4;
  src.cols = 4;
  src.cells.resize(16);  // all default Floor
  src.tick = 7;
  src.horizon = 50;

  AnnotationSnapshot a1;
  a1.target_type = 0;  // Cell
  a1.pos = Position{2, 3};
  a1.tag = SemanticTag::SynchroGoal;
  a1.owner_lens_id = 99;
  src.annotations.push_back(a1);

  AnnotationSnapshot a2;
  a2.target_type = 1;  // Agent
  a2.agent_id = 5;
  a2.tag = SemanticTag::SkillGiver;
  a2.params.emplace_back("skill", "fire");
  a2.owner_lens_id = -1;
  src.annotations.push_back(a2);

  auto bytes = src.Serialize();
  Snapshot dst = Snapshot::Deserialize(bytes);

  ASSERT_EQ(dst.annotations.size(), 2u);
  ASSERT_EQ(dst.annotations[0].tag, SemanticTag::SynchroGoal);
  ASSERT_EQ(dst.annotations[0].pos, (Position{2, 3}));
  ASSERT_EQ(dst.annotations[0].owner_lens_id, 99);
  ASSERT_EQ(dst.annotations[1].target_type, (uint8_t)1);
  ASSERT_EQ(dst.annotations[1].agent_id, 5);
  ASSERT_EQ(dst.annotations[1].tag, SemanticTag::SkillGiver);
  ASSERT_EQ(dst.annotations[1].params.size(), 1u);
  ASSERT_EQ(dst.annotations[1].params[0].first, std::string("skill"));
  ASSERT_EQ(dst.annotations[1].params[0].second, std::string("fire"));
}

// =============================================================================
// BaseEnv integration
// =============================================================================
TEST(BaseEnvAnnotationStoreIsUsable) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset();
  // Store is accessible and mutable; details of Reset-placed tags are covered
  // by SynchroEnvResetPopulatesSynchroGoalAnnotations.
  env.GetMutableAnnotations().Clear();
  ASSERT_TRUE(env.GetAnnotations().Empty());
}

TEST(BaseEnvSaveLoadRoundTripsAnnotations) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset();
  env.GetMutableAnnotations().Clear();  // Isolate from Reset-placed tags.

  // Add an external annotation and an agent-targeted annotation.
  env.GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Cell, Position{1, 1}, kInvalidObjectId},
      Annotation{SemanticTag::Room, {{"room", "main"}}, -1});
  env.GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Agent, Position{-1, -1}, 0},
      Annotation{SemanticTag::HtnName, {{"name", "p1"}}, -1});

  Snapshot snap = env.SaveSnapshot();
  ASSERT_EQ(snap.annotations.size(), 2u);

  // Wipe and reload.
  env.GetMutableAnnotations().Clear();
  ASSERT_TRUE(env.GetAnnotations().Empty());
  env.LoadSnapshot(snap);

  ASSERT_TRUE(env.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Cell, Position{1, 1}, kInvalidObjectId},
      SemanticTag::Room));
  ASSERT_TRUE(env.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Agent, Position{-1, -1}, 0},
      SemanticTag::HtnName));
}

TEST(SynchroEnvResetPopulatesSynchroGoalAnnotations) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset();

  // Each cell marked CellKind::Synchro by the level generator should also
  // have a SynchroGoal annotation attached (dual-path during migration).
  auto synchro_cells =
      env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  int stamped = 0;
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (env.GetGrid().GetCellKind(r, c) == CellKind::Synchro) {
        ++stamped;
      }
    }
  }
  ASSERT_EQ(synchro_cells.size(), static_cast<std::size_t>(stamped));
  ASSERT_TRUE(stamped > 0);
}

TEST(SynchroLensActivateAddsAnnotations) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset();

  // Wipe annotations to isolate the Activate path.
  env.GetMutableAnnotations().Clear();

  LensParams params;
  params.positions = {Position{1, 2}, Position{3, 4}};
  ASSERT_TRUE(
      env.SetTaskLensWithParams(std::make_unique<SynchroLens>(), params));

  auto cells = env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  ASSERT_EQ(cells.size(), 2u);

  // Deactivate by swapping in a fresh lens (the old one's annotations should go).
  env.SetTaskLens(std::make_unique<SynchroLens>());
  ASSERT_EQ(
      env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal).size(),
      0u);
}

TEST(AggroEnvResetPopulatesAggroTargetAnnotation) {
  AggroEnv env(10, 1);
  env.Reset();

  auto target_cells =
      env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  ASSERT_EQ(target_cells.size(), 1u);

  // The annotated cell matches the one CellKind::Target in the grid (dual-path).
  int target_grid_cells = 0;
  Position grid_target{-1, -1};
  for (int r = 0; r < env.GetRows(); ++r) {
    for (int c = 0; c < env.GetCols(); ++c) {
      if (env.GetGrid().GetCellKind(r, c) == CellKind::Target) {
        ++target_grid_cells;
        grid_target = {r, c};
      }
    }
  }
  ASSERT_EQ(target_grid_cells, 1);
  ASSERT_EQ(target_cells[0], grid_target);
}

TEST(BaseEnvCopyConstructorPreservesAnnotations) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset();
  env.GetMutableAnnotations().Clear();  // Isolate: ignore Reset-placed tags.
  env.GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Cell, Position{3, 3}, kInvalidObjectId},
      Annotation{SemanticTag::SynchroGoal, {}, 7});

  SynchroEnv copy = env;
  ASSERT_EQ(copy.GetAnnotations().Size(), 1u);
  ASSERT_TRUE(copy.GetAnnotations().HasTag(
      AnnotationKey{AnnotationTarget::Cell, Position{3, 3}, kInvalidObjectId},
      SemanticTag::SynchroGoal));

  // Mutating the copy does not affect the original (deep copy).
  copy.GetMutableAnnotations().Clear();
  ASSERT_EQ(env.GetAnnotations().Size(), 1u);
}

// =============================================================================
// Main
// =============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " tests...\n\n";
  int passed = 0, failed = 0;
  for (const auto& t : tests) {
    std::cout << "Running " << t.name << "... ";
    std::cout.flush();
    try {
      t.func();
      std::cout << "PASSED\n";
      ++passed;
    } catch (const std::exception& e) {
      std::cout << "FAILED: " << e.what() << "\n";
      ++failed;
    } catch (...) {
      std::cout << "FAILED (unknown exception)\n";
      ++failed;
    }
  }
  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
  return (failed > 0) ? 1 : 0;
}
