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
│   ├── env/                   # BaseEnv, SynchroEnv, AggroEnv
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
All the dynamics are defined in BaseEnv, and each of its subclass defines a "puzzle"
that defines the initial state, the rewards and the termination condition.





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

### Environments

**SynchroEnv**: Companions must stand on every Synchro cells simultaneously to win.

**AggroEnv**: Companions must lure FSM enemy away from TargetCell to step on it.
- 1-3 companions, 1 Goblin enemy with FSM
- 3x3 patrol square (8 cells perimeter, clockwise)
- Aggro range: 3, Return range: 5
- Smart spawning: companions and target outside aggro range
- Target not in corners (2+ walkable adjacent cells)

## Exporting the game
We want to integrate the pure C++ game in `companions/` into PufferLib


### Observations
5-plane tensor [5 × grid_size × grid_size]:
- Plane 0: Walkable cells
- Plane 1: Walls
- Plane 2: Synchro/goal cells
- Plane 3: Current player
- Plane 4: Other agents

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

