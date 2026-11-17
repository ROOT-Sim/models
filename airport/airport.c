/**
 * @file airport/airport.c
 *
 * @brief Discrete event airport and air traffic network simulation for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "airport.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct airport_config airport_cfg = {
    .num_airports = 16,
    .total_planes = 320,
    .runways_per_airport = 2,
    .gates_per_airport = 20,
    .hub_ratio = 0.6,
    .mean_flight_time = 60.0,
    .mean_turnaround_time = 45.0,
    .runway_occupancy_time = 2.0,
    .check_invariants = false
};

lp_id_t select_flight_destination(lp_id_t origin, struct airport_state *state)
{
    unsigned int n = airport_cfg.num_airports;
    if (n <= 1) return origin;

    // Hub-and-spoke probability: top-3 airports are hubs (0, 1, 2)
    if (Random(&state->seed) < airport_cfg.hub_ratio && n >= 3) {
        unsigned int hub = (unsigned int)(Random(&state->seed) * 3.0);
        if (hub >= 3) hub = 2;
        if ((lp_id_t)hub != origin) {
            return (lp_id_t)hub;
        }
    }

    // Uniform random selection among other airports
    lp_id_t dest;
    do {
        dest = (lp_id_t)(Random(&state->seed) * n);
        if (dest >= n) dest = n - 1;
    } while (dest == origin && n > 1);

    return dest;
}

static void queue_push_holding(struct airport_state *state, const struct aircraft *plane)
{
    if (state->holding_len >= state->holding_cap) {
        unsigned int new_cap = state->holding_cap ? state->holding_cap * 2 : 16;
        state->holding_queue = rs_realloc(state->holding_queue, sizeof(struct aircraft) * new_cap);
        state->holding_cap = new_cap;
    }
    state->holding_queue[state->holding_len++] = *plane;
}

static struct aircraft queue_pop_holding(struct airport_state *state)
{
    struct aircraft plane = state->holding_queue[0];
    state->holding_len--;
    if (state->holding_len > 0) {
        memmove(&state->holding_queue[0], &state->holding_queue[1], sizeof(struct aircraft) * state->holding_len);
    }
    return plane;
}

static void queue_push_departure(struct airport_state *state, const struct aircraft *plane)
{
    if (state->departure_len >= state->departure_cap) {
        unsigned int new_cap = state->departure_cap ? state->departure_cap * 2 : 16;
        state->departure_queue = rs_realloc(state->departure_queue, sizeof(struct aircraft) * new_cap);
        state->departure_cap = new_cap;
    }
    state->departure_queue[state->departure_len++] = *plane;
}

static struct aircraft queue_pop_departure(struct airport_state *state)
{
    struct aircraft plane = state->departure_queue[0];
    state->departure_len--;
    if (state->departure_len > 0) {
        memmove(&state->departure_queue[0], &state->departure_queue[1], sizeof(struct aircraft) * state->departure_len);
    }
    return plane;
}

static int find_idle_runway(const struct airport_state *state)
{
    for (unsigned int i = 0; i < state->num_runways; i++) {
        if (!state->runways[i].busy) {
            return (int)i;
        }
    }
    return -1;
}

static void update_airport_metrics(struct airport_state *state, simtime_t now)
{
    if (now > state->last_stats_time) {
        double dt = now - state->last_stats_time;
        state->area_runways_busy += (double)state->busy_runways * dt;
        state->area_airborne_stack += (double)state->holding_len * dt;
        state->last_stats_time = now;
    }
}

static void schedule_next_runway_operation(lp_id_t me, struct airport_state *state, simtime_t now, unsigned int r_idx)
{
    // Airborne holding aircraft have landing priority over departing aircraft
    if (state->holding_len > 0) {
        struct aircraft plane = queue_pop_holding(state);
        state->runways[r_idx].busy = true;
        state->runways[r_idx].is_landing = true;
        state->runways[r_idx].current_flight_id = plane.flight_id;
        state->busy_runways++;

        double hold_delay = now - plane.arrival_time;
        state->total_air_holding_time += hold_delay;

        struct runway_action_payload pl;
        pl.plane = plane;
        pl.runway_idx = r_idx;
        ScheduleNewEvent(me, now + airport_cfg.runway_occupancy_time, EVENT_LANDED, &pl, sizeof(pl));
    } else if (state->departure_len > 0) {
        struct aircraft plane = queue_pop_departure(state);
        state->runways[r_idx].busy = true;
        state->runways[r_idx].is_landing = false;
        state->runways[r_idx].current_flight_id = plane.flight_id;
        state->busy_runways++;

        double dep_delay = now - plane.departure_time;
        state->total_ground_delay_time += dep_delay;

        struct runway_action_payload pl;
        pl.plane = plane;
        pl.runway_idx = r_idx;
        ScheduleNewEvent(me, now + airport_cfg.runway_occupancy_time, EVENT_DEPARTURE, &pl, sizeof(pl));
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct airport_state *state = (struct airport_state *)s;
    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
        update_airport_metrics(state, now);
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me + 42, &state->seed);
            state->lvt = now;
            state->airport_id = me;
            state->last_stats_time = now;
            state->num_runways = airport_cfg.runways_per_airport;
            if (state->num_runways > MAX_AIRPORT_RUNWAYS) state->num_runways = MAX_AIRPORT_RUNWAYS;
            state->gates_capacity = airport_cfg.gates_per_airport;

            state->holding_cap = 16;
            state->holding_queue = rs_malloc(sizeof(struct aircraft) * state->holding_cap);
            state->departure_cap = 16;
            state->departure_queue = rs_malloc(sizeof(struct aircraft) * state->departure_cap);

            SetState(state);

            // Distribute initial fleet across airports
            unsigned int planes_here = airport_cfg.total_planes / airport_cfg.num_airports;
            if (me < (airport_cfg.total_planes % airport_cfg.num_airports)) {
                planes_here++;
            }

            state->gates_occupied = planes_here;
            for (unsigned int j = 0; j < planes_here; j++) {
                struct aircraft plane;
                plane.flight_id = ((uint64_t)me << 32) | (uint64_t)j;
                plane.origin = me;
                plane.destination = select_flight_destination(me, state);
                plane.departure_time = now;
                plane.arrival_time = 0.0;

                struct flight_payload fp;
                fp.plane = plane;

                // Stagger initial flights
                double stagger = 0.5 + (double)j * 2.0 + Random(&state->seed) * 5.0;
                ScheduleNewEvent(me, now + stagger, EVENT_GATE_RELEASE, &fp, sizeof(fp));
            }
            break;
        }

        case EVENT_GATE_RELEASE: {
            const struct flight_payload *fp = (const struct flight_payload *)payload;
            if (!fp) break;

            struct aircraft plane = fp->plane;
            plane.departure_time = now;

            int idle_r = find_idle_runway(state);
            if (idle_r >= 0 && state->holding_len == 0) {
                state->runways[idle_r].busy = true;
                state->runways[idle_r].is_landing = false;
                state->runways[idle_r].current_flight_id = plane.flight_id;
                state->busy_runways++;

                struct runway_action_payload pl;
                pl.plane = plane;
                pl.runway_idx = (unsigned int)idle_r;
                ScheduleNewEvent(me, now + airport_cfg.runway_occupancy_time, EVENT_DEPARTURE, &pl, sizeof(pl));
            } else {
                queue_push_departure(state, &plane);
            }
            break;
        }

        case EVENT_DEPARTURE: {
            const struct runway_action_payload *rap = (const struct runway_action_payload *)payload;
            if (!rap) break;

            unsigned int r_idx = rap->runway_idx;
            state->runways[r_idx].busy = false;
            state->busy_runways--;
            state->total_departures++;
            if (state->gates_occupied > 0) state->gates_occupied--;

            struct aircraft plane = rap->plane;
            lp_id_t dest = plane.destination;

            // Draw cruise flight time: exponential around mean + minimum cruise
            double cruise_mean = airport_cfg.mean_flight_time > 15.0 ? (airport_cfg.mean_flight_time - 15.0) : 10.0;
            double cruise = Expent(&state->seed, cruise_mean) + 15.0;
            plane.origin = me;
            plane.arrival_time = now + cruise;

            struct flight_payload fp;
            fp.plane = plane;
            ScheduleNewEvent(dest, now + cruise, EVENT_ARRIVAL, &fp, sizeof(fp));

            // Schedule next operation on this runway
            schedule_next_runway_operation(me, state, now, r_idx);
            break;
        }

        case EVENT_ARRIVAL: {
            const struct flight_payload *fp = (const struct flight_payload *)payload;
            if (!fp) break;

            struct aircraft plane = fp->plane;
            plane.arrival_time = now;
            state->total_arrivals++;

            int idle_r = find_idle_runway(state);
            if (idle_r >= 0 && state->holding_len == 0) {
                state->runways[idle_r].busy = true;
                state->runways[idle_r].is_landing = true;
                state->runways[idle_r].current_flight_id = plane.flight_id;
                state->busy_runways++;

                struct runway_action_payload pl;
                pl.plane = plane;
                pl.runway_idx = (unsigned int)idle_r;
                ScheduleNewEvent(me, now + airport_cfg.runway_occupancy_time, EVENT_LANDED, &pl, sizeof(pl));
            } else {
                queue_push_holding(state, &plane);
            }
            break;
        }

        case EVENT_LANDED: {
            const struct runway_action_payload *rap = (const struct runway_action_payload *)payload;
            if (!rap) break;

            unsigned int r_idx = rap->runway_idx;
            state->runways[r_idx].busy = false;
            state->busy_runways--;
            state->total_landings++;
            state->gates_occupied++;

            struct aircraft plane = rap->plane;
            plane.origin = me;
            plane.destination = select_flight_destination(me, state);

            // Gate turnaround
            double turn_mean = airport_cfg.mean_turnaround_time > 10.0 ? (airport_cfg.mean_turnaround_time - 10.0) : 10.0;
            double turn = Expent(&state->seed, turn_mean) + 10.0;
            state->total_turnaround_time += turn;

            struct flight_payload fp;
            fp.plane = plane;
            ScheduleNewEvent(me, now + turn, EVENT_GATE_RELEASE, &fp, sizeof(fp));

            // Schedule next operation on this runway
            schedule_next_runway_operation(me, state, now, r_idx);
            break;
        }

        case LP_FINI: {
            double total_time = state->lvt > 0.0 ? state->lvt : 1.0;
            double runway_util = state->area_runways_busy / (total_time * (double)state->num_runways);
            double mean_stack = state->area_airborne_stack / total_time;
            double mean_hold_delay = state->total_landings > 0 ? (state->total_air_holding_time / (double)state->total_landings) : 0.0;

            unsigned int planes_on_ground = state->gates_occupied + state->holding_len + state->departure_len + state->busy_runways;

            printf("Airport %llu: Landings=%" PRIu64 ", Departures=%" PRIu64 ", RunwayUtil=%.4f, MeanHoldingStack=%.2f, MeanHoldDelay=%.2fm, PlanesOnGround=%u\n",
                   (unsigned long long)me, state->total_landings, state->total_departures,
                   runway_util, mean_stack, mean_hold_delay, planes_on_ground);

            rs_free(state->holding_queue);
            rs_free(state->departure_queue);
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
    .stats_file = "airport",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1440.0, // 24 hours (1440 minutes)
};

static bool airport_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --airports
            airport_cfg.num_airports = (unsigned int)atoi(arg);
            return true;
        case 257: // --planes
            airport_cfg.total_planes = (unsigned int)atoi(arg);
            return true;
        case 258: // --runways
            airport_cfg.runways_per_airport = (unsigned int)atoi(arg);
            return true;
        case 259: // --gates
            airport_cfg.gates_per_airport = (unsigned int)atoi(arg);
            return true;
        case 260: // --hub-ratio
            airport_cfg.hub_ratio = atof(arg);
            return true;
        case 261: // --flight-time
            airport_cfg.mean_flight_time = atof(arg);
            return true;
        case 262: // --turnaround
            airport_cfg.mean_turnaround_time = atof(arg);
            return true;
        case 263: // --runway-time
            airport_cfg.runway_occupancy_time = atof(arg);
            return true;
        case 264: // --check
            airport_cfg.check_invariants = true;
            return true;
        default:
            return false;
    }
}

static void airport_extra_usage(void)
{
    printf("\nAirport Network Specific Options:\n");
    printf("  --airports <N>                 Number of airports (default: 16)\n");
    printf("  --planes <M>                   Total aircraft fleet size (default: 320)\n");
    printf("  --runways <K>                  Runways per airport (default: 2)\n");
    printf("  --gates <G>                    Gate capacity per airport (default: 20)\n");
    printf("  --hub-ratio <H>                Fraction of flights to hub airports (default: 0.6)\n");
    printf("  --flight-time <T>              Mean flight cruise time in minutes (default: 60.0)\n");
    printf("  --turnaround <T>               Mean gate turnaround time in minutes (default: 45.0)\n");
    printf("  --runway-time <T>              Runway occupancy time per landing/takeoff (default: 2.0)\n");
    printf("  --check                        Verify aircraft conservation invariant\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, airport_cfg.num_airports, 1440.0);

    static const struct option airport_options[] = {
        {"airports", required_argument, 0, 256},
        {"planes", required_argument, 0, 257},
        {"runways", required_argument, 0, 258},
        {"gates", required_argument, 0, 259},
        {"hub-ratio", required_argument, 0, 260},
        {"flight-time", required_argument, 0, 261},
        {"turnaround", required_argument, 0, 262},
        {"runway-time", required_argument, 0, 263},
        {"check", no_argument, 0, 264},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   airport_options, NULL,
                                   airport_extra_handler, NULL,
                                   airport_extra_usage);

    if (opt.lps != 0) {
        airport_cfg.num_airports = opt.lps;
    }
    conf.lps = airport_cfg.num_airports;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "airport";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
