# Shared-Memory Multiprocessor (SMMP) Simulation Model

A discrete-event parallel simulation model of shared-memory symmetric multiprocessors with multiple processing cores, private caches, interconnection network delays, and interleaved memory banks for ROOT-Sim.

## Overview

Based on classical multiprocessor queuing models by Cota & Sargent (1990) and Kumar (1989), this model captures:
- $P$ Processor LPs executing compute instructions with stochastic think times.
- Private L1 cache hit/miss evaluation.
- $M$ Memory Bank LPs with interleaved physical address spaces and M/M/1 / M/D/1 contention queuing.
- Crossbar / interconnection network propagation delays.

## Command-Line Options

- `--processors <P>`: Number of processor nodes (default: 16).
- `--memory-banks <M>`: Number of interleaved memory banks (default: 16).
- `--hit-ratio <H>`: L1 cache hit ratio (default: 0.85).
- `--write-ratio <W>`: Fraction of accesses that are writes (default: 0.20).
- `--think-time <T>`: Mean processor compute time between memory operations (default: 5.0).
- `--bank-access-time <T>`: Memory bank cycle latency (default: 10.0).
- `--network-latency <T>`: Interconnect transit delay (default: 1.0).
- `--check`: Verify simulated AMAT against analytical formula.

## Running the Model

### Sequential Mode
```bash
./smmp --serial --processors 16 --memory-banks 16 -t 500
```

### Parallel Time Warp Mode
```bash
./smmp -c 4 --processors 16 --memory-banks 16 -t 500
```
