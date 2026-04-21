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
// EffectStatusLine - one row of the active-effect panel used by the demo
//
// Each visible ActiveEffect contributes an EffectStatusLine so players can
// read what's on the board: the hazard's glyph (coloured for its phase),
// its effect name (e.g. "dodge_fire"), and its current phase with ticks
// remaining (e.g. "Telegraph:2" or "Active:1").
// =============================================================================
struct EffectStatusLine {
  char glyph = '?';            // Same glyph used in the grid (! / ~ / + / *)
  std::string name;            // Effect name (from effects.csv), e.g. "dodge_fire"
  std::string phase;           // "Telegraph:N" or "Active:N" where N = ticks_remaining
  int ansi_code = 0;           // ANSI colour for the glyph (matches the in-grid colour)
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

  // Side-panel support (used by the interactive demo).
  //
  // CollectNPCStatusLines returns one line per live AgentFSM in the env;
  // CollectEffectStatusLines returns one line per visible ActiveEffect.
  // Both panels share the same "glyph + single-word label" shape so a
  // player can scan them together.
  //
  // RenderAsciiWithNPCPanel returns the same ASCII map as RenderAscii but
  // with the NPC and effect status lines spliced on the RIGHT of the grid
  // data rows (NPCs first, then effects). When there are no FSM NPCs *and*
  // no visible effects the output is byte-identical to RenderAscii.
  std::vector<NPCStatusLine> CollectNPCStatusLines(const BaseEnv& env) const;
  std::vector<EffectStatusLine> CollectEffectStatusLines(const BaseEnv& env) const;
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

  // One-cell summary of any visible active effect (fire/wind/etc.) covering
  // `pos`. Returns an empty glyph (glyph == '\0') when no visible effect is
  // on the cell so callers can fall through to the annotation/terrain path.
  // Used to make DodgeEnv hazards visible during and before they strike.
  struct EffectGlyph {
    char glyph = '\0';   // '\0' means "no effect on this cell"
    int ansi_code = 0;   // colour for the glyph (yellow = telegraph, etc.)
  };
  EffectGlyph GetEffectGlyphAt(const BaseEnv& env, Position pos) const;
};

}  // namespace companions

#endif  // COMPANIONS_VIZ_RENDERER_H_
