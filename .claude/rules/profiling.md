---
paths: pufferlib/ocean/companions/scripts/profile_*.sh
---

# Profiling

## Quick Reference

| Tool | What it profiles | Best for |
|------|-----------------|----------|
| `puffer profile` | PyTorch ops (ATen) | CUDA profiling |
| `profile_cpp.sh` | C++ env code | Optimizing game/env logic |
| `profile_train.sh` | Full stack (Python + C++) | CPU profiling |

## 1. PufferLib PyTorch Profiler

```bash
puffer profile puffer_synchro
```

**How it works**: Instrumentation-based profiler that wraps ATen operations with timing hooks. Records every tensor operation invocation.

**Output**:
- Console table sorted by CPU time
- `trace.json` - open in `chrome://tracing` for timeline view

**Useful options** (edit `pufferlib/pufferl.py:profile()`):
```python
# Add stack traces to identify call sites
prof.key_averages(group_by_stack_n=5).table(...)
```

**Limitations**:
- CPU/CUDA profiling only on MPS
- ~10-30% overhead inflates absolute times (relative proportions accurate)
- Shows PyTorch ops (`aten::*`), not Python functions or C++ env code
- Cannot see inside C++ environment implementations

## 2. C++ Environment Profiler (samply)

```bash
# From repo root
./pufferlib/ocean/companions/scripts/profile_cpp.sh [grid_size]

# Examples
./pufferlib/ocean/companions/scripts/profile_cpp.sh      # 10x10 grid
./pufferlib/ocean/companions/scripts/profile_cpp.sh 20   # 20x20 grid
```

**How it works**: Sampling profiler (samply) at 8000 Hz running the C++ FSM benchmark directly.

**Output**: `build/profile_cpp.json.gz` - opens automatically in Firefox Profiler

**Limitations**:
- Only profiles C++ benchmark binary, not Python wrapper or training

## 3. Full Training Profiler (samply)

```bash
# From repo root
./pufferlib/ocean/companions/scripts/profile_train.sh [env] [duration] [grid_size]

# Examples
./pufferlib/ocean/companions/scripts/profile_train.sh                   # synchro 10x10 for 30s
./pufferlib/ocean/companions/scripts/profile_train.sh synchro 60 20     # synchro 20x20 for 60s
```

**How it works**: Sampling profiler at 1000 Hz on the full Python training process. Captures both Python and native code.

**Output**: `build/profile_train_<env>.json.gz` - opens automatically in Firefox Profiler

**Limitations**:
- Lower sample rate (1000 Hz vs 8000 Hz) - less detail than C++ profiler
- Forces Serial backend (single process) - different perf characteristics than parallel
- Python frames are missing symbols

## Interpreting Results

### Common PyTorch bottlenecks

| Operation | Cause | Fix |
|-----------|-------|-----|
| `aten::_local_scalar_dense` | `.item()` calls forcing sync | Batch scalar extractions, defer to end of epoch |


### Firefox Profiler tips

1. **Call Tree** tab shows hierarchical time breakdown
2. **Flame Graph** tab shows visual call stack
3. Use **Transform > Merge** to combine similar frames
4. Filter by thread to isolate Python vs C++ execution

## Future dev
Pytorch has a torch.mps.profiler.profile but it's MPS specific,
and it outputs to Apple Instruments only