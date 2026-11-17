# Wildfire Propagation Simulation Model

A discrete-event parallel simulation model of wildland surface fire propagation over 2D landscape grids using the Rothermel semi-empirical equations, directional wind vectors, terrain slope, fuel moisture, and cellular combustion dynamics for ROOT-Sim.

## Overview

Based on classical wildland fire spread literature by Rothermel (1972), Lopes et al. (2002), and discrete-event cellular wildfire simulators (Muzy et al. 2005), this model features:
- 2D spatial domain decomposition of $G \times G$ LPs, each managing a sub-grid block of $B \times B$ cells (Lubachevsky spatial decomposition).
- Discrete-event state machine per cell: `CELL_UNBURNED` -> `CELL_BURNING` -> `CELL_BURNED`.
- Rothermel surface fire rate of spread (ROS):
  $$R = R_0 (1 + \Phi_w + \Phi_s)$$
  where $\Phi_w = C_w U^{1.5} \cos(\Delta \theta_w)$ captures directional wind speed and alignment, and $\Phi_s = 5.275 \tan^2(\phi) \cos(\Delta \theta_s)$ captures terrain inclination.
- Event-driven ignition front advancement: burning cells schedule asynchronous `EVENT_CELL_IGNITE` arrivals across their 8 Moore neighbors without time-stepped global grid polling.
- Lookahead enforcement: minimum inter-cell propagation delay ensures bounded positive lookahead across LP boundaries for Time Warp parallel execution.

## Command-Line Options

- `--grid-dim <G>`: LP grid dimension $G \times G$ (default: 4).
- `--block-dim <B>`: Sub-grid block dimension per LP $B \times B$ cells (default: 8, max: 32).
- `--cell-size <M>`: Cell resolution width in meters (default: 10.0).
- `--base-rate <R>`: Base spread rate $R_0$ in m/s (default: 0.20).
- `--burn-time <T>`: Cell combustion duration in seconds (default: 30.0).
- `--wind-speed <S>`: Wind velocity in m/s (default: 5.0).
- `--wind-dir <D>`: Wind direction in degrees [0=East, 90=North] (default: 0.0).
- `--moisture <M>`: Fuel moisture content [0.0-1.0] (default: 0.08).
- `--slope <S>`: Terrain slope inclination in degrees (default: 0.0).
- `--slope-azimuth <A>`: Terrain slope aspect in degrees (default: 0.0).
- `--torus`: Enable toroidal boundary wrapping (default: absorbing planar boundaries).
- `--check`: Verify Rothermel directional spread asymmetry.

## Running the Model

### Sequential Mode
```bash
./wildfire --serial --grid-dim 4 --block-dim 8 -t 100
```

### Parallel Time Warp Mode
```bash
./wildfire -c 4 --grid-dim 4 --block-dim 8 -t 100
```
