# StupidModel Bug Ecology Model

An implementation of Railsback et al.'s "StupidModel", a standard reference benchmark for agent-based modeling platforms, running on ROOT-Sim 3.0.

## Overview

StupidModel was formulated by Railsback, Lytinen, and Jackson (2006) as a standardized, multi-feature benchmark to compare agent-based modeling platforms. It captures essential ecological dynamics: resource regeneration, foraging consumption, growth, reproduction, dispersal, and mortality.

In this model:
- The environment consists of a hexagonal grid of resource patches (cells).
- Each cell produces food at regular intervals up to a maximum production limit (`MAX_FOOD_PRODUCTION_RATE`).
- Bug agents roam between patches, consume available food, and increase in biomass (`size`).
- Upon reaching a size threshold (`REPRODUCTION_SIZE`), a bug reproduces by spawning offspring into unoccupied adjacent neighbor cells and then dies.
- During migration, bugs face a natural mortality rate governed by `SURVIVAL_PROBABILITY`.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`).
- **Hexagonal Topology**: Uses a hexagonal grid topology (`TOPOLOGY_HEXAGON`) via `rstopology`.
- **Dynamic Agent Population**: Bug agents are dynamically allocated and reclaimed using `SpawnAgent()` and `KillAgent()`.

## Model Details

- **Patch State (`region_t`)**:
  - `food_available`: Available food quantity in the cell.
  - `bugs`: Current count of bugs resident in the cell.
  - `is_explored`: Flag indicating whether a bug has visited the patch.
- **Bug Agent State (`bug_t`)**:
  - `size`: Accumulated biomass / energy reserve.
  - `first`: Boolean flag protecting initial seed bugs from early mortality.
- **Event Types**:
  - `LP_INIT`: Initializes patch state, food reserves, and populates initial cells with bug agents.
  - `PRODUCE_FOOD`: Periodically increments food availability.
  - `SPAWN_BUG`: Instantiates a new bug agent in a cell.
  - `BUG_VISIT`: The bug grazes on local food, updates its size, and either triggers reproduction or schedules departure.
  - `BUG_LEAVING`: Applies mortality risk. If the bug survives, it inspects adjacent neighbor patches and migrates to an available cell.
  - `BUG_DELAYED_VISIT`: Handles transit between patches.
- **Key Parameters** (defined in `stupid.h`):
  - `MAX_FOOD_PRODUCTION_RATE`: Maximum food produced per time step (default: 0.01).
  - `MAX_CONSUMPTION_RATE`: Maximum food consumed per visit (default: 0.1).
  - `REPRODUCTION_SIZE`: Biomass threshold triggering reproduction (default: 10.0).
  - `CHILD_COUNT`: Number of offspring produced upon reproduction (default: 5).
  - `SURVIVAL_PROBABILITY`: Percentage probability of surviving a movement step (default: 95%).
  - `BUG_PER_CELL`: Maximum bug carrying capacity per patch (default: 1).
  - `TIME_STEP`: Simulation time increment (default: 1.0).

## Building

### Standalone Build

```sh
cd stupid_model
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target stupid_model
```

## Running

```sh
./build/stupid_model [options]
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

Run on 64 patches with 4 worker threads:
```sh
./build/stupid_model -c 4 -p 64 -t 100
```

Run sequentially:
```sh
./build/stupid_model --serial -p 36 -t 50
```
