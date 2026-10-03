/**
 * @file interconnect/interconnect.c
 *
 * @brief Discrete event k-ary n-cube network interconnect simulator for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "interconnect.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct interconnect_config net_cfg = {
    .radix_k = 4,
    .dims_n = 3,
    .total_nodes = 64, // 4^3
    .routing = ROUTING_DOR,
    .traffic = TRAFFIC_UNIFORM,
    .injection_rate = 0.05,
    .packet_length = 4,
    .num_vcs = 2,
    .buffer_depth = 8,
    .router_delay = 1.0,
    .channel_delay = 1.0,
    .check_theory = false
};

struct topology *net_topology = NULL;

static inline uint32_t port_from_dim_dir(unsigned int dim, int dir)
{
    return 1 + 2 * dim + (dir > 0 ? 0 : 1);
}

static inline void dim_dir_from_port(uint32_t port, unsigned int *dim, int *dir)
{
    if (port == LOCAL_PORT) {
        *dim = 0;
        *dir = 0;
        return;
    }
    *dim = (port - 1) / 2;
    *dir = ((port - 1) % 2 == 0) ? 1 : -1;
}

lp_id_t compute_traffic_destination(lp_id_t src, const unsigned int *coords, struct router_state *state)
{
    unsigned int N = net_cfg.total_nodes;
    if (N <= 1) return src;

    switch (net_cfg.traffic) {
        case TRAFFIC_UNIFORM: {
            lp_id_t dst;
            do {
                dst = (lp_id_t)(Random(&state->seed) * N);
                if (dst >= N) dst = N - 1;
            } while (dst == src && N > 1);
            return dst;
        }

        case TRAFFIC_BIT_COMPLEMENT: {
            lp_id_t dst = (lp_id_t)(N - 1 - src);
            return dst != src ? dst : (src + 1) % N;
        }

        case TRAFFIC_TRANSPOSE: {
            if (net_cfg.dims_n >= 2) {
                unsigned int t_coords[MAX_DIMS];
                memcpy(t_coords, coords, sizeof(unsigned int) * net_cfg.dims_n);
                t_coords[0] = coords[1];
                t_coords[1] = coords[0];
                lp_id_t dst = GetIdFromCoordinatesND(net_topology, t_coords);
                return dst != src ? dst : (src + 1) % N;
            }
            return (src + 1) % N;
        }

        case TRAFFIC_TORNADO: {
            unsigned int shift = net_cfg.radix_k / 2;
            unsigned int t_coords[MAX_DIMS];
            for (unsigned int d = 0; d < net_cfg.dims_n; d++) {
                t_coords[d] = (coords[d] + shift) % net_cfg.radix_k;
            }
            lp_id_t dst = GetIdFromCoordinatesND(net_topology, t_coords);
            return dst != src ? dst : (src + 1) % N;
        }
    }
    return (src + 1) % N;
}

uint32_t route_next_port(lp_id_t me, const unsigned int *cur_coords,
                         lp_id_t dst, enum routing_algorithm alg, struct router_state *state)
{
    if (me == dst) return LOCAL_PORT;

    unsigned int dst_coords[MAX_DIMS];
    GetCoordinatesND(net_topology, dst, dst_coords);

    // Dimension-Order Routing (DOR)
    unsigned int target_dim = 0;
    int target_dir = 1;
    bool found = false;

    for (unsigned int d = 0; d < net_cfg.dims_n; d++) {
        if (cur_coords[d] != dst_coords[d]) {
            target_dim = d;
            int diff = (int)dst_coords[d] - (int)cur_coords[d];
            if (diff < 0) diff += (int)net_cfg.radix_k;
            target_dir = (diff <= (int)(net_cfg.radix_k / 2)) ? 1 : -1;
            found = true;
            break;
        }
    }

    if (!found) return LOCAL_PORT;
    uint32_t preferred_port = port_from_dim_dir(target_dim, target_dir);

    if (alg == ROUTING_DEFLECTION) {
        // If preferred port has credit, use it; otherwise deflect to another available port
        if (state->out_ports[preferred_port].credits[0] > 0) {
            return preferred_port;
        }
        for (uint32_t p = 1; p < state->num_ports; p++) {
            if (state->out_ports[p].credits[0] > 0) {
                state->deflections_count++;
                return p;
            }
        }
    }

    return preferred_port;
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct router_state *state = (struct router_state *)s;
    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me + 777, &state->seed);
            state->lvt = now;
            state->router_id = me;
            state->num_ports = 1 + 2 * net_cfg.dims_n;
            if (state->num_ports > MAX_PORTS) state->num_ports = MAX_PORTS;

            GetCoordinatesND(net_topology, me, state->coords);

            for (uint32_t p = 0; p < state->num_ports; p++) {
                for (uint32_t v = 0; v < net_cfg.num_vcs; v++) {
                    state->out_ports[p].credits[v] = net_cfg.buffer_depth;
                }
            }

            SetState(state);

            // Schedule first injection
            double dt = Expent(&state->seed, 1.0 / net_cfg.injection_rate);
            ScheduleNewEvent(me, now + dt, EVENT_INJECT, NULL, 0);
            break;
        }

        case EVENT_INJECT: {
            state->packets_injected++;
            lp_id_t dst = compute_traffic_destination(me, state->coords, state);

            static _Atomic uint64_t global_pkt_id = 0;
            uint64_t pid = ++global_pkt_id;

            lp_id_t target = dst;
            if (net_cfg.routing == ROUTING_ROMM) {
                target = compute_traffic_destination(me, state->coords, state);
            }

            uint32_t port = route_next_port(me, state->coords, target, net_cfg.routing, state);

            if (port != LOCAL_PORT) {
                unsigned int dim;
                int dir;
                dim_dir_from_port(port, &dim, &dir);
                lp_id_t next_lp = GetReceiverND(net_topology, me, dim, dir);

                uint8_t chosen_vc = 0;
                if (state->out_ports[port].credits[chosen_vc] > 0) {
                    state->out_ports[port].credits[chosen_vc]--;

                    struct packet_arrive_payload arr;
                    arr.flit_data.packet_id = pid;
                    arr.flit_data.flit_seq = 0;
                    arr.flit_data.packet_length = (uint16_t)net_cfg.packet_length;
                    arr.flit_data.src = me;
                    arr.flit_data.dst = dst;
                    arr.flit_data.intermediate = target;
                    arr.flit_data.phase2 = (net_cfg.routing != ROUTING_ROMM);
                    arr.flit_data.creation_time = now;
                    arr.flit_data.hop_count = 1;
                    arr.flit_data.vc = chosen_vc;
                    arr.in_port = port_from_dim_dir(dim, -dir);
                    arr.vc = chosen_vc;

                    simtime_t send_time = now + net_cfg.router_delay + net_cfg.channel_delay;
                    ScheduleNewEvent(next_lp, send_time, EVENT_PACKET_ARRIVE, &arr, sizeof(arr));
                }
            }

            // Schedule next injection
            double dt = Expent(&state->seed, 1.0 / net_cfg.injection_rate);
            ScheduleNewEvent(me, now + dt, EVENT_INJECT, NULL, 0);
            break;
        }

        case EVENT_PACKET_ARRIVE: {
            const struct packet_arrive_payload *arr = (const struct packet_arrive_payload *)payload;
            if (!arr) break;

            struct flit f = arr->flit_data;

            // Send link credit back to sender
            unsigned int in_dim;
            int in_dir;
            dim_dir_from_port(arr->in_port, &in_dim, &in_dir);
            lp_id_t sender_lp = GetReceiverND(net_topology, me, in_dim, in_dir);

            struct link_free_payload cred;
            cred.out_port = port_from_dim_dir(in_dim, -in_dir);
            cred.vc = arr->vc;
            ScheduleNewEvent(sender_lp, now + net_cfg.router_delay, EVENT_LINK_FREE, &cred, sizeof(cred));

            // Check if packet reached final destination
            if (f.dst == me) {
                state->packets_delivered++;
                state->flits_delivered += f.packet_length;
                state->total_packet_latency += (now - f.creation_time);
                state->total_hops += f.hop_count;
                break;
            }

            // Check ROMM phase 1 completion
            if (net_cfg.routing == ROUTING_ROMM && !f.phase2 && f.intermediate == me) {
                f.phase2 = true;
            }

            lp_id_t current_target = f.phase2 ? f.dst : f.intermediate;
            uint32_t out_port = route_next_port(me, state->coords, current_target, net_cfg.routing, state);

            if (out_port != LOCAL_PORT) {
                unsigned int out_dim;
                int out_dir;
                dim_dir_from_port(out_port, &out_dim, &out_dir);
                lp_id_t next_lp = GetReceiverND(net_topology, me, out_dim, out_dir);

                uint8_t chosen_vc = f.vc % net_cfg.num_vcs;
                f.hop_count++;

                struct packet_arrive_payload fwd;
                fwd.flit_data = f;
                fwd.in_port = port_from_dim_dir(out_dim, -out_dir);
                fwd.vc = chosen_vc;

                simtime_t forward_time = now + net_cfg.router_delay + net_cfg.channel_delay;
                ScheduleNewEvent(next_lp, forward_time, EVENT_PACKET_ARRIVE, &fwd, sizeof(fwd));
            }
            break;
        }

        case EVENT_LINK_FREE: {
            const struct link_free_payload *cred = (const struct link_free_payload *)payload;
            if (!cred) break;

            if (cred->out_port < state->num_ports && cred->vc < net_cfg.num_vcs) {
                state->out_ports[cred->out_port].credits[cred->vc]++;
            }
            break;
        }

        case LP_FINI: {
            double total_time = state->lvt > 0.0 ? state->lvt : 1.0;
            double avg_lat = state->packets_delivered > 0 ? (state->total_packet_latency / (double)state->packets_delivered) : 0.0;
            double avg_hops = state->packets_delivered > 0 ? ((double)state->total_hops / (double)state->packets_delivered) : 0.0;
            double throughput = (double)state->flits_delivered / total_time;

            printf("Router %llu (Coord: ", (unsigned long long)me);
            for (unsigned int d = 0; d < net_cfg.dims_n; d++) {
                printf("%u%s", state->coords[d], (d + 1 < net_cfg.dims_n) ? "," : "");
            }
            printf("): Injected=%" PRIu64 ", Delivered=%" PRIu64 ", AvgLatency=%.2f, AvgHops=%.2f, Deflections=%" PRIu64 ", Throughput=%.4f flits/cycle\n",
                   state->packets_injected, state->packets_delivered, avg_lat, avg_hops, state->deflections_count, throughput);

            rs_free(state);
            break;
        }

        default:
            fprintf(stderr, "Unknown event type %u on LP %llu\n", event_type, (unsigned long long)me);
            abort();
    }
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
    (void)me;
    (void)snapshot;
    return false;
}

static struct simulation_configuration conf = {
    .n_threads = 0,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "interconnect",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1000.0,
};

static bool interconnect_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --k-ary
            net_cfg.radix_k = (unsigned int)atoi(arg);
            return true;
        case 257: // --n-cube
            net_cfg.dims_n = (unsigned int)atoi(arg);
            return true;
        case 258: // --routing
            if (strcasecmp(arg, "dor") == 0) net_cfg.routing = ROUTING_DOR;
            else if (strcasecmp(arg, "deflection") == 0) net_cfg.routing = ROUTING_DEFLECTION;
            else if (strcasecmp(arg, "romm") == 0) net_cfg.routing = ROUTING_ROMM;
            else {
                fprintf(stderr, "Unknown routing algorithm: %s (choose dor, deflection, romm)\n", arg);
                return false;
            }
            return true;
        case 259: // --traffic
            if (strcasecmp(arg, "uniform") == 0) net_cfg.traffic = TRAFFIC_UNIFORM;
            else if (strcasecmp(arg, "complement") == 0) net_cfg.traffic = TRAFFIC_BIT_COMPLEMENT;
            else if (strcasecmp(arg, "transpose") == 0) net_cfg.traffic = TRAFFIC_TRANSPOSE;
            else if (strcasecmp(arg, "tornado") == 0) net_cfg.traffic = TRAFFIC_TORNADO;
            else {
                fprintf(stderr, "Unknown traffic pattern: %s (choose uniform, complement, transpose, tornado)\n", arg);
                return false;
            }
            return true;
        case 260: // --inject-rate
            net_cfg.injection_rate = atof(arg);
            return true;
        case 261: // --packet-length
            net_cfg.packet_length = (unsigned int)atoi(arg);
            return true;
        case 262: // --vcs
            net_cfg.num_vcs = (unsigned int)atoi(arg);
            return true;
        case 263: // --buffer-depth
            net_cfg.buffer_depth = (unsigned int)atoi(arg);
            return true;
        case 264: // --router-delay
            net_cfg.router_delay = atof(arg);
            return true;
        case 265: // --channel-delay
            net_cfg.channel_delay = atof(arg);
            return true;
        case 266: // --check
            net_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void interconnect_extra_usage(void)
{
    printf("\nInterconnect Network Specific Options:\n");
    printf("  --k-ary <k>                    Radix k of k-ary n-cube (default: 4)\n");
    printf("  --n-cube <n>                   Dimension n of k-ary n-cube (default: 3)\n");
    printf("  --routing <dor|deflection|romm> Routing algorithm (default: dor)\n");
    printf("  --traffic <uniform|complement|transpose|tornado> Traffic pattern (default: uniform)\n");
    printf("  --inject-rate <lambda>         Packet injection rate per node (default: 0.05)\n");
    printf("  --packet-length <L>            Flits per packet (default: 4)\n");
    printf("  --vcs <V>                      Virtual channels per physical port (default: 2)\n");
    printf("  --buffer-depth <B>             Buffer capacity per VC (default: 8)\n");
    printf("  --router-delay <D>             Switching / router delay (default: 1.0)\n");
    printf("  --channel-delay <D>            Channel transmission delay (default: 1.0)\n");
    printf("  --check                        Verify theoretical zero-load latency bounds\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, 64, 1000.0);

    static const struct option net_options[] = {
        {"k-ary", required_argument, 0, 256},
        {"n-cube", required_argument, 0, 257},
        {"routing", required_argument, 0, 258},
        {"traffic", required_argument, 0, 259},
        {"inject-rate", required_argument, 0, 260},
        {"packet-length", required_argument, 0, 261},
        {"vcs", required_argument, 0, 262},
        {"buffer-depth", required_argument, 0, 263},
        {"router-delay", required_argument, 0, 264},
        {"channel-delay", required_argument, 0, 265},
        {"check", no_argument, 0, 266},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   net_options, NULL,
                                   interconnect_extra_handler, NULL,
                                   interconnect_extra_usage);

    // Calculate total nodes N = k^n
    unsigned int total = 1;
    for (unsigned int d = 0; d < net_cfg.dims_n; d++) {
        total *= net_cfg.radix_k;
    }
    net_cfg.total_nodes = total;
    conf.lps = net_cfg.total_nodes;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "interconnect";
    }

    // Initialize TOPOLOGY_TORUS_ND from rstopology
    // Build array of dimension sizes
    unsigned int dim_sizes[MAX_DIMS];
    for (unsigned int d = 0; d < net_cfg.dims_n; d++) {
        dim_sizes[d] = net_cfg.radix_k;
    }

    // Use vInitializeTopology(TOPOLOGY_TORUS_ND, 1 + n_dims, n_dims, dim_0, dim_1, ...)
    switch (net_cfg.dims_n) {
        case 1:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 2, 1, dim_sizes[0]);
            break;
        case 2:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 3, 2, dim_sizes[0], dim_sizes[1]);
            break;
        case 3:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 4, 3, dim_sizes[0], dim_sizes[1], dim_sizes[2]);
            break;
        case 4:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 5, 4, dim_sizes[0], dim_sizes[1], dim_sizes[2], dim_sizes[3]);
            break;
        case 5:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 6, 5, dim_sizes[0], dim_sizes[1], dim_sizes[2], dim_sizes[3], dim_sizes[4]);
            break;
        case 6:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 7, 6, dim_sizes[0], dim_sizes[1], dim_sizes[2], dim_sizes[3], dim_sizes[4], dim_sizes[5]);
            break;
        default:
            net_topology = vInitializeTopology(TOPOLOGY_TORUS_ND, 4, 3, dim_sizes[0], dim_sizes[1], dim_sizes[2]);
            break;
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    if (net_topology) {
        ReleaseTopology(net_topology);
    }

    return ret;
}
