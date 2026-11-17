/**
 * @file spiking_net/spiking_net.h
 *
 * @brief Discrete event Spiking Neural Network (LIF) simulation model
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_FAN_OUT 128

enum spiking_net_event_type {
    EVENT_SYNAPSE_SPIKE = 1,
    EVENT_EXTERNAL_INPUT = 2
};

struct synapse_t {
    lp_id_t target_lp;
    double weight;
    double delay;
};

struct spike_payload {
    lp_id_t source_lp;
    double weight;
};

struct spiking_net_config {
    unsigned int num_neurons;
    unsigned int fan_out;
    double tau_m;          // Membrane time constant in seconds
    double tau_ref;        // Refractory period in seconds
    double v_thresh;       // Action potential threshold in mV
    double v_reset;        // Reset potential in mV
    double v_rest;         // Resting potential in mV
    double poisson_rate;   // Background external Poisson rate in Hz
    double delay_min;      // Minimum axonal delay in seconds
    double delay_max;      // Maximum axonal delay in seconds
    double weight_exc;     // Excitatory postsynaptic potential (EPSP) in mV
    double weight_inh;     // Inhibitory postsynaptic potential (IPSP) in mV
    double inhibitory_frac;// Fraction of inhibitory neurons
    bool check_theory;     // Verify firing rate within biologically plausible regime
};

struct neuron_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t id;
    bool is_inhibitory;

    double v_membrane;
    simtime_t last_spike_time;
    simtime_t last_update_time;
    simtime_t refractory_until;

    uint32_t fan_out_count;
    struct synapse_t synapses[MAX_FAN_OUT];

    uint64_t spikes_fired;
    uint64_t spikes_received;
    uint64_t external_inputs_received;
};

extern struct spiking_net_config net_cfg;
