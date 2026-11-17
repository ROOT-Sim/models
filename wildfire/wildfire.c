/**
 * @file wildfire/wildfire.c
 *
 * @brief Discrete event Wildfire Propagation simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "wildfire.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct wildfire_config wildfire_cfg = {
    .grid_dim = 4,              // 4x4 = 16 LPs
    .block_dim = 8,             // 8x8 cells per LP -> 32x32 = 1024 cells total
    .cell_size_m = 10.0,        // 10 meters per cell
    .base_spread_rate = 0.20,   // 0.20 m/s base spread rate
    .burn_duration = 30.0,      // 30 seconds fuel burnout duration
    .wind_speed = 5.0,          // 5 m/s wind
    .wind_direction_deg = 0.0,  // Wind blows towards East (0 degrees)
    .fuel_moisture = 0.08,      // 8% moisture
    .moisture_extinction = 0.30,// 30% extinction limit
    .slope_deg = 0.0,           // Flat terrain
    .slope_azimuth_deg = 0.0,
    .torus = false,
    .check_theory = false
};

static lp_id_t get_neighbor_lp(uint32_t lp_row, uint32_t lp_col, int dr, int dc)
{
    int nr = (int)lp_row + dr;
    int nc = (int)lp_col + dc;

    if (wildfire_cfg.torus) {
        nr = (nr + (int)wildfire_cfg.grid_dim) % (int)wildfire_cfg.grid_dim;
        nc = (nc + (int)wildfire_cfg.grid_dim) % (int)wildfire_cfg.grid_dim;
        return (lp_id_t)(nr * (int)wildfire_cfg.grid_dim + nc);
    } else {
        if (nr < 0 || nr >= (int)wildfire_cfg.grid_dim ||
            nc < 0 || nc >= (int)wildfire_cfg.grid_dim) {
            return INVALID_DIRECTION;
        }
        return (lp_id_t)(nr * (int)wildfire_cfg.grid_dim + nc);
    }
}

static double calc_spread_time(int dr, int dc)
{
    double dy = -(double)dr; // Matrix rows grow downwards
    double dx = (double)dc;
    double theta_dir = atan2(dy, dx);

    double theta_wind = wildfire_cfg.wind_direction_deg * M_PI / 180.0;
    double d_theta_w = theta_dir - theta_wind;
    double phi_w = 0.05 * pow(wildfire_cfg.wind_speed, 1.5) * cos(d_theta_w);
    if (phi_w < 0.0) phi_w = 0.0;

    double theta_slope = wildfire_cfg.slope_azimuth_deg * M_PI / 180.0;
    double slope_rad = wildfire_cfg.slope_deg * M_PI / 180.0;
    double d_theta_s = theta_dir - theta_slope;
    double phi_s = 5.275 * tan(slope_rad) * tan(slope_rad) * cos(d_theta_s);
    if (phi_s < 0.0) phi_s = 0.0;

    double moist_factor = 1.0 - (wildfire_cfg.fuel_moisture / wildfire_cfg.moisture_extinction);
    if (moist_factor < 0.05) moist_factor = 0.05;

    double R0 = wildfire_cfg.base_spread_rate * moist_factor;
    double R = R0 * (1.0 + phi_w + phi_s);
    if (R < 0.01) R = 0.01;

    double dist = (dr == 0 || dc == 0) ? wildfire_cfg.cell_size_m : (sqrt(2.0) * wildfire_cfg.cell_size_m);
    double t_prop = dist / R;
    if (t_prop < 0.5) t_prop = 0.5; // Guaranteed lookahead

    return t_prop;
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct wildfire_lp_state *state = (struct wildfire_lp_state *)s;

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
            state->lp_row = (uint32_t)(me / wildfire_cfg.grid_dim);
            state->lp_col = (uint32_t)(me % wildfire_cfg.grid_dim);
            initialize_stream((unsigned int)me + 100, &state->seed);

            SetState(state);

            // Ignite center cell of center LP
            lp_id_t center_lp = (wildfire_cfg.grid_dim / 2) * wildfire_cfg.grid_dim + (wildfire_cfg.grid_dim / 2);
            if (me == center_lp) {
                struct ignite_payload ip = {
                    .row = (uint16_t)(wildfire_cfg.block_dim / 2),
                    .col = (uint16_t)(wildfire_cfg.block_dim / 2),
                    .ignite_time = now + 0.1
                };
                ScheduleNewEvent(me, now + 0.1, EVENT_CELL_IGNITE, &ip, sizeof(ip));
            }
            break;
        }

        case EVENT_CELL_IGNITE: {
            const struct ignite_payload *ip = (const struct ignite_payload *)payload;
            if (!ip) break;
            uint16_t r = ip->row;
            uint16_t c = ip->col;
            if (r >= wildfire_cfg.block_dim || c >= wildfire_cfg.block_dim) break;

            if (state->cells[r][c].state != CELL_UNBURNED) {
                // Already burning or burned: ignore
                break;
            }

            state->cells[r][c].state = CELL_BURNING;
            state->cells[r][c].ignition_time = (float)now;
            state->burned_cells_count++;

            // Schedule cell burnout
            struct burnout_payload bp = { .row = r, .col = c };
            ScheduleNewEvent(me, now + wildfire_cfg.burn_duration, EVENT_CELL_BURNOUT, &bp, sizeof(bp));

            // Propagate to 8 neighbors
            static const int d_rows[8] = {-1, -1, -1,  0, 0,  1, 1, 1};
            static const int d_cols[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

            for (int k = 0; k < 8; k++) {
                int nr = (int)r + d_rows[k];
                int nc = (int)c + d_cols[k];
                double t_prop = calc_spread_time(d_rows[k], d_cols[k]);

                if (nr >= 0 && nr < (int)wildfire_cfg.block_dim &&
                    nc >= 0 && nc < (int)wildfire_cfg.block_dim) {
                    // Internal neighbor cell
                    if (state->cells[nr][nc].state == CELL_UNBURNED) {
                        struct ignite_payload nip = {
                            .row = (uint16_t)nr,
                            .col = (uint16_t)nc,
                            .ignite_time = now + t_prop
                        };
                        ScheduleNewEvent(me, now + t_prop, EVENT_CELL_IGNITE, &nip, sizeof(nip));
                        state->ignitions_propagated++;
                    }
                } else {
                    // Boundary neighbor across LP boundary
                    int lp_dr = (nr < 0) ? -1 : (nr >= (int)wildfire_cfg.block_dim ? 1 : 0);
                    int lp_dc = (nc < 0) ? -1 : (nc >= (int)wildfire_cfg.block_dim ? 1 : 0);
                    lp_id_t target_lp = get_neighbor_lp(state->lp_row, state->lp_col, lp_dr, lp_dc);

                    if (target_lp != INVALID_DIRECTION) {
                        int rem_r = (nr + (int)wildfire_cfg.block_dim) % (int)wildfire_cfg.block_dim;
                        int rem_c = (nc + (int)wildfire_cfg.block_dim) % (int)wildfire_cfg.block_dim;
                        struct ignite_payload nip = {
                            .row = (uint16_t)rem_r,
                            .col = (uint16_t)rem_c,
                            .ignite_time = now + t_prop
                        };
                        ScheduleNewEvent(target_lp, now + t_prop, EVENT_CELL_IGNITE, &nip, sizeof(nip));
                        state->ignitions_propagated++;
                    }
                }
            }
            break;
        }

        case EVENT_CELL_BURNOUT: {
            const struct burnout_payload *bp = (const struct burnout_payload *)payload;
            if (!bp) break;
            uint16_t r = bp->row;
            uint16_t c = bp->col;
            if (r < wildfire_cfg.block_dim && c < wildfire_cfg.block_dim) {
                state->cells[r][c].state = CELL_BURNED;
            }
            break;
        }

        case LP_FINI: {
            uint32_t total_cells = wildfire_cfg.block_dim * wildfire_cfg.block_dim;
            double burned_pct = (double)state->burned_cells_count / (double)total_cells * 100.0;

            printf("Wildfire LP %llu (row=%u, col=%u): BurnedCells=%u/%u (%.1f%%), Propagated=%u\n",
                   (unsigned long long)me, state->lp_row, state->lp_col,
                   state->burned_cells_count, total_cells, burned_pct,
                   state->ignitions_propagated);

            if (wildfire_cfg.check_theory && me == 0) {
                double t_east = calc_spread_time(0, 1);
                double t_west = calc_spread_time(0, -1);
                printf("[CHECK] Rothermel Spread Times: East=%.2fs, West=%.2fs (Wind Eastward=%s)\n",
                       t_east, t_west, (t_east < t_west) ? "VERIFIED" : "UNVERIFIED");
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
    .stats_file = "wildfire",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 100.0,
};

static bool wildfire_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --grid-dim
            wildfire_cfg.grid_dim = (unsigned int)atoi(arg);
            return true;
        case 257: // --block-dim
            wildfire_cfg.block_dim = (unsigned int)atoi(arg);
            if (wildfire_cfg.block_dim > MAX_BLOCK_DIM) wildfire_cfg.block_dim = MAX_BLOCK_DIM;
            return true;
        case 258: // --cell-size
            wildfire_cfg.cell_size_m = atof(arg);
            return true;
        case 259: // --base-rate
            wildfire_cfg.base_spread_rate = atof(arg);
            return true;
        case 260: // --burn-time
            wildfire_cfg.burn_duration = atof(arg);
            return true;
        case 261: // --wind-speed
            wildfire_cfg.wind_speed = atof(arg);
            return true;
        case 262: // --wind-dir
            wildfire_cfg.wind_direction_deg = atof(arg);
            return true;
        case 263: // --moisture
            wildfire_cfg.fuel_moisture = atof(arg);
            return true;
        case 264: // --slope
            wildfire_cfg.slope_deg = atof(arg);
            return true;
        case 265: // --slope-azimuth
            wildfire_cfg.slope_azimuth_deg = atof(arg);
            return true;
        case 266: // --torus
            wildfire_cfg.torus = true;
            return true;
        case 267: // --check
            wildfire_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void wildfire_extra_usage(void)
{
    printf("\nWildfire Propagation Simulation Specific Options:\n");
    printf("  --grid-dim <G>                 Grid dimension of LPs GxG (default: 4)\n");
    printf("  --block-dim <B>                Sub-grid dimension per LP BxB cells (default: 8, max: 32)\n");
    printf("  --cell-size <M>                Cell resolution width in meters (default: 10.0)\n");
    printf("  --base-rate <R>                Base spread rate R_0 in m/s (default: 0.20)\n");
    printf("  --burn-time <T>                Cell combustion duration in seconds (default: 30.0)\n");
    printf("  --wind-speed <S>               Wind velocity in m/s (default: 5.0)\n");
    printf("  --wind-dir <D>                 Wind direction in degrees [0=East, 90=North] (default: 0.0)\n");
    printf("  --moisture <M>                 Fuel moisture content [0.0-1.0] (default: 0.08)\n");
    printf("  --slope <S>                    Terrain slope inclination in degrees (default: 0.0)\n");
    printf("  --slope-azimuth <A>            Terrain slope aspect in degrees (default: 0.0)\n");
    printf("  --torus                        Enable toroidal boundary wrapping (default: absorbing)\n");
    printf("  --check                        Verify Rothermel directional spread asymmetry\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, wildfire_cfg.grid_dim * wildfire_cfg.grid_dim, 100.0);

    static const struct option wildfire_options[] = {
        {"grid-dim", required_argument, 0, 256},
        {"block-dim", required_argument, 0, 257},
        {"cell-size", required_argument, 0, 258},
        {"base-rate", required_argument, 0, 259},
        {"burn-time", required_argument, 0, 260},
        {"wind-speed", required_argument, 0, 261},
        {"wind-dir", required_argument, 0, 262},
        {"moisture", required_argument, 0, 263},
        {"slope", required_argument, 0, 264},
        {"slope-azimuth", required_argument, 0, 265},
        {"torus", no_argument, 0, 266},
        {"check", no_argument, 0, 267},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   wildfire_options, NULL,
                                   wildfire_extra_handler, NULL,
                                   wildfire_extra_usage);

    conf.lps = wildfire_cfg.grid_dim * wildfire_cfg.grid_dim;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "wildfire";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
