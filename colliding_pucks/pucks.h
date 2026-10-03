/**
 * @file colliding_pucks/pucks.h
 *
 * @brief State and event declarations for Colliding Pucks model
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>

#include "geometry.h"
#include "spatial_index.h"

enum puck_event_type {
    EVENT_PUCK_ARRIVAL = 1,
    EVENT_PLAN = 2,
    EVENT_PUCK_VELOCITY_UPDATE = 3
};

struct plan_payload {
    uint64_t epoch;
};

struct arrival_payload {
    struct puck p;
};

struct velocity_update_payload {
    unsigned int puck_id;
    double vx;
    double vy;
    double t;
};

struct sector_state {
    struct rng_t seed;
    simtime_t lvt;
    unsigned int sector_x;
    unsigned int sector_y;
    double x_min, y_min;
    double x_max, y_max;

    unsigned int n_pucks;
    unsigned int capacity;
    struct puck *pucks;

    uint64_t plan_epoch;
    simtime_t next_planned_time;

    struct spatial_stats stats;
    unsigned long long total_collisions;
    unsigned long long total_bounces;
    unsigned long long total_exits;
};

struct model_config {
    double table_width;
    double table_height;
    unsigned int grid_dim;
    unsigned int total_pucks;
    double radius_min;
    double radius_max;
    double mass_min;
    double mass_max;
    double init_speed;
    bool cluster;
    enum index_strategy index_mode;
    unsigned int max_per_cell;
    bool check_conservation;
};

extern struct model_config pucks_cfg;
extern struct topology *grid_topology;
