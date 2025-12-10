---
paths: pufferlib/config/**, pufferlib/models.py, pufferlib/ocean/**/networks/**, pufferlib/ocean/torch.py
---

# Hyperparameters & Configuration

## Configuration System

Environment configs in `pufferlib/config/ocean/<env>.ini`:

```ini
[base]
package = ocean
env_name = puffer_<env>
policy_name = <PolicyClass>  # Must exist in pufferlib/ocean/torch.py

[policy]
hidden_size = 64  # Policy-specific params

[env]
# Environment constructor kwargs (rows, cols, num_agents, etc.)

[vec]
num_envs = 8  # For vectorized wrapper

[train]
# PPO hyperparameters (learning_rate, gamma, etc.)

[sweep]
# Hyperparameter sweep configuration
```

## Synchro Configuration

Current config: `pufferlib/config/ocean/synchro.ini`

Key settings:
- `policy_name = SynchroD4V2` (D4-equivariant network)
- Grid: 8x8, 3 agents, 3 synchro cells
- Complexity: 2 (rooms + corridors)
- Training: MPS device, 5M timesteps

## Policy Models

Policies defined in `pufferlib/ocean/torch.py`. Must implement:
- `encode_observations(observations)` -> hidden state
- `decode_actions(hidden)` -> (action_logits, value)

### Synchro Policy Variants

| Class | Description | SPS |
|-------|-------------|-----|
| `Synchro` | Basic MLP | ~40k |
| `SynchroMobaTemplate` | MOBA-style encoder | ~40k |
| `SynchroD4` | D4-equivariant (escnn) | ~1k |
| `SynchroD4V2` | Optimized D4-equivariant | ~5-10k |

**SynchroD4V2** (current default):
- Uses escnn for D4 group equivariance
- Separate actor/critic with shared encoder
- Respects rotational/reflectional symmetry of grid world

### D4 Networks (`networks/d4.py`)

Low-level components:
- `D4EquivariantEncoder` - Convolutions equivariant to D4
- `D4Actor` / `D4ActorV2` - Policy head
- `D4Critic` / `D4CriticV2` - Value head

Data augmentation (no escnn required):
- `apply_d4_transform(obs, action, idx)` - Apply specific D4 transform
- `random_d4_augment(obs, action)` - Random augmentation

## Results Storage

Training results are saved with an 8-character run ID:

| Location | Content |
|----------|---------|
| `wandb/run-DATE_TIME-ID/` | W&B logs and metrics |
| `experiments/puffer_synchro_ID.pt` | Model checkpoint |
| `experiments/puffer_synchro_ID/` | Training artifacts |

### Loading a Model

```bash
# Evaluate with specific checkpoint
puffer eval puffer_synchro --render-mode ansi --load-model-path experiments/puffer_synchro_XXXXXXXX.pt

# Or use "latest" for most recent
puffer eval puffer_synchro --render-mode ansi --load-model-path latest
```

## Trained Models (may be outdated)

| ID | Environment | Policy | Size | Companions | Synchro | Complexity |
|----|-------------|--------|------|------------|---------|------------|
| cx9x30s2 | synchro | old | 5 | 2 | 2 | 0 |
| 7212qjtt | synchro | old | 10 | 3 | 2 | 2 |
| 5n85iosj | synchro | d4 | 10 | 3 | 2 | 2 |
