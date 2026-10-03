# RAID-5 Disk Array Simulation Model

A discrete-event parallel simulation model of a RAID-5 disk storage subsystem with rotational mechanics, head seeking, striping, small-write Read-Modify-Write (RMW) penalty, and single-disk failure degradation for ROOT-Sim.

## Overview

Based on classical storage subsystem models by Lee & Katz (1993) and Chen et al. (1994), this model captures:
- Centralized array controller LP dispatching user logical block I/O requests.
- Left-symmetric RAID-5 block and parity striping across $N$ disk LPs.
- Realistic hard disk drive mechanics:
  - Non-linear head seek time curve: $T_{seek} = a \sqrt{\Delta \text{cyl}} + b \Delta \text{cyl} + c$.
  - Spindle rotational latency uniformly distributed in $[0, 60 / \text{RPM}]$.
  - Track transfer delays proportional to requested sectors.
- Small-write Read-Modify-Write (RMW) penalty: each small user write expands into 4 sub-I/Os (read old data, read old parity, write new data, write new parity).
- Degraded mode operation: dynamic disk failure injection at runtime; read operations targeting the failed drive dynamically spawn parallel reads across all surviving $N-1$ disks to reconstruct data via parity XOR.

## Command-Line Options

- `--num-disks <N>`: Number of disks in RAID group (default: 5).
- `--stripe-size <K>`: Stripe unit size in KB (default: 64).
- `--workload-rate <R>`: User I/O arrival rate in IOPS (default: 100.0).
- `--write-fraction <F>`: Fraction of user I/Os that are writes (default: 0.30).
- `--fail-disk <D>`: Disk index to fail (-1 for none, default: -1).
- `--fail-time <T>`: Logical time to inject disk failure (default: 10.0).
- `--rpm <R>`: Disk spindle speed RPM (default: 7200.0).
- `--check`: Verify theoretical small-write amplification factor.

## Running the Model

### Sequential Mode
```bash
./raid5 --serial --num-disks 5 -t 10
```

### Parallel Time Warp Mode
```bash
./raid5 -c 4 --num-disks 5 -t 10
```

### Simulating Disk Failure and Reconstruction
```bash
./raid5 -c 4 --num-disks 5 --fail-disk 2 --fail-time 2.0 -t 10
```
