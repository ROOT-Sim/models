# Queueing Network Simulation Model

A discrete event simulation model of closed and open queueing networks with FCFS service stations, implemented for ROOT-Sim 3.0.

## Overview

Queueing networks represent one of the foundational benchmark and analytical workloads in parallel discrete event simulation (PDES) and operational research. The model captures networks of First-Come First-Served (FCFS) service stations traversed by competing customers/jobs, originally formulated by Chandy and Misra (1979, 1981), evaluated under Time Warp by Fujimoto (1989), and used by Nicol (1988) to analyze lookahead and service-time presampling.

Key capabilities include:
- Flexible Topologies: 2D Torus grid (4-neighbor random routing), 1D Ring (unidirectional cyclic routing), Tandem queue (sequential multistage processing with entry and exit), and Fully Connected Mesh (uniform random routing).
- Closed and Open Networks: closed mode sustains a constant population of circulating jobs ($N \times J$); open mode generates Poisson arrivals at entrance stations.
- Multi-Server Stations: each station can host $K \ge 1$ parallel servers.
- General Service Distributions: exponential, deterministic, uniform, and two-stage hyperexponential.
- Nicol's Lookahead and Presampling: optional pre-sampling of upcoming job service times upon arrival or departure, exposing lower bounds on earliest output events.
- Verification and Theory: computes empirical queue lengths, server utilizations, residence times, and throughputs.

---

## Model Architecture and Mathematical Foundations

### 1. Station State and Queuing Dynamics

Each service station is modeled as an independent Logical Process (LP). A station comprises:
- $K$ identical servers (each either idle or busy with an associated departure time).
- A FIFO waiting queue holding jobs when all $K$ servers are busy.
- Statistical accumulators for time-integrated server busy time $\int B(t) dt$ and jobs in system $\int N(t) dt$.

### 2. Service Time Distributions and Lookahead

The total service duration $S$ satisfies:

$$S = L + X$$

where $L \ge 0$ is a deterministic minimum duration (the lookahead offset), and $X$ is a non-negative random variable with mean $\mu - L$ drawn according to:
- Exponential: $X \sim \text{Exp}(1 / (\mu - L))$
- Deterministic: $X = \mu - L$
- Uniform: $X \sim \text{Uniform}(0.2(\mu - L), 1.8(\mu - L))$
- Hyperexponential: two-stage branch with probabilities 0.8 (fast phase) and 0.2 (slow phase).

### 3. Nicol's Service-Time Presampling

In standard discrete event execution, the service time for a job waiting in queue is drawn when a server becomes free. With `--presample`, the service duration is pre-computed when the job enters the queue. This exposes the minimum departure time in advance, establishing an explicit lookahead guarantee for conservative and hybrid protocols without altering the statistical distribution of the simulation.

---

## Event Architecture

Events are strictly numbered to maintain causal ordering within ROOT-Sim:

| Event Type | Numerical Value | Description |
|------------|-----------------|-------------|
| `EVENT_DEPARTURE` | 1 | A server completes a job, frees capacity, forwards the customer to the next station, and starts the next queued job if available. |
| `EVENT_ARRIVAL` | 2 | A job arrives at a station. If an idle server exists, service starts immediately; otherwise, the job is enqueued. |
| `EVENT_OPEN_ARRIVAL` | 3 | External Poisson arrival generator for open networks. |

---

## Configuration Options

| Option | Argument | Description | Default |
|--------|----------|-------------|---------|
| `--topology` | `<TYPE>` | Network topology: `torus`, `ring`, `tandem`, `fcmesh` | `torus` |
| `--grid-dim` | `<D>` | Grid dimension for torus topology ($D \times D$ LPs) | `4` |
| `--jobs-per-station` | `<J>` | Number of initial circulating jobs per station (closed mode) | `10` |
| `--open` | None | Run in open queueing network mode | Closed |
| `--lambda` | `<RATE>` | External Poisson arrival rate per second (open mode) | `1.0` |
| `--servers` | `<K>` | Number of parallel servers per station | `1` |
| `--service` | `<DIST>` | Service distribution: `exp`, `det`, `uniform`, `hyperexp` | `exp` |
| `--mean` | `<TIME>` | Mean service duration in seconds | `1.0` |
| `--lookahead` | `<TIME>` | Minimum service time offset $L$ | `0.0` |
| `--presample` | None | Enable Nicol's service-time presampling | Disabled |
| `--check` | None | Check theoretical invariants and Little's Law | Disabled |

Standard ROOT-Sim runtime arguments:
- `-c, --ncores <N>`: number of worker threads.
- `-p, --nprocesses <N>`: total number of LPs.
- `-t, --termination-time <T>`: simulation virtual time limit.
- `--serial`: single-threaded sequential execution.
- `--timewarp`: parallel optimistic Time Warp execution.

---

## Compilation and Execution

### Top-Level Build

```bash
cd models
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target queueing_network
```

### Standalone Build

```bash
cd models/queueing_network
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Example Runs

1. Closed 16-station Torus in sequential mode:
```bash
./build/queueing_network/queueing_network --serial --topology torus --grid-dim 4 --jobs-per-station 10 -t 100
```

2. Parallel Time Warp on 4 threads with Nicol's presampling and lookahead:
```bash
./build/queueing_network/queueing_network -c 4 --topology torus --grid-dim 8 --lookahead 0.2 --presample -t 200
```

3. Open Tandem network with 8 processing stages:
```bash
./build/queueing_network/queueing_network -c 2 --topology tandem -p 8 --open --lambda 0.8 --mean 1.0 -t 500
```
