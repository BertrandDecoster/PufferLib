#!/usr/bin/env python3
"""
Test MCTS vs MaxUtility.

Runs C++ and Python MCTS at different simulation counts and checks
which algorithms reach the optimal value (MaxUtility) and which exceed it (bug!).

Usage:
    python companions/tests/python/test_max_utility.py
"""

import re
import subprocess
import sys
from pathlib import Path

import numpy as np

# Add open_spiel python to path
# IMPORTANT: build/python must come BEFORE repo root to use fresh build
# Path: companions/tests/python/test_max_utility.py -> companions-game/
REPO_ROOT = Path(__file__).parent.parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "build" / "python"))  # Must be first!

import pyspiel
from open_spiel.python.algorithms import mcts

# ============================================================================
# Configuration
# ============================================================================

GRID_SIZE = 5
PLAYERS = 2
HORIZON = 20
SEEDS = [100, 101, 102]  # Seeds where agents don't start on synchro
SIMULATION_COUNTS = [10, 100, 1000]
NUM_GAMES = 10

# Reward constants (from synchro_env.h)
# With the new reward structure, time_penalty = -num_agents * K_PROGRESS_REWARD
# This ensures max progress per step is 0 when not winning
K_WIN_REWARD = 10.0
K_PROGRESS_REWARD = 0.1

# Paths
MCTS_BINARY = REPO_ROOT / "build" / "examples" / "companions_scripts" / "companions_mcts"


def compute_max_utility(horizon: int, num_agents: int) -> float:
    """Compute MaxUtility for given configuration.

    With the new reward structure where time_penalty = -num_agents * K_PROGRESS_REWARD,
    the max progress per step is 0 (when all agents on synchro but not winning).
    Therefore, MaxUtility is simply the win reward.
    """
    # time_penalty = -num_agents * K_PROGRESS_REWARD
    # intermediate_step_max = max(0, time_penalty + K_PROGRESS_REWARD * num_agents) = 0
    # final_step = time_penalty + K_PROGRESS_REWARD * num_agents + K_WIN_REWARD = K_WIN_REWARD
    return K_WIN_REWARD


def run_mcts_cpp(grid_size: int, players: int, horizon: int, seed: int,
                 max_simulations: int, num_games: int = 10) -> dict:
    """Run C++ MCTS via subprocess."""
    if not MCTS_BINARY.exists():
        print(f"ERROR: MCTS binary not found at {MCTS_BINARY}")
        return {"returns": [], "error": "binary not found"}

    cmd = [
        str(MCTS_BINARY),
        f"--grid_size={grid_size}",
        f"--players={players}",
        f"--horizon={horizon}",
        f"--seed={seed}",
        f"--max_simulations={max_simulations}",
        f"--num_games={num_games}",
        "--uct_c=2",
        "--solve=true",
    ]

    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    output = result.stderr + result.stdout

    # Parse output for returns
    returns = []
    for line in output.split("\n"):
        if "Returns:" in line and "Average" not in line and "Overall" not in line:
            match = re.search(r"Returns:\s*([\d.-]+)", line)
            if match:
                returns.append(float(match.group(1)))

    return {"returns": returns, "output": output}


def run_mcts_python(grid_size: int, players: int, horizon: int, seed: int,
                    max_simulations: int, num_games: int = 10) -> dict:
    """Run Python MCTS."""
    game_str = f"turn_based_simultaneous_game(game=companions(grid_size={grid_size},players={players},horizon={horizon},seed={seed}))"
    game = pyspiel.load_game(game_str)

    rng = np.random.RandomState(seed)
    evaluator = mcts.RandomRolloutEvaluator(n_rollouts=1, random_state=rng)

    returns = []

    for _ in range(num_games):
        bot = mcts.MCTSBot(
            game,
            uct_c=2,
            max_simulations=max_simulations,
            evaluator=evaluator,
            solve=True,
            random_state=rng,
        )
        bots = [bot] * game.num_players()

        state = game.new_initial_state()
        while not state.is_terminal():
            player = state.current_player()
            action = bots[player].step(state)
            state.apply_action(action)

        returns.append(state.returns()[0])

    return {"returns": returns}


def main():
    max_utility = compute_max_utility(HORIZON, PLAYERS)
    print(f"Configuration: grid={GRID_SIZE}x{GRID_SIZE}, players={PLAYERS}, horizon={HORIZON}")
    print(f"MaxUtility = {max_utility:.4f}")
    print(f"Seeds: {SEEDS}")
    print(f"Simulation counts: {SIMULATION_COUNTS}")
    print(f"Games per config: {NUM_GAMES}")
    print()

    results = []

    for sims in SIMULATION_COUNTS:
        print(f"--- Simulations = {sims} ---")

        for seed in SEEDS:
            print(f"  Seed {seed}:")

            # C++ MCTS
            cpp_result = run_mcts_cpp(GRID_SIZE, PLAYERS, HORIZON, seed, sims, NUM_GAMES)
            cpp_returns = cpp_result["returns"]

            if cpp_returns:
                cpp_avg = np.mean(cpp_returns)
                cpp_best = max(cpp_returns)
                cpp_status = "EXCEEDS-BUG!" if cpp_best > max_utility + 1e-6 else ("optimal" if abs(cpp_best - max_utility) < 1e-6 else "below")
                print(f"    C++ MCTS:    avg={cpp_avg:.4f}, best={cpp_best:.4f}, status={cpp_status}")
                results.append({
                    "algorithm": "mcts_cpp", "sims": sims, "seed": seed,
                    "avg": cpp_avg, "best": cpp_best, "status": cpp_status
                })
            else:
                print(f"    C++ MCTS:    ERROR - no returns parsed")

            # Python MCTS
            py_result = run_mcts_python(GRID_SIZE, PLAYERS, HORIZON, seed, sims, NUM_GAMES)
            py_returns = py_result["returns"]

            if py_returns:
                py_avg = np.mean(py_returns)
                py_best = max(py_returns)
                py_status = "EXCEEDS-BUG!" if py_best > max_utility + 1e-6 else ("optimal" if abs(py_best - max_utility) < 1e-6 else "below")
                print(f"    Python MCTS: avg={py_avg:.4f}, best={py_best:.4f}, status={py_status}")
                results.append({
                    "algorithm": "mcts_py", "sims": sims, "seed": seed,
                    "avg": py_avg, "best": py_best, "status": py_status
                })
            else:
                print(f"    Python MCTS: ERROR - no returns")

        print()

    # Summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"MaxUtility = {max_utility:.4f}")
    print()

    # Group by algorithm and sims
    for alg in ["mcts_cpp", "mcts_py"]:
        print(f"{alg}:")
        for sims in SIMULATION_COUNTS:
            alg_results = [r for r in results if r["algorithm"] == alg and r["sims"] == sims]
            if alg_results:
                avg_of_avgs = np.mean([r["avg"] for r in alg_results])
                best_of_bests = max(r["best"] for r in alg_results)
                statuses = [r["status"] for r in alg_results]
                exceeds = sum(1 for s in statuses if s == "EXCEEDS-BUG!")
                optimal = sum(1 for s in statuses if s == "optimal")
                below = sum(1 for s in statuses if s == "below")
                status_summary = f"exceeds={exceeds}, optimal={optimal}, below={below}"
                print(f"  sims={sims:4d}: avg={avg_of_avgs:.4f}, best={best_of_bests:.4f} | {status_summary}")
        print()

    # Check for bugs
    bugs = [r for r in results if r["status"] == "EXCEEDS-BUG!"]
    if bugs:
        print("WARNING: Some results EXCEED MaxUtility - potential bug!")
        for bug in bugs:
            print(f"  {bug['algorithm']} sims={bug['sims']} seed={bug['seed']}: best={bug['best']:.4f} > {max_utility:.4f}")
    else:
        print("No results exceed MaxUtility.")


if __name__ == "__main__":
    main()
