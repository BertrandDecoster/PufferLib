// Copyright 2024
// Renderer for The Companions game

#ifndef COMPANIONS_VIZ_RENDERER_H_
#define COMPANIONS_VIZ_RENDERER_H_

#include <string>

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

 private:
  // Get direction arrow for a companion
  std::string GetDirectionArrow(Direction dir) const;

  // Get display string for cell + actor combined (e.g., "S<" for agent on synchro)
  std::string GetCellActorDisplay(const Cell& cell, const Actor* actor) const;

  // Map logical ActorColor to ANSI terminal code
  int ActorColorToAnsi(ActorColor color) const;

  // Apply ANSI color to text
  std::string Colorize(const std::string& text, int color_code) const;

  // Map FSM state name to ANSI color code
  int FSMStateToAnsi(const std::string& state_name) const;
};

}  // namespace companions

#endif  // COMPANIONS_VIZ_RENDERER_H_
