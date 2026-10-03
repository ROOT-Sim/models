# k-ary n-cube Network Interconnect Model

A discrete-event parallel simulation of $k$-ary $n$-cube direct network interconnect topologies (meshes, tori, hypercubes) for multicomputers and Network-on-Chip (NoC) architectures in ROOT-Sim.

## Overview

Based on William Dally's foundational interconnect architecture models, this simulation models:
- $k$-ary $n$-cube networks of $N = k^n$ router nodes with periodic torus boundary conditions using `rstopology`'s `TOPOLOGY_TORUS_ND`.
- $2n$ network communication ports + 1 local injection/ejection port per router node.
- Multiple virtual channels (VCs) per physical link with credit-based flow control.
- Deterministic and adaptive routing algorithms:
  - **DOR (Dimension-Order Routing / $e$-cube)**: deadlock-free minimal routing.
  - **Deflection Routing (Hot-Potato)**: bufferless deflection when preferred output port is contested.
  - **ROMM (Randomized Oblivious Multi-phase Minimal)**: two-phase randomized routing to balance link load.
- Synthetic traffic patterns: Uniform random, Bit-complement, Transpose, and Tornado.

## Command-Line Options

- `--k-ary <k>`: Radix $k$ of $k$-ary $n$-cube (default: 4).
- `--n-cube <n>`: Dimension $n$ of $k$-ary $n$-cube (default: 3, yielding 64 router nodes).
- `--routing <dor|deflection|romm>`: Routing algorithm (default: dor).
- `--traffic <uniform|complement|transpose|tornado>`: Traffic pattern (default: uniform).
- `--inject-rate <lambda>`: Flit injection rate per router node (default: 0.05).
- `--packet-length <L>`: Flits per packet (default: 4).
- `--vcs <V>`: Virtual channels per physical port (default: 2).
- `--buffer-depth <B>`: Flit buffer capacity per VC (default: 8).
- `--router-delay <D>`: Pipeline router switching latency (default: 1.0).
- `--channel-delay <D>`: Physical channel link transmission delay (default: 1.0).
- `--check`: Verify theoretical zero-load latency bounds.

## Running the Model

### Sequential Mode
```bash
./interconnect --serial --k-ary 4 --n-cube 3 --routing dor -t 200
```

### Parallel Time Warp Mode
```bash
./interconnect -c 4 --k-ary 4 --n-cube 3 --routing dor -t 200
```
