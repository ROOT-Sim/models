# Tuberculosis Epidemic Model

A spatial agent-based epidemiological model simulating the transmission, progression, risk factors, and treatment of tuberculosis (*Mycobacterium tuberculosis*) running on ROOT-Sim 3.0.

## Overview

Tuberculosis (TB) is an airborne infectious disease characterized by complex progression dynamics, long latency periods, and significant dependence on socio-demographic risk factors.

This simulation models the spread of TB across a geographical territory partitioned into spatial regions (represented as a 2D grid):
- **Regions (LPs)**: Maintain census statistics of healthy susceptible individuals and manage local social contact mixing. Healthy individuals migrate across neighboring regions via daily binomial mobility draws (`MIDNIGHT`).
- **Infected Individuals (Agents)**: Represented as explicit agents with detailed clinical profiles, including age, disease status (latent vs. active), sputum smear positivity, risk comorbidities, and treatment adherence.
- **Epidemiological Mechanisms**: Incorporates transmission through respiratory droplet exposure, progression from latent to active disease, accelerated progression due to comorbidities (HIV, diabetes, smoking, alcohol abuse), and medical treatment (DOTS protocol) with adherence and default outcomes.

## Architecture

- **ABM Runtime**: Built on the shared ROOT-Sim ABM runtime layer (`common/abm.h`).
- **Square Grid Topology**: Spatial regions are arranged in a 2D square grid (`TOPOLOGY_SQUARE`) using `rstopology`.
- **Hybrid Population Dynamics**: Blends aggregate compartmental modeling for healthy populations with individualized agent-based tracking for infected and symptomatic individuals.

## Model Details

- **Region State (`region_t`)**:
  - `healthy`: Count of susceptible healthy individuals in the region.
  - `now`: Current local simulation virtual time.
- **Agent Attributes (`guy_t`)**:
  - Age, infection date, and active disease onset.
  - Sputum smear status (smear-positive or smear-negative, modulating infectiousness).
  - Comorbidities and risk factors (HIV, diabetes, smoking, malnutrition).
  - Treatment state: under therapy, therapy compliant, or defaulted.
- **Event Types**:
  - `LP_INIT`: Initializes regional population distributions and triggers initial cohort generation (`guy_init()`).
  - `MIDNIGHT`: Daily event triggering binomial migration of healthy individuals to neighboring regions.
  - `RECEIVE_HEALTHY`: Receives migrating healthy individuals from adjacent regions.
  - `INFECTION`: Evaluates contact exposure events and computes transmission probabilities.
  - `GUY_VISIT`: Arrival of an infected agent in a region, triggering contact-mixing calculations.
  - `GUY_LEAVE`: Agent departure or progression transition.
  - `GUY_INIT`: Instantiates infected agents in designated regions.
- **Parameters** (defined across `parameters.h` and `tbc.h`):
  - Contact rates, transmission coefficients, latent reactivation rates.
  - Relative risk multipliers for comorbidities (HIV, diabetes, tobacco smoking, alcohol).
  - Treatment completion and cure probabilities.

## Building

### Standalone Build

```sh
cd tuberculosis
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tuberculosis
```

## Running

```sh
./build/tuberculosis [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of spatial regions (default: 16; auto-squared to grid dimensions).
- `-t, --termination-time <T>`: Virtual time limit in days (default: 10).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run across 64 regions on 4 worker threads:
```sh
./build/tuberculosis -c 4 -p 64 -t 50
```

Run sequentially:
```sh
./build/tuberculosis --serial -p 36 -t 30
```
