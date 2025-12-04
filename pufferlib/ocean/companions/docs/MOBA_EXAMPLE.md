# MOBA Environment Feature Reference

This document lists features from PufferLib's MOBA environment (`pufferlib/ocean/moba/`) that were analyzed during companions development. **These features are NOT relevant to the companions game** and are documented here for reference only.

## Features NOT Applicable to Companions

### Resource & Progression Systems

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Mana System** | Current/max mana, 2 regen/tick, skill costs (5-200 mana) | Companions uses simple cooldown-based effects |
| **Health Regeneration** | Automatic 2 HP/tick regen for players | Companions episodes are short, health is scarce |
| **Experience/Leveling** | XP from kills, 30 levels, stat scaling tables | Companions has no progression within episodes |
| **XP Sharing** | XP split among allies within 7 tiles | No XP system |

### Combat Complexity

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Multiple Abilities (Q/W/E)** | Each hero has 3 unique skills + basic attack | Companions uses data-driven single-effect system |
| **Cooldown Timers per Ability** | Independent q_timer, w_timer, e_timer per hero | Effect system handles cooldowns via telegraph/recovery phases |

### Vision

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Fog of War** | Limited 11x11 vision window centered on player | Companions grids are small, full observability works |
| **Per-Entity Vision Range** | Players 5, creeps 7, neutrals 3 tiles | FSM AI uses detection range instead |

### Movement

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **8-Direction Movement** | 8 cardinal + diagonal directions | Companions uses 4-connected grid for simplicity |
| **Speed Modifiers** | Continuous 0.5-2.0 multiplier on movement rate | Companions uses cadence system (binary on/off) |
| **Movement Jittering** | Random jitter during pathfinding | Not needed for small grids |

### Entity Types

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Towers/Structures** | 24 static defensive towers with attacks | No base defense in companions |
| **Creeps/Minions** | 100 wave-spawning lane followers | No lane/wave concept |
| **Neutrals** | 72 monsters in 18 camps between lanes | No jungle/camp system |

### Spawning & Lifecycle

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Respawning** | Dead players respawn at base after timer | Companions episodes end on death or goal |
| **Wave-Based Spawning** | Creeps spawn every 150 ticks (5 per lane) | No persistent world |
| **Lane System** | 6 lanes with waypoints for creep pathing | Grid-based environments have no lanes |

### Game Structure

| Feature | MOBA Implementation | Why Not Relevant |
|---------|---------------------|------------------|
| **Win Condition: Destroy Ancient** | Episode ends when tier-5 tower destroyed | Companions has env-specific goals (synchro, dodge, aggro) |
| **Team Spawns** | Fixed spawn areas per team | Companions uses random spawn placement |
| **Kill/Death Statistics** | Comprehensive per-player stats | Basic game logger suffices |

---

## Features Companions DOES Have (Unique)

These features are present in companions but NOT in MOBA:

- **Data-Driven Effect System**: CSV-configured effects with telegraph/active/recovery phases
- **FSM AI System**: Sophisticated patrol/aggro/attack state machines
- **D4 Symmetry Transforms**: Data augmentation via grid rotations/reflections
- **Procedural Map Generation**: Complexity levels for curriculum learning
- **Status Effect: Marked**: 1.5x damage multiplier unique to companions
- **Multiple Environment Variants**: SynchroEnv, AggroEnv, DodgeEnv for different training objectives

---

## Reference: MOBA Architecture

For those interested in the MOBA implementation:

- Core game logic: `pufferlib/ocean/moba/moba.h` (~2300 lines)
- C implementation: `pufferlib/ocean/moba/moba.c` (~215 lines)
- Python wrapper: `pufferlib/ocean/moba/moba.py`
- Map data: `pufferlib/ocean/moba/game_map.h`

Key data structures:
- 128x128 grid with spatial indexing via `map->pids`
- Precomputed all-pairs shortest paths (N^4 array)
- Entity pool with 10 players + 100 creeps + 72 neutrals + 24 towers
