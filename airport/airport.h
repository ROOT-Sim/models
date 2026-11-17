/**
 * @file airport/airport.h
 *
 * @brief Discrete event airport and air traffic control simulation model
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

#define MAX_AIRPORT_RUNWAYS 8
#define MAX_AIRCRAFT_CAPACITY 512

enum airport_event_type {
    EVENT_DEPARTURE = 1,
    EVENT_LANDED = 2,
    EVENT_ARRIVAL = 3,
    EVENT_GATE_RELEASE = 4
};

struct aircraft {
    uint64_t flight_id;
    lp_id_t origin;
    lp_id_t destination;
    simtime_t departure_time;
    simtime_t arrival_time;
};

struct flight_payload {
    struct aircraft plane;
};

struct runway_action_payload {
    struct aircraft plane;
    unsigned int runway_idx;
};

struct airport_config {
    unsigned int num_airports;
    unsigned int total_planes;
    unsigned int runways_per_airport;
    unsigned int gates_per_airport;
    double hub_ratio;
    double mean_flight_time;
    double mean_turnaround_time;
    double runway_occupancy_time;
    bool check_invariants;
};

struct airport_runway {
    bool busy;
    bool is_landing; // true = landing, false = takeoff
    uint64_t current_flight_id;
};

struct airport_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t airport_id;

    unsigned int num_runways;
    unsigned int busy_runways;
    struct airport_runway runways[MAX_AIRPORT_RUNWAYS];

    // Queues
    unsigned int holding_len;
    unsigned int holding_cap;
    struct aircraft *holding_queue;

    unsigned int departure_len;
    unsigned int departure_cap;
    struct aircraft *departure_queue;

    unsigned int gates_occupied;
    unsigned int gates_capacity;

    // Statistics
    uint64_t total_arrivals;
    uint64_t total_landings;
    uint64_t total_departures;
    double total_air_holding_time;
    double total_ground_delay_time;
    double total_turnaround_time;

    simtime_t last_stats_time;
    double area_runways_busy;
    double area_airborne_stack;
};

extern struct airport_config airport_cfg;

lp_id_t select_flight_destination(lp_id_t origin, struct airport_state *state);
