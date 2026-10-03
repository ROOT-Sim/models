/**
 * @file ising/ising.c
 *
 * @brief Discrete event 2D continuous-time Ising spin model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "ising.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ising_config ising_cfg = {
    .grid_dim = 4,
    .block_size = 16,
    .temperature = 2.269, // Onsager critical temperature for 2D Ising
    .external_field = 0.0,
    .j_coupling = 1.0,
    .flip_rate = 1.0,
    .halo_latency = 0.01,
    .check_theory = false
};

struct topology *ising_topology = NULL;

static void send_border_update(lp_id_t dest, simtime_t send_time, uint8_t side, uint16_t offset, int8_t val)
{
    struct border_update_payload upd;
    upd.side = side;
    upd.offset = offset;
    upd.spin_value = val;
    ScheduleNewEvent(dest, send_time, EVENT_BORDER_UPDATE, &upd, sizeof(upd));
}

static void update_magnetization_integral(struct ising_state *state, simtime_t now)
{
    if (now > state->last_sample_time) {
        double dt = now - state->last_sample_time;
        double current_m = (double)state->sum_spins / (double)state->total_spins;
        state->time_integrated_mag += current_m * dt;
        state->last_sample_time = now;
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct ising_state *state = (struct ising_state *)s;
    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
        update_magnetization_integral(state, now);
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me + 314, &state->seed);
            state->lvt = now;
            state->lp_id = me;
            state->block_size = ising_cfg.block_size;
            if (state->block_size > MAX_BLOCK_SIZE) state->block_size = MAX_BLOCK_SIZE;
            state->total_spins = state->block_size * state->block_size;
            state->last_sample_time = now;

            // Initialize spins to +1 (ferromagnetic ground state)
            unsigned int B = state->block_size;
            for (unsigned int x = 0; x <= B + 1; x++) {
                for (unsigned int y = 0; y <= B + 1; y++) {
                    state->spins[x][y] = 1;
                }
            }
            state->sum_spins = (int64_t)state->total_spins;

            SetState(state);

            // Schedule first spin flip attempt
            double mean_dt = 1.0 / ((double)state->total_spins * ising_cfg.flip_rate);
            double dt = Expent(&state->seed, mean_dt);
            if (dt < 1e-5) dt = 1e-5;
            ScheduleNewEvent(me, now + dt, EVENT_FLIP_ATTEMPT, NULL, 0);
            break;
        }

        case EVENT_BORDER_UPDATE: {
            const struct border_update_payload *upd = (const struct border_update_payload *)payload;
            if (!upd) break;

            unsigned int B = state->block_size;
            uint16_t idx = upd->offset;
            if (idx < B) {
                switch (upd->side) {
                    case BORDER_NORTH:
                        state->spins[0][idx + 1] = upd->spin_value;
                        break;
                    case BORDER_SOUTH:
                        state->spins[B + 1][idx + 1] = upd->spin_value;
                        break;
                    case BORDER_WEST:
                        state->spins[idx + 1][0] = upd->spin_value;
                        break;
                    case BORDER_EAST:
                        state->spins[idx + 1][B + 1] = upd->spin_value;
                        break;
                }
                state->border_updates_recv++;
            }
            break;
        }

        case EVENT_FLIP_ATTEMPT: {
            state->total_attempts++;
            unsigned int B = state->block_size;

            // Pick a site uniformly at random in [1, B] x [1, B]
            unsigned int x = 1 + (unsigned int)(Random(&state->seed) * B);
            unsigned int y = 1 + (unsigned int)(Random(&state->seed) * B);
            if (x > B) x = B;
            if (y > B) y = B;

            int8_t s_curr = state->spins[x][y];
            int sum_nn = state->spins[x - 1][y] + state->spins[x + 1][y] +
                         state->spins[x][y - 1] + state->spins[x][y + 1];

            double delta_e = 2.0 * ising_cfg.j_coupling * (double)s_curr * (double)sum_nn +
                             2.0 * ising_cfg.external_field * (double)s_curr;

            bool accept = false;
            if (delta_e <= 0.0) {
                accept = true;
            } else if (ising_cfg.temperature > 0.0) {
                double p = exp(-delta_e / ising_cfg.temperature);
                if (Random(&state->seed) < p) {
                    accept = true;
                }
            }

            if (accept) {
                int8_t s_new = -s_curr;
                state->spins[x][y] = s_new;
                state->sum_spins += (int64_t)(2 * s_new);
                state->accepted_flips++;

                simtime_t halo_time = now + ising_cfg.halo_latency;
                // If on boundary, notify neighboring LPs
                if (x == 1) {
                    lp_id_t north = GetReceiver(ising_topology, me, DIRECTION_N);
                    send_border_update(north, halo_time, BORDER_SOUTH, (uint16_t)(y - 1), s_new);
                    state->border_updates_sent++;
                }
                if (x == B) {
                    lp_id_t south = GetReceiver(ising_topology, me, DIRECTION_S);
                    send_border_update(south, halo_time, BORDER_NORTH, (uint16_t)(y - 1), s_new);
                    state->border_updates_sent++;
                }
                if (y == 1) {
                    lp_id_t west = GetReceiver(ising_topology, me, DIRECTION_W);
                    send_border_update(west, halo_time, BORDER_EAST, (uint16_t)(x - 1), s_new);
                    state->border_updates_sent++;
                }
                if (y == B) {
                    lp_id_t east = GetReceiver(ising_topology, me, DIRECTION_E);
                    send_border_update(east, halo_time, BORDER_WEST, (uint16_t)(x - 1), s_new);
                    state->border_updates_sent++;
                }
            }

            // Schedule next attempt
            double mean_dt = 1.0 / ((double)state->total_spins * ising_cfg.flip_rate);
            double dt = Expent(&state->seed, mean_dt);
            if (dt < 1e-5) dt = 1e-5;
            ScheduleNewEvent(me, now + dt, EVENT_FLIP_ATTEMPT, NULL, 0);
            break;
        }

        case LP_FINI: {
            double total_time = state->lvt > 0.0 ? state->lvt : 1.0;
            double mean_mag = state->time_integrated_mag / total_time;
            double final_mag = (double)state->sum_spins / (double)state->total_spins;
            double accept_ratio = state->total_attempts > 0 ? ((double)state->accepted_flips / (double)state->total_attempts) : 0.0;

            printf("Block LP %llu: FinalMag=%.4f, MeanMag=%.4f, AcceptRatio=%.4f, Attempts=%" PRIu64 ", Flips=%" PRIu64 ", HaloSent=%" PRIu64 "\n",
                   (unsigned long long)me, final_mag, mean_mag, accept_ratio,
                   state->total_attempts, state->accepted_flips, state->border_updates_sent);

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
    .stats_file = "ising",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 100.0,
};

static bool ising_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --grid-dim
            ising_cfg.grid_dim = (unsigned int)atoi(arg);
            return true;
        case 257: // --block-size
            ising_cfg.block_size = (unsigned int)atoi(arg);
            return true;
        case 258: // --temperature
            ising_cfg.temperature = atof(arg);
            return true;
        case 259: // --field
            ising_cfg.external_field = atof(arg);
            return true;
        case 260: // --j-coupling
            ising_cfg.j_coupling = atof(arg);
            return true;
        case 261: // --flip-rate
            ising_cfg.flip_rate = atof(arg);
            return true;
        case 262: // --halo-latency / --lookahead / --opaque-period
            ising_cfg.halo_latency = atof(arg);
            return true;
        case 263: // --check
            ising_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void ising_extra_usage(void)
{
    printf("\n2D Ising Model Specific Options:\n");
    printf("  --grid-dim <P>                 LPs per dimension in PxP torus (default: 4)\n");
    printf("  --block-size <B>               Spins per block dimension BxB per LP (default: 16)\n");
    printf("  --temperature <T>              System temperature (default: 2.269, Onsager critical)\n");
    printf("  --field <H>                    External magnetic field (default: 0.0)\n");
    printf("  --j-coupling <J>               Exchange coupling constant J (default: 1.0)\n");
    printf("  --flip-rate <R>                Spin flip attempt rate per spin (default: 1.0)\n");
    printf("  --halo-latency <L>             Lubachevsky minimum delay / halo latency (default: 0.01)\n");
    printf("  --lookahead <L>                Alias for --halo-latency / opaque period (default: 0.01)\n");
    printf("  --opaque-period <L>            Alias for --halo-latency (default: 0.01)\n");
    printf("  --check                        Verify theoretical critical magnetization bounds\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, ising_cfg.grid_dim * ising_cfg.grid_dim, 100.0);

    static const struct option ising_options[] = {
        {"grid-dim", required_argument, 0, 256},
        {"block-size", required_argument, 0, 257},
        {"temperature", required_argument, 0, 258},
        {"field", required_argument, 0, 259},
        {"j-coupling", required_argument, 0, 260},
        {"flip-rate", required_argument, 0, 261},
        {"halo-latency", required_argument, 0, 262},
        {"lookahead", required_argument, 0, 262},
        {"opaque-period", required_argument, 0, 262},
        {"check", no_argument, 0, 263},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   ising_options, NULL,
                                   ising_extra_handler, NULL,
                                   ising_extra_usage);

    conf.lps = ising_cfg.grid_dim * ising_cfg.grid_dim;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "ising";
    }

    ising_topology = vInitializeTopology(TOPOLOGY_TORUS, 2, ising_cfg.grid_dim, ising_cfg.grid_dim);

    RootsimInit(&conf);
    int ret = RootsimRun();

    if (ising_topology) {
        ReleaseTopology(ising_topology);
    }

    return ret;
}
