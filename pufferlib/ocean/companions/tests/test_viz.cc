// Copyright 2024
// Visualization tests - verify that the Renderer surfaces task-semantic
// annotations (SynchroGoal, AggroTarget) in the rendered ASCII grid, not
// just on the status line.
//
// Tier 1: regression guards for the "Synchro cells invisible in the demo" bug
// (post-annotation-layer refactor).
// Tier 2: schema lock (goldens) + FSM state colours + D4 × annotations +
//         multi-tag priority on a single cell.
// Tier 3: multi-tick render pipeline smoke test.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/cell.h"
#include "../src/core/types.h"
#include "../src/env/aggro_env.h"
#include "../src/env/dodge_env.h"
#include "../src/env/synchro_env.h"
#include "../src/viz/renderer.h"

using namespace companions;

// =============================================================================
// Test harness (matches the style in the other test_* files in this dir)
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
    oss << "ASSERT_EQ failed: " << #a << " != " << #b \
        << " (got " << (a) << " vs " << (b) << ") at " \
        << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Rendering helpers
// =============================================================================
namespace {

// Strip ANSI escape sequences so golden comparisons don't depend on terminal
// colour choice. Removes \033[...m sequences.
std::string StripAnsi(const std::string& s) {
  static const std::regex kAnsi("\\x1b\\[[0-9;]*m");
  return std::regex_replace(s, kAnsi, "");
}

std::string RenderStripped(const BaseEnv& env) {
  Renderer r;
  return StripAnsi(r.RenderAscii(env));
}

// Extract the 2-char glyph rendered at grid cell (row, col).
// The Renderer emits:
//   Tick: N                         <- line 0
//   +--+--+...                      <- line 1 (first separator)
//   | AA| BB| ...                   <- line 2 (row 0 data)
//   +--+--+...                      <- line 3
//   | CC| DD| ...                   <- line 4 (row 1 data)
//   ...
// So row r data is on line (2 + 2*r). Column c glyph starts at byte offset
// 1 + 3*c within that line (the "|" preceding the glyph), length 2.
std::string GlyphAt(const std::string& rendered_stripped, int row, int col) {
  std::vector<std::string> lines;
  std::stringstream ss(rendered_stripped);
  std::string line;
  while (std::getline(ss, line)) lines.push_back(line);

  const std::size_t line_idx = static_cast<std::size_t>(2 + 2 * row);
  if (line_idx >= lines.size()) {
    throw std::runtime_error("GlyphAt: row out of range");
  }
  const std::string& data_line = lines[line_idx];
  const std::size_t glyph_start = static_cast<std::size_t>(1 + 3 * col);
  if (glyph_start + 2 > data_line.size()) {
    throw std::runtime_error("GlyphAt: col out of range");
  }
  return data_line.substr(glyph_start, 2);
}

// Count occurrences of `needle` in `haystack` (non-overlapping).
int CountOccurrences(const std::string& haystack, const std::string& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// =============================================================================
// Golden fixture support
//
// Pass UPDATE_GOLDENS=1 in the environment to regenerate fixture files on
// disk from the current render output. Without it, tests compare rendered
// output byte-for-byte against the committed fixture (diff on mismatch).
// =============================================================================
#ifndef TESTS_FIXTURE_DIR
#define TESTS_FIXTURE_DIR "tests/fixtures"
#endif

std::string FixturePath(const std::string& name) {
  return std::string(TESTS_FIXTURE_DIR) + "/" + name;
}

bool UpdateGoldensRequested() {
  const char* v = std::getenv("UPDATE_GOLDENS");
  return v != nullptr && std::strcmp(v, "0") != 0 && *v != '\0';
}

void WriteGolden(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write golden: " + path);
  f << content;
}

std::string ReadGolden(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("golden missing: " + path + " (run with UPDATE_GOLDENS=1)");
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Compare `actual` against the fixture at `fixture_name`. On mismatch, throw
// with a diff-friendly message. With UPDATE_GOLDENS=1, rewrite the fixture.
void AssertGolden(const std::string& fixture_name, const std::string& actual) {
  const std::string path = FixturePath(fixture_name);
  if (UpdateGoldensRequested()) {
    WriteGolden(path, actual);
    std::cout << "  [golden updated: " << path << "] ";
    return;
  }
  std::string expected = ReadGolden(path);
  if (expected == actual) return;
  std::ostringstream oss;
  oss << "Golden mismatch for " << fixture_name << "\n"
      << "--- expected (" << expected.size() << " bytes) ---\n" << expected
      << "\n--- actual (" << actual.size() << " bytes) ---\n" << actual;
  throw std::runtime_error(oss.str());
}

}  // namespace

// =============================================================================
// Tier 1: regression guards for the "goal cells invisible" bug
// =============================================================================

TEST(TestRendererShowsGlyphAtSynchroGoalCells) {
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset(42);

  auto goals = env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  ASSERT_TRUE(!goals.empty());

  const std::string out = RenderStripped(env);

  // Every SynchroGoal cell must render with a visible 'S' marker, whether or
  // not an actor stands on it.  A plain-Floor glyph (" .") is the bug.
  for (const Position& p : goals) {
    std::string glyph = GlyphAt(out, p.row, p.col);
    ASSERT_EQ(glyph.size(), 2u);
    // Second char is the cell char (the first is either ' ' for empty cell
    // or an actor char when someone stands on it).
    ASSERT_EQ(glyph[1], 'S');
  }
}

TEST(TestRendererShowsGlyphAtAggroTargetCell) {
  AggroEnv env(10, 1);
  env.Reset();

  auto targets = env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  ASSERT_EQ(targets.size(), 1u);

  const std::string out = RenderStripped(env);
  Position p = targets[0];
  std::string glyph = GlyphAt(out, p.row, p.col);
  ASSERT_EQ(glyph.size(), 2u);
  ASSERT_EQ(glyph[1], 'T');
}

TEST(TestRendererActorOverlayPreservesAnnotationGlyph) {
  // Put the SynchroGoal tag on whatever cell the first companion occupies, so
  // we force the "actor-on-goal" overlay path.  The fix must keep the 'S' in
  // the second char even when an actor is drawn in the first char.
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset(42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
  Position agent_pos = companions[0]->GetPosition();

  // Make sure there's a SynchroGoal tag on that exact cell.  Add a fresh one
  // owned by a synthetic lens id so we don't interfere with the existing
  // ones.
  env.GetMutableAnnotations().Add(
      AnnotationKey{AnnotationTarget::Cell, agent_pos, kInvalidObjectId},
      Annotation{SemanticTag::SynchroGoal, {}, 9001});

  std::string out = RenderStripped(env);
  std::string glyph = GlyphAt(out, agent_pos.row, agent_pos.col);
  ASSERT_EQ(glyph.size(), 2u);
  // First char is the actor arrow/char, not space — there IS an actor here.
  ASSERT_FALSE(glyph[0] == ' ');
  // Second char is the goal marker 'S'.
  ASSERT_EQ(glyph[1], 'S');
}

TEST(TestRendererAnnotationStatusLineMatchesGrid) {
  // Build a scenario where some companions sit on synchro cells and others
  // don't.  The SynchroLens status line "Synchro: N/M companions on goal
  // cells" must agree with the count of companions whose rendered glyph
  // ends in 'S'.
  SynchroEnv env(8, 8, 2, 2, 0, 42, 0, 100);
  env.Reset(42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  auto goals = env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  ASSERT_TRUE(!companions.empty() && !goals.empty());

  // Move the first companion to the first goal cell (teleport — bypasses the
  // normal step loop, fine for the test because the renderer reads positions
  // straight from ObjectManager).
  env.GetMutableObjectManager().UpdatePosition(companions[0]->GetId(), goals[0]);

  std::string out = RenderStripped(env);

  // Count agents standing on a 'S' glyph by scanning the companions list.
  int expected = 0;
  for (const Agent* a : env.GetObjectManager().GetAllAgents()) {
    if (!a->IsAlive()) continue;
    Position p = a->GetPosition();
    std::string glyph = GlyphAt(out, p.row, p.col);
    if (glyph.size() == 2 && glyph[1] == 'S' && glyph[0] != ' ') {
      ++expected;
    }
  }
  ASSERT_TRUE(expected >= 1);  // at least the one we moved

  // Parse the SynchroLens status line "Synchro: N/M companions on goal cells".
  std::smatch m;
  std::regex re("Synchro: (\\d+)/(\\d+) companions on goal cells");
  ASSERT_TRUE(std::regex_search(out, m, re));
  int reported = std::stoi(m[1].str());
  ASSERT_EQ(reported, expected);
}

// =============================================================================
// Tier 2: schema lock + D4 × annotations + multi-tag priority
// =============================================================================

TEST(TestRendererGoldenSynchroEnv) {
  SynchroEnv env(6, 6, 2, 2, 0, 42, 0, 100);
  env.Reset(42);
  AssertGolden("viz_golden_synchro.txt", RenderStripped(env));
}

TEST(TestRendererGoldenAggroEnv) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  env.Reset();
  AssertGolden("viz_golden_aggro.txt", RenderStripped(env));
}

TEST(TestRendererGoldenDodgeEnv) {
  DodgeEnv env(7, 1, 5, 50, 42, 0);
  env.Reset();
  AssertGolden("viz_golden_dodge.txt", RenderStripped(env));
}

TEST(TestRendererFSMStateColors) {
  // After Reset, the AggroEnv enemy begins in Patrol state.  The renderer
  // maps Patrol → ANSI 92 (bright green); the raw (un-stripped) output must
  // therefore carry an "\033[92m" somewhere.
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  env.Reset();

  Renderer r;
  std::string raw = r.RenderAscii(env);
  // Bright green escape: ESC [ 9 2 m
  ASSERT_TRUE(raw.find("\033[92m") != std::string::npos);
}

TEST(TestRendererD4TransformedEnvStillShowsGoalGlyphs) {
  // Rendering a d4_transform=1 (Rot90) env must still show 'S' at every
  // annotated SynchroGoal position.  If the renderer consulted CellKind
  // only, transformed goals would silently vanish.
  SynchroEnv env(6, 6, 2, 2, 0, 42, /*d4=*/1, 100);
  env.Reset(42);

  auto goals = env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  ASSERT_TRUE(!goals.empty());
  std::string out = RenderStripped(env);
  for (const Position& p : goals) {
    std::string glyph = GlyphAt(out, p.row, p.col);
    ASSERT_EQ(glyph[1], 'S');
  }
}

TEST(TestRendererMultipleTagsPriority) {
  // When a cell carries more than one visual tag, priority is
  //   SynchroGoal > AggroTarget > (Room is invisible)
  // This test documents the tiebreaker; failing it means the renderer
  // silently changed priority.
  SynchroEnv env(6, 6, 1, 1, 0, 42, 0, 100);
  env.Reset(42);

  // Pick an empty Floor cell we know is interior (1,1 is inside a 6x6 env
  // with the standard 1-cell wall border).
  Position target{1, 1};
  // Make sure there's no actor standing there.
  const Actor* occupant = env.GetObjectManager().GetActorAt(target);
  if (occupant) {
    // Move the occupying agent somewhere harmless so the test stays simple.
    env.GetMutableObjectManager().UpdatePosition(occupant->GetId(), Position{2, 2});
  }

  // Clear any existing tags on this cell for deterministic setup.
  AnnotationKey key{AnnotationTarget::Cell, target, kInvalidObjectId};
  env.GetMutableAnnotations().RemoveByKey(key, SemanticTag::SynchroGoal);
  env.GetMutableAnnotations().RemoveByKey(key, SemanticTag::AggroTarget);
  env.GetMutableAnnotations().RemoveByKey(key, SemanticTag::Room);

  // Case A: AggroTarget + Room → T wins (Room is invisible).
  env.GetMutableAnnotations().Add(key, Annotation{SemanticTag::AggroTarget, {}, -1});
  env.GetMutableAnnotations().Add(
      key, Annotation{SemanticTag::Room, {{"room", "ignored"}}, -1});
  {
    std::string out = RenderStripped(env);
    std::string glyph = GlyphAt(out, target.row, target.col);
    ASSERT_EQ(glyph[1], 'T');
  }

  // Case B: SynchroGoal + AggroTarget + Room → S wins.
  env.GetMutableAnnotations().Add(key, Annotation{SemanticTag::SynchroGoal, {}, -1});
  {
    std::string out = RenderStripped(env);
    std::string glyph = GlyphAt(out, target.row, target.col);
    ASSERT_EQ(glyph[1], 'S');
  }
}

// =============================================================================
// Tier 3: multi-tick pipeline smoke test
// =============================================================================

TEST(TestRenderPipelineRunsMultipleTicksWithoutBlank) {
  // Drive a SynchroEnv for several ticks with scripted Stay actions, rendering
  // each frame.  Every frame must:
  //   1) be non-empty,
  //   2) contain at least one 'S' glyph (annotations survive each step),
  //   3) show a non-zero tick after the first step.
  SynchroEnv env(6, 6, 2, 2, 0, 42, 0, 100);
  env.Reset(42);

  std::vector<Action> stay_actions(
      env.NumAgents(), EncodeAction(MovementAction::Stay, InteractAction::None));

  for (int tick = 0; tick < 5; ++tick) {
    std::string frame = RenderStripped(env);
    ASSERT_TRUE(!frame.empty());
    ASSERT_TRUE(CountOccurrences(frame, " S") + CountOccurrences(frame, "S") > 0);

    std::ostringstream tick_needle;
    tick_needle << "Tick: " << tick;
    ASSERT_TRUE(frame.find(tick_needle.str()) != std::string::npos);

    env.Step(stay_actions);
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
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "Running " << tests.size() << " viz tests...\n\n";
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
  return failed > 0 ? 1 : 0;
}
