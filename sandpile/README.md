# Bak-Tang-Wiesenfeld (BTW) Abelian Sandpile Simulation Model

A discrete-event parallel simulation model of the prototypical self-organized criticality (SOC) Bak-Tang-Wiesenfeld sandpile lattice on 2D grids with avalanche cascades, toppling dynamics, and dissipative boundary losses for ROOT-Sim.

## Overview

Based on the foundational self-organized criticality model by Bak, Tang & Wiesenfeld (1987) and Dhar (1990), this model features:
- 2D spatial domain decomposition of $G \times G$ LPs, each managing a sub-lattice block of $B \times B$ cells (Lubachevsky spatial decomposition).
- Discrete slope height per cell $z(x, y) \ge 0$.
- Independent Poisson grain drops perturbing the lattice.
- Toppling rule: when $z(x, y) \ge z_c = 4$, the cell topples, shedding 4 grains to its orthogonal neighbors (N, S, E, W).
- Dissipative open boundaries: grains reaching the edge of the physical lattice fall off and leave the system, driving the lattice toward a critical stationary state characterized by scale-invariant power-law avalanche cascades:
  $$P(S) \sim S^{-\tau}, \quad \tau \approx 1.22$$
- Bounded toppling propagation delay $\Delta t_{\text{topple}}$ providing non-zero lookahead for optimistic parallel simulation.

## Command-Line Options

- `--grid-dim <G>`: LP grid dimension $G \times G$ (default: 4).
- `--block-dim <B>`: Sub-grid dimension per LP $B \times B$ cells (default: 8, max: 32).
- `--critical-height <Z>`: Threshold height before toppling (default: 4).
- `--drop-rate <R>`: Poisson grain addition rate per LP in Hz (default: 10.0).
- `--topple-delay <D>`: Propagation delay for toppled grains in seconds (default: 0.05).
- `--initial-fill <F>`: Initial maximum random height per cell (default: 3).
- `--torus`: Enable toroidal boundaries (default: open dissipative).
- `--check`: Verify self-organized criticality (SOC) activity.

## Running the Model

### Sequential Mode
```bash
./sandpile --serial --grid-dim 4 --block-dim 8 -t 10
```

### Parallel Time Warp Mode
```bash
./sandpile -c 4 --grid-dim 4 --block-dim 8 -t 10
```
