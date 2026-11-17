# ROOT-Sim Simulation Models

A collection of discrete event simulation models targeting ROOT-Sim 3.0, showcasing diverse domains including synthetic benchmarks, cellular networks, distributed transactional databases, agent-based ecological and social simulations, epidemic dynamics, robotics exploration, and wireless sensor networks.

## Directory Structure

| Directory | Model Name | Domain | Description |
|-----------|------------|--------|-------------|
| `egress/` | Egress | Emergency / ABM | Agent-based crowd emergency evacuation simulation on hexagonal grids with obstacles, panic dynamics, and explosions. |
| `phold/` | PHOLD | Synthetic Benchmark | Parameterizable Fujimoto synthetic benchmark supporting custom granularity, fan-out, and hot spots. |
| `pcs/` | PCS | Telecommunications | Personal Communication Services cellular network simulation on hexagonal grids. |
| `nosql/` | NoSQL | Distributed Systems | Distributed non-relational database transaction processing and validation. |
| `tcar/` | TCAR | Robotics / ABM | Autonomous mobile robot terrain exploration using visit-count trails. |
| `segregation/` | Schelling Segregation | Social Dynamics / ABM | Agent-based Schelling spatial segregation model on hexagonal topologies. |
| `stupid_model/` | StupidModel | Ecology / ABM | Reference bug ecology benchmark with food production, feeding, and reproduction. |
| `sugarscape/` | Sugarscape | Artificial Society / ABM | Epstein and Axtell's artificial society model with resource foraging and wealth. |
| `tuberculosis/` | Tuberculosis | Epidemiology / ABM | Spatial epidemic spread model of tuberculosis with risk factors and treatment. |
| `robot_explore/` | Robot Explore | Robotics / ABM | Multi-robot cooperative terrain exploration with A* pathfinding and map sharing. |
| `sensors/` | Sensors (CTP) | Wireless Sensor Networks | Wireless Sensor Network running the Collection Tree Protocol (CTP) and CSMA MAC. |
| `traffic/` | Traffic | Urban Mobility | Microscopic car traffic simulation over graph road networks with congestion, accidents, and jam notifications. |
| `colliding_pucks/` | Colliding Pucks | Classical PDES / Physics | Continuous 2D elastic billiards simulation with naive, fixed, and adaptive quadtree spatial indexing. |
| `sharks/` | Sharks World | Ecological PDES / Spatial | Toroidal ocean predator-prey simulation with overlapping sectors and spheres of influence. |
| `queueing_network/` | Queueing Network | Operations Research / Queueing | Classical Jackson / BCMP open and closed queueing networks with Nicol presampling lookahead and Little's Law validation. |
| `logic_circuit/` | Logic Circuit | Digital CAD / VLSI | Gate-level digital logic simulator parsing ISCAS-85/89 netlists with inertial/transport delays and FNV-1 signatures. |
| `airport/` | Airport Operations | Transportation / Aviation | Multi-runway airport network simulation based on Fujimoto's benchmark with airborne holding priority and gate turnaround. |
| `ising/` | Ising Spin Model | Statistical Physics | 2D continuous-time ferromagnetic spin lattice using Lubachevsky's block decomposition, Glauber dynamics, and Onsager critical temperature. |
| `interconnect/` | Interconnect Network | Computer Architecture | $k$-ary $n$-cube direct network on $n$-dimensional tori with virtual channels, DOR/ROMM/Deflection routing, and synthetic traffic. |
| `smmp/` | SMMP | Computer Architecture | Shared-memory multiprocessor with private L1 caches, interconnection delays, and interleaved memory bank contention. |
| `raid5/` | RAID-5 Disk Array | Storage Systems | RAID-5 storage subsystem with rotational mechanics, head seeking, small-write RMW penalty, and dynamic drive failure reconstruction. |
| `spiking_net/` | Spiking Neural Network | Computational Neuroscience | Event-driven Leaky Integrate-and-Fire (LIF) network with refractory periods, delayed axonal propagation, and background Poisson drive. |
| `wildfire/` | Wildfire Propagation | Environmental / Physics | 2D wildland surface fire propagation using Rothermel semi-empirical spread equations, wind vectors, slope aspect, and cellular burnout. |
| `sandpile/` | BTW Sandpile | Complex Systems / SOC | Prototypical Bak-Tang-Wiesenfeld self-organized criticality (SOC) lattice with toppling avalanches and dissipative boundary losses. |
| `common/` | Common Library | Infrastructure | Shared Agent-Based Modeling (ABM) runtime, CLI parser, and CMake discovery modules. |

## Prerequisites

To compile and run the models, ensure the following prerequisites are installed:

- CMake (version 3.14 or later)
- C11-compliant C compiler (such as GCC or Clang)
- ROOT-Sim core library (`librscore`, develop branch)
- ROOT-Sim RNG library (`librsrng`)
- ROOT-Sim Topology library (`librstopology`)
- Optional: OpenMPI or MPICH for distributed-memory execution

The build system automatically locates ROOT-Sim components using `common/FindROOTSim.cmake`. By default, sibling repository checkouts (`../core`, `../random-number-generators`, `../topology`) or standard system installations are inspected. Alternatively, set the environment or CMake variables:
- `ROOTSIM_CORE_INCLUDE_PATH` and `ROOTSIM_CORE_LIBRARIES`
- `ROOTSIM_RNG_INCLUDE_PATH` and `ROOTSIM_RNG_LIBRARIES`
- `ROOTSIM_TOPOLOGY_INCLUDE_PATH` and `ROOTSIM_TOPOLOGY_LIBRARIES`

## Building the Models

### Building All Models Together

From the root of the `models` repository:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This compiles all 24 model executables into their respective subdirectories under `build/`.

### Building a Single Model

Each model contains its own standalone `CMakeLists.txt` and can be built independently:

```sh
cd phold
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Running Simulations

All models share a standardized command-line interface provided by `common/argparse.h`:

```sh
./build/<model_name>/<model_name> [options]
```

### Standard Command-Line Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-c <N>` | `--ncores <N>` | Number of worker threads (0 = auto-detect) | Auto |
| `-p <N>` | `--nprocesses <N>` | Number of Logical Processes (LPs) | Model-dependent |
| `-t <T>` | `--termination-time <T>` | Virtual time limit for simulation | Model-dependent |
| `-s <FILE>` | `--stats <FILE>` | Path to CSV/text statistics output file | None |
| `-l <LEVEL>` | `--log-level <LEVEL>` | Logging verbosity (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`) | `info` |
| | `--serial` | Execute sequentially in single-threaded event-queue mode | Disabled |
| | `--timewarp` | Execute concurrently using optimistic Time Warp synchronization | Enabled |
| `-h` | `--help` | Display command usage and option summary | |

### Example Invocations

Run PHOLD with 4 threads, 1024 LPs, up to virtual time 1000:
```sh
./build/phold/phold -c 4 -p 1024 -t 1000
```

Run PCS sequentially with 16 cells up to virtual time 500:
```sh
./build/pcs/pcs --serial -p 16 -t 500
```

Run Sugarscape in parallel Time Warp mode with log level warning:
```sh
./build/sugarscape/sugarscape -c 8 -p 64 -t 200 -l warn
```

Run Traffic sequentially on the dummy topology:
```sh
./build/traffic/traffic --serial -t 3600 traffic/config/dummy.json
```

Run Colliding Pucks in parallel Time Warp mode with adaptive quadtree spatial indexing:
```sh
./build/colliding_pucks/colliding_pucks -c 4 --index dynamic --cluster -t 100
```

Run Sharks World in parallel Time Warp mode on an 8x8 sector grid (64 LPs):
```sh
./build/sharks/sharks -c 4 --grid-dim 8 --creatures 1000 -t 50
```

## License

This software collection is distributed under the GNU General Public License v3.0 (GPL-3.0). See individual source files for copyright notices.
