# PHOLD Simulation Model

An advanced, parameterizable implementation of the classic PHOLD synthetic benchmark (Fujimoto, 1990) for parallel discrete event simulation (PDES) on ROOT-Sim 3.0.

## Overview

The PHOLD model is the standard synthetic benchmark for evaluating the efficiency, scalability, and synchronization overhead of parallel and distributed discrete event simulation engines.

While traditional PHOLD implementations only permit uniform random message destinations with a fixed event population, this version offers full runtime parameterization:
- Configurable synthetic CPU load per event (`--event-us`).
- Variable message fan-out with sterile events (`--fan-out`).
- Hot-spot LP clustering to induce spatial contention and evaluate rollback mechanics (`--hot-spots`, `--prob-hot-hit`).
- Configurable event timestamp distributions, lookahead, and population sizes (`--mean`, `--lookahead`, `--start-events`, `--p-remote`).
- Dual termination criteria: simulation virtual time (`-t`) or committed event counts per LP (`--num-events`).
- Cross-platform architecture support for hardware cycle counting on x86_64, ARM64 (Apple Silicon), and generic POSIX systems.

---

## Benchmark Theoretical Model

### 1. Event Circulation and Population

In PHOLD, each Logical Process (LP) initializes a set of circulating events (`--start-events`, default: 1). The total active event population in the system is invariant over time:

$$\text{Active Population} = \text{LPs} \times \text{start\_events}$$

When an LP processes an active event (`EVENT`), it schedules exactly one new self-event to guarantee that the active population remains constant.

### 2. Message Fan-Out and Sterile Events

To evaluate communication networks, queue pressure, and multi-core interconnects without causing exponential event explosion, secondary messages are classified as **Sterile Events** (`STERILE_EVENT`):
- When `--fan-out` is set to 0, the model acts as classic PHOLD: with probability `--p-remote` (default: 0.25), a single sterile event is sent to a remote LP.
- When `--fan-out` is greater than 0, exactly that number of sterile events are dispatched to remote destinations on every event execution.
- When a destination LP receives a `STERILE_EVENT`, it burns the configured event CPU time (`--event-us`), but does not schedule further events.

### 3. Timestamp Calculation

All scheduled events receive an increment drawn from an exponential distribution with mean $\mu$ (`--mean`), plus a deterministic lookahead offset $\lambda$ (`--lookahead`):

$$T_{\text{event}} = T_{\text{now}} + \text{Expent}(\mu) + \lambda$$

Independent, reproducible pseudorandom streams are maintained per LP using `initialize_stream()` from the ROOT-Sim RNG library (`librsrng`).

### 4. Hot-Spot Contention Model

To simulate load imbalance and stress the optimistic synchronization engine (Time Warp) under uneven traffic distributions, destination LPs can be concentrated around designated hot spots:
- If `--hot-spots` is set to $K > 0$, an event destination targets one of the first $K$ LPs with probability `--prob-hot-hit`.
- With probability $1 - \text{prob\_hot\_hit}$, the destination is mapped to the remaining $N - K$ LPs.
- Destination assignment formula:
  - If $\text{Random}() < \text{prob\_hot\_hit}$: $\text{dest} = \text{dest} \pmod K$
  - Else: $\text{dest} = K + (\text{dest} \pmod{N - K})$

### 5. Synthetic Payload Granularity

To model realistic application workloads that perform computational work upon event receipt, each event executes a calibrated busy-wait loop:
- The elapsed execution duration is measured in hardware clock cycles.
- On ARM64 (macOS / Linux on Apple Silicon or Graviton), cycles are read via `mrs cntvct_el0` and calibrated against the system counter frequency (`mrs cntfrq_el0`, typically 24 MHz on Apple Silicon).
- On x86_64, cycles are read via the `rdtsc` instruction.
- On generic POSIX platforms, timestamps are tracked with nanosecond precision using `clock_gettime(CLOCK_MONOTONIC)`.
- The cycle rate per microsecond can be manually overridden with `--clocks-per-us`.

---

## State and Event Definitions

### Logical Process State (`struct phold_state`)

```c
struct phold_state {
    struct rng_t seed;              // Per-LP RNG stream
    unsigned int executed_events;  // Cumulative committed events counter
};
```

### Event Codes

| Event Macro | Value | Description |
|-------------|-------|-------------|
| `LP_INIT` | 0 | Simulation kernel initialization handler. Allocates state, initializes RNG stream, and seeds initial events. |
| `EVENT` | 1 | Primary circulating event. Burns CPU cycles, schedules self-event, and triggers fan-out messages. |
| `STERILE_EVENT` | 2 | Communication payload event. Burns CPU cycles on target LP without spawning descendant events. |
| `LP_FINI` | 3 | Simulation kernel finalization handler. |

---

## Command-Line Options

### Standard ROOT-Sim Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-c <N>` | `--ncores <N>` | Number of worker threads (0 = auto-detect core count) | Auto |
| `-p <N>` | `--nprocesses <N>` | Total number of Logical Processes (LPs) | 1024 |
| `-t <T>` | `--termination-time <T>` | Simulation termination virtual time | 1000 |
| `-s <FILE>` | `--stats <FILE>` | Path to output statistics file | None |
| `-l <LEVEL>` | `--log-level <LEVEL>` | Verbosity (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`) | `info` |
| | `--serial` | Execute sequentially using a single-threaded event list | Disabled |
| | `--timewarp` | Execute in parallel using optimistic Time Warp synchronization | Enabled |
| `-h` | `--help` | Display usage banner and option descriptions | |

### PHOLD Benchmark Parameters

| Option | Argument | Description | Default |
|--------|----------|-------------|---------|
| `--event-us` | `<TIME>` | Computational work per event in microseconds | 5 |
| `--fan-out` | `<VALUE>` | Number of sterile events generated per regular event | 0 |
| `--num-events` | `<VALUE>` | Committed events per LP required to trigger completion (0 = disabled) | 0 |
| `--hot-spots` | `<VALUE>` | Number of designated hot-spot LPs | 0 |
| `--prob-hot-hit` | `<PROB>` | Probability of routing an event to a hot-spot LP (range: 0.0 - 1.0) | 0.0 |
| `--p-remote` | `<PROB>` | Probability of routing to a remote LP when fan-out is 0 | 0.25 |
| `--mean` | `<TIME>` | Mean interval of the exponential delay distribution | 1.0 |
| `--lookahead` | `<TIME>` | Minimum non-zero lookahead offset added to timestamps | 0.0 |
| `--start-events` | `<N>` | Initial population of circulating events per LP | 1 |
| `--clocks-per-us` | `<N>` | CPU clock cycle rate per microsecond (auto-calibrated if omitted) | Auto |

---

## Building

### Standalone Build

The model can be built directly inside its own folder:

```sh
cd phold
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Top-Level Build

To compile PHOLD alongside the other models from the repository root:

```sh
cd ..
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target phold
```

---

## Benchmark Recipes

### 1. Baseline Parallel Scaling

Run with 1024 LPs across 4 worker threads up to virtual time 1000:
```sh
./build/phold/phold -c 4 -p 1024 -t 1000
```

### 2. Sequential Validation Run

Execute sequentially for debugging or measuring parallel speedup:
```sh
./build/phold/phold --serial -p 128 -t 200
```

### 3. Event-Count Bounded Run

Terminate as soon as every LP has processed and committed exactly 5000 events:
```sh
./build/phold/phold -c 4 -p 256 --num-events 5000
```

### 4. Heavy Computation Granularity Sweep

Simulate events with 50 microseconds of computation time each:
```sh
./build/phold/phold -c 4 -p 128 -t 100 --event-us 50
```

### 5. High-Contention Hot-Spot Stress Test

Direct 80% of all remote traffic to the first 4 LPs, stressing rollback recovery under heavy state divergence:
```sh
./build/phold/phold -c 4 -p 256 -t 500 --hot-spots 4 --prob-hot-hit 0.8
```

### 6. Communication Fan-Out Test

Generate 4 remote sterile messages for every regular event:
```sh
./build/phold/phold -c 4 -p 256 -t 200 --fan-out 4 --event-us 2
```

### 7. Large Event Density Run

Initialize 10 concurrent circulating events per LP:
```sh
./build/phold/phold -c 4 -p 512 -t 500 --start-events 10
```

---

## Statistics and Analysis

When specifying `-s <FILE>`, ROOT-Sim outputs runtime metrics including:
- Total committed and rolled-back events.
- Event commit rate (events/second).
- Rollback frequency and average rollback length.
- Global Virtual Time (GVT) progression rate.
- Memory consumption and state checkpoint counts.

---

## License

This software is distributed under the GNU General Public License v3.0 (GPL-3.0). See source headers for details.
