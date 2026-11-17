# Parallel Logic Circuit Simulator

A discrete-event digital logic simulator for ROOT-Sim, modeling gate-level combinational and sequential VLSI circuits from the ISCAS-85 and ISCAS-89 benchmark suites.

## Overview

In discrete-event logic simulation, logic transitions (events) propagate through a graph of gates (logic gates and D flip-flops) interconnected by wires. Each gate or primary input acts as an independent Logical Process (LP). Gates evaluate logic functions (AND, NAND, OR, NOR, XOR, XNOR, NOT, BUF, DFF) upon signal arrivals and schedule output signal transitions after a propagation delay.

## Netlist Format

The simulator natively parses ISCAS `.bench` format netlists.
Bundled benchmarks:
- `benchmarks/c17.bench`: ISCAS-85 combinational benchmark (5 inputs, 2 outputs, 6 NAND gates).
- `benchmarks/s27.bench`: ISCAS-89 sequential benchmark with feedback and D flip-flops (4 inputs, 1 output, 3 DFFs).
- Run `scripts/fetch_benchmarks.sh` to download larger benchmarks (`c432`, `c880`, `c1355`, etc.).

## Command-Line Options

- `--circuit <path>`: Path to ISCAS `.bench` netlist file (default: bundled `c17.bench`).
- `--vectors <N>`: Number of test vectors generated on primary inputs (default: 1000).
- `--interval <T>`: Logical time between stimulus vectors (default: 10.0).
- `--clock-period <T>`: Clock period for sequential elements (default: 10.0).
- `--gate-delay <D>`: Gate propagation delay (default: 1.0).
- `--wire-delay <D>`: Interconnect wire delay (default: 0.1).
- `--delay-model <transport|inertial>`: Signal delay model (default: transport).
- `--check`: Verify output signature consistency.

## Running the Model

### Sequential Mode
```bash
./logic_circuit --serial --circuit benchmarks/c17.bench -t 200
```

### Parallel Time Warp Mode
```bash
./logic_circuit -c 4 --circuit benchmarks/s27.bench -t 500
```
