# Air Traffic and Airport Network Simulation Model

A discrete-event simulation model of an air traffic network with airport operations, runways, holding stacks, and gate turnarounds for ROOT-Sim.

## Overview

Based on Fujimoto's synthetic airport benchmark and FAA air traffic control simulation models, each Logical Process (LP) represents an airport. Airports maintain multiple runways for landings and departures, gates for passenger turnaround, and holding stacks for incoming flights. Flights transition between airports across flight routes, experiencing cruise propagation delays and queueing contention at runway thresholds.

## Key Features

- **Runway Scheduling**: Runways serve landings and takeoffs with configurable occupancy times; safety-critical airborne holding stacks receive landing priority.
- **Hub-and-Spoke Topology**: Configurable hub ratio routing traffic through major hub airports.
- **Fleet Conservation**: Tracks aircraft invariants across flights, gates, holding stacks, and runways.
- **Delay Analytics**: Measures airborne holding stack delays, ground departure delays, and runway utilization.

## Command-Line Options

- `--airports <N>`: Number of airports (LPs, default: 16).
- `--planes <M>`: Total aircraft fleet size (default: 320).
- `--runways <K>`: Runways per airport (default: 2).
- `--gates <G>`: Gate capacity per airport (default: 20).
- `--hub-ratio <H>`: Fraction of flights directed to hub airports (default: 0.6).
- `--flight-time <T>`: Mean flight cruise time in minutes (default: 60.0).
- `--turnaround <T>`: Mean gate turnaround time in minutes (default: 45.0).
- `--runway-time <T>`: Runway occupancy time per landing/takeoff in minutes (default: 2.0).
- `--check`: Verify aircraft conservation invariant.

## Running the Model

### Sequential Mode
```bash
./airport --serial --airports 16 --planes 320 -t 1440
```

### Parallel Time Warp Mode
```bash
./airport -c 4 --airports 16 --planes 320 -t 1440
```
