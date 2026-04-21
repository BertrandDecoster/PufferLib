// Copyright 2024
// Interactive keyboard play demo for The Companions game

#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

// Include project headers BEFORE Windows headers to avoid macro conflicts
#include "../core/agent_config.h"
#include "../core/effect_config.h"
#include "../core/game_logger.h"
#include "../env/aggro_env.h"
#include "../env/dodge_env.h"
#include "../env/synchro_env.h"
#include "../viz/renderer.h"

// Platform-specific headers
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// Cross-platform sleep macro
#ifdef _WIN32
#define SLEEP_MS(ms) Sleep(ms)
#else
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

// Global flag for signal handling (outside namespace for Windows callback)
volatile sig_atomic_t g_should_exit = 0;

// Windows-specific handlers (must be outside namespace for WINAPI callback)
#ifdef _WIN32
void EnableWindowsConsole() {
  // Enable ANSI escape codes for colors and cursor control
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  GetConsoleMode(hOut, &mode);
  SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  // Enable UTF-8 output for Unicode characters (■ □)
  SetConsoleOutputCP(CP_UTF8);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    g_should_exit = 1;
    return TRUE;
  }
  return FALSE;
}
#else
void SignalHandler(int /*signal*/) {
  g_should_exit = 1;
}
#endif

namespace companions {

// Cross-platform keyboard input handler
class KeyboardInput {
 public:
#ifdef _WIN32
  KeyboardInput() {}   // Windows conio is always in raw mode
  ~KeyboardInput() {}
#else
  KeyboardInput() {
    // Save old terminal settings
    tcgetattr(STDIN_FILENO, &old_settings_);

    // Configure new settings for raw input
    termios new_settings = old_settings_;
    new_settings.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echo
    new_settings.c_cc[VMIN] = 1;   // Read at least 1 character
    new_settings.c_cc[VTIME] = 0;  // No timeout

    tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);
  }

  ~KeyboardInput() {
    // Restore old terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_settings_);
  }
#endif

  // Returns MovementAction or nullopt if quit/reset requested
  // Returns nullopt with quit=true if 'p' pressed, reset=true if 'r' pressed
  // seed_input=true if digit pressed (seed will contain the entered value)
  std::optional<MovementAction> GetAction(bool& quit, bool& reset, bool& seed_input, unsigned int& seed) {
    quit = false;
    reset = false;
    seed_input = false;

#ifdef _WIN32
    int c = _getch();

    // Handle extended keys (arrow keys on Windows)
    // Arrow keys return 0 or 224 followed by the actual key code
    if (c == 0 || c == 224) {
      int ext = _getch();
      switch (ext) {
        case 72: return MovementAction::Up;     // Up arrow
        case 80: return MovementAction::Down;   // Down arrow
        case 75: return MovementAction::Left;   // Left arrow
        case 77: return MovementAction::Right;  // Right arrow
      }
      return std::nullopt;  // Unknown extended key
    }
#else
    int c = getchar();

    // Handle escape sequences (arrow keys on Unix)
    if (c == 27) {  // ESC
      int seq1 = getchar();
      int seq2 = getchar();
      if (seq1 == '[') {
        switch (seq2) {
          case 'A': return MovementAction::Up;
          case 'B': return MovementAction::Down;
          case 'C': return MovementAction::Right;
          case 'D': return MovementAction::Left;
        }
      }
      return std::nullopt;  // Unknown escape sequence
    }
#endif

    // Handle digit keys - enter seed input mode
    if (c >= '0' && c <= '9') {
      seed_input = true;
      seed = ReadSeed(static_cast<char>(c));
      return std::nullopt;
    }

    // Handle regular keys (ZQSD for French keyboard, WASD for US)
    switch (c) {
      case 'z': case 'Z': case 'w': case 'W': return MovementAction::Up;
      case 's': case 'S': return MovementAction::Down;
      case 'q': case 'Q': case 'a': case 'A': return MovementAction::Left;
      case 'd': case 'D': return MovementAction::Right;
      case ' ': return MovementAction::Stay;
      case 'r': case 'R':
        reset = true;
        return std::nullopt;
      case 'p': case 'P': case 3:  // P or Ctrl-C
        quit = true;
        return std::nullopt;
      default:
        return std::nullopt;  // Invalid key
    }
  }

  // Read a seed from keyboard input (first digit already read)
  unsigned int ReadSeed(char first_digit) {
    std::string seed_str;
    seed_str += first_digit;

    // Show prompt with first digit
    std::cout << "\n\033[KEnter seed: " << seed_str << std::flush;

    while (true) {
#ifdef _WIN32
      int c = _getch();
#else
      int c = getchar();
#endif
      if (c == '\n' || c == '\r') {
        // Enter pressed - done
        break;
      } else if (c == 27) {
        // Escape - cancel
        std::cout << " (cancelled)\n";
        return 0;
      } else if (c == 127 || c == 8) {
        // Backspace
        if (!seed_str.empty()) {
          seed_str.pop_back();
          std::cout << "\b \b" << std::flush;
        }
      } else if (c >= '0' && c <= '9' && seed_str.length() < 10) {
        // Add digit (max 10 digits for unsigned int)
        seed_str += static_cast<char>(c);
        std::cout << static_cast<char>(c) << std::flush;
      }
      // Ignore other keys
    }

    if (seed_str.empty()) {
      return 0;
    }

    return static_cast<unsigned int>(std::stoul(seed_str));
  }

 private:
#ifndef _WIN32
  termios old_settings_;
#endif
};

void PrintUsage(const char* prog_name) {
  std::cerr << "Usage: " << prog_name << " [options]\n";
  std::cerr << "Options:\n";
  std::cerr << "  --env NAME        Environment: synchro (default), aggro, dodge\n";
  std::cerr << "  --size N          Grid size NxN (default: 12 for synchro, 10 for aggro, 7 for dodge)\n";
  std::cerr << "  --width W         Grid width (columns) - synchro only\n";
  std::cerr << "  --height H        Grid height (rows) - synchro only\n";
  std::cerr << "  --companions N    Number of companions, 1-3 (default: 3 for synchro, 1 for others)\n";
  std::cerr << "  --synchro N       Number of synchro cells (default: same as companions) - synchro only\n";
  std::cerr << "  --complexity N    Map complexity 0-5 (default: 0 = empty) - synchro only\n";
  std::cerr << "  --enemy TYPE      Enemy type: zombie (default), goblin - aggro only\n";
  std::cerr << "  --interval N      Hazard spawn interval in ticks (default: 3) - dodge only\n";
  std::cerr << "  --survival N      Ticks to survive (default: 50) - dodge only\n";
  std::cerr << "  --seed N          Random seed (default: 42)\n";
  std::cerr << "  --transform N     D4 symmetry transform 0-7 (default: 0 = identity)\n";
  std::cerr << "  --log CATEGORIES  Enable logging (fsm, effects, game - comma-separated)\n";
  std::cerr << "  --help            Show this help\n";
  std::cerr << "\nNotes:\n";
  std::cerr << "  - For synchro env: Use --size OR (--width and --height), min 4x4\n";
  std::cerr << "  - For aggro env: Square grid only (use --size), min 10x10\n";
  std::cerr << "  - For dodge env: Survive hazards spawning every --interval ticks\n";
  std::cerr << "  - Zombie moves every 2 steps, Goblin moves every step\n";
}

void PrintHelp(const std::string& env_name, int num_companions,
               const std::string& enemy_type = "",
               int hazard_interval = 0, int survival_ticks = 0) {
  std::cout << "\n========================================\n";
  std::cout << "   THE COMPANIONS - Interactive Demo\n";
  std::cout << "========================================\n\n";

  if (env_name == "synchro") {
    std::cout << "Goal: Move all " << num_companions << " companion(s) to synchro cells (S)\n\n";
  } else if (env_name == "aggro") {
    std::cout << "Goal: Lure the enemy onto the target cell (T)\n\n";
    std::cout << "Enemy: " << enemy_type << " (Aggro range=3, Return range=5)\n";
    if (enemy_type == "zombie") {
      std::cout << "  - Zombie (Z) moves every 2 steps\n\n";
    } else {
      std::cout << "  - Goblin (G) moves every step\n\n";
    }
  } else if (env_name == "dodge") {
    std::cout << "Goal: Survive " << survival_ticks << " ticks without dying!\n\n";
    std::cout << "Hazards spawn every " << hazard_interval << " ticks\n";
    std::cout << "  - Fire (!) = 3x3 damage area\n";
    std::cout << "  - Wind (~) = Push effect\n";
    std::cout << "  - Yellow cells = Telegraph (danger incoming!)\n\n";
  }

  std::cout << "Controls:\n";
  std::cout << "  Arrow keys / ZQSD = Move\n";
  std::cout << "  Space = Stay\n";
  std::cout << "  R = Reset (new random seed)\n";
  std::cout << "  0-9 = Enter seed, then Enter to reset\n";
  std::cout << "  P = Quit\n\n";

  if (num_companions == 1) {
    std::cout << "Press action for the companion (Red)\n";
  } else if (num_companions == 2) {
    std::cout << "Press action for each companion in order (Red, Green)\n";
  } else {
    std::cout << "Press action for each companion in order (Red, Green, Blue)\n";
  }
  std::cout << "========================================\n\n";
}

}  // namespace companions

int main(int argc, char* argv[]) {
  using namespace companions;

  // Setup signal/console handlers for clean shutdown
#ifdef _WIN32
  EnableWindowsConsole();
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
  std::signal(SIGTERM, SignalHandler);
  std::signal(SIGHUP, SignalHandler);
  std::signal(SIGINT, SignalHandler);
#endif

  // Default parameters
  std::string env_name = "synchro";
  std::string enemy_type = "zombie";  // Default for aggro env
  int rows = -1;  // -1 means use default for env
  int cols = -1;
  int num_companions = -1;  // -1 means use default for env
  int num_synchro = -1;  // -1 means "same as companions"
  int map_complexity = 0;  // 0-5 for synchro env
  int hazard_interval = 3;   // For dodge env
  int survival_ticks = 50;   // For dodge env
  int d4_transform = 0;      // D4 symmetry transform (0-7)
  unsigned int seed = 42;
  bool size_set = false;
  bool width_set = false;
  bool height_set = false;
  std::vector<std::string> log_categories;  // Categories to enable

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
      env_name = argv[++i];
    } else if ((strcmp(argv[i], "--size") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
      int size = std::atoi(argv[++i]);
      rows = size;
      cols = size;
      size_set = true;
    } else if ((strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-w") == 0) && i + 1 < argc) {
      cols = std::atoi(argv[++i]);
      width_set = true;
    } else if ((strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-H") == 0) && i + 1 < argc) {
      rows = std::atoi(argv[++i]);
      height_set = true;
    } else if ((strcmp(argv[i], "--companions") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
      num_companions = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--synchro") == 0 && i + 1 < argc) {
      num_synchro = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--complexity") == 0 && i + 1 < argc) {
      map_complexity = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--enemy") == 0 && i + 1 < argc) {
      enemy_type = argv[++i];
    } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
      hazard_interval = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--survival") == 0 && i + 1 < argc) {
      survival_ticks = std::atoi(argv[++i]);
    } else if ((strcmp(argv[i], "--transform") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
      d4_transform = std::atoi(argv[++i]);
    } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = static_cast<unsigned int>(std::atoi(argv[++i]));
    } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
      // Parse comma-separated categories: --log fsm,effects,game
      std::string categories_str = argv[++i];
      std::stringstream ss(categories_str);
      std::string cat;
      while (std::getline(ss, cat, ',')) {
        log_categories.push_back(cat);
      }
    } else {
      std::cerr << "Error: Unrecognized argument: " << argv[i] << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  // Enable logging categories
  for (const std::string& cat_str : log_categories) {
    auto cat_opt = LogCategoryFromString(cat_str);
    if (cat_opt) {
      GameLogger::Instance().Enable(*cat_opt);
      std::cout << "Enabled logging: " << cat_str << "\n";
    } else {
      std::cerr << "Warning: Unknown log category '" << cat_str << "'\n";
    }
  }

  // Validate environment name
  if (env_name != "synchro" && env_name != "aggro" && env_name != "dodge") {
    std::cerr << "Error: Unknown environment '" << env_name << "'. Use 'synchro', 'aggro', or 'dodge'.\n";
    return 1;
  }

  // Validate D4 transform
  if (d4_transform < 0 || d4_transform > 7) {
    std::cerr << "Error: --transform must be 0-7\n";
    return 1;
  }

  // Apply defaults based on environment
  if (env_name == "synchro") {
    if (rows == -1) rows = kDefaultGridSize;
    if (cols == -1) cols = kDefaultGridSize;
    if (num_companions == -1) num_companions = 3;
    if (num_synchro == -1) num_synchro = num_companions;
  } else if (env_name == "aggro") {
    if (rows == -1) rows = 10;
    if (cols == -1) cols = rows;  // Square grid
    if (num_companions == -1) num_companions = 1;
  } else if (env_name == "dodge") {
    if (rows == -1) rows = 7;
    if (cols == -1) cols = rows;  // Square grid
    if (num_companions == -1) num_companions = 1;
  }

  // Environment-specific validation
  if (env_name == "synchro") {
    if (size_set && (width_set || height_set)) {
      std::cerr << "Error: Cannot use --size with --width or --height\n";
      PrintUsage(argv[0]);
      return 1;
    }

    if (num_companions < 1 || num_companions > 3) {
      std::cerr << "Error: --companions must be 1, 2, or 3\n";
      return 1;
    }

    if (num_synchro < 1 || num_synchro > num_companions) {
      std::cerr << "Error: --synchro must be >= 1 and <= companions\n";
      return 1;
    }

    if (rows < 4 || cols < 4) {
      std::cerr << "Error: Grid must be at least 4x4\n";
      return 1;
    }
  } else if (env_name == "aggro") {
    if (width_set || height_set) {
      std::cerr << "Error: AggroEnv requires square grid. Use --size instead of --width/--height\n";
      return 1;
    }

    if (rows != cols) {
      std::cerr << "Error: AggroEnv requires square grid (rows != cols)\n";
      return 1;
    }

    if (rows < 10) {
      std::cerr << "Error: AggroEnv requires grid size >= 10\n";
      return 1;
    }

    if (num_companions < 1 || num_companions > 3) {
      std::cerr << "Error: --companions must be 1, 2, or 3\n";
      return 1;
    }

    if (num_synchro != -1) {
      std::cerr << "Warning: --synchro is ignored for AggroEnv\n";
    }

    if (enemy_type != "zombie" && enemy_type != "goblin") {
      std::cerr << "Error: --enemy must be 'zombie' or 'goblin'\n";
      return 1;
    }
  } else if (env_name == "dodge") {
    if (width_set || height_set) {
      std::cerr << "Error: DodgeEnv requires square grid. Use --size instead of --width/--height\n";
      return 1;
    }

    if (rows != cols) {
      std::cerr << "Error: DodgeEnv requires square grid (rows != cols)\n";
      return 1;
    }

    if (rows < 5) {
      std::cerr << "Error: DodgeEnv requires grid size >= 5\n";
      return 1;
    }

    if (num_companions < 1 || num_companions > 3) {
      std::cerr << "Error: --companions must be 1, 2, or 3\n";
      return 1;
    }

    if (hazard_interval < 1) {
      std::cerr << "Error: --interval must be >= 1\n";
      return 1;
    }

    if (survival_ticks < 1) {
      std::cerr << "Error: --survival must be >= 1\n";
      return 1;
    }
  }

  // Track current seed for display
  unsigned int current_seed = seed;

  // Load agent and effect configs. We look under several locations because
  // the demo can be launched from different working directories (the build
  // dir, the repo root, the companions dir, ...). In particular:
  //   - "<exe_dir>/data/..."   (CMake copies data next to the binary)
  //   - "data/..."             (cwd == companions/ or companions/build/bin)
  //   - "../data/..."          (cwd == companions/build)
  //   - "pufferlib/ocean/companions/data/..."  (cwd == repo root)
  //
  // Attack damage is delivered via effects spawned from zombie/goblin configs,
  // so if these CSVs don't load the FSM cycles (Telegraph -> Attack ->
  // Recovery) run visually but no damage lands. A past regression silently
  // turned zombie attacks into no-ops when the demo was started from the
  // repo root; we now refuse to start if the data cannot be found.
  std::string exe_dir;
  {
    std::string path(argv[0]);
    auto slash = path.find_last_of('/');
    exe_dir = (slash == std::string::npos) ? std::string(".")
                                            : path.substr(0, slash);
  }
  auto try_paths = [&](const char* leaf, auto load_fn) -> bool {
    std::vector<std::string> candidates = {
        exe_dir + "/data/" + leaf,
        std::string("data/") + leaf,
        std::string("../data/") + leaf,
        std::string("pufferlib/ocean/companions/data/") + leaf,
        std::string("../pufferlib/ocean/companions/data/") + leaf,
    };
    for (const auto& p : candidates) {
      if (load_fn(p)) {
        return true;
      }
    }
    std::cerr << "Error: Could not load data/" << leaf << ". Tried:\n";
    for (const auto& p : candidates) std::cerr << "  " << p << "\n";
    std::cerr << "Start the demo from the companions/ directory or keep the "
                 "data/ folder next to the binary.\n";
    return false;
  };

  if (!try_paths("agents.csv", [](const std::string& p) {
        return AgentConfigRegistry::Instance().LoadFromCSV(p);
      })) {
    return 1;
  }
  if (!try_paths("effects.csv", [](const std::string& p) {
        return EffectConfigRegistry::Instance().LoadFromCSV(p);
      })) {
    return 1;
  }

  // Create environment (polymorphic)
  std::unique_ptr<BaseEnv> env;
  try {
    if (env_name == "synchro") {
      env = std::make_unique<SynchroEnv>(rows, cols, num_companions, num_synchro, map_complexity, current_seed, d4_transform);
    } else if (env_name == "aggro") {
      EnemyType etype = (enemy_type == "zombie") ? EnemyType::Zombie : EnemyType::Goblin;
      env = std::make_unique<AggroEnv>(rows, num_companions, etype, current_seed, d4_transform);
    } else if (env_name == "dodge") {
      env = std::make_unique<DodgeEnv>(rows, num_companions, hazard_interval, survival_ticks, current_seed, d4_transform);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  PrintHelp(env_name, num_companions, enemy_type, hazard_interval, survival_ticks);

  Renderer renderer;
  KeyboardInput input;

  // Color names for companions (Red is always the player)
  const char* color_names[] = {"Red  ", "Green", "Blue "};

  // Track cumulative reward
  double total_reward = 0.0;

  // Outer loop for continuous play (reset on finish)
  while (!g_should_exit) {
    bool reset_requested = false;
    unsigned int new_seed = 0;  // For seed input

    while (!env->IsDone() && !reset_requested && !g_should_exit) {
      // Clear screen only when logging is disabled (otherwise let output scroll)
      if (!GameLogger::Instance().HasAnyEnabled()) {
        std::cout << "\033[2J\033[H";  // ANSI escape: clear screen, move cursor to top
      } else {
        std::cout << "\n========== TICK " << env->GetTick() << " ==========\n";
      }
      std::cout << renderer.RenderAsciiWithNPCPanel(*env) << std::flush;
      std::cout << "Env: " << env_name << " | Seed: " << current_seed;
      if (d4_transform != 0) {
        std::cout << " | Transform: " << d4_transform;
      }
      std::cout << "\n\n";

      // Get companions sorted by agent index (Player first due to creation order)
      auto companions = env->GetObjectManager().GetAllCompanions();
      int total_agents = env->NumAgents();
      int human_agents = static_cast<int>(companions.size());

      // Initialize all actions to Stay (enemies will override via FSM in PreStep)
      std::vector<Action> actions(total_agents, EncodeAction(MovementAction::Stay));

      // Print initial lines with placeholder for human-controlled agents
      for (int i = 0; i < human_agents; ++i) {
        std::cout << "Companion " << color_names[i] << " "
                  << renderer.RenderHealth(*companions[i]) << ": _\n";
      }

      // Collect actions, updating display in place
      for (int i = 0; i < human_agents; ++i) {
        // Move cursor to line i (from bottom)
        std::cout << "\033[" << (human_agents - i) << "A";  // Move up
        // Column = "Companion Color " (17) + health boxes (3 chars each) + ": " (2)
        int col = 17 + companions[i]->GetMaxHealth() * 3 + 2;
        std::cout << "\033[" << col << "G";
        std::cout << std::flush;

        bool quit = false;
        bool reset = false;
        bool seed_input = false;
        while (!g_should_exit) {
          auto action = input.GetAction(quit, reset, seed_input, new_seed);
          if (quit || g_should_exit) {
            std::cout << "\n\nQuit requested. Goodbye!\n";
            return 0;
          }
          if (reset) {
            // Generate new random seed
            new_seed = std::random_device{}();
            reset_requested = true;
            break;
          }
          if (seed_input) {
            // User entered a specific seed
            if (new_seed != 0) {
              reset_requested = true;
            }
            break;
          }
          if (action.has_value()) {
            // Place action at correct agent index for this companion
            int agent_idx = companions[i]->GetAgentIndex();
            actions[agent_idx] = EncodeAction(action.value());
            std::cout << MovementActionToString(action.value()) << "\033[K";  // Print + clear rest of line
            std::cout << "\033[" << (human_agents - i) << "B";  // Move back down
            std::cout << std::flush;
            break;
          }
          // Invalid key, wait for valid input
        }
        if (reset_requested) break;
      }

      // Reset cursor to column 1 before any logging occurs
      std::cout << "\033[1G" << std::flush;

      if (reset_requested) break;

      // Update logger tick before step
      GameLogger::Instance().SetCurrentTick(env->GetTick());

      // Step environment
      auto result = env->Step(actions);

      // Log game state (rendering) after step
      if (LOG_ENABLED(Game)) {
        LOG_GAME("\n" << renderer.RenderAscii(*env));
      }

      // Flush logs in order: FSM -> Effects -> Game
      GameLogger::Instance().Flush();

      // Accumulate reward (use first agent's reward)
      if (!result.rewards.empty()) {
        total_reward += result.rewards[0];
      }

      // Brief pause to see the update
      SLEEP_MS(100);  // 100ms
    }

    // Check for signal exit
    if (g_should_exit) break;

    // Show final state (or reset message)
    if (!GameLogger::Instance().HasAnyEnabled()) {
      std::cout << "\033[2J\033[H";
    } else {
      std::cout << "\n========== FINAL STATE ==========\n";
    }
    renderer.Render(*env, RenderMode::Ascii);
    std::cout << "Env: " << env_name << " | Seed: " << current_seed;
    if (d4_transform != 0) {
      std::cout << " | Transform: " << d4_transform;
    }
    std::cout << "  |  Total Reward: " << total_reward << "\n\n";

    if (reset_requested) {
      std::cout << "*** Resetting with seed " << new_seed << "... ***\n";
      current_seed = new_seed;
      total_reward = 0.0;  // Reset reward on manual reset
    } else if (env->IsSuccess()) {
      if (env_name == "synchro") {
        std::cout << "*** SUCCESS! All companions reached synchro cells! ***\n";
      } else if (env_name == "aggro") {
        std::cout << "*** SUCCESS! Companion reached the target cell! ***\n";
      } else if (env_name == "dodge") {
        std::cout << "*** SUCCESS! You survived! ***\n";
      }
      std::cout << "*** Final Reward: " << total_reward << " ***\n";
      current_seed++;  // Increment seed for next game
      total_reward = 0.0;  // Reset for next game
    } else {
      if (env_name == "dodge") {
        std::cout << "*** GAME OVER - You died! ***\n";
      } else {
        std::cout << "*** Game ended (max steps reached) ***\n";
      }
      std::cout << "*** Final Reward: " << total_reward << " ***\n";
      current_seed++;  // Increment seed for next game
      total_reward = 0.0;  // Reset for next game
    }

    // Brief pause to see the result
    SLEEP_MS(1000);  // 1 second

    // Reset for next episode with new seed
    // Recreate environment with new seed (Reset changes map for complexity > 0)
    if (env_name == "synchro") {
      env = std::make_unique<SynchroEnv>(rows, cols, num_companions, num_synchro, map_complexity, current_seed, d4_transform);
    } else if (env_name == "aggro") {
      static_cast<AggroEnv*>(env.get())->Reset(current_seed);
    } else if (env_name == "dodge") {
      static_cast<DodgeEnv*>(env.get())->Reset(current_seed);
    }
  }

  std::cout << "\nShutting down...\n";
  return 0;
}
