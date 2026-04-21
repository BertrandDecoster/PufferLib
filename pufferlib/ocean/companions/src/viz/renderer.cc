// Copyright 2024
// Renderer implementation

#include "renderer.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "../core/annotations.h"
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
        // Display cell char + actor arrow (e.g., "<S" for agent on synchro)
        std::string display = GetCellActorDisplay(env, pos, cell, actor);
        ss << display << "|";
      } else {
        // Display cell only, with annotation overrides for goal cells.
        ss << GetCellDisplay(env, pos, cell) << "|";
      }
    }
    ss << "\n" << sep << "\n";
  }

  // Status line - count agents on goal cells (SynchroGoal annotation)
  int on_goal = 0;
  int total_agents = obj_mgr.GetNumAgents();
  const AnnotationStore& annotations = env.GetAnnotations();
  for (const Agent* agent : obj_mgr.GetAllAgents()) {
    if (!agent->IsAlive()) continue;
    if (annotations.HasTag(
            AnnotationKey{AnnotationTarget::Cell, agent->GetPosition(),
                          kInvalidObjectId},
            SemanticTag::SynchroGoal)) {
      on_goal++;
    }
  }
  (void)grid;
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

std::string Renderer::GetCellDisplay(const BaseEnv& env, Position pos,
                                      const Cell& cell) const {
  // Priority: SynchroGoal > AggroTarget > terrain. Room / HtnName / etc. are
  // metadata tags with no visual representation.
  const AnnotationStore& ann = env.GetAnnotations();
  AnnotationKey key{AnnotationTarget::Cell, pos, kInvalidObjectId};
  if (ann.HasTag(key, SemanticTag::SynchroGoal)) {
    return Colorize(" S", 93);  // Yellow, matches pre-refactor Synchro glyph
  }
  if (ann.HasTag(key, SemanticTag::AggroTarget)) {
    return Colorize(" T", 95);  // Magenta, matches pre-refactor Target glyph
  }
  std::string display = cell.GetDisplay();
  int color = cell.GetColorCode();
  if (color != 0) {
    display = Colorize(display, color);
  }
  return display;
}

char Renderer::GetCellChar(const BaseEnv& env, Position pos, const Cell& cell) const {
  const AnnotationStore& ann = env.GetAnnotations();
  AnnotationKey key{AnnotationTarget::Cell, pos, kInvalidObjectId};
  if (ann.HasTag(key, SemanticTag::SynchroGoal)) return 'S';
  if (ann.HasTag(key, SemanticTag::AggroTarget)) return 'T';
  return cell.GetDisplay()[1];
}

std::string Renderer::GetCellActorDisplay(const BaseEnv& env, Position pos,
                                           const Cell& cell,
                                           const Actor* actor) const {
  if (!actor) return "??";

  // Get cell character, honouring annotation overrides so actors on goal
  // cells still show 'S' / 'T' in the second slot rather than ' '.
  char cell_char = GetCellChar(env, pos, cell);

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

std::string Renderer::RenderHealth(const Companion& companion) const {
  int health = companion.GetHealth();
  int max_health = companion.GetMaxHealth();
  int ansi_code = ActorColorToAnsi(companion.GetColor());

  std::string result;
  for (int i = 0; i < max_health; ++i) {
    std::string box = (i < health) ? "\xE2\x96\xA0"   // U+25A0 ■
                                   : "\xE2\x96\xA1";  // U+25A1 □
    if (ansi_code != 0) {
      result += "\033[" + std::to_string(ansi_code) + "m[" + box + "]\033[0m";
    } else {
      result += "[" + box + "]";
    }
  }
  return result;
}

// =============================================================================
// NPC state panel
// =============================================================================
namespace {

// Map the verbose FSM state name to a single word suitable for an at-a-glance
// panel label. ReturnToPatrol is the only non-single-word state.
std::string ShortFSMStateName(const std::string& state_name) {
  if (state_name == "ReturnToPatrol") return "Returning";
  return state_name;
}

// Count displayable terminal columns in a string, ignoring ANSI CSI escape
// sequences ("\033[...m"). The ASCII grid uses single-byte glyphs only (no
// multi-byte UTF-8 in separators/data lines), so a simple byte count after
// stripping escapes is the right width measure.
int VisualWidth(const std::string& s) {
  int width = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
      // Skip until the final letter of the CSI sequence (typically 'm').
      i += 2;
      while (i < s.size() && !((s[i] >= '@' && s[i] <= '~'))) ++i;
      continue;  // the loop's ++i will also consume the terminator
    }
    ++width;
  }
  return width;
}

}  // namespace

std::vector<NPCStatusLine> Renderer::CollectNPCStatusLines(
    const BaseEnv& env) const {
  std::vector<NPCStatusLine> result;
  for (const Agent* agent : env.GetObjectManager().GetAllAgents()) {
    if (!agent || !agent->IsAlive()) continue;
    const AgentFSM* fsm_agent = dynamic_cast<const AgentFSM*>(agent);
    if (!fsm_agent || !fsm_agent->HasFSM()) continue;
    const FSMState* state = fsm_agent->GetCurrentState();
    if (!state) continue;

    NPCStatusLine line;
    line.letter = fsm_agent->GetChar();
    line.state_name = ShortFSMStateName(state->GetName());
    line.ansi_code = FSMStateToAnsi(state->GetName());
    result.push_back(std::move(line));
  }
  return result;
}

std::string Renderer::RenderAsciiWithNPCPanel(const BaseEnv& env) const {
  std::string map_str = RenderAscii(env);
  std::vector<NPCStatusLine> panel = CollectNPCStatusLines(env);
  if (panel.empty()) {
    return map_str;  // Nothing to splice - preserve byte-for-byte output.
  }

  // Split the map into lines (preserving trailing empty line behaviour).
  std::vector<std::string> lines;
  {
    std::size_t start = 0;
    while (start <= map_str.size()) {
      std::size_t nl = map_str.find('\n', start);
      if (nl == std::string::npos) {
        lines.push_back(map_str.substr(start));
        break;
      }
      lines.push_back(map_str.substr(start, nl - start));
      start = nl + 1;
    }
  }

  // Use the first separator line as the visual width reference. The separator
  // has no ANSI escapes, so its byte length equals its column width.
  //
  // Layout produced by RenderAscii:
  //   line 0: "Tick: N"
  //   line 1: "+--+--+..."         <- separator (reference width)
  //   line 2: "| .. | .. | ..."    <- row 0 data   <- panel slot 0
  //   line 3: separator
  //   line 4: row 1 data           <- panel slot 1
  //   ...
  int ref_width = 0;
  if (lines.size() > 1) ref_width = static_cast<int>(lines[1].size());

  auto format_line = [this](const NPCStatusLine& line) {
    std::string letter_str(1, line.letter);
    if (line.ansi_code != 0) {
      letter_str = Colorize(letter_str, line.ansi_code);
    }
    return letter_str + " " + line.state_name;
  };

  // Pack panel entries into the data rows (2, 4, 6, ..., 2 + 2*(rows-1)).
  // If there are more NPCs than grid rows, extra lines are appended below
  // the map on their own lines so the information is never lost.
  int grid_rows = env.GetGrid().GetRows();
  std::size_t last_data_row =
      static_cast<std::size_t>(2 + 2 * (grid_rows - 1));
  std::ostringstream out;
  std::size_t npc_idx = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    bool is_data_row = (i >= 2) && (i <= last_data_row) &&
                       ((i - 2) % 2 == 0);
    if (is_data_row && npc_idx < panel.size()) {
      int visual = VisualWidth(lines[i]);
      int pad = std::max(2, ref_width - visual + 2);
      out << std::string(static_cast<std::size_t>(pad), ' ')
          << format_line(panel[npc_idx]);
      ++npc_idx;
    }
    if (i + 1 < lines.size()) out << "\n";
  }
  // Emit any panel entries that didn't fit next to a data row.
  while (npc_idx < panel.size()) {
    out << "\n" << format_line(panel[npc_idx]);
    ++npc_idx;
  }
  return out.str();
}

}  // namespace companions
