/**
 * @file phold/phold.c
 *
 * @brief Parameterizable PHold benchmark implementation
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
#include <string.h>
#include <time.h>

#ifndef NUM_LPS
#define NUM_LPS 1024
#endif

#ifndef NUM_THREADS
#define NUM_THREADS 0
#endif

#define EVENT         1
#define STERILE_EVENT 2

struct phold_state {
	struct rng_t seed;
	unsigned int executed_events;
};

struct phold_message {
	long int dummy_data;
};

typedef struct __model_parameters {
	unsigned long long clocks_per_us;
	unsigned int event_us;
	unsigned int fan_out;
	unsigned int num_events;
	int start_events;
	simtime_t p_remote;
	simtime_t mean;
	simtime_t lookahead;
	double prob_hit_hot;
	unsigned int hot_spots;
} model_parameters;

static model_parameters model_args = {
	.event_us = 5,
	.fan_out = 0,
	.num_events = 0,
	.start_events = 1,
	.hot_spots = 0,
	.prob_hit_hot = 0.0,
	.p_remote = 0.25,
	.mean = 1.0,
	.lookahead = 0.0,
	.clocks_per_us = 24,
};

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

static inline unsigned long long raw_clock_read(void)
{
#if defined(__x86_64__) || defined(__i386__)
	unsigned int lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return ((unsigned long long)hi << 32) | lo;
#elif defined(__aarch64__) || defined(__arm64__)
	unsigned long long val;
	__asm__ __volatile__ ("mrs %0, cntvct_el0" : "=r" (val));
	return val;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

static inline unsigned long long get_default_clocks_per_us(void)
{
#if defined(__aarch64__) || defined(__arm64__)
	unsigned long long freq = 0;
	__asm__ __volatile__ ("mrs %0, cntfrq_el0" : "=r" (freq));
	if (freq > 0)
		return freq / 1000000ULL;
	return 24ULL;
#elif defined(__x86_64__) || defined(__i386__)
	return 2687ULL;
#else
	return 1000ULL;
#endif
}

static inline lp_id_t get_hot_spot(lp_id_t dest, void *seed)
{
	if (!model_args.hot_spots)
		return dest;
	if (conf.lps <= model_args.hot_spots)
		return dest;
	if (Random(seed) < model_args.prob_hit_hot)
		dest = dest % model_args.hot_spots;
	else
		dest = model_args.hot_spots + (dest % (conf.lps - model_args.hot_spots));
	return dest;
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s)
{
	(void)content;
	(void)size;
	struct phold_message new_event = {0};
	lp_id_t dest;
	struct phold_state *state = (struct phold_state *)s;
	unsigned long long clock_at_start = raw_clock_read();
	unsigned long long clock_at_now;
	unsigned int i;

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(*state));
			if(state == NULL)
				abort();
			initialize_stream((unsigned int)me, &state->seed);
			state->executed_events = 0;
			SetState(state);

			for(i = 0; i < (unsigned int)model_args.start_events; i++)
				ScheduleNewEvent(me, Expent(&state->seed, model_args.mean) + model_args.lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		case LP_FINI:
			break;

		case STERILE_EVENT:
		case EVENT:
			if (state != NULL) {
				state->executed_events++;
			}

			if (model_args.event_us > 0) {
				unsigned long long target_ticks = (unsigned long long)model_args.event_us * model_args.clocks_per_us;
				do {
					clock_at_now = raw_clock_read();
				} while ((clock_at_now - clock_at_start) < target_ticks);
			}

			if(event_type == STERILE_EVENT)
				break;

			if(!model_args.fan_out && Random(&state->seed) <= model_args.p_remote) {
				dest = (lp_id_t)(Random(&state->seed) * conf.lps);
				dest = get_hot_spot(dest, &state->seed);
				ScheduleNewEvent(dest, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, STERILE_EVENT, &new_event, sizeof(new_event));
			}

			for(i = 0; i < model_args.fan_out; i++) {
				dest = (lp_id_t)(Random(&state->seed) * conf.lps);
				dest = get_hot_spot(dest, &state->seed);
				ScheduleNewEvent(dest, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, STERILE_EVENT, &new_event, sizeof(new_event));
			}

			ScheduleNewEvent(me, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		default:
			fprintf(stderr, "Unknown event type %u\n", event_type);
			abort();
	}
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	if (model_args.num_events == 0)
		return false;
	const struct phold_state *state = (const struct phold_state *)snapshot;
	return state != NULL && state->executed_events >= model_args.num_events;
}

static bool phold_extra_handler(int c, const char *arg, void *user_data)
{
	(void)user_data;
	switch(c) {
		case 2000:
			model_args.event_us = (unsigned int)strtoul(arg, NULL, 10);
			return true;
		case 2001:
			model_args.fan_out = (unsigned int)strtoul(arg, NULL, 10);
			return true;
		case 2002:
			model_args.num_events = (unsigned int)strtoul(arg, NULL, 10);
			return true;
		case 2003:
			model_args.hot_spots = (unsigned int)strtoul(arg, NULL, 10);
			return true;
		case 2004:
			model_args.prob_hit_hot = strtod(arg, NULL);
			return true;
		case 2005:
			model_args.p_remote = strtod(arg, NULL);
			return true;
		case 2006:
			model_args.mean = strtod(arg, NULL);
			return true;
		case 2007:
			model_args.lookahead = strtod(arg, NULL);
			return true;
		case 2008:
			model_args.start_events = atoi(arg);
			return true;
		case 2009:
			model_args.clocks_per_us = strtoull(arg, NULL, 10);
			return true;
		default:
			return false;
	}
}

static void phold_extra_usage(void)
{
	printf("\nPHOLD Specific Options:\n");
	printf("  --event-us <TIME>          Event granularity in microseconds (busy-wait loop, default: 5)\n");
	printf("  --fan-out <VALUE>          Event fan-out (sterile events sent per event, default: 0)\n");
	printf("  --num-events <VALUE>       Events per LP to end simulation (default: 0, disabled)\n");
	printf("  --hot-spots <VALUE>        Number of hot-spot LPs (default: 0)\n");
	printf("  --prob-hot-hit <VALUE>     Probability of routing to a hot-spot LP (default: 0.0)\n");
	printf("  --p-remote <PROB>          Probability of routing to a remote LP (default: 0.25)\n");
	printf("  --mean <TIME>              Mean exponential inter-event delay (default: 1.0)\n");
	printf("  --lookahead <TIME>         Minimum lookahead offset (default: 0.0)\n");
	printf("  --start-events <N>         Initial event population per LP (default: 1)\n");
	printf("  --clocks-per-us <N>        CPU clock frequency in cycles per microsecond\n");
}

int main(int argc, char **argv)
{
	model_args.clocks_per_us = get_default_clocks_per_us();

	struct model_cli_options opt;
	init_default_cli_options(&opt, NUM_LPS, 1000);

	static const struct option phold_options[] = {
		{"event-us", required_argument, 0, 2000},
		{"fan-out", required_argument, 0, 2001},
		{"num-events", required_argument, 0, 2002},
		{"hot-spots", required_argument, 0, 2003},
		{"prob-hot-hit", required_argument, 0, 2004},
		{"p-remote", required_argument, 0, 2005},
		{"mean", required_argument, 0, 2006},
		{"lookahead", required_argument, 0, 2007},
		{"start-events", required_argument, 0, 2008},
		{"clocks-per-us", required_argument, 0, 2009},
		{0, 0, 0, 0}
	};

	parse_model_cli_options_custom(argc, argv, &opt, &conf,
	                               phold_options, NULL,
	                               phold_extra_handler, NULL,
	                               phold_extra_usage);

	RootsimInit(&conf);
	return RootsimRun();
}
