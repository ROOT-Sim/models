/**
 * @file wildfire/wildfire.h
 *
 * @brief Discrete event Wildfire Propagation simulation model
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

enum wildfire_event_type {
    EVENT_CELL_IGNITE = 1,
    EVENT_CELL_BURNOUT = 2,
    EVENT_SPARK = 3
};

enum cell_state {
    CELL_UNBURNED = 0,
    CELL_BURNING = 1,
    CELL_BURNED = 2
};

struct cell_t {
    uint8_t state;
    float ignition_time;
};

struct ignite_payload {
    uint16_t row;
    uint16_t col;
    simtime_t ignite_time;
};

struct burnout_payload {
    uint16_t row;
    uint16_t col;
};

struct wildfire_config {
    unsigned int grid_dim;         // Grid of LPs (grid_dim x grid_dim)
    unsigned int block_dim;        // Sub-grid per LP (block_dim x block_dim cells)
    double cell_size_m;            // Cell width in meters (default: 10.0m)
    double base_spread_rate;       // Base rate of spread R_0 in m/s (default: 0.20 m/s)
    double burn_duration;          // Combustion duration in seconds (default: 30.0s)
    double wind_speed;             // Wind speed in m/s (default: 5.0 m/s)
    double wind_direction_deg;     // Wind direction in degrees (0 = East, 90 = North)
    double fuel_moisture;          // Moisture content (default: 0.08)
    double moisture_extinction;    // Moisture extinction limit (default: 0.30)
    double slope_deg;              // Terrain slope in degrees (default: 0.0)
    double slope_azimuth_deg;      // Slope aspect in degrees (default: 0.0)
    bool torus;                    // Toroidal wrap vs absorbing planar boundaries
    bool check_theory;             // Verify elliptical fire propagation shape
};

struct wildfire_lp_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t me;
    uint32_t lp_row;
    uint32_t lp_col;

    uint32_t burned_cells_count;
    uint32_t ignitions_propagated;

    struct cell_t cells[MAX_BLOCK_DIM][MAX_BLOCK_DIM];
};

extern struct wildfire_config wildfire_cfg;
