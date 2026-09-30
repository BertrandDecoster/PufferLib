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
the terminals alone (the advantage and the policy's `done` read them) and never fills
its truncations buffer. So a truncation (companions: an Interrupted task, a down) trains
as neither a terminal nor a bootstrapped truncation (GAE runs on into the auto-reset
episode) until someone handles truncations here.

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
