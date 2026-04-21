// Copyright 2024
// Tests for the NPC state panel rendered next to the map in the interactive
// demo. The panel shows, for each live FSM NPC, its display letter and a
// single-word description of its current FSM state.
//
// Scenario test: a small scripted AggroEnv interaction where the player
// aggroes a zombie, survives the first telegraphed attack by stepping away,
// and is hit by the second telegraphed attack after standing still.

#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/agent_config.h"
#include "../src/core/annotations.h"
#include "../src/core/effect_config.h"
#include "../src/core/fsm/enemies.h"
#include "../src/core/fsm/fsm_states.h"
#include "../src/core/object_manager.h"
#include "../src/core/pcg32.h"
#include "../src/core/types.h"
#include "../src/env/aggro_env.h"
#include "../src/env/synchro_env.h"
#include "../src/viz/renderer.h"

using namespace companions;

// =============================================================================
// Test harness (matches the other test_*.cc files)
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
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b \
        << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// Helpers
// =============================================================================
namespace {

// Strip ANSI escape sequences (\033[...m) for byte-level comparisons that
// don't depend on the terminal colouring.
std::string StripAnsi(const std::string& s) {
  static const std::regex kAnsi("\\x1b\\[[0-9;]*m");
  return std::regex_replace(s, kAnsi, "");
}

// Build a SynchroEnv with a single companion teleported to `player_pos`, and
// a zombie parked at `zombie_pos` with a stationary patrol path (the zombie
// stays put during Patrol, which makes the scripted scenario deterministic).
//
// We clear all SynchroGoal annotations so moving the player around does not
// accidentally terminate the env via the SynchroLens goal check.
void BuildScriptedScenario(SynchroEnv& env, Position player_pos,
                           Position zombie_pos, pcg32& rng) {
  env.Reset(42);

  // Clear goal annotations so the env never "wins" during the scenario.
  auto goals = env.GetAnnotations().FindCellsWithTag(SemanticTag::SynchroGoal);
  auto& ann = env.GetMutableAnnotations();
  for (const Position& g : goals) {
    ann.RemoveByKey(AnnotationKey{AnnotationTarget::Cell, g, kInvalidObjectId},
                    SemanticTag::SynchroGoal);
  }

  // Teleport the player to the known position.
  auto& mgr = env.GetMutableObjectManager();
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), player_pos);
  }

  // Park a zombie with a single-point patrol so it stays still until it
  // detects the player.
  std::vector<Position> patrol_path = {zombie_pos};
  CreateZombie(mgr, zombie_pos, patrol_path, rng);
}

// Return the single FSM state name observed in the panel on a given tick.
// Expects exactly one NPC line.
std::string SoloPanelState(const Renderer& r, const BaseEnv& env) {
  auto lines = r.CollectNPCStatusLines(env);
  if (lines.size() != 1) {
    throw std::runtime_error("SoloPanelState: expected 1 NPC line, got " +
                             std::to_string(lines.size()));
  }
  return lines[0].state_name;
}

}  // namespace

// =============================================================================
// Basic shape tests
// =============================================================================

TEST(TestNPCPanelEmptyWhenNoFSMAgents) {
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  Renderer r;
  auto lines = r.CollectNPCStatusLines(env);
  ASSERT_TRUE(lines.empty());
}

TEST(TestNPCPanelShowsZombieLetterAndPatrolState) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  env.Reset();

  auto& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Place zombie far from the companion so it stays in Patrol.
  auto companions = mgr.GetAllCompanions();
  ASSERT_TRUE(!companions.empty());
  mgr.UpdatePosition(companions[0]->GetId(), {1, 1});

  std::vector<Position> patrol = {{8, 8}};
  CreateZombie(mgr, {8, 8}, patrol, rng);

  Renderer r;
  auto lines = r.CollectNPCStatusLines(env);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].letter, 'Z');
  ASSERT_EQ(lines[0].state_name, "Patrol");
}

TEST(TestNPCPanelUsesSingleWordForReturnToPatrol) {
  // ReturnToPatrol is camelcase in the FSM, but the on-screen label should
  // be a single human word — the panel uses "Returning".
  SynchroEnv env(12, 12, 1, 1, 0, 42);
  env.Reset();

  auto& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);

  // Stash companions out of detection range so the FSM update keeps us in
  // Return-mode rather than re-aggroing.
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), {11, 11});
  }

  std::vector<Position> patrol = {{1, 1}, {1, 3}};
  Zombie* z = CreateZombie(mgr, {5, 5}, patrol, rng);  // off-path on purpose
  // Force the FSM into ReturnToPatrolState.
  FSMContext& ctx = z->GetFSMContext();
  ctx.target_id = kInvalidObjectId;
  z->SetFSM(&ReturnToPatrolState::Instance(), std::move(ctx));

  Renderer r;
  auto lines = r.CollectNPCStatusLines(env);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].state_name, "Returning");
}

// =============================================================================
// Rendered output (panel spliced next to the grid)
// =============================================================================

TEST(TestRenderAsciiWithNPCPanelMatchesBaseRenderWhenNoNPC) {
  // With no FSM agents, the combined renderer is identical to RenderAscii.
  SynchroEnv env(8, 8, 1, 1, 0, 42);
  env.Reset();

  Renderer r;
  ASSERT_EQ(StripAnsi(r.RenderAsciiWithNPCPanel(env)),
            StripAnsi(r.RenderAscii(env)));
}

TEST(TestRenderAsciiWithNPCPanelIncludesZombiePatrolLabel) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  env.Reset();

  auto& mgr = env.GetMutableObjectManager();
  pcg32 rng(42);
  auto companions = mgr.GetAllCompanions();
  if (!companions.empty()) {
    mgr.UpdatePosition(companions[0]->GetId(), {1, 1});
  }
  std::vector<Position> patrol = {{8, 8}};
  CreateZombie(mgr, {8, 8}, patrol, rng);

  Renderer r;
  std::string out = StripAnsi(r.RenderAsciiWithNPCPanel(env));
  // The rendered output must contain the letter + single-word state somewhere
  // on the right-side panel.
  ASSERT_TRUE(out.find("Z Patrol") != std::string::npos);
}

// =============================================================================
// Scripted aggro scenario
//
// Layout (10x10, perimeter walls, interior Floor):
//
//     col 0 1 2 3 4 5 6 7 8 9
//     ........................
//   0 | W W W W W W W W W W |
//   1 | W . . . . . . . . W |
//   2 | W . . . . P . . . W |   <- Player starts here
//   3 | W . . . . . . . . W |
//   4 | W . . . . . . . . W |
//   5 | W . . . . Z . . . W |   <- Zombie starts here
//   ...
//
// Player at (2,5), Zombie at (5,5). Manhattan distance = 3 (= detection
// range, still detected). Zombie cadence [1,0] moves every other tick.
// FSM timings from data/agents.csv: telegraph=1, attack=1, recovery=2.
// zombie_attack effect: telegraph=2, active=1 (damage dealt at active start).
//
// Timeline (pre-step display = zombie FSM state at render time):
//   pre-0: Patrol  (zombie freshly spawned)
//   step 0 Stay -> pre-1: Aggro        (FSM: Patrol->Aggro; zombie (5,5)->(4,5))
//   step 1 Stay -> pre-2: Aggro        (FSM stays Aggro; cadence off, stays)
//   step 2 Stay -> pre-3: Aggro        (FSM stays Aggro; zombie (4,5)->(3,5))
//   step 3 Stay -> pre-4: Telegraph    (adjacent; spawn effect pending)
//   step 4 Up   -> pre-5: Attack       (effect spawned at (2,5); player at (1,5))
//   step 5 Stay -> pre-6: Recovery     (effect applies 1x1 damage at (2,5) -- miss)
//   step 6 Stay -> pre-7: Recovery     (still recovering, recovery_ticks=2)
//   step 7 Down -> pre-8: Aggro        (Recovery done, player back in range)
//   step 8 Stay -> pre-9: Telegraph    (adjacent at (3,5)/(2,5))
//   step 9 Stay -> pre-10: Attack      (effect spawned at (2,5); player AT (2,5))
//   step 10 Stay -> pre-11: Recovery   (effect applies damage at (2,5) -- HIT)
// =============================================================================

TEST(TestNPCPanelAggroScenarioMissAndHit) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  pcg32 rng(42);
  BuildScriptedScenario(env, /*player=*/{2, 5}, /*zombie=*/{5, 5}, rng);

  Renderer r;
  auto& mgr = env.GetMutableObjectManager();
  auto companions = mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1u);
  Player* player = dynamic_cast<Player*>(companions[0]);
  ASSERT_TRUE(player != nullptr);

  int initial_health = player->GetHealth();
  ASSERT_TRUE(initial_health > 1);  // Sanity: need at least 2 HP for the hit test

  // Before the first Step, the zombie is still in Patrol (the FSM hasn't run
  // yet — PreStep is what drives transitions).
  ASSERT_EQ(SoloPanelState(r, env), "Patrol");

  // Two actions per Step: player at index 0, zombie at index 1 (FSM
  // overrides the passed action in GatherIntentions).
  auto step = [&](MovementAction m) {
    std::vector<Action> actions = {EncodeAction(m),
                                   EncodeAction(MovementAction::Stay)};
    env.Step(actions);
  };

  // Step 0: zombie detects (dist=3) and transitions Patrol -> Aggro, moves up.
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Aggro");

  // Step 1: zombie cadence off-tick; stays at (4,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Aggro");

  // Step 2: zombie cadence on-tick; moves (4,5)->(3,5), now dist=1.
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Aggro");

  // Step 3: PreStep sees adjacency — transitions Aggro -> Telegraph, locks
  // attack target at player's current cell (2,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Telegraph");

  // Step 4 (Up): Telegraph -> Attack. OnEnter spawns zombie_attack at (2,5).
  // Player steps to (1,5) — out of the locked target cell.
  step(MovementAction::Up);
  ASSERT_EQ(SoloPanelState(r, env), "Attack");
  ASSERT_EQ(player->GetPosition().row, 1);

  // Step 5: Attack -> Recovery (effect still in telegraph, damage not yet applied).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Recovery");

  // Step 6: effect telegraph expires at end-of-step Tick() — damage applies
  // to whoever is at (2,5); player is at (1,5), so no damage. FSM stays in
  // Recovery (counter < recovery_ticks=2).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Recovery");
  ASSERT_EQ(player->GetHealth(), initial_health);  // MISSED

  // Step 7 (Down): Recovery completes -> Aggro (player back in range,
  // zombie still at (3,5)). Player moves (1,5)->(2,5).
  step(MovementAction::Down);
  ASSERT_EQ(SoloPanelState(r, env), "Aggro");
  ASSERT_EQ(player->GetPosition().row, 2);

  // Step 8: Aggro sees dist=1 adjacency again -> Telegraph, locks at (2,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Telegraph");

  // Step 9: Telegraph -> Attack. Spawn new zombie_attack at (2,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Attack");

  // Step 10: Attack -> Recovery. Effect telegraph expires at end-of-step,
  // damage applies at (2,5) — player is still there this time. HIT.
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Recovery");
  ASSERT_TRUE(player->GetHealth() < initial_health);
}

// =============================================================================
// Zombie damage: dodge vs hit (same scenario, health-focused assertions)
//
// Reuses the exact timing from TestNPCPanelAggroScenarioMissAndHit. The
// focus here is the *damage* outcome: player health must stay at the
// starting value after dodging one attack and drop by 1 after standing
// through the next one. We additionally assert that the rendered health
// display reflects the change — "damage dealt" means "damage shown".
//
// Health glyphs (see Renderer::RenderHealth):
//   filled box = "■"   (UTF-8 bytes 0xE2 0x96 0xA0)
//   empty box  = "□"   (UTF-8 bytes 0xE2 0x96 0xA1)
// After one hit the player loses one filled box and gains one empty box.
// =============================================================================

namespace {

// Count UTF-8 occurrences of the filled / empty health glyphs in a string
// that may contain ANSI escapes. Simple byte-search works because the two
// glyphs share no byte with ANSI CSI terminators.
int CountFilledBoxes(const std::string& s) {
  const std::string needle = "\xE2\x96\xA0";  // ■
  int count = 0;
  std::size_t pos = 0;
  while ((pos = s.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

int CountEmptyBoxes(const std::string& s) {
  const std::string needle = "\xE2\x96\xA1";  // □
  int count = 0;
  std::size_t pos = 0;
  while ((pos = s.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

TEST(TestZombieAttackDamagesPlayerOnHitNotOnDodge) {
  SynchroEnv env(10, 10, 1, 1, 0, 42);
  pcg32 rng(42);
  BuildScriptedScenario(env, /*player=*/{2, 5}, /*zombie=*/{5, 5}, rng);

  Renderer r;
  auto& mgr = env.GetMutableObjectManager();
  auto companions = mgr.GetAllCompanions();
  ASSERT_EQ(companions.size(), 1u);
  Player* player = dynamic_cast<Player*>(companions[0]);
  ASSERT_TRUE(player != nullptr);

  const int initial_health = player->GetHealth();
  ASSERT_TRUE(initial_health >= 2);

  // Rendered health before any attacks: all boxes filled, none empty.
  {
    std::string hud = r.RenderHealth(*player);
    ASSERT_EQ(CountFilledBoxes(hud), initial_health);
    ASSERT_EQ(CountEmptyBoxes(hud), 0);
  }

  auto step = [&](MovementAction m) {
    std::vector<Action> actions = {EncodeAction(m),
                                   EncodeAction(MovementAction::Stay)};
    env.Step(actions);
  };

  // === First attack: the dodge ===
  // Steps 0-3: zombie walks within range and telegraphs at (2,5).
  step(MovementAction::Stay);  // Patrol -> Aggro, zombie (5,5)->(4,5)
  step(MovementAction::Stay);  // cadence off-tick, zombie stays
  step(MovementAction::Stay);  // zombie (4,5)->(3,5), now dist=1
  step(MovementAction::Stay);  // Aggro -> Telegraph, locks (2,5)
  ASSERT_EQ(SoloPanelState(r, env), "Telegraph");
  ASSERT_EQ(player->GetHealth(), initial_health);

  // Step 4 (Up): Telegraph -> Attack spawns zombie_attack at (2,5). Player
  // steps to (1,5), out of the 1x1 attack area. No damage this tick (the
  // effect is still in its own telegraph phase anyway).
  step(MovementAction::Up);
  ASSERT_EQ(player->GetPosition().row, 1);
  ASSERT_EQ(player->GetHealth(), initial_health);

  // Step 5 & 6: zombie cycles through Recovery; effect's telegraph expires
  // at end-of-step Tick() and damage resolves against whoever is at (2,5)
  // — the player is at (1,5), so nothing lands.
  step(MovementAction::Stay);
  step(MovementAction::Stay);
  ASSERT_EQ(player->GetHealth(), initial_health);  // DODGED

  // HUD after dodge: unchanged.
  {
    std::string hud = r.RenderHealth(*player);
    ASSERT_EQ(CountFilledBoxes(hud), initial_health);
    ASSERT_EQ(CountEmptyBoxes(hud), 0);
  }

  // === Second attack: the hit ===
  // Step 7 (Down): Recovery -> Aggro, player moves back to (2,5).
  step(MovementAction::Down);
  ASSERT_EQ(player->GetPosition().row, 2);

  // Step 8: Aggro -> Telegraph at (2,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Telegraph");

  // Step 9: Telegraph -> Attack spawns new zombie_attack at (2,5).
  step(MovementAction::Stay);
  ASSERT_EQ(SoloPanelState(r, env), "Attack");
  ASSERT_EQ(player->GetHealth(), initial_health);  // not yet resolved

  // Step 10: Attack -> Recovery. Effect telegraph ticks to 0 at end-of-step
  // and damage applies at (2,5). Player is still there. HIT.
  step(MovementAction::Stay);
  ASSERT_EQ(player->GetHealth(), initial_health - 1);

  // HUD after hit: one fewer filled box, one empty box where the missing
  // health was.  This is the "displayed as such" requirement.
  {
    std::string hud = r.RenderHealth(*player);
    ASSERT_EQ(CountFilledBoxes(hud), initial_health - 1);
    ASSERT_EQ(CountEmptyBoxes(hud), 1);
  }
}

// =============================================================================
// AggroEnv direct damage test (not SynchroEnv with manual placement)
//
// Regression for a bug observed in the interactive demo where the zombie
// telegraphed and attacked correctly on-screen but never dealt damage: the
// demo was started from a working directory where data/agents.csv and
// data/effects.csv could not be found, so CreateZombie fell back to default
// FSM timings WITHOUT an attack_effect_name. AttackState::OnEnter only
// spawns an effect when attack_effect_name is non-empty, so every attack
// cycle became a no-op.
//
// This test pins in the happy-path contract: with configs loaded, an
// AggroEnv with a static player eventually loses health to the zombie.
// =============================================================================

TEST(TestAggroEnvZombieDealsDamageToStandingPlayer) {
  AggroEnv env(10, 1, EnemyType::Zombie, 42);
  auto companions = env.GetMutableObjectManager().GetAllCompanions();
  ASSERT_EQ(companions.size(), 1u);
  Player* player = dynamic_cast<Player*>(companions[0]);
  ASSERT_TRUE(player != nullptr);

  const int start_hp = player->GetHealth();
  ASSERT_TRUE(start_hp > 0);

  // Two actions per Step (1 player + 1 FSM zombie). Player stays — lets
  // the zombie close in and attack.
  const int max_ticks = 40;
  bool damaged = false;
  for (int t = 0; t < max_ticks; ++t) {
    std::vector<Action> actions = {EncodeAction(MovementAction::Stay),
                                   EncodeAction(MovementAction::Stay)};
    env.Step(actions);
    if (player->GetHealth() < start_hp) {
      damaged = true;
      break;
    }
  }
  ASSERT_TRUE(damaged);
}

// =============================================================================
// Test runner
// =============================================================================
int main() {
  // Load configs so Zombies use the CSV attack timings (telegraph=1,
  // attack=1, recovery=2, effect=zombie_attack with telegraph=2/active=1/
  // recovery=2). Same multi-path fallback as the demo.
  AgentConfigRegistry::Instance().LoadFromCSV("data/agents.csv") ||
      AgentConfigRegistry::Instance().LoadFromCSV("../data/agents.csv") ||
      AgentConfigRegistry::Instance().LoadFromCSV(
          "../../pufferlib/ocean/companions/data/agents.csv");
  EffectConfigRegistry::Instance().LoadFromCSV("data/effects.csv") ||
      EffectConfigRegistry::Instance().LoadFromCSV("../data/effects.csv") ||
      EffectConfigRegistry::Instance().LoadFromCSV(
          "../../pufferlib/ocean/companions/data/effects.csv");

  int failed = 0;
  for (const auto& t : tests) {
    try {
      t.func();
      std::cout << "[PASS] " << t.name << "\n";
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
      ++failed;
    }
  }
  std::cout << (tests.size() - failed) << "/" << tests.size() << " tests passed\n";
  return failed == 0 ? 0 : 1;
}
