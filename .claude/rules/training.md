---
paths: pufferlib/pufferl.py, pufferlib/vector.py
---

# Training & Evaluation

## CLI Commands

```bash
# Train
puffer train puffer_synchro

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
| `--env.KEY VALUE` | Override env config |
| `--train.KEY VALUE` | Override train config |
| `--wandb` | Enable W&B logging |

## VS Code Launch Configs

In `launch.json`:
- **Synchro: Train** - Start training run
- **Synchro: Eval** - Evaluate with model path prompt

## Environment Naming

Ocean environments use `puffer_` prefix:
- `puffer_synchro`, `puffer_snake`, `puffer_breakout`, etc.

## Distributed Training

```bash
torchrun --standalone --nnodes=1 --nproc-per-node=6 -m pufferlib.pufferl train puffer_synchro
```
