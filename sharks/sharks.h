/**
 * @file sharks/sharks.h
 *
 * @brief State and event definitions for Sharks World model
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>

#include "sector_geom.h"

enum sharks_event_type {
    EVENT_KILL = 1,
    EVENT_ARRIVAL = 2,
    EVENT_PLAN = 3
};

struct sharks_plan_payload {
    uint64_t epoch;
};

struct sharks_arrival_payload {
    struct creature c;
};

struct sharks_kill_payload {
    uint32_t fish_id;
    uint32_t version;
    double x, y;
};

struct sector_state {
    struct rng_t seed;
    simtime_t lvt;
    unsigned int sector_x;
    unsigned int sector_y;

    double nominal_x0, nominal_y0;
    double nominal_x1, nominal_y1;
    double outer_x0, outer_y0;
    double outer_x1, outer_y1;

    unsigned int n_creatures;
    unsigned int capacity;
    struct creature *creatures;

    uint64_t plan_epoch;
    simtime_t next_planned_time;

    unsigned long long total_kills;
    unsigned long long total_arrivals;
    unsigned long long total_exits;
    unsigned int live_fish_count;
};

struct sharks_config {
    double world_size;
    unsigned int grid_dim;
    unsigned int total_creatures;
    double sharks_ratio;
    double attack_range;
    double max_velocity;
};

extern struct sharks_config sharks_cfg;
extern struct topology *torus_topology;
