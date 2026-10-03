/**
 * @file sharks/sharks.c
 *
 * @brief Discrete-event simulation model of Sharks World for ROOT-Sim 3.0
 *
 * Implements the asynchronous overlapping sector model from Conklin, Cleary & Unger.
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "sharks.h"
#include "argparse.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct sharks_config sharks_cfg = {
    .world_size = 1000.0,
    .grid_dim = 4,
    .total_creatures = 1000,
    .sharks_ratio = 0.10,
    .attack_range = 25.0,
    .max_velocity = 20.0,
};

struct topology *torus_topology = NULL;

static inline lp_id_t get_torus_neighbor(unsigned int gx, unsigned int gy, int dx, int dy)
{
    unsigned int S = sharks_cfg.grid_dim;
    unsigned int nx = (unsigned int)((int)gx + dx + (int)S) % S;
    unsigned int ny = (unsigned int)((int)gy + dy + (int)S) % S;
    return (lp_id_t)(ny * S + nx);
}

static inline void to_sector_rel(double x, double y, double cx, double cy, double world_size,
                                 double *rx, double *ry)
{
    double dx = x - cx;
    if (dx > world_size * 0.5) dx -= world_size;
    else if (dx < -world_size * 0.5) dx += world_size;

    double dy = y - cy;
    if (dy > world_size * 0.5) dy -= world_size;
    else if (dy < -world_size * 0.5) dy += world_size;

    *rx = dx;
    *ry = dy;
}

static void add_creature_to_state(struct sector_state *state, const struct creature *c)
{
    // Check if creature is already in state
    for (unsigned int i = 0; i < state->n_creatures; i++) {
        if (state->creatures[i].id == c->id) {
            if (c->version >= state->creatures[i].version) {
                state->creatures[i] = *c;
            }
            return;
        }
    }

    if (state->n_creatures >= state->capacity) {
        state->capacity = (state->capacity == 0) ? 32 : state->capacity * 2;
        state->creatures = rs_realloc(state->creatures, sizeof(struct creature) * state->capacity);
    }
    state->creatures[state->n_creatures++] = *c;
}

static void remove_creature_from_state(struct sector_state *state, unsigned int idx)
{
    if (idx < state->n_creatures) {
        state->creatures[idx] = state->creatures[state->n_creatures - 1];
        state->n_creatures--;
    }
}

static void resolve_immediate_attacks(lp_id_t me, struct sector_state *state, simtime_t now, bool notify_neighbors)
{
    (void)me;
    double sec_w = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;
    double sec_h = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;
    double cx = (state->sector_x + 0.5) * sec_w;
    double cy = (state->sector_y + 0.5) * sec_h;
    double half_w = sec_w * 0.5;
    double half_h = sec_h * 0.5;

    for (unsigned int i = 0; i < state->n_creatures; i++) {
        struct creature *sh = &state->creatures[i];
        if (sh->type != CREATURE_SHARK || !sh->alive) continue;

        double rx, ry;
        to_sector_rel(sh->x, sh->y, cx, cy, sharks_cfg.world_size, &rx, &ry);
        if (fabs(rx) >= half_w || fabs(ry) >= half_h) continue;

        double closest_d = DBL_MAX;
        int target_idx = -1;
        for (unsigned int j = 0; j < state->n_creatures; j++) {
            struct creature *fi = &state->creatures[j];
            if (fi->type != CREATURE_FISH || !fi->alive) continue;

            double d = torus_dist(sh->x, sh->y, fi->x, fi->y, sharks_cfg.world_size);
            if (d <= sharks_cfg.attack_range + 1e-4 && d < closest_d) {
                closest_d = d;
                target_idx = (int)j;
            }
        }

        if (target_idx >= 0) {
            struct creature *victim = &state->creatures[target_idx];
            victim->alive = false;
            victim->version++;
            state->total_kills++;

            if (notify_neighbors) {
                struct sharks_kill_payload kmsg;
                kmsg.fish_id = victim->id;
                kmsg.version = victim->version;
                kmsg.x = victim->x;
                kmsg.y = victim->y;

                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        lp_id_t nlp = get_torus_neighbor(state->sector_x, state->sector_y, dx, dy);
                        ScheduleNewEvent(nlp, now, EVENT_KILL, &kmsg, sizeof(kmsg));
                    }
                }
            }
        }
    }
}

static void replan_sharks(lp_id_t me, struct sector_state *state, simtime_t now)
{
    (void)me;
    double earliest_t = DBL_MAX;
    double sec_w = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;
    double sec_h = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;
    double cx = (state->sector_x + 0.5) * sec_w;
    double cy = (state->sector_y + 0.5) * sec_h;
    double half_w = sec_w * 0.5;
    double half_h = sec_h * 0.5;
    double out_w = half_w + sharks_cfg.attack_range;
    double out_h = half_h + sharks_cfg.attack_range;

    // 1. Advance all creatures to now and count live fish
    state->live_fish_count = 0;
    for (unsigned int i = 0; i < state->n_creatures; i++) {
        creature_advance_to(&state->creatures[i], now, sharks_cfg.world_size);
        if (state->creatures[i].type == CREATURE_FISH && state->creatures[i].alive) {
            state->live_fish_count++;
        }
    }

    // 2. Check future shark attacks for sharks owned by this sector
    for (unsigned int i = 0; i < state->n_creatures; i++) {
        struct creature *sh = &state->creatures[i];
        if (sh->type != CREATURE_SHARK || !sh->alive) continue;

        double rx, ry;
        to_sector_rel(sh->x, sh->y, cx, cy, sharks_cfg.world_size, &rx, &ry);
        if (fabs(rx) >= half_w || fabs(ry) >= half_h) {
            // Shark nominal center is not in this sector
            continue;
        }

        // Check against all live fish in the sector view
        for (unsigned int j = 0; j < state->n_creatures; j++) {
            struct creature *fi = &state->creatures[j];
            if (fi->type != CREATURE_FISH || !fi->alive) continue;

            double t_att;
            if (predict_shark_fish_attack(sh, fi, now, sharks_cfg.world_size,
                                          sharks_cfg.attack_range, &t_att)) {
                if (t_att > now + 1e-6 && t_att < earliest_t) {
                    earliest_t = t_att;
                }
            }
        }
    }

    // 3. Check outer boundary exits and entries into neighbor outer bounds
    for (unsigned int i = 0; i < state->n_creatures; i++) {
        struct creature *c = &state->creatures[i];
        if (!c->alive) continue;

        double rx, ry;
        to_sector_rel(c->x, c->y, cx, cy, sharks_cfg.world_size, &rx, &ry);

        // Predict exit from this sector's outer bounds
        if (c->vx < -1e-9) {
            double dt = (-out_w - rx) / c->vx;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        } else if (c->vx > 1e-9) {
            double dt = (out_w - rx) / c->vx;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }

        if (c->vy < -1e-9) {
            double dt = (-out_h - ry) / c->vy;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        } else if (c->vy > 1e-9) {
            double dt = (out_h - ry) / c->vy;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }

        // Predict entry into neighbor sector outer bounds
        // East neighbor: boundary is at x = half_w - attack_range
        if (c->vx > 1e-9 && rx < half_w - sharks_cfg.attack_range) {
            double dt = ((half_w - sharks_cfg.attack_range) - rx) / c->vx;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }
        // West neighbor: boundary is at x = -(half_w - attack_range)
        if (c->vx < -1e-9 && rx > -(half_w - sharks_cfg.attack_range)) {
            double dt = (-(half_w - sharks_cfg.attack_range) - rx) / c->vx;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }
        // North neighbor: boundary is at y = half_h - attack_range
        if (c->vy > 1e-9 && ry < half_h - sharks_cfg.attack_range) {
            double dt = ((half_h - sharks_cfg.attack_range) - ry) / c->vy;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }
        // South neighbor: boundary is at y = -(half_h - attack_range)
        if (c->vy < -1e-9 && ry > -(half_h - sharks_cfg.attack_range)) {
            double dt = (-(half_h - sharks_cfg.attack_range) - ry) / c->vy;
            if (dt > 1e-6 && (now + dt) < earliest_t) earliest_t = now + dt;
        }
    }

    if (earliest_t < DBL_MAX && earliest_t > now + 1e-6) {
        state->plan_epoch++;
        state->next_planned_time = earliest_t;
        struct sharks_plan_payload payload = { .epoch = state->plan_epoch };
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

    double sec_w = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;
    double sec_h = sharks_cfg.world_size / (double)sharks_cfg.grid_dim;

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me, &state->seed);
            state->lvt = now;
            state->sector_x = (unsigned int)(me % sharks_cfg.grid_dim);
            state->sector_y = (unsigned int)(me / sharks_cfg.grid_dim);

            state->nominal_x0 = state->sector_x * sec_w;
            state->nominal_x1 = (state->sector_x + 1) * sec_w;
            state->nominal_y0 = state->sector_y * sec_h;
            state->nominal_y1 = (state->sector_y + 1) * sec_h;

            state->outer_x0 = state->nominal_x0 - sharks_cfg.attack_range;
            state->outer_x1 = state->nominal_x1 + sharks_cfg.attack_range;
            state->outer_y0 = state->nominal_y0 - sharks_cfg.attack_range;
            state->outer_y1 = state->nominal_y1 + sharks_cfg.attack_range;

            state->capacity = 64;
            state->creatures = rs_malloc(sizeof(struct creature) * state->capacity);
            state->n_creatures = 0;

            SetState(state);

            // Generate creatures
            unsigned int lps_total = sharks_cfg.grid_dim * sharks_cfg.grid_dim;
            unsigned int base_n = sharks_cfg.total_creatures / lps_total;
            unsigned int extra_n = sharks_cfg.total_creatures % lps_total;
            unsigned int my_count = base_n + (me < extra_n ? 1 : 0);
            unsigned int id_base = (unsigned int)me * base_n + (me < extra_n ? (unsigned int)me : extra_n);

            for (unsigned int k = 0; k < my_count; k++) {
                struct creature c;
                c.id = id_base + k;
                c.type = (Random(&state->seed) < sharks_cfg.sharks_ratio) ? CREATURE_SHARK : CREATURE_FISH;
                c.x = state->nominal_x0 + Random(&state->seed) * sec_w;
                c.y = state->nominal_y0 + Random(&state->seed) * sec_h;
                c.speed = 1.0 + Random(&state->seed) * (sharks_cfg.max_velocity - 1.0);
                c.angle_deg = (int)(Random(&state->seed) * 360.0);
                double rad = (double)c.angle_deg * M_PI / 180.0;
                c.vx = c.speed * cos(rad);
                c.vy = c.speed * sin(rad);
                c.t_ref = now;
                c.version = 1;
                c.alive = true;
                c.home_sector = (unsigned int)me;

                add_creature_to_state(state, &c);
            }

            resolve_immediate_attacks(me, state, now, false);
            replan_sharks(me, state, now);
            break;
        }

        case EVENT_KILL: {
            const struct sharks_kill_payload *kmsg = (const struct sharks_kill_payload *)payload;
            if (kmsg) {
                for (unsigned int i = 0; i < state->n_creatures; i++) {
                    if (state->creatures[i].id == kmsg->fish_id) {
                        state->creatures[i].alive = false;
                        state->creatures[i].version = kmsg->version;
                        break;
                    }
                }
                replan_sharks(me, state, now);
            }
            break;
        }

        case EVENT_ARRIVAL: {
            const struct sharks_arrival_payload *arr = (const struct sharks_arrival_payload *)payload;
            if (arr && arr->c.alive) {
                add_creature_to_state(state, &arr->c);
                state->total_arrivals++;
                resolve_immediate_attacks(me, state, now, true);
                replan_sharks(me, state, now);
            }
            break;
        }

        case EVENT_PLAN: {
            const struct sharks_plan_payload *plan = (const struct sharks_plan_payload *)payload;
            if (!plan || plan->epoch != state->plan_epoch) {
                break;
            }

            double cx = (state->sector_x + 0.5) * sec_w;
            double cy = (state->sector_y + 0.5) * sec_h;
            double half_w = sec_w * 0.5;
            double half_h = sec_h * 0.5;
            double out_w = half_w + sharks_cfg.attack_range;
            double out_h = half_h + sharks_cfg.attack_range;

            for (unsigned int i = 0; i < state->n_creatures; i++) {
                creature_advance_to(&state->creatures[i], now, sharks_cfg.world_size);
            }

            // 1. Resolve attacks
            resolve_immediate_attacks(me, state, now, true);

            // 2. Dispatch arrivals to neighbors
            for (unsigned int i = 0; i < state->n_creatures; i++) {
                struct creature *c = &state->creatures[i];
                if (!c->alive) continue;

                double rx, ry;
                to_sector_rel(c->x, c->y, cx, cy, sharks_cfg.world_size, &rx, &ry);

                // Check 4 cardinal neighbors
                int dx_arr[4] = {1, -1, 0, 0};
                int dy_arr[4] = {0, 0, 1, -1};

                for (int dir = 0; dir < 4; dir++) {
                    int dx = dx_arr[dir];
                    int dy = dy_arr[dir];
                    bool crossing = false;

                    if (dx == 1 && c->vx > 1e-9 && fabs(rx - (half_w - sharks_cfg.attack_range)) < 1e-3) {
                        crossing = true;
                    } else if (dx == -1 && c->vx < -1e-9 && fabs(rx - (-(half_w - sharks_cfg.attack_range))) < 1e-3) {
                        crossing = true;
                    } else if (dy == 1 && c->vy > 1e-9 && fabs(ry - (half_h - sharks_cfg.attack_range)) < 1e-3) {
                        crossing = true;
                    } else if (dy == -1 && c->vy < -1e-9 && fabs(ry - (-(half_h - sharks_cfg.attack_range))) < 1e-3) {
                        crossing = true;
                    }

                    if (crossing) {
                        lp_id_t nlp = get_torus_neighbor(state->sector_x, state->sector_y, dx, dy);
                        struct sharks_arrival_payload arr;
                        arr.c = *c;
                        arr.c.t_ref = now;
                        ScheduleNewEvent(nlp, now, EVENT_ARRIVAL, &arr, sizeof(arr));
                    }
                }
            }

            // 3. Clean up creatures that left outer boundary
            for (int i = (int)state->n_creatures - 1; i >= 0; i--) {
                struct creature *c = &state->creatures[i];
                double rx, ry;
                to_sector_rel(c->x, c->y, cx, cy, sharks_cfg.world_size, &rx, &ry);

                if (fabs(rx) > out_w + 1e-3 || fabs(ry) > out_h + 1e-3 || !c->alive) {
                    remove_creature_from_state(state, (unsigned int)i);
                    state->total_exits++;
                }
            }

            replan_sharks(me, state, now);
            break;
        }

        case LP_FINI: {
            printf("LP %llu: Creatures=%u, Kills=%llu, Arrivals=%llu, Exits=%llu\n",
                   (unsigned long long)me, state->n_creatures,
                   state->total_kills, state->total_arrivals, state->total_exits);
            rs_free(state->creatures);
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
    .stats_file = "sharks",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1000.0,
};

static bool sharks_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 2000:
            sharks_cfg.total_creatures = (unsigned int)strtoul(arg, NULL, 10);
            return true;
        case 2001:
            sharks_cfg.grid_dim = (unsigned int)strtoul(arg, NULL, 10);
            return true;
        case 2002:
            sharks_cfg.world_size = strtod(arg, NULL);
            return true;
        case 2003:
            sharks_cfg.sharks_ratio = strtod(arg, NULL);
            return true;
        case 2004:
            sharks_cfg.attack_range = strtod(arg, NULL);
            return true;
        case 2005:
            sharks_cfg.max_velocity = strtod(arg, NULL);
            return true;
        default:
            return false;
    }
}

static void sharks_extra_usage(void)
{
    printf("\nSharks World Specific Options:\n");
    printf("  --creatures <N>            Total number of creatures (default: 1000)\n");
    printf("  --grid-dim <S>             Sector grid dimension SxS (LPs = S*S, default: 4)\n");
    printf("  --world-size <M>           Torus ocean dimension MxM (default: 1000.0)\n");
    printf("  --sharks-ratio <R>         Fraction of creatures that are sharks (default: 0.10)\n");
    printf("  --attack-range <A>         Shark effective attack range (default: 25.0)\n");
    printf("  --max-velocity <V>         Maximum creature velocity (default: 20.0)\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, sharks_cfg.grid_dim * sharks_cfg.grid_dim, 1000.0);

    static const struct option sharks_options[] = {
        {"creatures", required_argument, 0, 2000},
        {"grid-dim", required_argument, 0, 2001},
        {"world-size", required_argument, 0, 2002},
        {"sharks-ratio", required_argument, 0, 2003},
        {"attack-range", required_argument, 0, 2004},
        {"max-velocity", required_argument, 0, 2005},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   sharks_options, NULL,
                                   sharks_extra_handler, NULL,
                                   sharks_extra_usage);

    conf.lps = sharks_cfg.grid_dim * sharks_cfg.grid_dim;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "sharks";
    }

    torus_topology = vInitializeTopology(TOPOLOGY_TORUS, 2, sharks_cfg.grid_dim, sharks_cfg.grid_dim);

    RootsimInit(&conf);
    int ret = RootsimRun();

    if (torus_topology) {
        ReleaseTopology(torus_topology);
    }

    return ret;
}
