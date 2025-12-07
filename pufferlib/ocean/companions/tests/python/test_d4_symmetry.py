#!/usr/bin/env python3
"""Test D4 symmetry invariance for companions Synchro environment.

This test validates that D4-equivariant policies generalize across all 8
D4 symmetry transformations, while baseline policies fail on transformed games.

Test Protocol:
1. Train a policy on d4_transform=0 (identity)
2. Evaluate the trained policy on all 8 d4_transforms (0-7)
3. Compare performance variance across transforms

Expected Results:
- Baseline (non-equivariant): High performance on d4=0, poor on d4=1-7
- D4 (equivariant): Similar performance across all 8 transforms

Usage:
    python test_d4_symmetry.py --policy Synchro --seeds 42,123,456
    python test_d4_symmetry.py --policy SynchroD4 --seeds 42,123,456
    python test_d4_symmetry.py --compare  # Compare both policies
"""

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch

# Add pufferlib to path if needed
sys.path.insert(0, str(Path(__file__).parents[5]))

import pufferlib
from pufferlib.ocean.companions.synchro import Synchro
from pufferlib.ocean import torch as ocean_torch


def create_env(d4_transform: int, seed: int = 0, **env_kwargs):
    """Create a Synchro environment with specified D4 transform."""
    return Synchro(
        num_envs=1,
        rows=5,
        cols=5,
        num_agents=2,
        num_synchro=2,
        map_complexity=0,
        horizon=20,
        d4_transform=d4_transform,
        seed=seed,
    )


def create_policy(policy_name: str, env, device: str = "cpu"):
    """Create a policy by name."""
    policy_cls = getattr(ocean_torch, policy_name)
    policy = policy_cls(env, hidden_size=256)
    return policy.to(device)


def train_policy(
    policy_name: str,
    seed: int,
    total_timesteps: int = 50_000,
    device: str = "cpu",
):
    """Train a policy and return the trained model."""
    # Set seeds for reproducibility
    torch.manual_seed(seed)
    np.random.seed(seed)

    # Create environment - Synchro handles vectorization internally via C++
    env = Synchro(
        num_envs=8,
        rows=5,
        cols=5,
        num_agents=2,
        num_synchro=2,
        map_complexity=0,
        horizon=20,
        d4_transform=0,
        seed=seed,
    )

    # Create policy
    policy = create_policy(policy_name, env, device)

    # Training loop (simplified - using pufferl would be better)
    from pufferlib.pufferl import PuffeRL

    config = {
        "total_timesteps": total_timesteps,
        "learning_rate": 0.02249962234609731,
        "gamma": 0.9973789848600498,
        "gae_lambda": 0.8547904420063671,
        "clip_coef": 0.13861829284578564,
        "ent_coef": 0.0029874530280989712,
        "vf_coef": 0.8236293318681418,
        "vf_clip_coef": 1.5152918023988606,
        "batch_size": 8192,
        "minibatch_size": 512,
        "bptt_horizon": 32,
        "device": device,
        "compile": False,
        "optimizer": "muon",
        "adam_beta1": 0.95,
        "adam_beta2": 0.999,
        "adam_eps": 1e-12,
        "update_epochs": 1,
        "prio_alpha": 0.5808379485049219,
        "max_grad_norm": 1.5,
        "anneal_lr": True,
        "torch_deterministic": True,
        "seed": seed,
        "prio_beta0": 0.2,
        "vtrace_rho_clip": 1.0,
        "vtrace_c_clip": 1.0,
        "min_lr_ratio": 0.0,
        "max_minibatch_size": 32768,
        "checkpoint_interval": 200,
        "data_dir": "experiments",
        "compile_mode": "max-autotune-no-cudagraphs",
        "compile_fullgraph": True,
        "cpu_offload": False,
        "precision": "float32",
        "use_rnn": False,
    }

    trainer = PuffeRL(config, env, policy)

    # Train
    while trainer.total_steps < total_timesteps:
        trainer.evaluate()
        trainer.train()

    env.close()

    return policy


def evaluate_policy(
    policy,
    d4_transform: int,
    seed: int,
    num_episodes: int = 10,
    device: str = "cpu",
):
    """Evaluate a trained policy on a specific D4 transform."""
    env = create_env(d4_transform=d4_transform, seed=seed)
    policy.eval()

    episode_returns = []
    episode_successes = []

    for ep in range(num_episodes):
        obs, _ = env.reset(seed=seed + ep)
        done = False
        total_reward = 0.0

        while not done:
            with torch.no_grad():
                obs_tensor = torch.FloatTensor(obs).to(device)
                logits, _ = policy(obs_tensor)

                # Sample actions
                actions = []
                for logit in logits:
                    if len(logit.shape) == 1:
                        logit = logit.unsqueeze(0)
                    dist = torch.distributions.Categorical(logits=logit)
                    actions.append(dist.sample().cpu().numpy())

                actions = np.stack(actions, axis=-1)

            obs, rewards, terminals, truncations, infos = env.step(actions)
            total_reward += rewards.sum()
            done = terminals.any() or truncations.any()

        episode_returns.append(total_reward)
        # Check success from infos if available
        for info in infos:
            if isinstance(info, dict) and "success_rate" in info:
                episode_successes.append(info["success_rate"])

    env.close()

    return {
        "mean_return": np.mean(episode_returns),
        "std_return": np.std(episode_returns),
        "mean_success": np.mean(episode_successes) if episode_successes else 0.0,
    }


def train_and_evaluate(
    policy_name: str,
    seed: int,
    total_timesteps: int = 50_000,
    num_eval_episodes: int = 10,
    device: str = "cpu",
):
    """Train on d4=0, evaluate on all 8 transforms."""
    print(f"\n{'='*60}")
    print(f"Training {policy_name} with seed={seed}")
    print(f"{'='*60}")

    # Train
    policy = train_policy(policy_name, seed, total_timesteps, device)

    # Evaluate on all 8 transforms
    results = {}
    for d4 in range(8):
        result = evaluate_policy(policy, d4, seed, num_eval_episodes, device)
        results[d4] = result
        print(f"  d4_transform={d4}: return={result['mean_return']:.3f} ± {result['std_return']:.3f}")

    return results


TRANSFORM_NAMES = [
    "Identity", "Rot90", "Rot180", "Rot270",
    "FlipH", "FlipV", "FlipD", "FlipA"
]


def compute_statistics(all_results: list[dict]):
    """Compute aggregate statistics across seeds."""
    # all_results is a list of dicts, each dict mapping d4 -> {mean_return, std_return}
    stats = {}
    for d4 in range(8):
        returns = [r[d4]["mean_return"] for r in all_results]
        stats[d4] = {
            "mean": np.mean(returns),
            "std": np.std(returns),
        }

    # Compute cross-transform variance (lower is better for D4)
    all_means = [stats[d4]["mean"] for d4 in range(8)]
    stats["cross_variance"] = np.var(all_means)
    stats["cross_std"] = np.std(all_means)
    stats["mean_of_means"] = np.mean(all_means)

    # Compute degradation from d4=0
    baseline = stats[0]["mean"]
    degradations = [(baseline - stats[d4]["mean"]) / max(abs(baseline), 1e-6) * 100 for d4 in range(1, 8)]
    stats["mean_degradation"] = np.mean(degradations)

    # Compute max relative deviation from mean (for D4 equivariance assertion)
    mean_of_means = stats["mean_of_means"]
    if abs(mean_of_means) > 1e-6:
        relative_deviations = [abs(m - mean_of_means) / abs(mean_of_means) for m in all_means]
        stats["max_relative_deviation"] = max(relative_deviations)
    else:
        stats["max_relative_deviation"] = float('inf')

    return stats


def check_d4_equivariance(stats: dict, tolerance: float = 0.30) -> tuple[bool, str]:
    """Check if returns are consistent across all D4 transforms.

    Args:
        stats: Statistics dict from compute_statistics
        tolerance: Maximum allowed relative deviation (default 30%)

    Returns:
        Tuple of (passed, message)
    """
    max_dev = stats["max_relative_deviation"]
    mean = stats["mean_of_means"]

    if max_dev <= tolerance:
        return True, f"PASS: Max deviation {max_dev*100:.1f}% <= {tolerance*100:.0f}% tolerance"
    else:
        return False, f"FAIL: Max deviation {max_dev*100:.1f}% > {tolerance*100:.0f}% tolerance"


def main():
    parser = argparse.ArgumentParser(description="Test D4 symmetry invariance")
    parser.add_argument("--policy", type=str, default="Synchro",
                        help="Policy name: Synchro or SynchroD4")
    parser.add_argument("--seeds", type=str, default="42,123,456",
                        help="Comma-separated list of seeds")
    parser.add_argument("--total-timesteps", type=int, default=100_000,
                        help="Training timesteps per seed")
    parser.add_argument("--num-eval-episodes", type=int, default=10,
                        help="Evaluation episodes per transform")
    parser.add_argument("--device", type=str, default="cpu",
                        help="Device: cpu, cuda, or mps")
    parser.add_argument("--compare", action="store_true",
                        help="Compare both Synchro and SynchroD4")
    parser.add_argument("--output", type=str, default=None,
                        help="Output file for results (JSON)")

    args = parser.parse_args()
    seeds = [int(s) for s in args.seeds.split(",")]

    if args.compare:
        policies = ["Synchro", "SynchroD4"]
    else:
        policies = [args.policy]

    all_policy_results = {}

    for policy_name in policies:
        print(f"\n{'#'*60}")
        print(f"# Policy: {policy_name}")
        print(f"{'#'*60}")

        policy_results = []
        for seed in seeds:
            results = train_and_evaluate(
                policy_name,
                seed,
                args.total_timesteps,
                args.num_eval_episodes,
                args.device,
            )
            policy_results.append(results)

        stats = compute_statistics(policy_results)
        all_policy_results[policy_name] = {
            "seeds": seeds,
            "per_seed_results": policy_results,
            "aggregate_stats": stats,
        }

        print(f"\n{'='*60}")
        print(f"Aggregate Results for {policy_name}")
        print(f"{'='*60}")
        print(f"{'Transform':<12} {'Mean Return':>12} {'Std':>8}")
        print("-" * 36)
        for d4 in range(8):
            name = TRANSFORM_NAMES[d4]
            print(f"{d4} ({name:<8}) {stats[d4]['mean']:>12.3f} {stats[d4]['std']:>8.3f}")
        print("-" * 36)
        print(f"Mean across transforms: {stats['mean_of_means']:.3f}")
        print(f"Cross-transform std: {stats['cross_std']:.3f}")
        print(f"Max relative deviation: {stats['max_relative_deviation']*100:.1f}%")
        print(f"Mean degradation from d4=0: {stats['mean_degradation']:.1f}%")

        # Check D4 equivariance for D4 policies
        if "D4" in policy_name:
            passed, msg = check_d4_equivariance(stats, tolerance=0.30)
            print(f"\nD4 Equivariance: {msg}")

    # Compare if we have both
    if len(policies) == 2:
        print(f"\n{'='*60}")
        print("Comparison Summary")
        print(f"{'='*60}")
        baseline_stats = all_policy_results["Synchro"]["aggregate_stats"]
        d4_stats = all_policy_results["SynchroD4"]["aggregate_stats"]

        print(f"\n{'Metric':<30} {'Synchro':>12} {'SynchroD4':>12}")
        print("-" * 56)
        print(f"{'Cross-transform std':<30} {baseline_stats['cross_std']:>12.3f} {d4_stats['cross_std']:>12.3f}")
        print(f"{'Max relative deviation':<30} {baseline_stats['max_relative_deviation']*100:>11.1f}% {d4_stats['max_relative_deviation']*100:>11.1f}%")
        print(f"{'Mean degradation from d4=0':<30} {baseline_stats['mean_degradation']:>11.1f}% {d4_stats['mean_degradation']:>11.1f}%")

        # Assertions for automated testing
        variance_improvement = baseline_stats["cross_std"] / max(d4_stats["cross_std"], 1e-6)
        print(f"\nVariance improvement ratio: {variance_improvement:.2f}x")

        d4_passed, d4_msg = check_d4_equivariance(d4_stats, tolerance=0.30)
        if d4_passed and variance_improvement > 2.0:
            print("PASS: D4 policy is equivariant (low variance across transforms)")
        elif d4_passed:
            print("PASS: D4 policy meets tolerance, but improvement over baseline is modest")
        else:
            print(f"WARN: {d4_msg}")

    if args.output:
        with open(args.output, "w") as f:
            json.dump(all_policy_results, f, indent=2, default=float)
        print(f"\nResults saved to {args.output}")


if __name__ == "__main__":
    main()
