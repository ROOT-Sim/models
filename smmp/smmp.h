/**
 * @file smmp/smmp.h
 *
 * @brief Discrete event shared-memory multiprocessor simulation model
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_MEM_QUEUE_DEPTH 64

enum smmp_event_type {
    EVENT_BANK_FREE = 1,
    EVENT_MEM_RESPONSE = 2,
    EVENT_MEM_READ = 3,
    EVENT_MEM_WRITE = 4,
    EVENT_THINK_DONE = 5
};

enum lp_role {
    ROLE_PROCESSOR = 0,
    ROLE_MEMORY_BANK = 1
};

struct mem_request {
    uint64_t req_id;
    lp_id_t processor_id;
    uint64_t address;
    bool is_write;
    simtime_t issue_time;
};

struct mem_response {
    uint64_t req_id;
    bool is_write;
    simtime_t issue_time;
};

struct smmp_config {
    unsigned int num_processors;
    unsigned int num_banks;
    double cache_hit_ratio;
    double write_ratio;
    double mean_think_time;
    double bank_access_time;
    double network_latency;
    bool check_theory;
};

struct processor_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t proc_id;

    uint64_t reads_issued;
    uint64_t writes_issued;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t requests_completed;
    double total_access_latency;

    bool waiting_for_memory;
};

struct bank_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t bank_id;

    bool server_busy;
    simtime_t busy_until;

    unsigned int queue_len;
    struct mem_request queue[MAX_MEM_QUEUE_DEPTH];

    uint64_t total_requests_serviced;
    uint64_t total_queue_stalls;

    simtime_t last_stats_time;
    double area_server_busy;
    double area_queue_length;
};

union smmp_state {
    struct processor_state proc;
    struct bank_state bank;
};

extern struct smmp_config smmp_cfg;
