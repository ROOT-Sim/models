/**
 * @file logic_circuit/circuit.h
 *
 * @brief Logic circuit discrete event simulation header for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_GATE_INPUTS 32
#define MAX_NAME_LEN 64

enum circuit_event_type {
    EVENT_SIGNAL = 1,
    EVENT_CLOCK = 2,
    EVENT_STIMULUS = 3
};

enum logic_value {
    LOGIC_0 = 0,
    LOGIC_1 = 1,
    LOGIC_X = 2,
    LOGIC_Z = 3
};

enum gate_type {
    GATE_INPUT = 0,
    GATE_AND,
    GATE_NAND,
    GATE_OR,
    GATE_NOR,
    GATE_XOR,
    GATE_XNOR,
    GATE_NOT,
    GATE_BUF,
    GATE_DFF
};

enum circuit_delay_model {
    DELAY_TRANSPORT = 0,
    DELAY_INERTIAL
};

struct signal_payload {
    uint32_t dest_pin;
    uint8_t logic_val;
};

struct stimulus_payload {
    uint32_t vector_idx;
};

struct circuit_config {
    const char *netlist_path;
    uint32_t num_vectors;
    double vector_interval;
    double clock_period;
    double gate_delay;
    double wire_delay;
    enum circuit_delay_model delay_model;
    bool check_signature;
    bool sequential;
};

struct gate_fanout {
    lp_id_t dest_lp;
    uint32_t dest_pin;
};

struct gate_info {
    char name[MAX_NAME_LEN];
    enum gate_type type;
    bool is_primary_input;
    bool is_primary_output;
    uint32_t num_inputs;
    char input_names[MAX_GATE_INPUTS][MAX_NAME_LEN];
    uint32_t num_fanouts;
    struct gate_fanout *fanouts;
};

struct circuit_graph {
    uint32_t num_nodes;
    uint32_t num_inputs;
    uint32_t num_outputs;
    struct gate_info *nodes;
    lp_id_t *primary_inputs;
    lp_id_t *primary_outputs;
};

struct gate_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t me;

    enum gate_type type;
    bool is_primary_input;
    bool is_primary_output;

    uint32_t num_inputs;
    uint8_t input_vals[MAX_GATE_INPUTS];

    uint8_t current_output;
    uint8_t pending_output;
    simtime_t pending_time;

    // For DFF
    uint8_t dff_internal_state;

    // Statistics
    uint64_t total_transitions;
    uint64_t glitches_filtered;
    uint32_t output_signature;
};

extern struct circuit_config circuit_cfg;
extern struct circuit_graph circuit_netlist;

uint8_t evaluate_gate(enum gate_type type, const uint8_t *inputs, uint32_t num_inputs);
