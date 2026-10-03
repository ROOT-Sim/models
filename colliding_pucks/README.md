# Colliding Pucks Simulation Model

A discrete event simulation model of 2D elastic colliding pucks moving across a partitioned billiards table, implemented for ROOT-Sim 3.0.

## Overview

The Colliding Pucks model is a classic parallel discrete event simulation (PDES) benchmark originally formulated by David Jefferson et al. at the Jet Propulsion Laboratory for the Time Warp Operating System (TWOS), analyzed in temporal logic by John Cleary (1990), and evaluated for spatial complexity by Harless & Rogers (WSC 1995).

In this model, circular elastic pucks of varying radius and mass travel at continuous velocities across a flat rectangular table bounded by rigid cushions. Pucks undergo perfectly elastic collisions with one another (conserving linear momentum and kinetic energy) and reflect specularly off the boundary cushions.

Key features include:
- Sectorized table decomposition: the table is partitioned into a GxG grid of sectors, each modeled as an independent Logical Process (LP).
- Event-driven motion planning: each sector predicts the earliest upcoming event within its boundaries (puck-puck collision, cushion reflection, or sector boundary exit) and schedules execution.
- Adaptive spatial indexing: supports naive all-pairs search, fixed-sector binning, and adaptive quadtree subdivision (Harless & Rogers 1995) to prune candidate collision pairs.
- Clustered and uniform initial configurations: evaluates performance and rollback behavior under both uniform and highly skewed spatial distributions.
- Conservation validation: built-in verification of momentum and kinetic energy conservation.

---

## Physical and Mathematical Formulation

### 1. Motion and Trajectory

Each puck is characterized by:
- Position vector p = (x, y)
- Velocity vector v = (vx, vy)
- Radius R and mass M
- Reference time t_ref

At any future time t >= t_ref, the position is given by:

p(t) = p(t_ref) + v * (t - t_ref)

### 2. Puck-Puck Collision Prediction

For two pucks 1 and 2 at current simulation time t_now:
Let delta_p = p1(t_now) - p2(t_now), delta_v = v1 - v2, and D = R1 + R2.
A collision occurs at time t_now + dt when ||delta_p + delta_v * dt||^2 = D^2.
This expands to the quadratic equation:

a * dt^2 + b * dt + c = 0

where:
- a = delta_v . delta_v
- b = 2 * (delta_p . delta_v)
- c = delta_p . delta_p - D^2

If a < 1e-12 or b >= 0, the pucks are parallel or separating.
Otherwise, the discriminant disc = b^2 - 4*a*c determines whether a collision will occur. If disc >= 0, the earliest impact time is:

dt = (-b - sqrt(disc)) / (2 * a)

### 3. Elastic Collision Response

Upon collision at time t, the pucks exchange momentum along the normal unit vector n connecting their centers:

n = (p1(t) - p2(t)) / ||p1(t) - p2(t)||

The relative velocity along the normal is v_rel = (v1 - v2) . n.
For perfectly elastic collision (restitution coefficient e = 1.0), the impulse magnitude is:

J = -2 * v_rel / (1 / M1 + 1 / M2)

Velocities are updated according to:

v1' = v1 + (J / M1) * n
v2' = v2 - (J / M2) * n

Total momentum (M1*v1 + M2*v2) and kinetic energy (0.5*M1*v1^2 + 0.5*M2*v2^2) are strictly conserved.

### 4. Cushion Reflections

When a puck hits an outer table boundary cushion:
- Left / Right wall: vx' = -vx
- Bottom / Top wall: vy' = -vy

---

## Sector Decomposition and Event Types

The table of dimensions W x H is partitioned into G x G sectors. Each sector LP is responsible for scheduling events for the pucks currently located within its area [x_min, x_max) x [y_min, y_max).

### Event Types

| Event | Code | Description |
|---|---|---|
| `LP_INIT` | 65534 | Initializes PRNG stream, computes sector bounding box, and creates initial pucks. |
| `EVENT_PLAN` | 1 | Self-scheduled event corresponding to the earliest predicted interaction in the sector. |
| `EVENT_PUCK_ARRIVAL` | 2 | Handover event when a puck crosses from an adjacent sector across the boundary. |
| `LP_FINI` | 65535 | Finalizes simulation, verifies conservation if enabled, and releases resources. |

---

## Spatial Indexing Strategies

Following Harless & Rogers (1995), the model provides three strategies for collision candidate search within each sector:

1. `naive`: Checks all N*(N-1)/2 pairs of pucks. Complexity: O(N^2).
2. `fixed`: Places pucks into fixed sub-bins and checks only intra-bin and adjacent-bin pairs. Complexity: O(n^2 * K) where n = N/K.
3. `dynamic`: Constructs an adaptive quadtree where nodes with more than t pucks (`--max-per-cell`) are recursively subdivided into quadrants. Bounds checks to O(t^2 * cells), reducing comparison count significantly in clustered distributions.

---

## Command-Line Options

### Colliding Pucks Specific Options

| Option | Argument | Description | Default |
|---|---|---|---|
| `--pucks` | `<N>` | Total number of pucks | 64 |
| `--grid-dim` | `<G>` | Sector grid dimension GxG (LPs = G*G) | 4 (16 LPs) |
| `--table-width` | `<W>` | Table width in units | 1000.0 |
| `--table-height` | `<H>` | Table height in units | 1000.0 |
| `--radius-min` | `<R>` | Minimum puck radius | 5.0 |
| `--radius-max` | `<R>` | Maximum puck radius | 10.0 |
| `--mass-min` | `<M>` | Minimum puck mass | 1.0 |
| `--mass-max` | `<M>` | Maximum puck mass | 5.0 |
| `--speed` | `<V>` | Initial puck speed | 50.0 |
| `--cluster` | none | Initial clustered placement in corner sector (1995 paper) | Uniform |
| `--index` | `<MODE>` | Candidate search mode: `naive`, `fixed`, `dynamic` | `dynamic` |
| `--max-per-cell` | `<T>` | Quadtree subdivision threshold t | 4 |
| `--check` | none | Verify momentum/energy conservation and print LP stats | Disabled |

### Standard ROOT-Sim Options

| Option | Long Option | Description | Default |
|---|---|---|---|
| `-c <N>` | `--ncores <N>` | Number of worker threads (0 = auto) | Auto |
| `-p <N>` | `--nprocesses <N>` | Number of LPs (overridden by grid-dim) | G*G |
| `-t <T>` | `--termination-time <T>` | Simulation termination virtual time | 3600.0 |
| `-s <FILE>` | `--stats <FILE>` | Path to stats output file | `colliding_pucks` |
| `-l <LEVEL>` | `--log-level <LEVEL>` | Verbosity (trace, debug, info, warn, error, fatal, silent) | `info` |
| | `--serial` | Run sequentially using single-threaded event processing | Disabled |
| | `--timewarp` | Run in parallel using optimistic Time Warp synchronization | Enabled |
| `-h` | `--help` | Display usage banner and option descriptions | |

---

## Building

### Standalone Build

```sh
cd colliding_pucks
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Top-Level Build

```sh
cd ..
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target colliding_pucks
```

---

## Execution Recipes

### 1. Minimal Sequential Run

```sh
./build/colliding_pucks/colliding_pucks --serial --pucks 64 -t 100
```

### 2. Parallel Time Warp on 4 Threads

```sh
./build/colliding_pucks/colliding_pucks -c 4 --grid-dim 4 --pucks 128 -t 200
```

### 3. Clustered High-Contention Benchmark with Dynamic Indexing

Simulate 256 pucks concentrated initially in a single sector, using the adaptive quadtree:

```sh
./build/colliding_pucks/colliding_pucks -c 4 --grid-dim 4 --pucks 256 --cluster --index dynamic -t 100
```

### 4. Verification Mode with Conservation Checks

```sh
./build/colliding_pucks/colliding_pucks --serial --pucks 32 --check -t 50
```

---

## License

This software is distributed under the GNU General Public License v3.0 (GPL-3.0). See source headers for details.
