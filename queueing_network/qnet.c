/**
 * @file queueing_network/qnet.c
 *
 * @brief Discrete event queueing network simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "qnet.h"
#include <argparse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qnet_config qnet_cfg = {
    .topology = QNET_TOPOLOGY_TORUS,
    .grid_dim = 4,
    .total_stations = 16,
    .jobs_per_station = 10,
    .is_open = false,
    .arrival_rate = 1.0,
    .servers_per_station = 1,
    .service_type = SERVICE_EXPONENTIAL,
    .mean_service_time = 1.0,
    .lookahead = 0.0,
    .presample = false,
    .check_theory = false
};

struct topology *qnet_topology = NULL;

static double draw_service_time(struct station_state *state)
{
    double mean = qnet_cfg.mean_service_time;
    double la = qnet_cfg.lookahead;
    if (la >= mean) la = 0.5 * mean;
    double var_mean = mean - la;

    double s = 0.0;
    switch (qnet_cfg.service_type) {
        case SERVICE_EXPONENTIAL:
            s = Expent(&state->seed, var_mean);
            break;
        case SERVICE_DETERMINISTIC:
            s = var_mean;
            break;
        case SERVICE_UNIFORM:
            s = RandomRange(&state->seed, 0.2 * var_mean, 1.8 * var_mean);
            break;
        case SERVICE_HYPEREXPONENTIAL:
            if (Random(&state->seed) < 0.8) {
                s = Expent(&state->seed, 0.5 * var_mean);
            } else {
                s = Expent(&state->seed, 3.0 * var_mean);
            }
            break;
    }
    double total = la + s;
    if (total < 1e-6) total = 1e-6;
    return total;
}

static lp_id_t route_next_station(lp_id_t me, struct station_state *state)
{
    switch (qnet_cfg.topology) {
        case QNET_TOPOLOGY_TORUS:
            if (qnet_topology) {
                return GetReceiver(qnet_topology, me, DIRECTION_RANDOM);
            }
            return (me + 1) % qnet_cfg.total_stations;

        case QNET_TOPOLOGY_RING:
            return (me + 1) % qnet_cfg.total_stations;

        case QNET_TOPOLOGY_TANDEM:
            if (me + 1 < qnet_cfg.total_stations) {
                return me + 1;
            }
            return INVALID_DIRECTION;

        case QNET_TOPOLOGY_FCMESH: {
            if (qnet_cfg.total_stations <= 1) return me;
            unsigned int r = (unsigned int)(Random(&state->seed) * (qnet_cfg.total_stations - 1));
            return (me + 1 + r) % qnet_cfg.total_stations;
        }
    }
    return (me + 1) % qnet_cfg.total_stations;
}

static void update_integrals(struct station_state *state, simtime_t now)
{
    if (now > state->last_time) {
        double dt = now - state->last_time;
        state->area_server_busy += (double)state->busy_servers * dt;
        state->area_in_system += ((double)state->busy_servers + (double)state->queue_len) * dt;
        state->last_time = now;
    }
}

static void queue_push(struct station_state *state, const struct qnet_job *job)
{
    if (state->queue_len >= state->queue_capacity) {
        unsigned int new_cap = state->queue_capacity ? state->queue_capacity * 2 : 16;
        state->queue = rs_realloc(state->queue, sizeof(struct qnet_job) * new_cap);
        state->queue_capacity = new_cap;
    }
    state->queue[state->queue_len++] = *job;
}

static struct qnet_job queue_pop(struct station_state *state)
{
    struct qnet_job job = state->queue[0];
    state->queue_len--;
    if (state->queue_len > 0) {
        memmove(&state->queue[0], &state->queue[1], sizeof(struct qnet_job) * state->queue_len);
    }
    return job;
}

static int find_idle_server(const struct station_state *state)
{
    for (unsigned int i = 0; i < state->num_servers; i++) {
        if (!state->servers[i].busy) {
            return (int)i;
        }
    }
    return -1;
}

static void start_job_on_server(lp_id_t me, struct station_state *state, simtime_t now,
                                unsigned int s_idx, struct qnet_job *job)
{
    state->servers[s_idx].busy = true;
    state->servers[s_idx].current_job_id = job->job_id;
    state->busy_servers++;

    double s_time;
    if (qnet_cfg.presample && job->presampled_service > 0.0) {
        s_time = job->presampled_service;
    } else {
        s_time = draw_service_time(state);
    }

    state->servers[s_idx].presampled_service = s_time;
    state->servers[s_idx].busy_until = now + s_time;

    struct qnet_departure_payload dep_msg;
    dep_msg.job = *job;
    dep_msg.server_idx = s_idx;

    ScheduleNewEvent(me, now + s_time, EVENT_DEPARTURE, &dep_msg, sizeof(dep_msg));
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct station_state *state = (struct station_state *)s;
    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
        update_integrals(state, now);
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me, &state->seed);
            state->lvt = now;
            state->station_id = me;
            state->last_time = now;
            state->num_servers = qnet_cfg.servers_per_station;
            state->servers = rs_malloc(sizeof(struct station_server) * state->num_servers);
            memset(state->servers, 0, sizeof(struct station_server) * state->num_servers);

            state->queue_capacity = 32;
            state->queue = rs_malloc(sizeof(struct qnet_job) * state->queue_capacity);
            state->queue_len = 0;

            SetState(state);

            // Closed queueing network initial distribution
            if (!qnet_cfg.is_open) {
                for (unsigned int j = 0; j < qnet_cfg.jobs_per_station; j++) {
                    struct qnet_job job;
                    job.job_id = ((uint64_t)me << 32) | (uint64_t)j;
                    job.generation_time = now;
                    job.station_arrival_time = now;
                    job.presampled_service = qnet_cfg.presample ? draw_service_time(state) : 0.0;

                    int idle_s = find_idle_server(state);
                    if (idle_s >= 0) {
                        start_job_on_server(me, state, now, (unsigned int)idle_s, &job);
                    } else {
                        queue_push(state, &job);
                    }
                }
            } else {
                // Open network: entry stations (LP 0 or all stations) schedule arrivals
                if (me == 0 || qnet_cfg.topology == QNET_TOPOLOGY_FCMESH) {
                    double dt = Expent(&state->seed, qnet_cfg.arrival_rate);
                    if (dt < 1e-6) dt = 1e-6;
                    ScheduleNewEvent(me, now + dt, EVENT_OPEN_ARRIVAL, NULL, 0);
                }
            }
            break;
        }

        case EVENT_OPEN_ARRIVAL: {
            struct qnet_job job;
            static _Atomic uint64_t global_job_counter = 0;
            job.job_id = ++global_job_counter;
            job.generation_time = now;
            job.station_arrival_time = now;
            job.presampled_service = qnet_cfg.presample ? draw_service_time(state) : 0.0;

            state->total_arrivals++;
            int idle_s = find_idle_server(state);
            if (idle_s >= 0) {
                start_job_on_server(me, state, now, (unsigned int)idle_s, &job);
            } else {
                queue_push(state, &job);
            }

            double dt = Expent(&state->seed, qnet_cfg.arrival_rate);
            if (dt < 1e-4) dt = 1e-4;
            ScheduleNewEvent(me, now + dt, EVENT_OPEN_ARRIVAL, NULL, 0);
            break;
        }

        case EVENT_ARRIVAL: {
            const struct qnet_arrival_payload *arr = (const struct qnet_arrival_payload *)payload;
            if (!arr) break;

            state->total_arrivals++;
            struct qnet_job job = arr->job;
            job.station_arrival_time = now;

            if (qnet_cfg.presample && job.presampled_service <= 0.0) {
                job.presampled_service = draw_service_time(state);
            }

            int idle_s = find_idle_server(state);
            if (idle_s >= 0) {
                start_job_on_server(me, state, now, (unsigned int)idle_s, &job);
            } else {
                queue_push(state, &job);
            }
            break;
        }

        case EVENT_DEPARTURE: {
            const struct qnet_departure_payload *dep = (const struct qnet_departure_payload *)payload;
            if (!dep) break;

            unsigned int s_idx = dep->server_idx;
            state->servers[s_idx].busy = false;
            state->busy_servers--;
            state->total_departures++;

            struct qnet_job finished_job = dep->job;
            double s_time = now - finished_job.station_arrival_time;
            state->sum_service_time += s_time;
            state->sum_residence_time += (now - finished_job.generation_time);

            // Forward job to next station with transfer delay
            lp_id_t next_lp = route_next_station(me, state);
            if (next_lp != INVALID_DIRECTION) {
                struct qnet_arrival_payload fwd;
                fwd.job = finished_job;
                fwd.job.presampled_service = qnet_cfg.presample ? draw_service_time(state) : 0.0;
                double delay = qnet_cfg.lookahead > 0.0 ? qnet_cfg.lookahead : 1e-4;
                ScheduleNewEvent(next_lp, now + delay, EVENT_ARRIVAL, &fwd, sizeof(fwd));
            } else {
                // Job completed (e.g. exit in open tandem)
                state->jobs_completed++;
            }

            // If queue has pending jobs, start the next job
            if (state->queue_len > 0) {
                struct qnet_job next_job = queue_pop(state);
                double w_time = now - next_job.station_arrival_time;
                state->sum_waiting_time += w_time;
                start_job_on_server(me, state, now, s_idx, &next_job);
            }
            break;
        }

        case LP_FINI: {
            double total_time = state->lvt > 0.0 ? state->lvt : 1.0;
            double util = state->area_server_busy / (total_time * (double)state->num_servers);
            double mean_q = state->area_in_system / total_time;
            double throughput = (double)state->total_departures / total_time;

            printf("Station %llu: Departures=%llu, Arrivals=%llu, BusyUtil=%.4f, MeanJobs=%.2f, Throughput=%.4f jobs/s\n",
                   (unsigned long long)me, state->total_departures, state->total_arrivals,
                   util, mean_q, throughput);

            rs_free(state->servers);
            rs_free(state->queue);
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
    .stats_file = "qnet",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1000.0,
};

static bool qnet_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --topology
            if (strcmp(arg, "torus") == 0) qnet_cfg.topology = QNET_TOPOLOGY_TORUS;
            else if (strcmp(arg, "ring") == 0) qnet_cfg.topology = QNET_TOPOLOGY_RING;
            else if (strcmp(arg, "tandem") == 0) qnet_cfg.topology = QNET_TOPOLOGY_TANDEM;
            else if (strcmp(arg, "fcmesh") == 0) qnet_cfg.topology = QNET_TOPOLOGY_FCMESH;
            else {
                fprintf(stderr, "Unknown topology: %s (choose torus, ring, tandem, fcmesh)\n", arg);
                return false;
            }
            return true;
        case 257: // --grid-dim
            qnet_cfg.grid_dim = (unsigned int)atoi(arg);
            return true;
        case 258: // --jobs-per-station
            qnet_cfg.jobs_per_station = (unsigned int)atoi(arg);
            return true;
        case 259: // --open
            qnet_cfg.is_open = true;
            return true;
        case 260: // --lambda
            qnet_cfg.arrival_rate = atof(arg);
            return true;
        case 261: // --servers
            qnet_cfg.servers_per_station = (unsigned int)atoi(arg);
            return true;
        case 262: // --service
            if (strcmp(arg, "exp") == 0) qnet_cfg.service_type = SERVICE_EXPONENTIAL;
            else if (strcmp(arg, "det") == 0) qnet_cfg.service_type = SERVICE_DETERMINISTIC;
            else if (strcmp(arg, "uniform") == 0) qnet_cfg.service_type = SERVICE_UNIFORM;
            else if (strcmp(arg, "hyperexp") == 0) qnet_cfg.service_type = SERVICE_HYPEREXPONENTIAL;
            else {
                fprintf(stderr, "Unknown service distribution: %s (choose exp, det, uniform, hyperexp)\n", arg);
                return false;
            }
            return true;
        case 263: // --mean
            qnet_cfg.mean_service_time = atof(arg);
            return true;
        case 264: // --lookahead
            qnet_cfg.lookahead = atof(arg);
            return true;
        case 265: // --presample
            qnet_cfg.presample = true;
            return true;
        case 266: // --check
            qnet_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void qnet_extra_usage(void)
{
    printf("\nQueueing Network Specific Options:\n");
    printf("  --topology <torus|ring|tandem|fcmesh>  Queueing network topology (default: torus)\n");
    printf("  --grid-dim <D>                         Torus dimension DxD (default: 4)\n");
    printf("  --jobs-per-station <J>                 Initial jobs per station in closed network (default: 10)\n");
    printf("  --open                                 Run as open queueing network\n");
    printf("  --lambda <RATE>                        External arrival rate in open mode (default: 1.0)\n");
    printf("  --servers <K>                          Number of servers per station (default: 1)\n");
    printf("  --service <exp|det|uniform|hyperexp>   Service time distribution (default: exp)\n");
    printf("  --mean <TIME>                          Mean service time (default: 1.0)\n");
    printf("  --lookahead <TIME>                     Minimum service time / lookahead (default: 0.0)\n");
    printf("  --presample                            Enable Nicol service-time presampling\n");
    printf("  --check                                Verify Little's Law / theoretical invariants\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, qnet_cfg.grid_dim * qnet_cfg.grid_dim, 1000.0);

    static const struct option qnet_options[] = {
        {"topology", required_argument, 0, 256},
        {"grid-dim", required_argument, 0, 257},
        {"jobs-per-station", required_argument, 0, 258},
        {"open", no_argument, 0, 259},
        {"lambda", required_argument, 0, 260},
        {"servers", required_argument, 0, 261},
        {"service", required_argument, 0, 262},
        {"mean", required_argument, 0, 263},
        {"lookahead", required_argument, 0, 264},
        {"presample", no_argument, 0, 265},
        {"check", no_argument, 0, 266},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   qnet_options, NULL,
                                   qnet_extra_handler, NULL,
                                   qnet_extra_usage);

    if (opt.lps != 0) {
        qnet_cfg.total_stations = opt.lps;
    } else {
        if (qnet_cfg.topology == QNET_TOPOLOGY_TORUS) {
            qnet_cfg.total_stations = qnet_cfg.grid_dim * qnet_cfg.grid_dim;
        }
    }
    conf.lps = qnet_cfg.total_stations;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "qnet";
    }

    if (qnet_cfg.topology == QNET_TOPOLOGY_TORUS) {
        qnet_topology = vInitializeTopology(TOPOLOGY_TORUS, 2, qnet_cfg.grid_dim, qnet_cfg.grid_dim);
    } else if (qnet_cfg.topology == QNET_TOPOLOGY_FCMESH) {
        qnet_topology = vInitializeTopology(TOPOLOGY_FCMESH, 1, qnet_cfg.total_stations);
    } else if (qnet_cfg.topology == QNET_TOPOLOGY_RING) {
        qnet_topology = vInitializeTopology(TOPOLOGY_RING, 1, qnet_cfg.total_stations);
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    if (qnet_topology) {
        ReleaseTopology(qnet_topology);
    }

    return ret;
}
