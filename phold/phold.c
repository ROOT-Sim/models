/**
 * @file phold/phold.c
 *
 * @brief Minimalist PHold implementation
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include "argparse.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NUM_LPS
#define NUM_LPS 1024
#endif

#ifndef NUM_THREADS
#define NUM_THREADS 0
#endif

#define EVENT 1

struct phold_state {
	struct rng_t seed;
};

struct phold_message {
	long int dummy_data;
};

static simtime_t p_remote = 0.25;
static simtime_t mean = 1.0;
static simtime_t lookahead = 0.0;
static int start_events = 1;

static bool CanEnd(lp_id_t me, const void *snapshot);
static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s);

static struct simulation_configuration conf = {
    .lps = NUM_LPS,
    .n_threads = NUM_THREADS,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "phold",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s)
{
	(void)content;
	(void)size;
	struct phold_message new_event = {0};
	lp_id_t dest;
	struct phold_state *state = (struct phold_state *)s;

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(*state));
			if(state == NULL)
				abort();
			initialize_stream((unsigned int)me, &state->seed);
			SetState(state);

			for(int i = 0; i < start_events; i++)
				ScheduleNewEvent(me, Expent(&state->seed, mean) + lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		case LP_FINI:
			break;

		case EVENT:
			dest = me;
			if(Random(&state->seed) <= p_remote)
				dest = (lp_id_t)(Random(&state->seed) * conf.lps);

			ScheduleNewEvent(dest, now + Expent(&state->seed, mean) + lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		default:
			fprintf(stderr, "Unknown event type\n");
			abort();
	}
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	(void)snapshot;
	return false;
}

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, NUM_LPS, 1000);
	parse_model_cli_options(argc, argv, &opt, &conf);

	RootsimInit(&conf);
	return RootsimRun();
}
