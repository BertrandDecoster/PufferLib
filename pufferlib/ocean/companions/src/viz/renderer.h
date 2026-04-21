// Copyright 2024
// Renderer for The Companions game

#ifndef COMPANIONS_VIZ_RENDERER_H_
#define COMPANIONS_VIZ_RENDERER_H_

#include <string>
#include <vector>

#include "../core/cell.h"
#include "../core/object.h"
#include "../core/types.h"
#include "../env/base_env.h"

namespace companions {

// =============================================================================
// Render Mode
// =============================================================================
enum class RenderMode { None, Ascii, Visual };

// =============================================================================
// NPCStatusLine - one row of the NPC state panel used by the demo
//
// Each live AgentFSM with an active FSM contributes a NPCStatusLine. The
// panel renders, for each NPC: its display glyph (colored with the FSM
// state's palette) followed by a single-word label for the state.
// =============================================================================
struct NPCStatusLine {
  char letter = '?';           // Display char (e.g. 'Z' for Zombie, 'G' for Goblin)
  std::string state_name;      // Single-word label (e.g. "Patrol", "Returning")
  int ansi_code = 0;           // ANSI colour for the letter (0 = none)
};

// =============================================================================
// Renderer - renders the game state
// =============================================================================
class Renderer {
 public:
  Renderer() = default;

  // Main render function
  void Render(const BaseEnv& env, RenderMode mode) const;

  // Individual renderers
  std::string RenderAscii(const BaseEnv& env) const;
  void RenderVisual(const BaseEnv& env) const;  // Placeholder for now

  // NPC state panel support (used by the interactive demo)
  //
  // CollectNPCStatusLines returns one line per live AgentFSM in the env,
  // keyed to the agent's display glyph and the FSM state's single-word label.
  // The collection order mirrors ObjectManager::GetAllAgents().
  //
  // RenderAsciiWithNPCPanel returns the same ASCII map as RenderAscii but
  // with each NPC's status line spliced on the RIGHT of a data row. When
  // there are no FSM NPCs the output is byte-identical to RenderAscii.
  std::vector<NPCStatusLine> CollectNPCStatusLines(const BaseEnv& env) const;
  std::string RenderAsciiWithNPCPanel(const BaseEnv& env) const;

  // Render a Companion's current/max health as coloured filled/empty boxes
  // (e.g. "[■][■][□]"). Used by the interactive demo under the map so the
  // player sees their health change when the zombie attack lands. Kept on
  // the Renderer so tests can assert that damage is "displayed as such".
  std::string RenderHealth(const Companion& companion) const;

 private:
  // Get direction arrow for a companion
  std::string GetDirectionArrow(Direction dir) const;

  // Resolve the 2-char display for an empty cell at `pos`. Task-semantic tags
  // in the annotation layer (SynchroGoal, AggroTarget) override the terrain
  // display so goal cells stay visible after the annotation refactor.
  std::string GetCellDisplay(const BaseEnv& env, Position pos, const Cell& cell) const;

  // Resolve the single cell char for composing an actor-on-cell glyph, with
  // the same annotation precedence as GetCellDisplay.
  char GetCellChar(const BaseEnv& env, Position pos, const Cell& cell) const;

  // Get display string for cell + actor combined (e.g., "<S" for agent on synchro)
  std::string GetCellActorDisplay(const BaseEnv& env, Position pos,
                                   const Cell& cell, const Actor* actor) const;

  // Map logical ActorColor to ANSI terminal code
  int ActorColorToAnsi(ActorColor color) const;

  // Apply ANSI color to text
  std::string Colorize(const std::string& text, int color_code) const;

  // Map FSM state name to ANSI color code
  int FSMStateToAnsi(const std::string& state_name) const;
};

}  // namespace companions

#endif  // COMPANIONS_VIZ_RENDERER_H_
