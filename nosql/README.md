# NoSQL Database Transaction Model

A simulation model of transaction execution and validation in a distributed non-relational (NoSQL) database running on ROOT-Sim 3.0.

## Overview

This benchmark simulates the concurrency control, distributed commit protocol, and data conflict dynamics of a distributed transactional key-value store.

Logical Processes (LPs) represent database nodes or shards hosting subsets of data items. Clients submit transactions consisting of sequences of read and write operations. Upon completing operations, transactions initiate a two-phase distributed validation and commit procedure across participant nodes.

## Model Details

- **Logical Process State (`lp_state_type`)**:
  - `committed_tx`: Counter of successfully committed transactions.
  - `conflicted_tx`: Counter of transactions that encountered concurrency conflicts.
  - `residual_tx_ops`: Remaining operations for the active transaction.
  - `read_set`: Array of data keys read by the transaction.
  - `write_set`: Array of data keys updated by the transaction.
  - `seed`: Per-LP pseudorandom stream state.
- **Event Types**:
  - `LP_INIT`: Initializes the database partition state and schedules the first transaction via `START_TX`.
  - `START_TX`: Generates transaction parameters (number of operations, read set target size) and schedules the initial operation.
  - `TX_OP`: Simulates key access latency and appends items to the read set. When operations finish, selects participant shards and sends a `PREPARE` validation message.
  - `PREPARE`: Received by participant nodes to validate against local state and schedule a `COMMIT` acknowledgment back to the coordinator.
  - `COMMIT`: Finalizes transaction execution, increments committed counters, and schedules subsequent transactions.
- **Key Parameters** (defined in `nosql.h`):
  - `TX_OP_ARRIVAL`: Mean arrival delay between transaction operations (250).
  - `MIN_OP_COUNT` / `MAX_OP_COUNT`: Minimum and maximum operations per transaction (10 to 50).
  - `MAX_RS_SIZE` / `MAX_WS_SIZE`: Maximum size of read and write sets (50 items).
  - `TOTAL_COMMITTED_TX`: Committed transaction threshold for LP termination (100).

## Building

### Standalone Build

```sh
cd nosql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Combined Repository Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nosql
```

## Running

```sh
./build/nosql [options]
```

### Available Options

- `-c, --ncores <N>`: Number of worker threads (default: auto).
- `-p, --nprocesses <N>`: Number of database nodes/LPs (default: 16).
- `-t, --termination-time <T>`: Virtual time limit (default: 5000).
- `-s, --stats <FILE>`: Path to statistics output file.
- `-l, --log-level <LEVEL>`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `fatal`, `silent`).
- `--serial`: Execute sequentially in single-threaded mode.
- `--timewarp`: Execute using optimistic Time Warp synchronization (default).
- `-h, --help`: Display help and exit.

### Examples

Run with 16 database partitions on 4 worker threads:
```sh
./build/nosql -c 4 -p 16 -t 5000
```

Run in sequential mode:
```sh
./build/nosql --serial -p 8 -t 2000
```
