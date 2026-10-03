/**
 * @file interconnect/interconnect.h
 *
 * @brief Discrete event k-ary n-cube network interconnect simulation for ROOT-Sim
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

#define MAX_DIMS 6
#define MAX_PORTS (2 * MAX_DIMS + 1) // 2 ports per dimension + 1 local injection/ejection port
#define LOCAL_PORT 0
#define MAX_VCS 2
#define MAX_BUF_DEPTH 8

enum interconnect_event_type {
    EVENT_LINK_FREE = 1,
    EVENT_PACKET_ARRIVE = 2,
    EVENT_INJECT = 3
};

enum routing_algorithm {
    ROUTING_DOR = 0,
    ROUTING_DEFLECTION,
    ROUTING_ROMM
};

enum traffic_pattern {
    TRAFFIC_UNIFORM = 0,
    TRAFFIC_BIT_COMPLEMENT,
    TRAFFIC_TRANSPOSE,
    TRAFFIC_TORNADO
};

struct flit {
    uint64_t packet_id;
    uint16_t flit_seq;
    uint16_t packet_length;
    lp_id_t src;
    lp_id_t dst;
    lp_id_t intermediate;
    bool phase2;
    simtime_t creation_time;
    uint32_t hop_count;
    uint8_t vc;
};

struct packet_arrive_payload {
    struct flit flit_data;
    uint32_t in_port;
    uint8_t vc;
};

struct link_free_payload {
    uint32_t out_port;
    uint8_t vc;
};

struct interconnect_config {
    unsigned int radix_k;
    unsigned int dims_n;
    unsigned int total_nodes;
    enum routing_algorithm routing;
    enum traffic_pattern traffic;
    double injection_rate;
    unsigned int packet_length;
    unsigned int num_vcs;
    unsigned int buffer_depth;
    double router_delay;
    double channel_delay;
    bool check_theory;
};

struct router_in_port {
    unsigned int flit_count[MAX_VCS];
};

struct router_out_port {
    unsigned int credits[MAX_VCS];
};

struct router_state {
    struct rng_t seed;
    simtime_t lvt;
    lp_id_t router_id;

    unsigned int num_ports;
    unsigned int coords[MAX_DIMS];

    struct router_in_port in_ports[MAX_PORTS];
    struct router_out_port out_ports[MAX_PORTS];

    // Statistics
    uint64_t packets_injected;
    uint64_t packets_delivered;
    uint64_t flits_delivered;
    uint64_t deflections_count;
    double total_packet_latency;
    uint64_t total_hops;
};

extern struct interconnect_config net_cfg;
extern struct topology *net_topology;

lp_id_t compute_traffic_destination(lp_id_t src, const unsigned int *coords, struct router_state *state);
uint32_t route_next_port(lp_id_t me, const unsigned int *cur_coords,
                         lp_id_t dst, enum routing_algorithm alg, struct router_state *state);
