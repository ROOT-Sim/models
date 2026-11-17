/**
 * @file colliding_pucks/pucks.c
 *
 * @brief Discrete-event simulation model of Colliding Pucks for ROOT-Sim 3.0
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "pucks.h"
#include "argparse.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct model_config pucks_cfg = {
    .table_width = 1000.0,
    .table_height = 1000.0,
    .grid_dim = 4,
    .total_pucks = 64,
    .radius_min = 5.0,
    .radius_max = 10.0,
    .mass_min = 1.0,
    .mass_max = 5.0,
    .init_speed = 50.0,
    .cluster = false,
    .index_mode = INDEX_DYNAMIC,
    .max_per_cell = 4,
    .check_conservation = false,
};

struct topology *grid_topology = NULL;

static void replan_sector(lp_id_t me, struct sector_state *state, simtime_t now);

static inline lp_id_t get_neighbor_lp(lp_id_t me, int dir)
{
    unsigned int gx = me % pucks_cfg.grid_dim;
    unsigned int gy = me / pucks_cfg.grid_dim;

    switch (dir) {
        case 0: // West
            return (gx > 0) ? (me - 1) : INVALID_DIRECTION;
        case 1: // East
            return (gx < pucks_cfg.grid_dim - 1) ? (me + 1) : INVALID_DIRECTION;
        case 2: // South
            return (gy > 0) ? (me - pucks_cfg.grid_dim) : INVALID_DIRECTION;
        case 3: // North
            return (gy < pucks_cfg.grid_dim - 1) ? (me + pucks_cfg.grid_dim) : INVALID_DIRECTION;
        default:
            return INVALID_DIRECTION;
    }
}

static void add_puck_to_state(struct sector_state *state, const struct puck *p)
{
    if (state->n_pucks >= state->capacity) {
        state->capacity = (state->capacity == 0) ? 16 : state->capacity * 2;
        state->pucks = rs_realloc(state->pucks, sizeof(struct puck) * state->capacity);
    }
    state->pucks[state->n_pucks++] = *p;
}

static void remove_puck_from_state(struct sector_state *state, unsigned int idx)
{
    if (idx < state->n_pucks) {
        state->pucks[idx] = state->pucks[state->n_pucks - 1];
        state->n_pucks--;
    }
}

static void replan_sector(lp_id_t me, struct sector_state *state, simtime_t now)
{
    (void)me;
    double earliest_t = DBL_MAX;

    // 1. Check puck-puck collisions
    #define MAX_PAIRS 8192
    struct candidate_pair pairs[MAX_PAIRS];
    unsigned int n_pairs = find_collision_candidates(pucks_cfg.index_mode,
                                                     state->pucks, state->n_pucks,
                                                     now,
                                                     state->x_min, state->y_min,
                                                     state->x_max, state->y_max,
                                                     pucks_cfg.max_per_cell,
                                                     pairs, MAX_PAIRS,
                                                     &state->stats);

    for (unsigned int k = 0; k < n_pairs; k++) {
        unsigned int i = pairs[k].i;
        unsigned int j = pairs[k].j;
        double t_coll;
        if (predict_puck_puck_collision(&state->pucks[i], &state->pucks[j], now, &t_coll)) {
            if (t_coll > now && t_coll < earliest_t) {
                earliest_t = t_coll;
            }
        }
    }

    // 2. Check cushion collisions (only for sectors touching table borders)
    for (unsigned int i = 0; i < state->n_pucks; i++) {
        double t_cushion;
        int wall;
        if (predict_puck_cushion_collision(&state->pucks[i], now,
                                           0.0, 0.0,
                                           pucks_cfg.table_width, pucks_cfg.table_height,
                                           &t_cushion, &wall)) {
            if (t_cushion > now && t_cushion < earliest_t) {
                earliest_t = t_cushion;
            }
        }
    }

    // 3. Check sector boundary exits
    for (unsigned int i = 0; i < state->n_pucks; i++) {
        double t_exit;
        int dir;
        if (predict_sector_exit(&state->pucks[i], now,
                                state->x_min, state->y_min,
                                state->x_max, state->y_max,
                                &t_exit, &dir)) {
            if (t_exit > now && t_exit < earliest_t) {
                earliest_t = t_exit;
            }
        }
    }

    if (earliest_t < DBL_MAX) {
        state->plan_epoch++;
        state->next_planned_time = earliest_t;
        struct plan_payload payload = { .epoch = state->plan_epoch };
        ScheduleNewEvent(me, earliest_t, EVENT_PLAN, &payload, sizeof(payload));
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct sector_state *state = (struct sector_state *)s;
    if (state != NULL) {
        state->lvt = now;
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me, &state->seed);
            state->lvt = now;
            state->sector_x = (unsigned int)(me % pucks_cfg.grid_dim);
            state->sector_y = (unsigned int)(me / pucks_cfg.grid_dim);

            double sec_w = pucks_cfg.table_width / (double)pucks_cfg.grid_dim;
            double sec_h = pucks_cfg.table_height / (double)pucks_cfg.grid_dim;
            state->x_min = state->sector_x * sec_w;
            state->x_max = (state->sector_x + 1) * sec_w;
            state->y_min = state->sector_y * sec_h;
            state->y_max = (state->sector_y + 1) * sec_h;

            state->capacity = 32;
            state->pucks = rs_malloc(sizeof(struct puck) * state->capacity);
            state->n_pucks = 0;

            SetState(state);

            unsigned int lps_total = pucks_cfg.grid_dim * pucks_cfg.grid_dim;

            // Place pucks
            if (pucks_cfg.cluster) {
                // Clustered scenario (Harless & Rogers 1995): all pucks in sector 0
                if (me == 0) {
                    for (unsigned int k = 0; k < pucks_cfg.total_pucks; k++) {
                        struct puck p;
                        p.id = k;
                        p.radius = pucks_cfg.radius_min +
                            Random(&state->seed) * (pucks_cfg.radius_max - pucks_cfg.radius_min);
                        p.mass = pucks_cfg.mass_min +
                            Random(&state->seed) * (pucks_cfg.mass_max - pucks_cfg.mass_min);

                        double margin = p.radius + 1.0;
                        p.x = (state->x_min + margin) +
                            Random(&state->seed) * (sec_w - 2.0 * margin);
                        p.y = (state->y_min + margin) +
                            Random(&state->seed) * (sec_h - 2.0 * margin);

                        double angle = Random(&state->seed) * 2.0 * M_PI;
                        p.vx = pucks_cfg.init_speed * cos(angle);
                        p.vy = pucks_cfg.init_speed * sin(angle);
                        p.t_ref = now;
                        p.owner_lp = (unsigned int)me;

                        add_puck_to_state(state, &p);
                    }
                }
            } else {
                // Uniform scenario across all sectors
                unsigned int base_pucks = pucks_cfg.total_pucks / lps_total;
                unsigned int extra = pucks_cfg.total_pucks % lps_total;
                unsigned int my_pucks = base_pucks + (me < extra ? 1 : 0);
                unsigned int id_start = (unsigned int)me * base_pucks + (me < extra ? (unsigned int)me : extra);

                for (unsigned int k = 0; k < my_pucks; k++) {
                    struct puck p;
                    p.id = id_start + k;
                    p.radius = pucks_cfg.radius_min +
                        Random(&state->seed) * (pucks_cfg.radius_max - pucks_cfg.radius_min);
                    p.mass = pucks_cfg.mass_min +
                        Random(&state->seed) * (pucks_cfg.mass_max - pucks_cfg.mass_min);

                    double margin = p.radius + 1.0;
                    p.x = (state->x_min + margin) +
                        Random(&state->seed) * (sec_w - 2.0 * margin);
                    p.y = (state->y_min + margin) +
                        Random(&state->seed) * (sec_h - 2.0 * margin);

                    double angle = Random(&state->seed) * 2.0 * M_PI;
                    p.vx = pucks_cfg.init_speed * cos(angle);
                    p.vy = pucks_cfg.init_speed * sin(angle);
                    p.t_ref = now;
                    p.owner_lp = (unsigned int)me;

                    add_puck_to_state(state, &p);
                }
            }

            replan_sector(me, state, now);
            break;
        }

        case EVENT_PLAN: {
            const struct plan_payload *plan = (const struct plan_payload *)payload;
            if (!plan || plan->epoch != state->plan_epoch) {
                // Stale plan event, ignore
                break;
            }

            // Advance all pucks to now
            for (unsigned int i = 0; i < state->n_pucks; i++) {
                puck_advance_to(&state->pucks[i], now);
            }

            // 1. Check if a puck-puck collision occurs at now
            bool resolved = false;
            for (unsigned int i = 0; i < state->n_pucks; i++) {
                for (unsigned int j = i + 1; j < state->n_pucks; j++) {
                    double dx = state->pucks[i].x - state->pucks[j].x;
                    double dy = state->pucks[i].y - state->pucks[j].y;
                    double dist = sqrt(dx * dx + dy * dy);
                    double r_sum = state->pucks[i].radius + state->pucks[j].radius;

                    if (fabs(dist - r_sum) < 1e-3) {
                        resolve_elastic_collision(&state->pucks[i], &state->pucks[j], now);
                        state->total_collisions++;
                        resolved = true;
                        break;
                    }
                }
                if (resolved) break;
            }

            // 2. Check if a cushion bounce occurs at now
            if (!resolved) {
                for (unsigned int i = 0; i < state->n_pucks; i++) {
                    double r = state->pucks[i].radius;
                    if (state->pucks[i].x <= r + 1e-4 && state->pucks[i].vx < 0.0) {
                        resolve_cushion_bounce(&state->pucks[i], now, 0); // Left
                        state->total_bounces++;
                        resolved = true;
                        break;
                    }
                    if (state->pucks[i].x >= pucks_cfg.table_width - r - 1e-4 && state->pucks[i].vx > 0.0) {
                        resolve_cushion_bounce(&state->pucks[i], now, 1); // Right
                        state->total_bounces++;
                        resolved = true;
                        break;
                    }
                    if (state->pucks[i].y <= r + 1e-4 && state->pucks[i].vy < 0.0) {
                        resolve_cushion_bounce(&state->pucks[i], now, 2); // Bottom
                        state->total_bounces++;
                        resolved = true;
                        break;
                    }
                    if (state->pucks[i].y >= pucks_cfg.table_height - r - 1e-4 && state->pucks[i].vy > 0.0) {
                        resolve_cushion_bounce(&state->pucks[i], now, 3); // Top
                        state->total_bounces++;
                        resolved = true;
                        break;
                    }
                }
            }

            // 3. Check if a puck exited the sector at now
            if (!resolved) {
                for (unsigned int i = 0; i < state->n_pucks; i++) {
                    int exit_dir = -1;
                    if (state->pucks[i].x <= state->x_min + 1e-4 && state->pucks[i].vx < 0.0 && state->sector_x > 0) {
                        exit_dir = 0; // West
                    } else if (state->pucks[i].x >= state->x_max - 1e-4 && state->pucks[i].vx > 0.0 && state->sector_x < pucks_cfg.grid_dim - 1) {
                        exit_dir = 1; // East
                    } else if (state->pucks[i].y <= state->y_min + 1e-4 && state->pucks[i].vy < 0.0 && state->sector_y > 0) {
                        exit_dir = 2; // South
                    } else if (state->pucks[i].y >= state->y_max - 1e-4 && state->pucks[i].vy > 0.0 && state->sector_y < pucks_cfg.grid_dim - 1) {
                        exit_dir = 3; // North
                    }

                    if (exit_dir >= 0) {
                        lp_id_t dest_lp = get_neighbor_lp(me, exit_dir);
                        if (dest_lp != INVALID_DIRECTION) {
                            struct arrival_payload arr;
                            arr.p = state->pucks[i];
                            arr.p.t_ref = now;
                            arr.p.owner_lp = (unsigned int)dest_lp;

                            remove_puck_from_state(state, i);
                            state->total_exits++;

                            ScheduleNewEvent(dest_lp, now, EVENT_PUCK_ARRIVAL, &arr, sizeof(arr));
                            resolved = true;
                            break;
                        }
                    }
                }
            }

            replan_sector(me, state, now);
            break;
        }

        case EVENT_PUCK_ARRIVAL: {
            const struct arrival_payload *arr = (const struct arrival_payload *)payload;
            if (arr) {
                // Advance existing pucks to now
                for (unsigned int i = 0; i < state->n_pucks; i++) {
                    puck_advance_to(&state->pucks[i], now);
                }
                struct puck new_puck = arr->p;
                new_puck.t_ref = now;
                new_puck.owner_lp = (unsigned int)me;
                add_puck_to_state(state, &new_puck);
                replan_sector(me, state, now);
            }
            break;
        }

        case LP_FINI: {
            if (pucks_cfg.check_conservation) {
                double ke = 0.0;
                double px = 0.0, py = 0.0;
                for (unsigned int i = 0; i < state->n_pucks; i++) {
                    puck_advance_to(&state->pucks[i], now);
                    double v2 = state->pucks[i].vx * state->pucks[i].vx + state->pucks[i].vy * state->pucks[i].vy;
                    ke += 0.5 * state->pucks[i].mass * v2;
                    px += state->pucks[i].mass * state->pucks[i].vx;
                    py += state->pucks[i].mass * state->pucks[i].vy;
                }
                printf("LP %llu: Pucks=%u, Collisions=%llu, Bounces=%llu, Exits=%llu, KE=%.4f, Momentum=(%.2f, %.2f), PairChecks=%llu\n",
                       (unsigned long long)me, state->n_pucks,
                       state->total_collisions, state->total_bounces, state->total_exits,
                       ke, px, py, state->stats.pair_checks);
            }
            rs_free(state->pucks);
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
    .stats_file = "colliding_pucks",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 3600.0,
};

static bool pucks_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 2000:
            pucks_cfg.total_pucks = (unsigned int)strtoul(arg, NULL, 10);
            return true;
        case 2001:
            pucks_cfg.grid_dim = (unsigned int)strtoul(arg, NULL, 10);
            return true;
        case 2002:
            pucks_cfg.table_width = strtod(arg, NULL);
            return true;
        case 2003:
            pucks_cfg.table_height = strtod(arg, NULL);
            return true;
        case 2004:
            pucks_cfg.radius_min = strtod(arg, NULL);
            return true;
        case 2005:
            pucks_cfg.radius_max = strtod(arg, NULL);
            return true;
        case 2006:
            pucks_cfg.mass_min = strtod(arg, NULL);
            return true;
        case 2007:
            pucks_cfg.mass_max = strtod(arg, NULL);
            return true;
        case 2008:
            pucks_cfg.init_speed = strtod(arg, NULL);
            return true;
        case 2009:
            pucks_cfg.cluster = true;
            return true;
        case 2010:
            if (strcmp(arg, "naive") == 0) {
                pucks_cfg.index_mode = INDEX_NAIVE;
            } else if (strcmp(arg, "fixed") == 0) {
                pucks_cfg.index_mode = INDEX_FIXED;
            } else if (strcmp(arg, "dynamic") == 0) {
                pucks_cfg.index_mode = INDEX_DYNAMIC;
            } else {
                fprintf(stderr, "Unknown index mode: %s (choose naive, fixed, dynamic)\n", arg);
                exit(EXIT_FAILURE);
            }
            return true;
        case 2011:
            pucks_cfg.max_per_cell = (unsigned int)strtoul(arg, NULL, 10);
            return true;
        case 2012:
            pucks_cfg.check_conservation = true;
            return true;
        default:
            return false;
    }
}

static void pucks_extra_usage(void)
{
    printf("\nColliding Pucks Specific Options:\n");
    printf("  --pucks <N>                Total number of pucks (default: 64)\n");
    printf("  --grid-dim <G>             Sector grid dimension GxG (LPs = G*G, default: 4)\n");
    printf("  --table-width <W>          Table width (default: 1000.0)\n");
    printf("  --table-height <H>         Table height (default: 1000.0)\n");
    printf("  --radius-min <R>           Minimum puck radius (default: 5.0)\n");
    printf("  --radius-max <R>           Maximum puck radius (default: 10.0)\n");
    printf("  --mass-min <M>             Minimum puck mass (default: 1.0)\n");
    printf("  --mass-max <M>             Maximum puck mass (default: 5.0)\n");
    printf("  --speed <V>                Initial puck speed (default: 50.0)\n");
    printf("  --cluster                  Initial clustered placement in corner sector (1995 paper)\n");
    printf("  --index <MODE>             Candidate search mode: naive, fixed, dynamic (default: dynamic)\n");
    printf("  --max-per-cell <T>         Quadtree subdivision threshold t (default: 4)\n");
    printf("  --check                    Verify momentum/energy conservation and print stats at completion\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, pucks_cfg.grid_dim * pucks_cfg.grid_dim, 3600.0);

    static const struct option pucks_options[] = {
        {"pucks", required_argument, 0, 2000},
        {"grid-dim", required_argument, 0, 2001},
        {"table-width", required_argument, 0, 2002},
        {"table-height", required_argument, 0, 2003},
        {"radius-min", required_argument, 0, 2004},
        {"radius-max", required_argument, 0, 2005},
        {"mass-min", required_argument, 0, 2006},
        {"mass-max", required_argument, 0, 2007},
        {"speed", required_argument, 0, 2008},
        {"cluster", no_argument, 0, 2009},
        {"index", required_argument, 0, 2010},
        {"max-per-cell", required_argument, 0, 2011},
        {"check", no_argument, 0, 2012},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   pucks_options, NULL,
                                   pucks_extra_handler, NULL,
                                   pucks_extra_usage);

    conf.lps = pucks_cfg.grid_dim * pucks_cfg.grid_dim;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "colliding_pucks";
    }

    grid_topology = vInitializeTopology(TOPOLOGY_SQUARE, 2, pucks_cfg.grid_dim, pucks_cfg.grid_dim);

    RootsimInit(&conf);
    int ret = RootsimRun();

    if (grid_topology) {
        ReleaseTopology(grid_topology);
    }

    return ret;
}
