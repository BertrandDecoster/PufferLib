// Copyright 2024
// Renderer implementation

#include "renderer.h"

#include <iostream>
#include <sstream>

#include "../core/fsm/fsm_states.h"

namespace companions {

void Renderer::Render(const BaseEnv& env, RenderMode mode) const {
  switch (mode) {
    case RenderMode::Ascii:
      std::cout << RenderAscii(env) << std::flush;
      break;
    case RenderMode::Visual:
      RenderVisual(env);
      break;
    case RenderMode::None:
    default:
      break;
  }
}

std::string Renderer::RenderAscii(const BaseEnv& env) const {
  std::ostringstream ss;

  const Grid& grid = env.GetGrid();
  const ObjectManager& obj_mgr = env.GetObjectManager();
  int rows = grid.GetRows();
  int cols = grid.GetCols();

  // Build separator line
  std::string sep = "+";
  for (int c = 0; c < cols; ++c) {
    sep += "--+";
  }

  // Header with tick info
  ss << "Tick: " << env.GetTick() << "\n";
  ss << sep << "\n";

  // Render each row
  for (int r = 0; r < rows; ++r) {
    ss << "|";
    for (int c = 0; c < cols; ++c) {
      Position pos{r, c};
      const Cell& cell = grid.GetCell(pos);
      const Actor* actor = obj_mgr.GetActorAt(pos);

      if (actor && actor->IsAlive()) {
        // Display cell char + actor arrow (e.g., "S<" for agent on synchro)
        std::string display = GetCellActorDisplay(cell, actor);
        ss << display << "|";
      } else {
        // Display cell only
        std::string display = cell.GetDisplay();
        int color = cell.GetColorCode();
        if (color != 0) {
          display = Colorize(display, color);
        }
        ss << display << "|";
      }
    }
    ss << "\n" << sep << "\n";
  }

  // Status line - count agents on synchro cells
  int on_goal = 0;
  int total_agents = obj_mgr.GetNumAgents();
  for (const Agent* agent : obj_mgr.GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    if (grid.GetCellKind(agent->GetPosition()) == CellKind::Synchro) {
      on_goal++;
    }
  }
  ss << "Agents on goals: " << on_goal << "/" << total_agents << "\n";

  return ss.str();
}

void Renderer::RenderVisual(const BaseEnv& /*env*/) const {
  // Placeholder for future graphical rendering
  std::cout << "[Visual rendering not yet implemented]\n";
}

std::string Renderer::GetDirectionArrow(Direction dir) const {
  switch (dir) {
    case Direction::Up:
      return "^";
    case Direction::Down:
      return "v";
    case Direction::Left:
      return "<";
    case Direction::Right:
      return ">";
    default:
      return "?";
  }
}

int Renderer::ActorColorToAnsi(ActorColor color) const {
  switch (color) {
    case ActorColor::Red:   return 91;  // Bright red
    case ActorColor::Green: return 92;  // Bright green
    case ActorColor::Blue:  return 94;  // Bright blue
    case ActorColor::None:
    default:                return 0;   // No color
  }
}

std::string Renderer::GetCellActorDisplay(const Cell& cell, const Actor* actor) const {
  if (!actor) return "??";

  // Get cell character (second char of 2-char display, e.g., " ." -> '.', "  " -> ' ')
  const char* display = cell.GetDisplay();
  char cell_char = display[1];

  // Check if it's a Companion (has direction and color)
  const Companion* comp = dynamic_cast<const Companion*>(actor);
  if (comp) {
    std::string arrow = GetDirectionArrow(comp->GetDirection());
    int ansi_code = ActorColorToAnsi(comp->GetColor());
    if (ansi_code != 0) {
      arrow = Colorize(arrow, ansi_code);
    }
    // Colored arrow + cell char (e.g., "<S")
    return arrow + std::string(1, cell_char);
  }

  // Check if it's an AgentFSM (has FSM state)
  const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(actor);
  if (fsm_agent && fsm_agent->HasFSM()) {
    char actor_char = fsm_agent->GetChar();
    const FSMState* state = fsm_agent->GetCurrentState();
    if (state) {
      int ansi_code = FSMStateToAnsi(state->GetName());
      if (ansi_code != 0) {
        return Colorize(std::string(1, actor_char), ansi_code) + std::string(1, cell_char);
      }
    }
    return std::string(1, actor_char) + std::string(1, cell_char);
  }

  // For other actors, use actor char + cell char
  char actor_char = actor->GetChar();
  return std::string(1, actor_char) + std::string(1, cell_char);
}

std::string Renderer::Colorize(const std::string& text, int color_code) const {
  if (color_code == 0) return text;
  return "\033[" + std::to_string(color_code) + "m" + text + "\033[0m";
}

int Renderer::FSMStateToAnsi(const std::string& state_name) const {
  if (state_name == "Patrol") return 92;           // Green
  if (state_name == "Aggro") return 93;            // Yellow
  if (state_name == "Telegraph") return 91;        // Red
  if (state_name == "Attack") return 95;           // Pink
  if (state_name == "Recovery") return 96;         // Cyan/Pink
  if (state_name == "ReturnToPatrol") return 90;   // Grey
  return 0;  // No color
}

}  // namespace companions
