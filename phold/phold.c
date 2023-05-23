/**
 * @file phold/phold.c
 *
 * @brief Minimalist PHold implementation
 *
 * SPDX-FileCopyrightText: 2008-2023 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NUM_LPS
#define NUM_LPS 8192
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

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s)
{
	struct phold_message new_event = {0};
	lp_id_t dest;
	struct phold_state *state = (struct phold_state *)s;

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(*state));
			if(state == NULL)
				abort();
			initialize_stream(me, &state->seed);
			SetState(state);

			for(int i = 0; i < start_events; i++)
				ScheduleNewEvent(me, Expent(&state->seed, mean) + lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		case LP_FINI:
			break;

		case EVENT:
			dest = me;
			if(Random(&state->seed) <= p_remote)
				dest = (lp_id_t)(Random(&state->seed) * NUM_LPS);

			ScheduleNewEvent(dest, now + Expent(&state->seed, mean) + lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		default:
			fprintf(stderr, "Unknown event type\n");
			abort();
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	return false;
}

struct simulation_configuration conf = {
    .lps = NUM_LPS,
    .n_threads = NUM_THREADS,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "phold",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

int main(void)
{
	RootsimInit(&conf);
	return RootsimRun();
}
