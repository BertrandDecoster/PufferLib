// Copyright 2024
// FSM Performance Benchmarks

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../../src/core/fsm/enemies.h"
#include "../../src/core/fsm/fsm_state.h"
#include "../../src/core/fsm/fsm_states.h"
#include "../../src/core/level_builder.h"
#include "../../src/core/pathfinder.h"
#include "../../src/env/synchro_env.h"

using namespace companions;
using Clock = std::chrono::high_resolution_clock;

/**
 * 
 * 
cd pufferlib/ocean/companions
./scripts/profile_benchmark.sh

OR

cd pufferlib/ocean/companions/build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo  # rebuild with debug symbols if needed
make companions_fsm_benchmark
samply record ./companions_fsm_benchmark
 * 
 */

// =============================================================================
// Benchmark utilities
// =============================================================================
struct BenchmarkResult {
  std::string name;
  int iterations;
  double total_time_ms;
  double avg_time_us;
};

void PrintResult(const BenchmarkResult& result) {
  std::cout << std::setw(40) << std::left << result.name
            << std::setw(10) << result.iterations << " iters.  "
            << std::setw(12) << std::fixed << std::setprecision(2)
            << result.total_time_ms << " ms total.  "
            << std::setw(12) << std::fixed << std::setprecision(3)
            << result.avg_time_us << " us/iter"
            << "\n";
}

// =============================================================================
// Benchmark: Single Zombie FSM Update
// =============================================================================
BenchmarkResult BenchmarkSingleZombieFSMUpdate(int iterations) {
  SynchroEnv env(12, 12, 1, 1, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  std::mt19937 rng(42);

  // Create a zombie
  std::vector<Position> patrol_path = {{2, 2}, {2, 8}, {8, 8}, {8, 2}};
  Zombie* zombie = CreateZombie(mgr, {2, 2}, patrol_path, rng);

  // Warm up
  for (int i = 0; i < 100; ++i) {
    zombie->UpdateFSM(env);
  }

  // Benchmark
  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    zombie->UpdateFSM(env);
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;

  return {"Single Zombie FSM Update", iterations, total_ms, avg_us};
}

// =============================================================================
// Benchmark: Multiple Zombies FSM Update (typical gameplay)
// =============================================================================
BenchmarkResult BenchmarkMultipleZombiesFSMUpdate(int num_zombies, int iterations) {
  SynchroEnv env(16, 16, 3, 1, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  std::mt19937 rng(42);

  // Create multiple zombies with different patrol paths
  std::vector<Zombie*> zombies;
  for (int i = 0; i < num_zombies; ++i) {
    int row = 2 + (i % 12);
    int col = 2 + (i / 12);
    std::vector<Position> patrol_path = {
        {row, col}, {row, col + 5}, {row + 3, col + 5}, {row + 3, col}};
    zombies.push_back(CreateZombie(mgr, {row, col}, patrol_path, rng));
  }

  // Warm up
  for (int i = 0; i < 100; ++i) {
    for (Zombie* z : zombies) {
      z->UpdateFSM(env);
    }
  }

  // Benchmark
  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    for (Zombie* z : zombies) {
      z->UpdateFSM(env);
    }
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;
  double per_zombie_us = avg_us / num_zombies;

  std::cout << "  (per zombie: " << std::fixed << std::setprecision(3)
            << per_zombie_us << " us)\n";

  return {std::to_string(num_zombies) + " Zombies FSM Update", iterations, total_ms, avg_us};
}

// =============================================================================
// Benchmark: Full Environment Step (with FSM enemies)
// =============================================================================
BenchmarkResult BenchmarkFullEnvStepWithFSM(int num_zombies, int iterations) {
  SynchroEnv env(16, 16, 3, 1, 42);
  env.Reset(42);

  ObjectManager& mgr = env.GetMutableObjectManager();
  std::mt19937 rng(42);

  // Create zombies
  for (int i = 0; i < num_zombies; ++i) {
    int row = 2 + (i % 10);
    int col = 2 + (i / 10);
    std::vector<Position> patrol_path = {{row, col}, {row, col + 3}};
    CreateZombie(mgr, {row, col}, patrol_path, rng);
  }

  // Actions for all agents (companions + zombies, though zombies use FSM)
  std::vector<Action> actions(env.NumAgents(), EncodeAction(MovementAction::Stay));

  // Warm up
  for (int i = 0; i < 50; ++i) {
    env.Step(actions);
  }

  // Benchmark
  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    env.Step(actions);
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;

  return {"Full Step (" + std::to_string(num_zombies) + " zombies)", iterations, total_ms, avg_us};
}

// =============================================================================
// Benchmark: Environment Step without FSM (baseline)
// =============================================================================
BenchmarkResult BenchmarkEnvStepBaseline(int grid_size, int iterations) {
  SynchroEnv env(grid_size, grid_size, 3, 1, 42);
  env.Reset(42);

  std::mt19937 rng(42);
  std::vector<Action> actions(env.NumAgents());

  // Warm up
  for (int i = 0; i < 50; ++i) {
    for (auto& a : actions) {
      auto mov = static_cast<MovementAction>(rng() % kNumMovementActions);
      auto interact = static_cast<InteractAction>(rng() % kNumInteractActions);
      a = EncodeAction(mov, interact);
    }
    StepResult step_result = env.Step(actions);
    if (step_result.done){
      env.Reset();
    }
  }

  // Benchmark
  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    for (auto& a : actions) {
      auto mov = static_cast<MovementAction>(rng() % kNumMovementActions);
      auto interact = static_cast<InteractAction>(rng() % kNumInteractActions);
      a = EncodeAction(mov, interact);
    }
    StepResult step_result = env.Step(actions);
    if (step_result.done){
      env.Reset();
    }
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;

  return {"Full Step " + std::to_string(grid_size) + "x" + std::to_string(grid_size) + " (random)", iterations, total_ms, avg_us};
}

// =============================================================================
// Benchmark: State Transition Overhead
// =============================================================================
BenchmarkResult BenchmarkStateTransition(int iterations) {
  SynchroEnv env(12, 12, 1, 1, 42);
  env.Reset(42);

  auto companions = env.GetObjectManager().GetAllCompanions();
  Position comp_pos = companions[0]->GetPosition();

  ObjectManager& mgr = env.GetMutableObjectManager();
  std::mt19937 rng(42);

  // Create goblin that will constantly transition between states
  Position goblin_pos = {comp_pos.row, comp_pos.col + 3};  // Just at detection edge
  std::vector<Position> patrol_path = {{goblin_pos.row, goblin_pos.col + 3}};
  Goblin* goblin = CreateGoblin(mgr, goblin_pos, patrol_path, rng);

  // Manually force state transitions for benchmark
  FSMContext ctx;
  ctx.patrol_path = patrol_path;
  ctx.detection_range = 4;
  ctx.lose_target_range = 6;
  ctx.rng = &rng;

  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    // Alternate between states
    if (i % 2 == 0) {
      goblin->SetFSM(&PatrolState::Instance(), ctx);
    } else {
      goblin->SetFSM(&AggroState::Instance(), ctx);
    }
    goblin->UpdateFSM(env);
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;

  return {"State Transition + Update", iterations, total_ms, avg_us};
}

// =============================================================================
// Benchmark: Pathfinding (isolated)
// =============================================================================
BenchmarkResult BenchmarkPathfinding(int iterations) {
  Grid grid(16, 16);
  LevelBuilder builder(grid);
  builder.Fill(CellKind::Floor);

  // Add some obstacles
  builder.Line(CellKind::Wall, {4, 4}, {4, 12});
  builder.Line(CellKind::Wall, {8, 4}, {8, 12});
  builder.Line(CellKind::Wall, {12, 4}, {12, 12});

  Pathfinder pathfinder(grid);

  // Warm up
  for (int i = 0; i < 100; ++i) {
    pathfinder.FindPath({1, 1}, {14, 14});
  }

  // Benchmark
  auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    pathfinder.FindPath({1, 1}, {14, 14});
  }
  auto end = Clock::now();

  double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double avg_us = (total_ms * 1000.0) / iterations;

  return {"Pathfinding (16x16 with walls)", iterations, total_ms, avg_us};
}

// =============================================================================
// Main
// =============================================================================
// int main() {
//   std::cout << "=============================================================\n";
//   std::cout << "FSM Performance Benchmarks\n";
//   std::cout << "=============================================================\n\n";

//   std::vector<BenchmarkResult> results;

//   // FSM Update benchmarks
//   std::cout << "--- FSM Update Benchmarks ---\n";
//   results.push_back(BenchmarkSingleZombieFSMUpdate(100000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkMultipleZombiesFSMUpdate(10, 10000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkMultipleZombiesFSMUpdate(50, 2000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkMultipleZombiesFSMUpdate(100, 1000));
//   PrintResult(results.back());

//   // Full environment step benchmarks
//   std::cout << "\n--- Full Environment Step Benchmarks ---\n";
//   results.push_back(BenchmarkEnvStepBaseline(16, 10000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkFullEnvStepWithFSM(10, 5000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkFullEnvStepWithFSM(50, 2000));
//   PrintResult(results.back());

//   // Component benchmarks
//   std::cout << "\n--- Component Benchmarks ---\n";
//   results.push_back(BenchmarkStateTransition(50000));
//   PrintResult(results.back());

//   results.push_back(BenchmarkPathfinding(10000));
//   PrintResult(results.back());

//   // Summary
//   std::cout << "\n=============================================================\n";
//   std::cout << "Summary\n";
//   std::cout << "=============================================================\n";
//   std::cout << "Single FSM Update:     ~" << std::fixed << std::setprecision(1)
//             << results[0].avg_time_us << " us\n";
//   std::cout << "Pathfinding:           ~" << results[8].avg_time_us << " us\n";
//   std::cout << "Full Step (random):    ~" << results[4].avg_time_us << " us\n";
//   std::cout << "Full Step (10 enemies): ~" << results[5].avg_time_us << " us\n";

//   return 0;
// }


int main(int argc, char* argv[]) {
  int grid_size = 10;
  int iterations = 100000;

  // Parse command line args
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--grid-size" && i + 1 < argc) {
      grid_size = std::stoi(argv[++i]);
    } else if (arg == "--iterations" && i + 1 < argc) {
      iterations = std::stoi(argv[++i]);
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --grid-size N    Grid size (default: 10)\n"
                << "  --iterations N   Number of iterations (default: 100000)\n";
      return 0;
    }
  }

  std::cout << "=============================================================\n";
  std::cout << "Benchmark rand moves in synchro (" << grid_size << "x" << grid_size << ")\n";
  std::cout << "=============================================================\n\n";

  std::vector<BenchmarkResult> results;

  // Full environment step benchmarks
  std::cout << "\n--- Full Environment Step Benchmarks ---\n";
  results.push_back(BenchmarkEnvStepBaseline(grid_size, iterations));
  PrintResult(results.back());

  // Summary
  std::cout << "\n=============================================================\n";
  std::cout << "Summary\n";
  std::cout << "=============================================================\n";
  std::cout << "Random move Update:     ~" << std::fixed << std::setprecision(1)
            << results[0].avg_time_us << " us\n";

  return 0;
}
