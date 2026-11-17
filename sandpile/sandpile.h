/**
 * @file sandpile/sandpile.h
 *
 * @brief Discrete event Bak-Tang-Wiesenfeld (BTW) Sandpile simulation model
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

#define MAX_BLOCK_DIM 32

enum sandpile_event_type {
    EVENT_GRAIN_DROP = 1,
    EVENT_TOPPLE = 2
};

struct grain_payload {
    uint16_t row;
    uint16_t col;
};

struct sandpile_config {
    unsigned int grid_dim;         // Grid of LPs (grid_dim x grid_dim)
    unsigned int block_dim;        // Cells per LP (block_dim x block_dim)
    unsigned int critical_height;  // Height threshold to topple (default: 4)
    double drop_rate;              // External Poisson grain arrival rate per LP in Hz
    double topple_delay;           // Delay for toppling grain propagation in seconds
    unsigned int initial_fill;     // Initial maximum random height [0, initial_fill]
    bool torus;                    // Toroidal closed boundaries vs open dissipative boundaries
    bool check_theory;             // Verify self-organized criticality (SOC) toppling activity
};

struct sandpile_lp_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t me;
    uint32_t lp_row;
    uint32_t lp_col;

    uint64_t total_drops;
    uint64_t total_topples;
    uint64_t total_grains_dissipated;
    uint64_t total_boundary_transfers;

    uint16_t height[MAX_BLOCK_DIM][MAX_BLOCK_DIM];
};

extern struct sandpile_config sandpile_cfg;
