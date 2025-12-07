import time
import numpy as np
from pufferlib.ocean import env_creator
from pufferlib.vector import make, Serial

TIMEOUT = 10  # seconds
NUM_ENVS = 8  # Match training config

print("=== Testing vectorized env (Serial backend) ===")
make_env = env_creator('puffer_synchro')
vecenv = make(make_env, num_envs=NUM_ENVS, backend=Serial)

print(f"Observation shape: {vecenv.single_observation_space.shape}")
print(f"Action shape: {vecenv.single_action_space.shape}")
print(f"num_envs: {vecenv.num_envs}")

actions = [vecenv.action_space.sample() for _ in range(100)]
agent_steps = 0

vecenv.reset()
start = time.time()
while time.time() - start < TIMEOUT:
    vecenv.send(actions[agent_steps % 100])
    o, r, d, t, i, env_id, mask = vecenv.recv()
    agent_steps += sum(mask)

elapsed = time.time() - start
sps = agent_steps / elapsed
vecenv.close()

print(f"\nAgent steps: {agent_steps}")
print(f"SPS (agent steps/sec): {sps:.1f}")