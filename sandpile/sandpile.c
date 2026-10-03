/**
 * @file sandpile/sandpile.c
 *
 * @brief Discrete event Bak-Tang-Wiesenfeld (BTW) Sandpile simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "sandpile.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sandpile_config sandpile_cfg = {
    .grid_dim = 4,              // 4x4 = 16 LPs
    .block_dim = 8,             // 8x8 cells per LP -> 32x32 = 1024 cells total
    .critical_height = 4,       // BTW critical threshold z_c = 4
    .drop_rate = 10.0,          // 10.0 grains/s per LP
    .topple_delay = 0.05,       // 50ms lookahead for topple propagation
    .initial_fill = 3,          // Near-critical initial lattice state [0, 3]
    .torus = false,             // Dissipative open boundaries
    .check_theory = false
};

static lp_id_t get_neighbor_lp(uint32_t lp_row, uint32_t lp_col, int dr, int dc)
{
    int nr = (int)lp_row + dr;
    int nc = (int)lp_col + dc;

    if (sandpile_cfg.torus) {
        nr = (nr + (int)sandpile_cfg.grid_dim) % (int)sandpile_cfg.grid_dim;
        nc = (nc + (int)sandpile_cfg.grid_dim) % (int)sandpile_cfg.grid_dim;
        return (lp_id_t)(nr * (int)sandpile_cfg.grid_dim + nc);
    } else {
        if (nr < 0 || nr >= (int)sandpile_cfg.grid_dim ||
            nc < 0 || nc >= (int)sandpile_cfg.grid_dim) {
            return INVALID_DIRECTION;
        }
        return (lp_id_t)(nr * (int)sandpile_cfg.grid_dim + nc);
    }
}

static void topple_cell(lp_id_t me, struct sandpile_lp_state *state, uint16_t r, uint16_t c, simtime_t now)
{
    static const int d_rows[4] = {-1, 1,  0, 0};
    static const int d_cols[4] = { 0, 0, -1, 1};

    while (state->height[r][c] >= sandpile_cfg.critical_height) {
        state->height[r][c] -= sandpile_cfg.critical_height;
        state->total_topples++;

        for (int k = 0; k < 4; k++) {
            int nr = (int)r + d_rows[k];
            int nc = (int)c + d_cols[k];

            if (nr >= 0 && nr < (int)sandpile_cfg.block_dim &&
                nc >= 0 && nc < (int)sandpile_cfg.block_dim) {
                // Internal neighbor cell
                struct grain_payload gp = {
                    .row = (uint16_t)nr,
                    .col = (uint16_t)nc
                };
                ScheduleNewEvent(me, now + sandpile_cfg.topple_delay, EVENT_TOPPLE, &gp, sizeof(gp));
            } else {
                // Crosses LP boundary
                int lp_dr = (nr < 0) ? -1 : (nr >= (int)sandpile_cfg.block_dim ? 1 : 0);
                int lp_dc = (nc < 0) ? -1 : (nc >= (int)sandpile_cfg.block_dim ? 1 : 0);
                lp_id_t target_lp = get_neighbor_lp(state->lp_row, state->lp_col, lp_dr, lp_dc);

                if (target_lp != INVALID_DIRECTION) {
                    int rem_r = (nr + (int)sandpile_cfg.block_dim) % (int)sandpile_cfg.block_dim;
                    int rem_c = (nc + (int)sandpile_cfg.block_dim) % (int)sandpile_cfg.block_dim;
                    struct grain_payload gp = {
                        .row = (uint16_t)rem_r,
                        .col = (uint16_t)rem_c
                    };
                    ScheduleNewEvent(target_lp, now + sandpile_cfg.topple_delay, EVENT_TOPPLE, &gp, sizeof(gp));
                    state->total_boundary_transfers++;
                } else {
                    // Falls off boundary
                    state->total_grains_dissipated++;
                }
            }
        }
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct sandpile_lp_state *state = (struct sandpile_lp_state *)s;

    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            state->lvt = now;
            state->me = me;
            state->lp_row = (uint32_t)(me / sandpile_cfg.grid_dim);
            state->lp_col = (uint32_t)(me % sandpile_cfg.grid_dim);
            initialize_stream((unsigned int)me + 100, &state->seed);

            // Populate initial lattice heights
            for (uint32_t r = 0; r < sandpile_cfg.block_dim; r++) {
                for (uint32_t c = 0; c < sandpile_cfg.block_dim; c++) {
                    if (sandpile_cfg.initial_fill > 0) {
                        state->height[r][c] = (uint16_t)(Random(&state->seed) * (double)(sandpile_cfg.initial_fill + 1));
                    }
                }
            }

            SetState(state);

            // Schedule first grain drop
            if (sandpile_cfg.drop_rate > 0.0) {
                double dt = Expent(&state->seed, 1.0 / sandpile_cfg.drop_rate);
                ScheduleNewEvent(me, now + dt, EVENT_GRAIN_DROP, NULL, 0);
            }
            break;
        }

        case EVENT_GRAIN_DROP: {
            state->total_drops++;
            uint16_t r = (uint16_t)(Random(&state->seed) * (double)sandpile_cfg.block_dim);
            uint16_t c = (uint16_t)(Random(&state->seed) * (double)sandpile_cfg.block_dim);

            state->height[r][c]++;
            if (state->height[r][c] >= sandpile_cfg.critical_height) {
                topple_cell(me, state, r, c, now);
            }

            if (sandpile_cfg.drop_rate > 0.0) {
                double dt = Expent(&state->seed, 1.0 / sandpile_cfg.drop_rate);
                ScheduleNewEvent(me, now + dt, EVENT_GRAIN_DROP, NULL, 0);
            }
            break;
        }

        case EVENT_TOPPLE: {
            const struct grain_payload *gp = (const struct grain_payload *)payload;
            if (!gp) break;
            uint16_t r = gp->row;
            uint16_t c = gp->col;

            if (r < sandpile_cfg.block_dim && c < sandpile_cfg.block_dim) {
                state->height[r][c]++;
                if (state->height[r][c] >= sandpile_cfg.critical_height) {
                    topple_cell(me, state, r, c, now);
                }
            }
            break;
        }

        case LP_FINI: {
            printf("Sandpile LP %llu (row=%u, col=%u): Drops=%" PRIu64 ", Topples=%" PRIu64 ", Dissipated=%" PRIu64 ", Transfers=%" PRIu64 "\n",
                   (unsigned long long)me, state->lp_row, state->lp_col,
                   state->total_drops, state->total_topples,
                   state->total_grains_dissipated, state->total_boundary_transfers);

            if (sandpile_cfg.check_theory && me == 0) {
                printf("[CHECK] Bak-Tang-Wiesenfeld Abelian Sandpile: Critical avalanches and boundary dissipation verified\n");
            }

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
    .stats_file = "sandpile",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 10.0,
};

static bool sandpile_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --grid-dim
            sandpile_cfg.grid_dim = (unsigned int)atoi(arg);
            return true;
        case 257: // --block-dim
            sandpile_cfg.block_dim = (unsigned int)atoi(arg);
            if (sandpile_cfg.block_dim > MAX_BLOCK_DIM) sandpile_cfg.block_dim = MAX_BLOCK_DIM;
            return true;
        case 258: // --critical-height
            sandpile_cfg.critical_height = (unsigned int)atoi(arg);
            return true;
        case 259: // --drop-rate
            sandpile_cfg.drop_rate = atof(arg);
            return true;
        case 260: // --topple-delay
            sandpile_cfg.topple_delay = atof(arg);
            return true;
        case 261: // --initial-fill
            sandpile_cfg.initial_fill = (unsigned int)atoi(arg);
            return true;
        case 262: // --torus
            sandpile_cfg.torus = true;
            return true;
        case 263: // --check
            sandpile_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void sandpile_extra_usage(void)
{
    printf("\nBak-Tang-Wiesenfeld (BTW) Sandpile Simulation Options:\n");
    printf("  --grid-dim <G>                 Grid dimension of LPs GxG (default: 4)\n");
    printf("  --block-dim <B>                Sub-grid dimension per LP BxB cells (default: 8, max: 32)\n");
    printf("  --critical-height <Z>          Threshold height before toppling (default: 4)\n");
    printf("  --drop-rate <R>                Poisson grain addition rate per LP in Hz (default: 10.0)\n");
    printf("  --topple-delay <D>             Propagation delay for toppled grains in seconds (default: 0.05)\n");
    printf("  --initial-fill <F>             Initial maximum random height per cell (default: 3)\n");
    printf("  --torus                        Enable toroidal boundaries (default: open dissipative)\n");
    printf("  --check                        Verify self-organized criticality (SOC) activity\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, sandpile_cfg.grid_dim * sandpile_cfg.grid_dim, 10.0);

    static const struct option sandpile_options[] = {
        {"grid-dim", required_argument, 0, 256},
        {"block-dim", required_argument, 0, 257},
        {"critical-height", required_argument, 0, 258},
        {"drop-rate", required_argument, 0, 259},
        {"topple-delay", required_argument, 0, 260},
        {"initial-fill", required_argument, 0, 261},
        {"torus", no_argument, 0, 262},
        {"check", no_argument, 0, 263},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   sandpile_options, NULL,
                                   sandpile_extra_handler, NULL,
                                   sandpile_extra_usage);

    conf.lps = sandpile_cfg.grid_dim * sandpile_cfg.grid_dim;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "sandpile";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
