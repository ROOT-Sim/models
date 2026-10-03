# Spiking Neural Network (LIF) Simulation Model

A discrete-event parallel simulation model of large-scale spiking neural networks using Leaky Integrate-and-Fire (LIF) neurons, random synaptic connectivity, delayed spike propagation, and background Poisson drive for ROOT-Sim.

## Overview

Based on classical event-driven neural network simulation literature by Mattia & Del Giudice (2000), Morrison et al. (2005), and Brette et al. (2007), this model features:
- One Logical Process (LP) per neuron.
- Continuous-time Leaky Integrate-and-Fire (LIF) membrane potential dynamics:
  $$V(t) = V_{\text{rest}} + (V(t_0) - V_{\text{rest}}) e^{-(t - t_0)/\tau_m}$$
- Absolute refractory period $\tau_{\text{ref}}$ during which the membrane remains clamped at $V_{\text{reset}}$.
- Balanced cortical network architecture with 80% excitatory neurons and 20% inhibitory interneurons.
- Directed synaptic connections with axonal propagation delays providing non-zero lookahead for optimistic parallel simulation.
- Background Poisson drive per neuron to sustain asynchronous irregular (AI) firing regimes.

## Command-Line Options

- `--neurons <N>`: Number of simulated neurons / LPs (default: 64).
- `--fan-out <K>`: Synaptic connections per neuron (default: 16).
- `--tau-m <T>`: Membrane time constant in seconds (default: 0.020).
- `--tau-ref <T>`: Refractory period in seconds (default: 0.002).
- `--v-thresh <V>`: Spike threshold voltage in mV (default: -50.0).
- `--v-reset <V>`: Reset potential in mV (default: -65.0).
- `--v-rest <V>`: Resting potential in mV (default: -65.0).
- `--poisson-rate <R>`: Background Poisson drive per neuron in Hz (default: 100.0).
- `--delay-min <D>`: Minimum axonal delay in seconds (default: 0.001).
- `--delay-max <D>`: Maximum axonal delay in seconds (default: 0.003).
- `--check`: Verify firing rate dynamics within physiological bounds.

## Running the Model

### Sequential Mode
```bash
./spiking_net --serial --neurons 64 -t 1.0
```

### Parallel Time Warp Mode
```bash
./spiking_net -c 4 --neurons 64 -t 1.0
```
