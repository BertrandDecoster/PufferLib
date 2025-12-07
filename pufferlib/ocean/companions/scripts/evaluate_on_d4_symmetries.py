#!/usr/bin/env python3
"""Evaluate a trained Synchro model on all 8 D4 transformations.

Usage:
    python evaluate_on_d4_symmetries.py \
        --model experiments/puffer_synchro_cx9x30s2.pt \
        --rows 5 --cols 5 --num-agents 2 --num-synchro 2 \
        --complexity 0 --horizon 20 --num-episodes 10 --device mps
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import torch

# Add pufferlib to path if needed
sys.path.insert(0, str(Path(__file__).parents[4]))

from pufferlib.ocean.companions.synchro import Synchro
from pufferlib.ocean import torch as ocean_torch


D4_TRANSFORM_NAMES = [
    "Identity",
    "Rot90 CCW",
    "Rot180",
    "Rot270 CCW",
    "FlipH",
    "FlipV",
    "FlipD (transpose)",
    "FlipA (anti-diag)",
]


def evaluate_on_transform(
    policy,
    d4_transform: int,
    num_episodes: int,
    env_kwargs: dict,
    device: str,
):
    """Evaluate policy on a specific D4 transform."""
    env = Synchro(num_envs=1, d4_transform=d4_transform, **env_kwargs)
    policy.eval()

    episode_returns = []

    for ep in range(num_episodes):
        obs, _ = env.reset(seed=ep)
        done = False
        total_reward = 0.0

        while not done:
            with torch.no_grad():
                obs_tensor = torch.FloatTensor(obs).to(device)
                logits, _ = policy(obs_tensor)

                # Sample actions from logits
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

    env.close()

    return {
        "mean_return": np.mean(episode_returns),
        "std_return": np.std(episode_returns),
        "min_return": np.min(episode_returns),
        "max_return": np.max(episode_returns),
    }


def main():
    parser = argparse.ArgumentParser(
        description="Evaluate a trained Synchro model on all 8 D4 transformations"
    )
    parser.add_argument(
        "--model", type=str, required=True, help="Path to the .pt model file"
    )
    parser.add_argument("--rows", type=int, default=5, help="Grid rows")
    parser.add_argument("--cols", type=int, default=5, help="Grid cols")
    parser.add_argument("--num-agents", type=int, default=2, help="Number of agents")
    parser.add_argument("--num-synchro", type=int, default=2, help="Number of synchro goals")
    parser.add_argument("--complexity", type=int, default=0, help="Map complexity")
    parser.add_argument("--horizon", type=int, default=20, help="Episode horizon")
    parser.add_argument("--num-episodes", type=int, default=10, help="Episodes per transform")
    parser.add_argument("--device", type=str, default="cpu", help="Device: cpu, cuda, or mps")
    parser.add_argument("--hidden-size", type=int, default=256, help="Policy hidden size")
    parser.add_argument("--policy", type=str, default="Synchro", help="Policy class name")

    args = parser.parse_args()

    # Environment kwargs (excluding d4_transform which varies)
    env_kwargs = {
        "rows": args.rows,
        "cols": args.cols,
        "num_agents": args.num_agents,
        "num_synchro": args.num_synchro,
        "map_complexity": args.complexity,
        "horizon": args.horizon,
    }

    # Create a temporary env to initialize the policy
    temp_env = Synchro(num_envs=1, d4_transform=0, **env_kwargs)

    # Create and load policy
    policy_cls = getattr(ocean_torch, args.policy)
    policy = policy_cls(temp_env, hidden_size=args.hidden_size)

    # Load weights
    state_dict = torch.load(args.model, map_location=args.device)
    policy.load_state_dict(state_dict)
    policy = policy.to(args.device)
    policy.eval()

    temp_env.close()

    # Print header
    print(f"\n{'='*60}")
    print(f"Evaluating model: {args.model}")
    print(f"Environment: {args.rows}x{args.cols} grid, {args.num_agents} agents, {args.num_synchro} synchro")
    print(f"Complexity: {args.complexity}, Horizon: {args.horizon}")
    print(f"Episodes per transform: {args.num_episodes}")
    print(f"{'='*60}\n")

    # Evaluate on all 8 D4 transforms
    results = {}
    for d4 in range(8):
        result = evaluate_on_transform(
            policy, d4, args.num_episodes, env_kwargs, args.device
        )
        results[d4] = result
        print(
            f"D4={d4} ({D4_TRANSFORM_NAMES[d4]:16s}): "
            f"mean={result['mean_return']:7.2f} ± {result['std_return']:5.2f}  "
            f"[{result['min_return']:6.2f}, {result['max_return']:6.2f}]"
        )

    # Summary statistics
    means = [results[d4]["mean_return"] for d4 in range(8)]
    print(f"\n{'='*60}")
    print("Summary:")
    print(f"  Mean across transforms: {np.mean(means):.2f}")
    print(f"  Std across transforms:  {np.std(means):.2f}")
    print(f"  Min transform mean:     {np.min(means):.2f} (D4={np.argmin(means)})")
    print(f"  Max transform mean:     {np.max(means):.2f} (D4={np.argmax(means)})")

    # Degradation from identity
    baseline = results[0]["mean_return"]
    if abs(baseline) > 1e-6:
        degradations = [(baseline - results[d4]["mean_return"]) / abs(baseline) * 100 for d4 in range(1, 8)]
        print(f"  Mean degradation from D4=0: {np.mean(degradations):.1f}%")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    main()
