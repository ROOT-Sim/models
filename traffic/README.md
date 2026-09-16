# Traffic Simulation Model

A discrete event simulation model for microscopic road traffic dynamics on complex urban road networks, implemented for ROOT-Sim 3.0.

## Overview

The Traffic model simulates vehicle movement across interconnected road networks represented as directed graphs. Vehicles are individually modeled with realistic acceleration, speed throttling based on congestion, queuing behavior at junctions and along road segments, stochastic accident occurrences, and proactive traffic jam detection and mitigation.

Key features include:
- Graph-based road network representation where both junctions and road segments are modeled as distinct Logical Processes (LPs).
- Dynamic vehicle velocity computation based on real-time traffic density, free-flow velocity, and road capacity.
- Proactive jam notification: roads and junctions near saturation dispatch notifications upstream to throttle vehicle injection and route entry until congestion clears.
- Stochastic vehicular accidents that block road segments for variable durations, inducing downstream starve and upstream spillback.
- Traversal and waiting time statistical estimation.
- Flexible JSON topology loading, including bundled real-world city networks (Trento, Rome, Rostock, Shenzhen) and an OpenStreetMap (OSM) conversion tool.

---

## Model Architecture and Theory

### 1. Network Topology and LP Mapping

The simulated network consists of two types of entities, each mapped to a unique LP ID:
- Junctions (Node LPs: IDs 0 through num_nodes - 1): Intersections where roads meet. Junctions act as entry points where new vehicles enter the network with an average frequency enter_freq, as turning points directing cars to outbound roads, and as exit points where vehicles leave the network with probability leave_prob.
- Road Segments (Edge LPs: IDs num_nodes through num_nodes + num_edges - 1): Unidirectional road stretches connecting two junctions. Each road segment has a defined length (in kilometers) and a maximum vehicle capacity determined by length and lane density.

The total number of LPs is dynamically determined at startup from the input topology:

$$\text{Total LPs} = \text{num\_nodes} + \text{num\_edges}$$

### 2. Vehicle Dynamics and Congestion

Each road segment calculates car travel times based on current occupancy:
- Road capacity is modeled assuming an average vehicle footprint (vehicle length plus safety distance) across available lanes.
- When traffic density increases, vehicle speeds decrease smoothly according to density-dependent deceleration curves.
- Speed is bounded by safety minimums (e.g., crawling speed) and maximum free-flow velocity to avoid numerical divergence.
- When a car reaches the end of a road segment, it requests entry into the destination junction. If the junction is congested, the car remains queued on the road.

### 3. Jam Detection and Upstream Notification

To prevent cascading gridlock and model driver awareness:
- When the occupancy of a road or junction crosses the saturation threshold (JAM_START_FACTOR, 90% of capacity), a jam condition is detected.
- The congested LP calculates the estimated time required to drain the queue to a recovery threshold (JAM_END_FACTOR, 75% of capacity).
- A JAM notification is dispatched upstream to preceding roads and junctions.
- Upstream junctions throttle their injection frequency and divert/slow inbound traffic until the congestion clears.

### 4. Accidents and Obstructions

Road segments can experience stochastic traffic accidents:
- When an accident occurs, vehicular flow is halted.
- The duration of the accident is drawn from a normal distribution with an average clearance time (e.g., 1 hour).
- Upon accident clearance (FINISH_ACCIDENT event), queued vehicles are sequentially released and normal traversal resumes.

---

## State and Event Definitions

### Event Types

| Event Macro | Value | Description |
|---|---|---|
| `LP_INIT` | 65534 | Kernel initialization: allocates state, initializes PRNG stream, and configures node/edge parameters. |
| `ARRIVAL` | 1 | Vehicle arrival at a junction or road segment. |
| `LEAVE` | 3 | Vehicle departure after completing travel time along a road or through a junction. |
| `JAM` | 0 | Notification sent to upstream LPs warning of downstream congestion. |
| `FINISH_ACCIDENT` | 2 | Accident clearance event restoring normal flow on a road segment. |
| `LP_FINI` | 65535 | Finalization: releases memory structures and logs summary statistics. |

---

## Configuration Files and Topologies

Topologies are provided as JSON files structured with metadata, node definitions, and edge definitions:

```json
{
  "num_nodes": 4,
  "num_edges": 5,
  "nodes": [
    {"enter_freq": 15.0, "leave_prob": 0.25},
    {"enter_freq": 20.0, "leave_prob": 0.10}
  ],
  "edges": [
    {"source": 0, "target": 1, "length": 0.5},
    {"source": 1, "target": 2, "length": 1.2}
  ]
}
```

### Bundled Topologies

The `traffic/config/` directory includes several sample topologies:
- `dummy.json`: Minimal 4-node, 5-edge network (9 LPs total) for quick testing and validation.
- `trento.json`: Detailed road network of Trento, Italy (12,785 LPs total).
- `rome.json`: Road network of Rome, Italy.
- `rostock.json`: Road network of Rostock, Germany.
- `shenzhen.json`: Road network of Shenzhen, China.

### Generating Topologies from OpenStreetMap

The bundled script `config/generateTopology.py` can download and convert real-world OpenStreetMap bounding boxes into compatible JSON topologies using OSMnx:

```sh
python3 config/generateTopology.py
```

---

## Command-Line Options

### Traffic-Specific Options

| Option | Long Option | Description | Required | Default |
|---|---|---|---|---|
| `-T <FILE>` | `--topology <FILE>` | Path to the JSON topology file | Yes | None |
| `<FILE>` | (positional argument) | Positional path to the JSON topology file | Yes | None |

Note: If no topology file is specified (either via `--topology` or as a positional argument), the model will display an error message and exit immediately.

### Standard ROOT-Sim Options

| Option | Long Option | Description | Default |
|---|---|---|---|
| `-c <N>` | `--ncores <N>` | Number of worker threads (0 = auto-detect core count) | Auto |
| `-p <N>` | `--nprocesses <N>` | Number of logical processes (overridden by topology size) | From JSON |
| `-t <T>` | `--termination-time <T>` | Simulation termination virtual time in seconds | 3600.0 (1 hour) |
| `-s <FILE>` | `--stats <FILE>` | Path to output statistics file | `traffic` |
| `-l <LEVEL>` | `--log-level <LEVEL>` | Verbosity (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`) | `info` |
| | `--serial` | Run sequentially using single-threaded event processing | Disabled |
| | `--timewarp` | Run in parallel using optimistic Time Warp synchronization | Enabled |
| `-h` | `--help` | Display usage banner and option descriptions | |

---

## Building

### Standalone Build

The model can be built directly inside its own folder:

```sh
cd traffic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Top-Level Build

To compile Traffic alongside all other models from the repository root:

```sh
cd ..
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target traffic
```

---

## Execution Recipes

### 1. Minimal Sequential Run

Execute a short run on the dummy topology using the serial engine:

```sh
./build/traffic/traffic --serial -t 50 traffic/config/dummy.json
```

### 2. Parallel Time Warp on Dummy Network

Run with 2 worker threads up to virtual time 100:

```sh
./build/traffic/traffic -c 2 -t 100 --topology traffic/config/dummy.json
```

### 3. Full-Scale Urban Network (Trento)

Run a multi-threaded parallel simulation on the Trento road network (12,785 LPs) for 1 hour of virtual time:

```sh
./build/traffic/traffic -c 4 -t 3600 traffic/config/trento.json
```

### 4. Sequential Run with Custom Statistics Output

```sh
./build/traffic/traffic --serial -t 600 -s trento_stats.txt traffic/config/trento.json
```

---

## License

This software is distributed under the GNU General Public License v3.0 (GPL-3.0). See source headers for details.
