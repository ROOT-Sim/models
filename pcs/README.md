# PCS (Personal Communication Services) Simulation Model

A simulation model of a cellular mobile communication network (Personal Communication Services) running on ROOT-Sim 3.0.

## Overview

The PCS model simulates mobile telephone traffic across a grid of hexagonal wireless cells (based on Carothers et al., 1999). Each cell represents a base station with a fixed allocation of radio channels. Mobile callers initiate calls, occupy channels, experience radio signal fading, and migrate across cell boundaries, triggering handoff procedures.

If a new call arrives when all channels in a cell are occupied, the call is blocked on setup. If a mobile user crosses into a neighbor cell with no available channels, the call is dropped on handoff.

## Architecture and Topology

- **Spatial Representation**: Cells are arranged on a hexagonal lattice managed by the ROOT-Sim Topology library (`rstopology`). Each cell interacts with up to six adjacent neighbors.
- **Topology Initialization**: `InitializeTopology(TOPOLOGY_HEXAGON, width, height)` dynamically computes neighbor adjacency based on the configured number of Logical Processes (LPs).

## Model Details

- **Logical Process State (`lp_state_type`)**:
  - `channel_counter`: Number of available wireless channels.
  - `channel_state`: Bitmask array tracking channel occupancy.
  - `complete_calls`: Counter for successfully completed calls.
  - `arriving_calls`, `blocked_on_setup`, `blocked_on_handoff`: Quality-of-service metrics.
  - `seed`: Independent PRNG stream per cell.
- **Event Types**:
  - `LP_INIT`: Initializes the cell state, allocates channel bitmasks, and schedules the initial `START_CALL` and `FADING_RECHECK` events.
  - `START_CALL`: Handles incoming call arrivals. If a channel is free, the call is established and events for call termination (`END_CALL`) or cell change (`HANDOFF_LEAVE`) are scheduled.
  - `END_CALL`: Releases the allocated wireless channel and increments completed call statistics.
  - `HANDOFF_LEAVE`: Departure of a caller from the current cell, freeing its local channel and forwarding the call context to the neighbor cell via `HANDOFF_RECV`.
  - `HANDOFF_RECV`: Arrival of a caller in the destination cell, requesting channel allocation.
  - `FADING_RECHECK`: Periodic reassessment of radio channel fading and link conditions.
- **Parameters** (defined in `pcs.h`):
  - `CHANNELS_PER_CELL`: Number of wireless channels per cell (default: 100).
  - `TA`: Mean call interarrival time (default: 30.0).
  - `TA_DURATION`: Mean call conversation duration (default: 120.0).
  - `TA_CHANGE`: Mean mobility dwell time before crossing into an adjacent cell (default: 180.0).
  - `FADING_RECHECK_FREQUENCY`: Time interval between fading checks (default: 10.0).
  - `COMPLETE_CALLS`: Target completed call count per LP for early termination (default: 20000).

## Building

### Standalone Build

```sh
cd pcs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pcs
```

## Running

```sh
./build/pcs [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of cells/LPs (default: 16).
- `-t, --termination-time <T>`: Virtual time limit (default: 500).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run on 16 hexagonal cells with 4 worker threads:
```sh
./build/pcs -c 4 -p 16 -t 500
```

Run sequentially on 64 cells:
```sh
./build/pcs --serial -p 64 -t 1000
```
