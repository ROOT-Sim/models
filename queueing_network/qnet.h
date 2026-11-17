/**
 * @file queueing_network/qnet.h
 *
 * @brief State and event definitions for Queueing Network model
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

enum qnet_event_type {
    EVENT_DEPARTURE = 1,
    EVENT_ARRIVAL = 2,
    EVENT_OPEN_ARRIVAL = 3
};

enum qnet_service_dist {
    SERVICE_EXPONENTIAL = 0,
    SERVICE_DETERMINISTIC,
    SERVICE_UNIFORM,
    SERVICE_HYPEREXPONENTIAL
};

enum qnet_topology_type {
    QNET_TOPOLOGY_TORUS = 0,
    QNET_TOPOLOGY_RING,
    QNET_TOPOLOGY_TANDEM,
    QNET_TOPOLOGY_FCMESH
};

struct qnet_job {
    uint64_t job_id;
    simtime_t generation_time;
    simtime_t station_arrival_time;
    double presampled_service;
};

struct qnet_departure_payload {
    struct qnet_job job;
    unsigned int server_idx;
};

struct qnet_arrival_payload {
    struct qnet_job job;
};

struct qnet_config {
    enum qnet_topology_type topology;
    unsigned int grid_dim;
    unsigned int total_stations;
    unsigned int jobs_per_station;
    bool is_open;
    double arrival_rate;
    unsigned int servers_per_station;
    enum qnet_service_dist service_type;
    double mean_service_time;
    double lookahead;
    bool presample;
    bool check_theory;
};

struct station_server {
    bool busy;
    uint64_t current_job_id;
    simtime_t busy_until;
    double presampled_service;
};

struct station_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t station_id;

    unsigned int num_servers;
    unsigned int busy_servers;
    struct station_server *servers;

    unsigned int queue_len;
    unsigned int queue_capacity;
    struct qnet_job *queue;

    unsigned long long total_arrivals;
    unsigned long long total_departures;
    unsigned long long jobs_completed;

    double sum_waiting_time;
    double sum_service_time;
    double sum_residence_time;

    simtime_t last_time;
    double area_server_busy;
    double area_in_system;

    simtime_t next_open_arrival;
};

extern struct qnet_config qnet_cfg;
extern struct topology *qnet_topology;
