"""PufferLib wrapper for Companions SynchroEnv."""

import gymnasium
import numpy as np

import pufferlib
from pufferlib.ocean.companions import binding

# Observation tensor: 5 channels x rows x cols
# Plane 0: Floor cells (1.0 if walkable)
# Plane 1: Wall cells (1.0 if wall)
# Plane 2: Synchro cells (1.0 if goal)
# Plane 3: Current player position (1.0 at own position)
# Plane 4: Other agents positions (1.0 at teammate positions)
NUM_CHANNELS = 5


class Synchro(pufferlib.PufferEnv):
    """Companions SynchroEnv - cooperative goal-reaching task.

    Agents must coordinate to reach synchro cells simultaneously.
    """

    def __init__(
        self,
        num_envs: int = 1,
        rows: int = 12,
        cols: int = 12,
        num_agents: int = 3,  # companions per environment
        num_synchro: int = 3,
        map_complexity: int = 0,
        horizon: int = 100,
        report_interval: int = 128,
        render_mode: str = None,
        buf=None,
        seed: int = 0,
    ):
        self.report_interval = report_interval
        self.render_mode = render_mode
        self.agents_per_env = num_agents
        self.num_agents = num_envs * num_agents  # Total agents across all envs
        self.rows = rows
        self.cols = cols

        # 3D observation tensor: [channels, rows, cols] per agent
        self.single_observation_space = gymnasium.spaces.Box(
            low=0.0, high=1.0, shape=(NUM_CHANNELS, rows, cols), dtype=np.float32
        )

        # MultiDiscrete: [movement(5), interact(2)]
        # movement: 0=Stay, 1=Up, 2=Down, 3=Left, 4=Right
        # interact: 0=None, 1=Attack
        self.single_action_space = gymnasium.spaces.MultiDiscrete([5, 2], dtype=np.int32)

        super().__init__(buf=buf)

        # Initialize environments
        c_envs = []
        for i in range(num_envs):
            start_idx = i * num_agents
            end_idx = start_idx + num_agents

            env_seed = i + seed * num_envs
            env_id = binding.env_init(
                self.observations[start_idx:end_idx],
                self.actions[start_idx:end_idx],
                self.rewards[start_idx:end_idx],
                self.terminals[start_idx:end_idx],
                self.truncations[start_idx:end_idx],
                env_seed,
                rows=rows,
                cols=cols,
                num_agents=num_agents,
                num_synchro=num_synchro,
                map_complexity=map_complexity,
                horizon=horizon,
            )
            c_envs.append(env_id)

        self.c_envs = binding.vectorize(*c_envs)
        self.tick = 0

    def reset(self, seed=None):
        if seed is None:
            seed = 0
        binding.vec_reset(self.c_envs, seed)
        self.tick = 0
        return self.observations, []

    def step(self, actions):
        self.actions[:] = actions
        self.tick += 1
        binding.vec_step(self.c_envs)

        infos = []
        if self.tick % self.report_interval == 0:
            log = binding.vec_log(self.c_envs)
            if log:
                infos.append(log)

        return (
            self.observations,
            self.rewards,
            self.terminals,
            self.truncations,
            infos,
        )

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)


if __name__ == "__main__":
    import time

    print("Testing Synchro environment...")

    # Create environment
    num_envs = 1
    agents_per_env = 3
    env = Synchro(num_envs=num_envs, num_agents=agents_per_env)

    print(f"  num_agents: {env.num_agents}")
    print(f"  observation_space: {env.single_observation_space}")
    print(f"  action_space: {env.single_action_space}")

    # Reset
    obs, info = env.reset()
    print(f"  obs shape: {obs.shape}")

    # Run 1000 random steps
    num_steps = 1000
    start = time.time()

    for step in range(num_steps):
        # MultiDiscrete actions: [movement, interact] per agent
        # Shape: [num_agents, 2]
        actions = np.column_stack([
            np.random.randint(0, 5, env.num_agents),  # movement
            np.random.randint(0, 2, env.num_agents),  # interact
        ])
        obs, rewards, terminals, truncations, infos = env.step(actions)

    elapsed = time.time() - start
    sps = (num_steps * env.num_agents) / elapsed

    print(f"  Completed {num_steps} steps")
    print(f"  Steps per second: {sps:.0f}")
    print("Success!")

    env.close()
