/**
 * @file ising/ising.h
 *
 * @brief 2D Continuous-time Ising model using Lubachevsky's block decomposition
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>

#include <stdbool.h>
#include <stdint.h>

#define MAX_BLOCK_SIZE 64

enum ising_event_type {
    EVENT_BORDER_UPDATE = 1,
    EVENT_FLIP_ATTEMPT = 2
};

enum border_direction {
    BORDER_NORTH = 0,
    BORDER_SOUTH,
    BORDER_WEST,
    BORDER_EAST
};

struct border_update_payload {
    uint8_t side;       // Destination's boundary side (NORTH, SOUTH, WEST, EAST)
    uint16_t offset;    // Offset along the boundary [0, B-1]
    int8_t spin_value;  // New spin value (+1 or -1)
};

struct ising_config {
    unsigned int grid_dim;     // P x P LPs on torus
    unsigned int block_size;   // B x B spins per LP
    double temperature;        // System temperature T
    double external_field;     // External magnetic field h
    double j_coupling;         // Coupling constant J
    double flip_rate;          // Attempt rate per spin
    double halo_latency;       // Inter-LP border message latency
    bool check_theory;
};

struct ising_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t lp_id;

    unsigned int block_size;
    unsigned int total_spins;

    // Grid with 1-cell halo: [B + 2][B + 2]
    // Row 0 is North halo, Row B+1 is South halo
    // Col 0 is West halo, Col B+1 is East halo
    int8_t spins[MAX_BLOCK_SIZE + 2][MAX_BLOCK_SIZE + 2];

    int64_t sum_spins;
    uint64_t total_attempts;
    uint64_t accepted_flips;
    uint64_t border_updates_sent;
    uint64_t border_updates_recv;

    simtime_t last_sample_time;
    double time_integrated_mag;
};

extern struct ising_config ising_cfg;
extern struct topology *ising_topology;
