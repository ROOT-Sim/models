# Egress Emergency Evacuation Model

An agent-based simulation of crowd emergency evacuation dynamics across a multi-room building floorplan with obstacles, panic propagation, group behavior, and explosion events on ROOT-Sim 3.0.

This is the implementation of the model described in the paper "Agent-based Modeling and Simulation for Emergency Scenarios: A Holistic Approach" (Piccione 2020).

## Overview

The Egress model simulates 9,960 individuals evacuating a 32×32 hexagonal grid building layout (`topology.json`) partitioned into 6 rooms connected by doors and exits, subjected to timed explosions:

- **Regions (LPs)**: Represent spatial cells in a 32×32 hexagonal grid (`TOPOLOGY_HEXAGON`). Obstacle cells (walls) are inactive; walkable cells track explosion proximity, local psycho count, group leader direction hints, and local evacuation/mortality counters.
- **People (Agents)**: Dynamically spawned in Room 3 with individual attributes (age, emotional factor, current emotion, destination, group ID, and role: `role_gleader`, `role_gfollower`, `role_rescuer`, `role_psycho`, or `role_on_my_own`).
- **Behavioral & Environmental Dynamics**:
  - Scheduled explosions (`EXPLOSION`) propagate hop-distance hazards around obstacles via shortest-path BFS (`abm_compute_min_tour`) and cause distance-dependent mortality.
  - Periodic `REFRESH` events update emotional panic levels (modulated by exit visibility, agent crowding, explosion proximity, and psycho influence) and check explosion casualties.
  - Agents navigate toward doors and exits using shortest-path routing avoiding walls (`FindReceiverToward`) or wander randomly when confused or panicked.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`), including obstacle-aware neighbor discovery and shortest-path routing (`abm_set_obstacles`, `abm_compute_min_tour`, `FindReceiverToward`).
- **Hexagonal Grid Topology with Obstacles**: Configured from `topology.json` (default 1024 regions, 32×32 `TOPOLOGY_HEXAGON`).
- **Rollback-Safe Statistics**: Per-LP `saved` and `deaths` counters are maintained inside `region_t` and aggregated upon `LP_FINI`.

## Model Details

- **Region State (`region_t`)**:
  - `saved`, `deaths`: Number of agents evacuated or killed in this cell.
  - `psycho_count`: Current count of `role_psycho` agents in the cell.
  - `expl_dist`: Minimum hop distance to the nearest triggered explosion.
  - `nearest_exit`, `room_id`, `exit_dis`, `can_see_exit`: Precomputed room/exit awareness metadata.
  - `group_d[GROUPS_CNT]`: Next-hop direction hints left by rescuers and group leaders.
  - `exit`: Termination flag set after `termination_time` (default 10000).
- **Event Types**:
  - `LP_INIT`: Skips obstacle cells; initializes walkable cell metadata, spawns initial population in Room 3, and schedules `EXPLOSION` and `REFRESH` events.
  - `VISIT`: Agent arrives at a cell and schedules its next `LEAVE` event.
  - `LEAVE`: Evaluates age delay, confusion, panic threshold, role-specific routing, and triggers `SAVE` when entering an exit after an explosion.
  - `SAVE`: Evacuates the agent at an exit (`KillAgent` and increments `saved`).
  - `REFRESH`: Periodically updates agent emotion, psycho influence, and explosion casualties.
  - `EXPLOSION`: Updates `expl_dist` around obstacles and triggers `person_refresh_all`.
  - `LP_FINI`: Aggregates regional `saved` and `deaths` counts and frees LP state.

## Building

### Standalone Build

```sh
cd egress
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target egress
```

## Running

```sh
./build/egress/egress [options]
```

### Standard Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of logical processes (overridden by `regions_count` in `topology.json`, default: 1024).
- `-t, --termination-time <T>`: Virtual time limit (default: 10000).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Model-Specific Options

- `--confusion-p <DOUBLE>`: Probability an agent delays leaving due to confusion (default: 0.10).
- `--avg-age <DOUBLE>`: Average agent age controlling movement delay (default: 50.0).
- `--group-p <DOUBLE>`: Probability of joining a group (default: 0.10).
- `--env-p <DOUBLE>`: Probability of knowing exit locations (default: 0.10).
- `--trust-p <DOUBLE>`: Probability of trusting rescuer hints (default: 0.10).
- `--psycho-p <DOUBLE>`: Psycho influence probability per psycho agent (default: 0.10).
- `-T, --topology <FILE>`: Path to JSON obstacles topology file (defaults to `topology.json` or `egress/topology.json`).
- `--escape-stats <FILE>`: Write per-LP `lp_id saved deaths` statistics to `FILE` at the end of the simulation.

### Examples

Run with 4 worker threads:
```sh
./build/egress/egress -c 4 -t 1000
```

Run sequentially with custom environment knowledge and confusion parameters:
```sh
./build/egress/egress --serial -t 1000 --env-p 0.8 --confusion-p 0.01
```

## Scripts

The `scripts/` folder contains Python 3 utilities updated for ROOT-Sim 3.0:

- **`scripts/topology.py`**: Regenerates the 32×32 hexagonal obstacle floorplan (`topology.json`).
  ```sh
  python3 egress/scripts/topology.py -o egress/topology.json
  ```
- **`scripts/escape.py`**: Summarizes total `saved` and `deaths` from a per-LP stats file produced with `--escape-stats`.
  ```sh
  ./build/egress/egress --serial -t 1000 --escape-stats escape_stats.txt
  python3 egress/scripts/escape.py escape_stats.txt
  ```
- **`scripts/run_tests.py`**: Sweeps a model parameter (default `--env-p` across `[0, 1]`), parses `saved`, `deaths`, and `t95` (95% completion virtual time) from the `egress` output, and writes `<out-prefix>-f.dat` (fatality ratio), `<out-prefix>-s.dat` (save rate), and `<out-prefix>-t.dat` (evacuation time).
  ```sh
  python3 egress/scripts/run_tests.py --param env-p --points 15 --runs 5 --out-prefix knowledge
  ```

