#!/usr/bin/env python
"""Evaluate a synchro checkpoint on multiple levels and report average episode return."""

import argparse
import torch
import numpy as np

from pufferlib.ocean.companions.synchro import Synchro


def evaluate(
    checkpoint_path: str,
    num_levels: int = 10,
    rows: int = 5,
    cols: int = 5,
    num_agents: int = 2,
    num_synchro: int = 2,
    map_complexity: int = 0,
    horizon: int = 20,
    device: str = "mps",
    hidden_size: int = 256,
):
    """Run evaluation on multiple levels and return average episode return."""

    # Create a dummy env to initialize policy (get obs/action space)
    dummy_env = Synchro(
        num_envs=1,
        rows=rows,
        cols=cols,
        num_agents=num_agents,
        num_synchro=num_synchro,
        map_complexity=map_complexity,
        horizon=horizon,
        seed=0,
    )

    # Load policy
    from pufferlib.ocean.torch import Synchro as SynchroPolicy
    policy = SynchroPolicy(dummy_env, hidden_size=hidden_size).to(device)

    # Load checkpoint
    checkpoint = torch.load(checkpoint_path, map_location=device, weights_only=True)
    policy.load_state_dict(checkpoint)
    policy.eval()

    dummy_env.close()

    episode_returns = []
    success_count = 0

    for level_seed in range(num_levels):
        # Create environment for this level
        env = Synchro(
            num_envs=1,
            rows=rows,
            cols=cols,
            num_agents=num_agents,
            num_synchro=num_synchro,
            map_complexity=map_complexity,
            horizon=horizon,
            seed=level_seed,
        )

        obs, _ = env.reset(seed=level_seed)
        episode_return = 0.0
        done = False

        while not done:
            # Get actions from policy
            with torch.no_grad():
                obs_tensor = torch.from_numpy(obs).float().to(device)
                action_logits, value = policy(obs_tensor)
                # action_logits is tuple of (movement_logits, interact_logits)
                # Sample argmax for deterministic eval
                actions = np.column_stack([
                    logits.argmax(dim=-1).cpu().numpy()
                    for logits in action_logits
                ])

            obs, rewards, terminals, truncations, infos = env.step(actions)
            episode_return += rewards.sum()

            # Episode ends when any agent is terminal or truncated
            done = terminals.any() or truncations.any()

        env.close()

        # Check if successful (all agents reached synchro)
        success = episode_return > 0
        if success:
            success_count += 1

        episode_returns.append(episode_return)
        print(f"Level {level_seed}: return={episode_return:.2f}, success={success}")

    avg_return = np.mean(episode_returns)
    success_rate = success_count / num_levels

    print(f"\n=== Results over {num_levels} levels ===")
    print(f"Average episode return: {avg_return:.2f}")
    print(f"Success rate: {success_rate*100:.1f}%")
    print(f"Returns: {episode_returns}")

    return avg_return, success_rate


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Evaluate synchro checkpoint")
    parser.add_argument("--checkpoint", type=str, required=True, help="Path to checkpoint .pt file")
    parser.add_argument("--num-levels", type=int, default=10, help="Number of levels to evaluate")
    parser.add_argument("--rows", type=int, default=5, help="Grid rows")
    parser.add_argument("--cols", type=int, default=5, help="Grid cols")
    parser.add_argument("--num-agents", type=int, default=2, help="Number of agents")
    parser.add_argument("--num-synchro", type=int, default=2, help="Number of synchro cells")
    parser.add_argument("--map-complexity", type=int, default=0, help="Map complexity (0-5)")
    parser.add_argument("--horizon", type=int, default=20, help="Episode horizon")
    parser.add_argument("--device", type=str, default="mps", help="Device (cpu, cuda, mps)")
    parser.add_argument("--hidden-size", type=int, default=256, help="Policy hidden size")

    args = parser.parse_args()

    evaluate(
        checkpoint_path=args.checkpoint,
        num_levels=args.num_levels,
        rows=args.rows,
        cols=args.cols,
        num_agents=args.num_agents,
        num_synchro=args.num_synchro,
        map_complexity=args.map_complexity,
        horizon=args.horizon,
        device=args.device,
        hidden_size=args.hidden_size,
    )
