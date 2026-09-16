# Schelling Segregation Model

An agent-based simulation of Thomas Schelling's spatial segregation model running on a hexagonal grid topology with ROOT-Sim 3.0.

## Overview

Schelling's model of segregation (1971) demonstrates how mild individual preferences for living near similar neighbors can lead to dramatic, emergent macro-level geographic segregation, even when individuals are tolerant of diversity.

In this model:
- The environment is structured as a hexagonal lattice where each cell represents a residential location.
- Cells may be vacant or occupied by an agent belonging to one of two groups: "engineers" or "non-engineers".
- Each agent periodically evaluates its immediate neighborhood. If the fraction of dissimilar neighbors exceeds an intolerance threshold (`AGENT_THRESHOLD`), the agent becomes unhappy and relocates to an adjacent vacant cell.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`).
- **Hexagonal Topology**: Uses a hexagonal grid geometry (`TOPOLOGY_HEXAGON`) from the ROOT-Sim Topology library (`rstopology`), where each interior cell has up to six neighbors.
- **Neighbor Tracking**: Each cell exposes occupant status (`struct n_data`) to its neighbors via `TrackNeighbourInfo()`.

## Model Details

- **Cell State (`region_t`)**:
  - `n.agents`: Number of agents currently present in the cell (0 or 1).
  - `n.has_engineer`: Boolean indicating whether the resident agent is an engineer.
  - `happy`: Boolean status indicating whether the agent's neighborhood preference is satisfied.
  - `violation`: Counter for occupancy violations (more than one agent per cell).
- **Agent State (`guy_t`)**:
  - `engineer`: Boolean attribute distinguishing the two agent groups.
- **Event Types**:
  - `LP_INIT`: Initializes the cell state, registers neighbor information, probabilistically spawns an initial agent, and schedules events.
  - `GUY_VISIT`: Registers an agent entering the cell and schedules subsequent departure.
  - `GUY_LEAVE`: Evaluates neighbor similarity. If unhappy and an empty adjacent neighbor exists, the agent migrates; otherwise, it remains in place.
  - `GUY_DELAYED_VISIT`: Handles inter-cell transit of migrating agents.
  - `KEEP_ALIVE`: Periodic heartbeat event to maintain simulation progress.
- **Key Parameters** (defined in `segregation.h`):
  - `AGENT_THRESHOLD`: Maximum tolerable fraction of dissimilar neighbors (default: 0.35).
  - `AGENT_SPAWN_PROBABILITY`: Probability that a cell is initially occupied (default: 0.7).
  - `AGENT_IS_ENGINEER_PROBABILITY`: Probability that a spawned agent is an engineer (default: 0.5).
  - `TIME_STEP`: Base time step for relocation decisions (default: 10.0).

## Building

### Standalone Build

```sh
cd segregation
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target segregation
```

## Running

```sh
./build/segregation [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of cells/LPs (default: 16; auto-squared to grid dimensions).
- `-t, --termination-time <T>`: Virtual time limit (default: 100).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run on a 64-cell hexagonal grid with 4 worker threads:
```sh
./build/segregation -c 4 -p 64 -t 100
```

Run sequentially:
```sh
./build/segregation --serial -p 36 -t 50
```
