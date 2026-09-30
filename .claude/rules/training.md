---
paths: pufferlib/pufferl.py, pufferlib/vector.py
---

# Training & Evaluation

## CLI Commands

```bash
# Train
puffer train puffer_synchro
puffer train puffer_revive      # companions revive task (revive.ini: task = revive)

# Evaluate (with rendering)
puffer eval puffer_synchro --render-mode ansi --load-model-path latest

# Hyperparameter sweep
puffer sweep puffer_synchro

# Profile performance
puffer profile puffer_synchro

# Export model
puffer export puffer_synchro --load-model-path latest
```

## Common Options

| Option | Description |
|--------|-------------|
| `--load-model-path PATH` | Load checkpoint (or `latest`) |
| `--render-mode ansi/human/rgb_array` | Rendering mode |
| `--train.device mps/cuda/cpu` | Training device |
| `--train.total-timesteps N` | Total training steps |
| `--env.KEY VALUE` | Override env config (companions: `--env.task`, `--env.down-cost`) |
| `--train.KEY VALUE` | Override train config |
| `--wandb` | Enable W&B logging |

## Truncations

`pufferl.py` (~253) computes `done_mask = d + t` but never uses it: the rollout stores
the terminals alone (~303; the advantage reads them, and the state's `done`, passed to
the policy, is read by none in-tree) and never fills its truncations buffer. So a
truncation would train as neither a terminal nor a bootstrapped truncation (GAE runs on
into the auto-reset episode). Latent for the companions: no RL task truncates today
(Synchro RL cannot down a companion by play; the ReviveLens is not interruptible). Once
something can, either store `d | t` as the terminal here (~303), or have the wrapper
write Interrupted as a terminal. An exact truncation bootstrap is impossible anyway: the
C auto-reset overwrites the final observation.

Rewards are clamped to [-1, 1] (~276): a companions `down_cost` at or below about -1
trains as -1, and two downs on one step (2 x -0.5 plus the time penalty) are clipped.

## VS Code Launch Configs

In `launch.json`:
- **Synchro: Train** - Start training run
- **Synchro: Eval** - Evaluate with model path prompt

## Environment Naming

Ocean environments use `puffer_` prefix:
- `puffer_synchro`, `puffer_revive`, `puffer_snake`, `puffer_breakout`, etc.

## Distributed Training

```bash
torchrun --standalone --nnodes=1 --nproc-per-node=6 -m pufferlib.pufferl train puffer_synchro
```
