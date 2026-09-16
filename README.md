# ROOT-Sim Simulation Models

A collection of discrete event simulation models targeting ROOT-Sim 3.0, showcasing diverse domains including synthetic benchmarks, cellular networks, distributed transactional databases, agent-based ecological and social simulations, epidemic dynamics, robotics exploration, and wireless sensor networks.

## Directory Structure

| Directory | Model Name | Domain | Description |
|-----------|------------|--------|-------------|
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

This compiles all 10 model executables into their respective subdirectories under `build/`.

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

## License

This software collection is distributed under the GNU General Public License v3.0 (GPL-3.0). See individual source files for copyright notices.
