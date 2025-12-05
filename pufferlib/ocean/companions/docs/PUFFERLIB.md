# PufferLib Internals Reference

This document captures key implementation details of PufferLib's PPO training system for reference when developing companions integration.

---

## 1. PPO Improvements over CleanRL

PufferLib extends CleanRL's reference PPO with production-grade optimizations:

| Feature | CleanRL | PufferLib |
|---------|---------|-----------|
| Advantage computation | Python GAE | Custom C++/CUDA V-trace kernel |
| Experience sampling | Random | Priority-weighted by \|advantage\|^alpha |
| LR schedule | Linear decay → 0 | Cosine annealing with `min_lr_ratio` |
| Optimizer | Adam only | Adam + Muon (second-order) |
| Mixed precision | No | Optional AMP (bfloat16/float32) |
| JIT compilation | No | `torch.compile` support |
| Distributed | No | Full DDP with torchrun |
| Value clipping | Same as policy clip | Separate `vf_clip_coef` |
| Gradient accumulation | No | Configurable via `accumulate_minibatches` |

### V-Trace Advantage Kernel

Located in `pufferlib/extensions/pufferlib.cpp` (CPU) and `pufferlib/extensions/cuda/pufferlib.cu` (CUDA):

```c
float rho_t = fminf(importance[t], rho_clip);  // Importance clipping for TD error
float c_t = fminf(importance[t], c_clip);       // Importance clipping for accumulation
float delta = rho_t * (rewards[t+1] + gamma * values[t+1] * nextnonterminal - values[t]);
lastpufferlam = delta + gamma * lambda * c_t * lastpufferlam * nextnonterminal;
```

This enables off-policy corrections when policy ratios drift during multiple update epochs.

### Prioritized Experience Sampling

Within a single on-policy batch (no replay buffer):
1. Compute advantages for all transitions
2. Sample minibatches weighted by `|advantage|^alpha`
3. Apply importance correction with annealing `beta` (from `beta0` → 1.0)

Key config params: `prio_alpha`, `prio_beta0`

---

## 2. Multi-Agent: IPPO with Parameter Sharing

PufferLib uses **Independent PPO (IPPO) with parameter sharing**:

- **Shared Policy**: All agents use the same network weights
- **Local Observations**: Each agent only sees its own view
- **Local Value Function**: `V(local_obs)`, not `V(global_state)`
- **Not MAPPO**: No centralized critic with privileged global information

```python
# All agents' observations batched together, processed by same policy
actions, logprob, value = policy(observations)  # observations: [num_agents * num_envs, obs_dim]
```

This approach is simpler and works well for homogeneous agents.

---

## 3. Observation Processing Pattern (MOBA Example)

For environments with mixed observation types (spatial + scalar), use a multi-stream encoder.

### MOBA Observation Structure

```
Flat tensor: [484 spatial values | 26 scalar values]
                      ↓                    ↓
              Reshape to (11,11,4)    Keep as vector
```

### The 4 Spatial Channels

| Channel | Content | Processing |
|---------|---------|------------|
| 0 | Tile type (0-15) | One-hot encode → 16 channels |
| 1-3 | Continuous values | Normalize /255 → 3 channels |

### Why One-Hot Encoding?

Tile types are **categorical** (empty, wall, player, enemy, tower, etc.):
- Without one-hot: CNN sees `tower(4)` as "twice" `enemy(2)` - meaningless
- With one-hot: Each type becomes a separate binary channel

```python
# From pufferlib/ocean/torch.py:412-418
cnn_features = observations[:, :-26].view(-1, 11, 11, 4).long()
map_features = F.one_hot(cnn_features[:, :, :, 0], 16).permute(0, 3, 1, 2).float()
extra_map_features = (cnn_features[:, :, :, -3:].float() / 255).permute(0, 3, 1, 2)
cnn_features = torch.cat([map_features, extra_map_features], dim=1)  # 19 channels
```

---

## 4. Policy Architecture Pattern (MOBA Example)

Multi-stream architecture for mixed observations:

```
Observation (flat tensor)
    │
    ├─► Spatial (484) → reshape(11,11,4) → one-hot → CNN → 128-dim
    │
    └─► Scalar (26) → Linear(26, 128) → 128-dim
                │
                ▼
        Concatenate [256-dim]
                │
                ▼
        Projection → hidden_size
                │
        ┌───────┴───────┐
        ▼               ▼
     Actor           Critic
```

### CNN Architecture (for small 11x11 inputs)

```python
self.cnn = nn.Sequential(
    layer_init(nn.Conv2d(19, 128, kernel_size=5, stride=3)),  # 11x11 → 3x3
    nn.ReLU(),
    layer_init(nn.Conv2d(128, 128, kernel_size=3, stride=1)), # 3x3 → 1x1
    nn.Flatten(),  # → 128
)
```

Only 2 conv layers needed for small spatial inputs.

---

## 5. Neural Network Configuration

### Config File Structure

```ini
[base]
package = ocean
env_name = puffer_myenv
policy_name = MyPolicy      # Class name from pufferlib.ocean.torch
rnn_name = Recurrent        # Optional: wrap with LSTM

[policy]
hidden_size = 256           # Policy-specific kwargs
cnn_channels = 64

[rnn]
input_size = 256            # Output size of policy.encode_observations()
hidden_size = 256           # LSTM hidden state dimension
```

### Available Policy Classes

| Class | Description | Location |
|-------|-------------|----------|
| `Policy` | Simple dense (flatten → linear → GELU) | `pufferlib/models.py` |
| `Conv` | NatureCNN (3-layer CNN for images) | `pufferlib/models.py` |
| `Recurrent` | LSTM wrapper for any policy | `pufferlib/models.py` |
| `ProcgenResnet` | Impala-style ResNet | `pufferlib/models.py` |
| Environment-specific | Snake, NMMO3, MOBA, Grid, etc. | `pufferlib/ocean/torch.py` |

### Policy Loading (pufferl.py:1179-1193)

```python
policy_cls = getattr(env_module.torch, args['policy_name'])
policy = policy_cls(vecenv.driver_env, **args['policy'])
if args['rnn_name']:
    rnn_cls = getattr(env_module.torch, args['rnn_name'])
    policy = rnn_cls(policy, **args['rnn'])
```

### Custom Policy Interface

To enable LSTM wrapping, implement:
```python
class MyPolicy(nn.Module):
    def encode_observations(self, observations, state=None):
        # Return: hidden_features (fed to LSTM if wrapped)

    def decode_actions(self, hidden):
        # Return: (action_logits, value)
```

---

## 6. Key File References

| Component | File |
|-----------|------|
| Main PPO trainer | `pufferlib/pufferl.py` |
| Default policies | `pufferlib/models.py` |
| Ocean env policies | `pufferlib/ocean/torch.py` |
| C++ advantage kernel | `pufferlib/extensions/pufferlib.cpp` |
| CUDA advantage kernel | `pufferlib/extensions/cuda/pufferlib.cu` |
| Default config | `pufferlib/config/default.ini` |
| Layer init utilities | `pufferlib/pytorch.py` |

---

## 7. Auto-Reset Mechanism

PufferLib has two auto-reset patterns depending on environment type:

### Standard Gym Environments (Python-level reset)

For non-Ocean environments, auto-reset happens in `pufferlib/vector.py:144-145`:

```python
# In Serial.send()
if env.done:
    o, i = env.reset()
```

The `PufferEnv.done` property (line 83 in `pufferlib.py`) signals when reset is needed.

### Ocean Environments (C-level reset)

Ocean environments handle auto-reset **internally in `c_step()`** for performance:

```c
// In synchro_wrapper.cc:92-115
if (result.done) {
    // Log episode stats
    env->log.episode_return += env->cumulative_reward;
    env->log.episode_length += (float)env->episode_steps;
    env->log.n += 1.0f;

    // Auto-reset immediately
    cpp_env->Reset();
    // Populate fresh observations for next episode
    for (int i = 0; i < env->num_agents; i++) {
        cpp_env->ObservationTensor(obs, i);
        memcpy(env->observations + i * obs_size, obs.data(), ...);
    }
}
```

Because reset happens in C, `PufferEnv.done` returns `False` (no Python-level reset needed).

### Termination Conditions (Companions)

Episode ends when either condition is met in `synchro_env.cc:170-171`:

```cpp
bool SynchroEnv::IsDone() const {
    return success_ || tick_ >= horizon_;
}
```

- **Success**: All agents reach synchro cells (`success_ = true`)
- **Horizon**: Maximum steps reached (`tick_ >= horizon_`)

---

## 8. Hyperparameter Optimization (Sweep)

PufferLib implements Bayesian optimization using Gaussian Processes for HPO.

### Running a Sweep

```bash
puffer sweep puffer_moba --max-runs 200
```

### Sweep Algorithm (Protein)

The recommended `Protein` method maintains **two GP models**:
1. **Score GP**: Predicts training performance
2. **Cost GP**: Predicts computational cost (runtime)

The algorithm finds **Pareto-optimal** hyperparameters balancing score vs compute cost.

### Reward Shaping via Sweep

Sweep can optimize **environment reward coefficients**, not just training hyperparameters:

```ini
# From config/ocean/moba.ini
[sweep.env.reward_death]
distribution = uniform
min = -1.0
max = 0

[sweep.env.reward_xp]
distribution = uniform
min = 0.0
max = 0.05

[sweep.env.reward_tower]
distribution = uniform
min = 0.0
max = 1.0
```

This finds reward weights that lead to faster learning and better emergent behaviors.

### Self-Play Considerations

**PufferLib does NOT implement Population-Based Training (PBT).**

For competitive self-play games (like MOBA), the `score` metric is typically winrate:

```c
// From moba.h:385
log->score += radiant_victory;  // score = count of Radiant wins
```

**The 50% winrate problem**: In self-play, winrate is always ~50% since both teams use the same policy. The sweep still works because:

- It optimizes for **training efficiency** (score vs compute cost)
- Better reward shaping → faster convergence to the same 50%
- Auxiliary metrics (levels, kills, towers) reveal policy quality

**What sweep actually finds**: Reward weights where agents learn stronger strategies faster, even though final winrate remains 50%.

### Key Sweep Parameters

| Parameter | Purpose |
|-----------|---------|
| `method` | HPO algorithm (`Protein`, `Random`, `ParetoGenetic`) |
| `metric` | What to optimize (e.g., `score`) |
| `goal` | `maximize` or `minimize` |
| `max_suggestion_cost` | Max seconds per training run |
| `distribution` | `uniform`, `log_normal`, `logit_normal`, etc. |

### Key Files

| File | Purpose |
|------|---------|
| `pufferlib/sweep.py` | Sweep algorithms (Protein, Random, etc.) |
| `pufferlib/pufferl.py:1053-1127` | `sweep()` orchestration loop |
| `pufferlib/config/default.ini` | Default sweep configuration |
