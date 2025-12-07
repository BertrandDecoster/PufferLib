#!/usr/bin/env python
"""Interactive viewer for trained synchro models.

Usage:
    python view_model.py --id cx9x30s2
    python view_model.py --id 7212qjtt --fps 10 --seed 42

Controls:
    RIGHT / l : Step forward
    LEFT  / h : Step backward (replay from history)
    SPACE     : Toggle pause
    r         : Reset environment
    q         : Quit
"""

import argparse
import curses
import glob
import os
import re
import sys
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

import numpy as np
import torch
import yaml


# ANSI color code to curses color pair mapping
ANSI_TO_CURSES = {
    90: 8,   # Grey (bright black)
    91: 1,   # Bright red
    92: 2,   # Bright green
    93: 3,   # Bright yellow
    94: 4,   # Bright blue
    95: 5,   # Bright magenta
    96: 6,   # Bright cyan
}


@dataclass
class Frame:
    """Stores a single frame of the simulation.

    Important: render and obs represent the state BEFORE the step that produced
    this frame's reward/done status. This allows showing the winning state.
    """
    render: str         # Rendered state BEFORE the step
    obs: np.ndarray     # Observation BEFORE the step
    reward: float       # Reward from taking action in this state
    step: int           # Step number within episode
    done: bool          # Whether this step ended the episode
    episode: int        # Episode number


def find_wandb_config(run_id: str, base_path: str = "wandb") -> Optional[str]:
    """Find wandb config.yaml for a given run ID."""
    pattern = os.path.join(base_path, f"run-*-{run_id}", "files", "config.yaml")
    matches = glob.glob(pattern)
    if matches:
        return matches[0]
    return None


def find_model_path(run_id: str, base_path: str = "experiments") -> Optional[str]:
    """Find model weights file for a given run ID."""
    # Try direct file first
    direct_path = os.path.join(base_path, f"puffer_synchro_{run_id}.pt")
    if os.path.exists(direct_path):
        return direct_path

    # Try subdirectory with latest checkpoint
    subdir = os.path.join(base_path, f"puffer_synchro_{run_id}")
    if os.path.isdir(subdir):
        checkpoints = glob.glob(os.path.join(subdir, "model_puffer_synchro_*.pt"))
        if checkpoints:
            # Return the latest checkpoint (highest number)
            return sorted(checkpoints)[-1]

    return None


def parse_wandb_config(config_path: str) -> dict:
    """Parse wandb config.yaml and extract environment/policy parameters."""
    with open(config_path, 'r') as f:
        config = yaml.safe_load(f)

    # Extract values from nested .value structure
    env_config = config.get('env', {}).get('value', {})
    policy_config = config.get('policy', {}).get('value', {})

    return {
        'rows': env_config.get('rows', 5),
        'cols': env_config.get('cols', 5),
        'num_agents': env_config.get('num_agents', 2),
        'num_synchro': env_config.get('num_synchro', 2),
        'map_complexity': env_config.get('map_complexity', 0),
        'horizon': env_config.get('horizon', 20),
        'hidden_size': policy_config.get('hidden_size', 256),
    }


def run_viewer(
    run_id: str,
    fps: float = 5.0,
    device: str = "mps",
    history_size: int = 100,
    seed: int = 0,
    wandb_path: str = "wandb",
    experiments_path: str = "experiments",
):
    """Run the interactive viewer."""
    # Find config and model
    config_path = find_wandb_config(run_id, wandb_path)
    if not config_path:
        print(f"Error: Could not find wandb config for ID '{run_id}'")
        print(f"Searched in: {wandb_path}/run-*-{run_id}/files/config.yaml")
        sys.exit(1)

    model_path = find_model_path(run_id, experiments_path)
    if not model_path:
        print(f"Error: Could not find model weights for ID '{run_id}'")
        print(f"Searched in: {experiments_path}/puffer_synchro_{run_id}.pt")
        sys.exit(1)

    print(f"Config: {config_path}")
    print(f"Model:  {model_path}")

    # Parse config
    config = parse_wandb_config(config_path)
    print(f"Environment: {config['rows']}x{config['cols']} grid, "
          f"{config['num_agents']} agents, {config['num_synchro']} synchros, "
          f"complexity={config['map_complexity']}")

    # Import here to avoid import errors when just checking help
    from pufferlib.ocean.companions.synchro import Synchro
    from pufferlib.ocean.torch import Synchro as SynchroPolicy

    # Create environment
    env = Synchro(
        num_envs=1,
        rows=config['rows'],
        cols=config['cols'],
        num_agents=config['num_agents'],
        num_synchro=config['num_synchro'],
        map_complexity=config['map_complexity'],
        horizon=config['horizon'],
        seed=seed,
    )

    # Load policy
    policy = SynchroPolicy(env, hidden_size=config['hidden_size']).to(device)
    checkpoint = torch.load(model_path, map_location=device, weights_only=True)
    policy.load_state_dict(checkpoint)
    policy.eval()

    print(f"Model loaded. Starting viewer at {fps} FPS...")
    print("Controls: LEFT/RIGHT=step, SPACE=pause, r=reset, q=quit")
    time.sleep(1)

    # Run curses-based viewer
    curses.wrapper(lambda stdscr: _curses_main(
        stdscr, env, policy, device, fps, history_size, seed, run_id, config
    ))

    env.close()


class ViewerState:
    """Mutable state for the viewer."""
    def __init__(self, env, policy, device, seed, history_size):
        self.env = env
        self.policy = policy
        self.device = device
        self.seed = seed
        self.history_size = history_size

        self.history: List[Frame] = []
        self.current_idx = 0
        self.paused = False
        self.total_reward = 0.0
        self.episode = 0
        self.obs = None  # Current observation (for next step)

        # Initialize
        self._reset_episode()

    def _reset_episode(self):
        """Reset to a new episode."""
        self.obs, _ = self.env.reset(seed=self.seed + self.episode)
        render = self.env.render()
        self.history = [Frame(
            render=render,
            obs=self.obs.copy(),
            reward=0.0,
            step=0,
            done=False,
            episode=self.episode
        )]
        self.current_idx = 0
        self.total_reward = 0.0

    def step_forward(self):
        """Step forward in time."""
        # If we're not at the end of history, just advance
        if self.current_idx < len(self.history) - 1:
            self.current_idx += 1
            return

        current_frame = self.history[self.current_idx]

        # If current episode is done, start a new one
        if current_frame.done:
            self.episode += 1
            # The env auto-reset, so self.obs is already the new episode's initial obs
            render = self.env.render()
            new_frame = Frame(
                render=render,
                obs=self.obs.copy(),
                reward=0.0,
                step=0,
                done=False,
                episode=self.episode
            )
            self._add_frame(new_frame)
            self.total_reward = 0.0
            return

        # Take a step: capture state BEFORE step
        render_before = self.env.render()
        obs_before = self.obs.copy()

        # Get action from policy
        with torch.no_grad():
            obs_tensor = torch.from_numpy(self.obs).float().to(self.device)
            action_logits, value = self.policy(obs_tensor)
            actions = np.column_stack([
                logits.argmax(dim=-1).cpu().numpy()
                for logits in action_logits
            ])

        # Execute step
        self.obs, rewards, terminals, truncations, infos = self.env.step(actions)
        reward = float(rewards.sum())
        done = bool(terminals.any() or truncations.any())

        self.total_reward += reward

        # Store frame with BEFORE state
        new_frame = Frame(
            render=render_before,
            obs=obs_before,
            reward=reward,
            step=current_frame.step + 1,
            done=done,
            episode=self.episode
        )
        self._add_frame(new_frame)

    def _add_frame(self, frame: Frame):
        """Add a frame to history, respecting history_size limit."""
        self.history.append(frame)
        if len(self.history) > self.history_size:
            self.history.pop(0)
        self.current_idx = len(self.history) - 1

    def step_backward(self):
        """Step backward in time (replay from history)."""
        if self.current_idx > 0:
            self.current_idx -= 1

    def reset(self):
        """Force reset to new episode."""
        self.episode += 1
        self._reset_episode()

    def toggle_pause(self):
        """Toggle pause state."""
        self.paused = not self.paused

    def current_frame(self) -> Frame:
        """Get current frame."""
        return self.history[self.current_idx]

    def is_at_end(self) -> bool:
        """Check if we're at the end of history."""
        return self.current_idx == len(self.history) - 1

    def get_total_reward_for_episode(self, target_episode: int) -> float:
        """Calculate total reward for a specific episode up to current frame."""
        total = 0.0
        for i in range(self.current_idx + 1):
            frame = self.history[i]
            if frame.episode == target_episode:
                total += frame.reward
        return total


def _init_colors():
    """Initialize curses color pairs."""
    curses.start_color()
    curses.use_default_colors()

    # Define color pairs: pair_number, foreground, background
    curses.init_pair(1, curses.COLOR_RED, -1)      # Red
    curses.init_pair(2, curses.COLOR_GREEN, -1)    # Green
    curses.init_pair(3, curses.COLOR_YELLOW, -1)   # Yellow
    curses.init_pair(4, curses.COLOR_BLUE, -1)     # Blue
    curses.init_pair(5, curses.COLOR_MAGENTA, -1)  # Magenta
    curses.init_pair(6, curses.COLOR_CYAN, -1)     # Cyan
    curses.init_pair(7, curses.COLOR_WHITE, -1)    # White
    curses.init_pair(8, curses.COLOR_BLACK, -1)    # Grey (will be bright)

    # Special pair for winning state highlight (red background)
    curses.init_pair(9, curses.COLOR_WHITE, curses.COLOR_RED)


def _curses_main(
    stdscr,
    env,
    policy,
    device: str,
    fps: float,
    history_size: int,
    seed: int,
    run_id: str,
    config: dict,
):
    """Main curses loop."""
    # Setup curses
    curses.curs_set(0)  # Hide cursor
    stdscr.nodelay(True)  # Non-blocking input
    stdscr.timeout(int(1000 / fps))  # Timeout for getch
    _init_colors()

    # Initialize state
    state = ViewerState(env, policy, device, seed, history_size)

    while True:
        # Handle input
        key = stdscr.getch()

        if key == ord('q'):
            break
        elif key == ord(' '):
            state.toggle_pause()
        elif key == ord('r'):
            state.reset()
        elif key == curses.KEY_RIGHT or key == ord('l'):
            state.step_forward()
        elif key == curses.KEY_LEFT or key == ord('h'):
            state.step_backward()

        # Auto-advance if not paused and at end of history
        if not state.paused and state.is_at_end():
            state.step_forward()

        # Display current frame
        _display_frame(stdscr, state, run_id, config, fps)


def _parse_ansi_line(line: str) -> List[Tuple[str, int]]:
    """Parse a line with ANSI codes into (text, color_pair) segments."""
    segments = []
    current_color = 0  # Default color

    # Pattern to match ANSI escape sequences
    pattern = re.compile(r'\033\[(\d+)m')

    pos = 0
    for match in pattern.finditer(line):
        # Add text before this escape sequence
        if match.start() > pos:
            text = line[pos:match.start()]
            if text:
                segments.append((text, current_color))

        # Update color based on escape code
        code = int(match.group(1))
        if code == 0:
            current_color = 0  # Reset
        elif code in ANSI_TO_CURSES:
            current_color = ANSI_TO_CURSES[code]

        pos = match.end()

    # Add remaining text
    if pos < len(line):
        text = line[pos:]
        if text:
            segments.append((text, current_color))

    return segments


def _render_colored_line(stdscr, row: int, line: str, width: int, highlight_synchro: bool = False):
    """Render a line with ANSI colors using curses."""
    segments = _parse_ansi_line(line)

    col = 0
    for text, color_pair in segments:
        if col >= width - 1:
            break

        # Truncate if needed
        remaining = width - 1 - col
        display_text = text[:remaining]

        try:
            # Check if this is a synchro cell (" S") and we want to highlight
            if highlight_synchro and 'S' in display_text:
                # Highlight synchro cells with red background
                attr = curses.color_pair(9) | curses.A_BOLD
            elif color_pair > 0:
                attr = curses.color_pair(color_pair)
                if color_pair == 8:  # Grey needs bright attribute
                    attr |= curses.A_DIM
            else:
                attr = 0

            stdscr.addstr(row, col, display_text, attr)
        except curses.error:
            pass

        col += len(display_text)


def _display_frame(
    stdscr,
    state: ViewerState,
    run_id: str,
    config: dict,
    fps: float,
):
    """Display a frame using curses with color support."""
    stdscr.clear()

    height, width = stdscr.getmaxyx()
    frame = state.current_frame()

    # Line 1: Static info (model, config)
    static_info = (f"Model: {run_id}  |  "
                   f"{config['rows']}x{config['cols']} grid, "
                   f"{config['num_agents']} agents, "
                   f"{config['num_synchro']} synchros")

    # Line 2: Dynamic info (episode, step, frame)
    status = "PAUSED" if state.paused else "PLAYING"
    episode_reward = state.get_total_reward_for_episode(frame.episode)

    dynamic_info = (f"Episode: {frame.episode}  Step: {frame.step}  "
                    f"Frame: {state.current_idx+1}/{len(state.history)}  "
                    f"[{status}]  Reward: {episode_reward:.2f}")

    # Line 3: Level complete indicator if done
    if frame.done:
        complete_str = "*** LEVEL COMPLETE! ***"
    else:
        complete_str = ""

    try:
        stdscr.addstr(0, 0, static_info[:width-1])
        stdscr.addstr(1, 0, dynamic_info[:width-1])
        if complete_str:
            stdscr.addstr(2, 0, complete_str[:width-1], curses.color_pair(2) | curses.A_BOLD)
            grid_start_row = 4
        else:
            grid_start_row = 3

        stdscr.addstr(grid_start_row - 1, 0, "-" * min(60, width-1))

        # Render grid with colors
        lines = frame.render.split('\n')
        for i, line in enumerate(lines):
            if grid_start_row + i < height - 2:
                # Highlight synchro cells if this is a winning frame
                _render_colored_line(stdscr, grid_start_row + i, line, width,
                                    highlight_synchro=frame.done)

        # Footer
        footer = "LEFT/RIGHT=step  SPACE=pause  r=reset  q=quit"
        if height > grid_start_row + len(lines) + 1:
            stdscr.addstr(height - 1, 0, footer[:width-1])
    except curses.error:
        pass  # Ignore errors from writing to edge of screen

    stdscr.refresh()


def main():
    parser = argparse.ArgumentParser(
        description="Interactive viewer for trained synchro models",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--id", type=str, required=True,
                        help="Run ID (e.g., 'cx9x30s2', '7212qjtt')")
    parser.add_argument("--fps", type=float, default=5.0,
                        help="Playback speed in frames per second (default: 5)")
    parser.add_argument("--device", type=str, default="mps",
                        help="Torch device (default: mps)")
    parser.add_argument("--history-size", type=int, default=100,
                        help="Number of frames to store for backward stepping (default: 100)")
    parser.add_argument("--seed", type=int, default=0,
                        help="Environment seed (default: 0)")
    parser.add_argument("--wandb-path", type=str, default="wandb",
                        help="Path to wandb directory (default: wandb)")
    parser.add_argument("--experiments-path", type=str, default="experiments",
                        help="Path to experiments directory (default: experiments)")

    args = parser.parse_args()

    run_viewer(
        run_id=args.id,
        fps=args.fps,
        device=args.device,
        history_size=args.history_size,
        seed=args.seed,
        wandb_path=args.wandb_path,
        experiments_path=args.experiments_path,
    )


if __name__ == "__main__":
    main()
