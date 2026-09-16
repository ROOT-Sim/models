# Multi-Robot Cooperative Exploration Model

A multi-agent simulation of cooperative robotic terrain exploration on a hexagonal grid with obstacles running on ROOT-Sim 3.0.

## Overview

This model simulates decentralized exploration of an unfamiliar terrain by a team of autonomous mobile robots. The operational environment is represented as a hexagonal lattice where certain cells are impassable obstacles.

Key exploration mechanisms include:
- **Local Frontier Exploration**: Each robot maintains an internal map of discovered cells and obstacles. Robots identify the closest unexplored frontier cells and plan movement trajectories toward them.
- **A\* Pathfinding**: Navigation routes across known hexagonal cells toward exploration targets are computed dynamically via an A\* search implementation (`topology_utils.c`).
- **Opportunistic Map Exchange**: Robots have no global communication network. However, whenever two robots meet within the same spatial cell, they exchange and merge their discovery maps. This enables collaborative terrain coverage without centralized orchestration.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`).
- **Hexagonal Topology**: Uses a hexagonal grid geometry (`TOPOLOGY_HEXAGON`) via `rstopology`.
- **Obstacle Modeling**: Randomly places impassable obstacles across the grid based on `OBSTACLE_PROB`.

## Model Details

- **Cell State (`cell_state_t`)**:
  - `neighbours`: Array of valid neighboring cell identifiers (or `-1` if blocked by an obstacle).
  - `present_agents`: Count of robot agents currently occupying the cell.
  - `has_obstacles`: Boolean flag indicating if the cell contains an obstacle.
  - `max_ratio`: Highest exploration coverage ratio achieved.
- **Robot Agent State (`agent_state_type`)**:
  - `current_cell`: Present cell index.
  - `target_cell`: Exploration frontier target cell.
  - `visited_cells`: Number of unique cells discovered by this robot.
  - `met_robots`: Counter of encounter and map-sharing events with other robots.
  - `visit_map`: Internal dynamic map tracking discovered cells, neighbor links, and visited statuses.
- **Event Types**:
  - `LP_INIT`: Initializes cell state, sets up obstacle boundaries, and triggers initial robot creation.
  - `NEW_ROBOT`: Spawns a new robot agent with an empty discovery map.
  - `REGION_IN`: Handles robot entry, records cell visit, detects meetings with other robots to merge maps, selects next frontier target, and computes movement direction.
  - `REGION_OUT`: Handles robot departure and updates cell occupancy counts.
  - `KEEP_ALIVE`: Heartbeat event ensuring continuous simulation progression.
- **Key Parameters** (defined in `robot_explore.h` and `robot_explore.c`):
  - `ROBOTS`: Number of mobile robot agents (default: 2).
  - `TIME_STEP`: Movement step duration (default: 5.0).
  - `OBSTACLE_PROB`: Probability of a cell being designated as an obstacle (default: 0.01).

## Building

### Standalone Build

```sh
cd robot_explore
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target robot_explore
```

## Running

```sh
./build/robot_explore [options]
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

Run on a 64-cell hexagonal grid (8x8) on 4 threads:
```sh
./build/robot_explore -c 4 -p 64 -t 200
```

Run sequentially:
```sh
./build/robot_explore --serial -p 36 -t 100
```
