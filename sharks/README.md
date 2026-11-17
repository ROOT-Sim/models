# Sharks World Simulation Model

A discrete event simulation model of Sharks World in a toroidal ocean, implemented for ROOT-Sim 3.0.

## Overview

The Sharks World model is a classic parallel discrete event simulation (PDES) benchmark originally formulated by Darrell Conklin, John Cleary, and Brian Unger at the University of Calgary ("The Sharks World: A study in distributed simulation design").

In this model, two types of marine creatures (sharks and fish) move across a continuous two-dimensional toroidal ocean of dimension $M \times M$. Sharks swim and attack any fish that enters within their effective attack range $A$. When a shark attacks a fish, the fish is killed and removed from the simulation.

Key features include:
- Toroidal Ocean Geometry: continuous wrap-around coordinates in both dimensions without reflective boundaries.
- Overlapping Sector Decomposition (Method 2 / Spheres of Influence): the ocean is divided into an $S \times S$ grid of sectors, each managed by an independent Logical Process (LP).
- Sphere of Influence: each sector maintains an extended outer bounding box of margin $A$ (the shark attack radius) around its nominal boundaries.
- Distributed Attack Resolution: a sector has exclusive authority to resolve attacks for sharks whose nominal centers lie within its boundary, querying both local creatures and overlapping border creatures.
- Non-blocking Projection Pipeline: creatures entering the outer bounds of adjacent sectors generate arrival events, while creatures leaving the outer boundary are garbage-collected.
- Coordinated Kill Broadcast: when a fish is attacked, a kill notification is broadcast to surrounding overlapping neighbor sectors to maintain global casualty consistency.

---

## Model Geometry and Mathematics

### 1. Torus Coordinate System

The world is a continuous square $[0, M) \times [0, M)$ with periodic boundary conditions.
For any two coordinates $x_1$ and $x_2$, the minimum displacement along an axis of length $M$ is:

$$\Delta x = |x_1 - x_2|$$
$$\text{if } \Delta x > \frac{M}{2}, \quad \Delta x = M - \Delta x$$

The shortest distance between creatures on the torus is:

$$d = \sqrt{(\Delta x)^2 + (\Delta y)^2}$$

### 2. Creature Kinematics

Each creature $i$ has:
- Coordinates $(x, y)$
- Speed $v \in [1.0, v_{max}]$ and heading angle $\theta \in [0, 360^\circ)$
- Velocity components $v_x = v \cos \theta$, $v_y = v \sin \theta$
- Reference timestamp $t_{ref}$
- Status: alive or dead, version counter, and home sector LP

At time $t \ge t_{ref}$, position wraps according to:

$$x(t) = (x(t_{ref}) + v_x (t - t_{ref})) \bmod M$$
$$y(t) = (y(t_{ref}) + v_y (t - t_{ref})) \bmod M$$

### 3. Shark-Fish Attack Intercept Prediction

Let a shark $S$ and a fish $F$ have relative displacement $\Delta \mathbf{p} = \mathbf{p}_F - \mathbf{p}_S$ (computed under torus minimum image convention) and relative velocity $\Delta \mathbf{v} = \mathbf{v}_F - \mathbf{v}_S$.

An attack occurs when the distance equals the attack radius $A$:

$$\|\Delta \mathbf{p} + \Delta \mathbf{v} \Delta t\|^2 = A^2$$

Expanding yields the quadratic equation:

$$a \Delta t^2 + b \Delta t + c = 0$$

where:
- $a = \|\Delta \mathbf{v}\|^2$
- $b = 2 (\Delta \mathbf{p} \cdot \Delta \mathbf{v})$
- $c = \|\Delta \mathbf{p}\|^2 - A^2$

If $c \le 0$, the fish is already within attack range and is resolved immediately.
If $a < 10^{-12}$ or $b \ge 0$, the creatures are relative stationary or diverging.
If the discriminant $\Delta = b^2 - 4ac \ge 0$, the earliest entry time is:

$$\Delta t = \frac{-b - \sqrt{\Delta}}{2a}$$

The attack is scheduled for simulation time $t = t_{now} + \Delta t$ if $\Delta t > 10^{-6}$.

### 4. Overlapping Sectoring (Method 2)

Each sector $(X_s, Y_s)$ has nominal dimension $W_s = M / S$ and $H_s = M / S$:
- Nominal Region: $[X_s W_s, (X_s + 1) W_s) \times [Y_s H_s, (Y_s + 1) H_s)$
- Outer Region (Sphere of Influence): extended by margin $A$ in all four directions:
  $$[-W_s/2 - A, W_s/2 + A] \times [-H_s/2 - A, H_s/2 + A]$$

Boundary interactions:
- Entry into Neighbor Outer Region: when a creature moving east crosses $x_{rel} = W_s/2 - A$, it enters the East neighbor's outer bounding box. The sector schedules an `EVENT_ARRIVAL` to the East neighbor at that crossing time.
- Exit from Sector Outer Region: when a creature moving east crosses $x_{rel} = W_s/2 + A$, it exits this sector's outer bounding box and is deleted from the sector state.

---

## Event Architecture

The model uses three numerical event types, ordered strictly by causality priority in ROOT-Sim:

| Event Type | ID | Purpose |
|------------|----|---------|
| `EVENT_KILL` | 1 | Notifies neighbor sectors that a fish was killed at $(x, y)$. |
| `EVENT_ARRIVAL` | 2 | Delivers a creature entering an overlapping neighbor's outer bounding box. |
| `EVENT_PLAN` | 3 | Sector wake-up event to resolve the earliest predicted attack or boundary crossing. |

---

## Configuration and Command-Line Options

| Parameter | Short | Long Option | Default | Description |
|-----------|-------|-------------|---------|-------------|
| Creatures | | `--creatures <N>` | `1000` | Total number of initial creatures |
| Grid Dim | | `--grid-dim <S>` | `4` | Torus sector grid dimension ($S \times S$ LPs) |
| World Size | | `--world-size <M>` | `1000.0` | Ocean dimension ($M \times M$) |
| Sharks Ratio | | `--sharks-ratio <R>` | `0.10` | Fraction of creatures that are sharks (10%) |
| Attack Range | | `--attack-range <A>` | `25.0` | Shark attack radius ($A$) |
| Max Velocity | | `--max-velocity <V>` | `20.0` | Maximum creature velocity |

Standard ROOT-Sim runtime arguments:
- `-c, --ncores <N>`: number of worker threads.
- `-t, --termination-time <T>`: simulation end virtual time.
- `--serial`: run in sequential mode.
- `--timewarp`: run in Time Warp optimistic parallel mode.

---

## Compilation and Execution

### Top-Level Build

```bash
cd models
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sharks
```

### Standalone Build

```bash
cd models/sharks
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Example Runs

1. Sequential execution with 500 creatures:
```bash
./build/sharks/sharks --serial --creatures 500 -t 50
```

2. Optimistic parallel Time Warp execution on 4 cores with an 8x8 sector grid (64 LPs):
```bash
./build/sharks/sharks -c 4 --grid-dim 8 --creatures 2000 -t 100
```

3. High-density predator scenario (25% sharks, attack range 40):
```bash
./build/sharks/sharks -c 4 --creatures 2000 --sharks-ratio 0.25 --attack-range 40 -t 50
```

---

## References

1. D. Conklin, J. Cleary, and B. Unger. *The Sharks World (A study in distributed simulation design)*. Research Report, Department of Computer Science, The University of Calgary, 1990.
2. D. Jefferson, B. Beckman, et al. *Distributed Simulation and the Time Warp Operating System*. ACM SIGOPS Operating Systems Review, 1987.
