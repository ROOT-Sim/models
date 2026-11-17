# 2D Continuous-Time Ising Model (Lubachevsky's Decomposition)

A discrete-event parallel simulation of the 2D Ising spin system using Boris Lubachevsky's continuous-time spatial block decomposition with halo boundary updates for ROOT-Sim.

## Overview

In statistical physics, the Ising model on a square lattice exhibits a second-order phase transition at Onsager's analytical critical temperature:
$$T_c = \frac{2}{\ln(1 + \sqrt{2})} \approx 2.269185$$

In this model:
- The $L \times L$ periodic lattice is decomposed into $P \times P$ sub-grids of size $B \times B$ spins.
- Each Logical Process (LP) manages a local $B \times B$ block of spins plus 1-cell halo boundaries for its 4 periodic neighbors on the torus.
- Continuous-time Glauber dynamics / Metropolis-Hastings update spins asynchronously.

## Lubachevsky's Improvements

Based on Boris D. Lubachevsky (1988 JCP, 1989 CACM):
1. **Lookahead / Minimum Propagation Delay**: Changes at site $x$ take at least distance $d$ hops to influence a remote site. Within a block of size $B \times B$, an internal update at distance $d$ from the boundary cannot affect the boundary in fewer than $d$ steps.
2. **Opaque Periods**: Defining a guaranteed lookahead window (`--lookahead` / `--opaque-period`) during which an LP guarantees no external messages will disrupt neighbor states.
3. **Filtering Non-Events**: In Metropolis Monte Carlo, trials that do not result in accepted spin flips (rejections) do not change the physical spin state and never emit inter-processor communication.
4. **Decoupled Interior vs Border Scheduling**: Interior spins have zero immediate dependence on neighbor states, enabling high concurrency and minimal inter-LP synchronization.
5. **Implicit Ground-State Boundary Initialization**: Eliminates redundant initial synchronization storms across processors.

## Command-Line Options

- `--grid-dim <P>`: Number of LPs per dimension in $P \times P$ torus (default: 4, yielding 16 LPs).
- `--block-size <B>`: Number of spins per dimension $B \times B$ per LP (default: 16, yielding 256 spins/LP, 4096 spins total).
- `--temperature <T>`: System temperature $T$ (default: 2.269).
- `--field <H>`: External magnetic field $h$ (default: 0.0).
- `--j-coupling <J>`: Exchange coupling constant $J$ (default: 1.0).
- `--flip-rate <R>`: Attempt rate per spin per unit time (default: 1.0).
- `--halo-latency <L>`: Lubachevsky minimum delay / halo latency (default: 0.01).
- `--lookahead <L>`: Alias for `--halo-latency` / opaque period (default: 0.01).
- `--opaque-period <L>`: Alias for `--halo-latency` (default: 0.01).
- `--check`: Verify theoretical critical magnetization bounds.

## Running the Model

### Sequential Mode
```bash
./ising --serial --grid-dim 4 --block-size 16 --temperature 2.269 -t 50
```

### Parallel Time Warp Mode
```bash
./ising -c 4 --grid-dim 4 --block-size 16 --temperature 2.269 --lookahead 0.05 -t 50
```
