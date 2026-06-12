# The Companions - PufferLib Game Implementation

## Project Overview

Implementations of "The Companions" cooperative grid game for MARL training:

1. **Standalone Pure C++** (`companions/`) - Complete, tested
2. **Documentation** (`companions/docs/`) - Extensive doc, only read if you need to

**CRITICAL** This repo follows TDD principles. Tests are first class citizen.

## Repository Structure

```
companions/                    # Standalone pure C++ implementation
├── CMakeLists.txt
├── src/
│   ├── core/                  # Types, Grid, Cells, Objects, Pathfinder
│   │   └── fsm/               # Enemy FSM AI (fsm_state.h, fsm_states.cc, enemies.cc)
│   ├── demo/                  # Interactive play
│   ├── env/                   # BaseEnv, Envs, TaskLens
│   │   ├── task_lens.h        # Abstract TaskLens interface
│   │   ├── synchro_lens.h/cc  # SynchroLens implementation
│   │   ├── aggro_lens.h/cc    # AggroLens implementation
│   │   └── dodge_lens.h/cc    # DodgeLens implementation
│   └── viz/                   # ASCII/ANSI renderer
├── tests
└── benchmarks/
    ├── cpp/fsm_benchmark.cc   # C++ FSM performance benchmark
    └── python/                # RL algorithm comparison scripts
```

## Game design
**DOCUMENTATION** look at `docs/GDD.md` to know more
Only read it if you need to develop new environments so you can follow
the spirit of the game.
The companions is a multi agent cooperative env played on a 2D grid.
All the dynamics are defined in BaseEnv. Task-specific behavior (rewards, termination,
goal-cell visibility, vector obs tail) is handled by **TaskLens** objects that can be
swapped at runtime.





## Key Technical Details

### Grid & Movement
- 4-connected movement (Up/Down/Left/Right + Stay)
- The world is a rectangular 2D grid, and there is a Cell in each square of the grid
- Actors are Objects that have a position on the grid
- There can be at most a single Actor in a Cell

### Action space
 - Multidiscrete action space: movement X interaction
 - Movement has 5 options
 - Interaction is currently None or Attack
 - Can flatten / unflatten the action space to be compatible with other frameworks

### Collision Resolution
Fixed-point iteration algorithm in `base_env.cc:ResolveCollisions()`:
1. Invalidate moves into walls/out-of-bounds
2. Cancel swap conflicts (A→B, B→A)
3. Cancel same-cell conflicts (A→X, B→X)
4. Validate chase moves (only if target is vacating)

### Pathfinding
A* with Euclidean heuristic in `pathfinder.cc`:
- Euclidean heuristic naturally prioritizes reducing the larger axis first (long axis)
- Random tie-breaking for equal distances via optional RNG (`SetRng()`)
- FSM enemies pass `FSMContext::rng` to pathfinder for varied but optimal paths

### Enemy FSM AI
Location: `companions/src/core/fsm/`

**Architecture** (Flyweight pattern):
- `FSMState` - Singleton states with no per-actor data
- `FSMContext` - Per-actor runtime data (target_id, patrol_path, detection_range, etc.)
- States call `enemy.MoveTo()` directly to set movement intentions

**States**: PatrolState → AggroState → ReturnToPatrolState → PatrolState

**Enemy Types** (in `enemies.h/.cc`):
- `Zombie`: cadence [1,0] (moves every 2 turns), A* pathfinding, detection=3, lose=5
- `Goblin`: no cadence (always moves), A* pathfinding, detection=4, lose=6
- `Dragon`: flying (ignores walls), direct movement, detection=5, lose=8. Not implemented yet

**Integration**: `BaseEnv::PreStep()` calls `UpdateEnemyFSM()` before gathering intentions

### Environments & TaskLens

Each environment uses a **TaskLens** to define task-specific behavior:

| Env | Lens | Goal | Goal cells (obs plane 2, via `IsGoalCell`) |
|-----|------|------|--------------------------------------------|
| SynchroEnv | SynchroLens | All companions on synchro cells | SynchroGoal-annotated cells |
| AggroEnv | AggroLens | Lure enemy to target cell | AggroTarget-annotated cell |
| DodgeEnv | DodgeLens | Survive until horizon | none (survival task) |
| GameEnv | any (swappable) | Game-serving shell: never done, success polled via `companions_is_success()` | active lens decides |

Goal cells are SemanticTag annotations (`src/core/annotations.h`), not
CellKinds; each lens reports only its own tag so other tasks' goals stay
invisible — there is no cell masking/rewriting.

**Runtime task switching** (no snapshot needed):
```cpp
env.SetTaskLens(std::make_unique<SynchroLens>());
// ... complete task ...
env.SetTaskLens(std::make_unique<AggroLens>());  // World state preserved
```

**AggroEnv details:**
- 1-3 companions, 1 Goblin enemy with FSM
- 3x3 patrol square (8 cells perimeter, clockwise)
- Aggro range: 3, Return range: 5
- Smart spawning: companions and target outside aggro range

## Exporting the game
We want to integrate the pure C++ game in `companions/` into PufferLib


### Observations
Universal 7-plane tensor [7 × grid_size × grid_size], identical for all tasks
(BaseEnv::kNumObservationPlanes):
- Plane 0: Walkable cells
- Plane 1: Walls
- Plane 2: Goal cells (the active TaskLens decides via IsGoalCell)
- Plane 3: Current player
- Plane 4: Other agents
- Plane 5: Telegraphed hazard zones
- Plane 6: Active hazard zones

Vector observation = 9 base features (BaseEnv) + the active lens's tail
(TaskLens::WriteVectorObs / AdditionalVectorObsSize). SynchroLens adds 0,
AggroLens 8, DodgeLens 10.

### Curriculum Learning

For this repo, we don't use the curriculum learning at all. But it is present
so that the training scripts (another repo) can use the levers provided here. 

Four main levers for difficulty progression:

| Lever | Range | Effect |
|-------|-------|--------|
| **Grid Size** | 6→16 | Larger grids = longer paths, more exploration |
| **Num Companions** | 1→3 | More agents = harder coordination |
| **Map Complexity** | 0→5 | Procedural obstacles, rooms, corridors |
| **Level Difficulty** | 0→5 | Depend on the env. Not implemented |


**MapGenerator** (`core/map_generator.cc`) creates procedural maps:
- Complexity 0: Empty rectangle (backward compatible with original SynchroEnv)
- Complexity 1: Scattered obstacles while maintaining connectivity
- Complexity 2-5: Rooms connected by corridors with increasing density

All generated maps guarantee full connectivity via flood-fill validation.
See `map_generator.h` for detailed visual examples at each complexity level.

