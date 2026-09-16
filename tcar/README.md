# TCAR Mobile Robot Exploration Model

An agent-based simulation of autonomous mobile robots exploring a 2D square grid environment, using visit-trail heuristics to maximize territorial coverage on ROOT-Sim 3.0.

## Overview

In the TCAR (Territory Coverage by Autonomous Robots) model, autonomous robots explore an unfamiliar terrain partitioned into discrete spatial regions. The environment is represented as a 2D square grid of cells.

To achieve decentralized coverage without a centralized map:
- Each spatial cell tracks the cumulative number of times it has been visited (`trails`).
- Robots enter a cell (`REGION_IN`), increment its trail counter, and schedule a departure (`REGION_OUT`).
- Upon departure, robots query adjacent neighbor cells via the Agent-Based Modeling (ABM) runtime layer and steer toward the neighbor with the lowest trail count, thereby naturally repelling from saturated areas toward less-explored sectors.

## Architecture

- **ABM Runtime Layer**: Built on the shared ROOT-Sim ABM runtime (`common/abm.h`), providing agent spawning, mobility transitions, and neighbor data tracking.
- **Spatial Topology**: Configured as a 2D square lattice (`TOPOLOGY_SQUARE`) through `abm_init_simulation()`.
- **LPs and Agents**: Logical Processes represent terrain cells, while mobile robots are modeled as dynamic agents migrating between LPs.

## Model Details

- **Cell State (`lp_state_type`)**:
  - `trails`: Cumulative visit count for the region.
- **Event Types**:
  - `LP_INIT`: Initializes the grid cell state, registers the neighbor data pointer (`TrackNeighbourInfo`), and populates initial cells with robot agents.
  - `REGION_IN`: Handles robot arrival in a cell, increments the visit count, and schedules departure.
  - `REGION_OUT`: Evaluates neighbor visit statistics, selects the neighbor with the minimum trail count, and schedules agent transit to the target LP.
  - `PING`: Periodic keep-alive event ensuring continuous simulation progress.
- **Key Parameters** (defined in `tcar.h`):
  - `OCCUPIED_CELLS`: Number of cells initially populated with robots (default: 4).
  - `ROBOTS_PER_CELL`: Initial robot agent count per occupied cell (default: 4).
  - `TIME_STEP`: Base duration for cell traversal (default: 1.0).
  - `MINIMUM_VISITS`: Visit threshold per cell for simulation completion (default: 10).
  - `DISTRIBUTION`: Traversal delay distribution (`UNIFORM` or `EXPONENTIAL`).

## Building

### Standalone Build

```sh
cd tcar
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tcar
```

## Running

```sh
./build/tcar [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of grid cells (default: 16; auto-adjusted to square grid dimensions).
- `-t, --termination-time <T>`: Virtual time limit (default: 1000).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run with 64 grid cells (8x8 grid) on 4 threads:
```sh
./build/tcar -c 4 -p 64 -t 500
```

Run sequentially:
```sh
./build/tcar --serial -p 36 -t 300
```
