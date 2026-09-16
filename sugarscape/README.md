# Sugarscape Simulation Model

An implementation of Epstein and Axtell's classic Sugarscape artificial society model running on ROOT-Sim 3.0.

## Overview

Sugarscape (Epstein & Axtell, 1996, *Growing Artificial Societies*) is a foundational agent-based computational model used to study the emergence of social phenomena such as wealth distribution, trade, migration, and demographics.

In this model:
- The landscape is a 2D square grid featuring localized mounds of renewable resources ("sugar"). Each cell has a maximum sugar carrying capacity determined by its proximity to sugar source centers.
- Sugar patches regenerate resource reserves at a steady rate over time (`SUGAR_REFILL`).
- Autonomous agents ("sugar eaters") populate the landscape. Each agent possesses an initial endowment of wealth, a metabolic burn rate (`eat_rate`), and an individual lifespan (`remaining_steps`).
- Upon visiting a cell, an agent harvests the available sugar, burns resources to satisfy metabolism, and ages. If wealth is exhausted or maximum lifespan is reached, the agent dies.
- Surviving agents scan adjacent neighboring cells, identify the neighbor with the highest available sugar reserve that is not already occupied, and migrate to it.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`).
- **Square Grid Topology**: Configured as a 2D square lattice (`TOPOLOGY_SQUARE`) using `rstopology`.
- **Dynamic Agents**: Sugar eaters are instantiated and retired dynamically using the ABM agent lifecycle API (`SpawnAgent()`, `KillAgent()`).

## Model Details

- **Cell State (`region_t`)**:
  - `capacity`: Maximum sugar storage for the cell.
  - `n.sugar`: Current available sugar units.
  - `n.eaters`: Number of eaters currently occupying the cell.
- **Agent State (`sugar_eater_t`)**:
  - `wealth`: Accumulated sugar reserves.
  - `eat_rate`: Metabolic consumption required per step.
  - `remaining_steps`: Remaining lifespan in simulation steps.
- **Event Types**:
  - `LP_INIT`: Computes cell capacity based on distance to sugar mounds, sets initial resources, and triggers agent spawning.
  - `SUGAR_INIT`: Spawns and initializes a sugar eater agent with randomized attributes.
  - `SUGAR_VISIT`: Harvests local sugar, decrements lifespan, consumes metabolic requirements, and schedules departure.
  - `SUGAR_LEAVE`: Scans neighboring cells, identifies the optimal unoccupied candidate, and enqueues movement.
  - `SUGAR_REFILL`: Regenerates one unit of sugar up to cell capacity.
- **Key Parameters** (defined in `sugarscape.h`):
  - `INIT_EATERS`: Initial population of sugar eaters (default: 40).
  - `SOURCEBASERADIUS`: Base radius defining sugar mound capacity gradients (default: 2.0).
  - `MIN_INITIAL_WEALTH` / `MAX_INITIAL_WEALTH`: Initial endowment range (default: 5 to 25).
  - `MIN_EAT_RATE` / `MAX_EAT_RATE`: Metabolic rate range (default: 1 to 4 units/step).
  - `MIN_MAX_AGE` / `MAX_MAX_AGE`: Lifespan range (default: 60 to 100 steps).
  - `TIME_STEP`: Simulation clock cycle (default: 1.0).

## Building

### Standalone Build

```sh
cd sugarscape
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sugarscape
```

## Running

```sh
./build/sugarscape [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of grid cells (default: 16; auto-squared to grid dimensions).
- `-t, --termination-time <T>`: Virtual time limit (default: 100).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run on a 64-cell grid (8x8) with 4 worker threads:
```sh
./build/sugarscape -c 4 -p 64 -t 100
```

Run sequentially:
```sh
./build/sugarscape --serial -p 36 -t 50
```
