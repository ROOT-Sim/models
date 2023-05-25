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

#include <argp.h>         

#define EVENT 		  1
#define STERILE_EVENT 2

struct phold_state {
	struct rng_t seed;
};

struct phold_message {
	long int dummy_data;
};


typedef struct __model_parameters{
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



bool CanEnd(lp_id_t me, const void *snapshot);
void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s);


model_parameters model_args = {
	.event_us = 5,
	.fan_out  = 0,  
	.num_events = 5000,
	.start_events = 1,
	.hot_spots = 0,
	.prob_hit_hot = 0.0,
	.start_events = 1,
	.p_remote = 0.25,
	.mean = 1.0,
	.lookahead = 0.0,
	.clocks_per_us = 2687,
};


struct simulation_configuration conf = {
    .lps = 0,
    .n_threads = 0,
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


struct argp_option model_options[] = {
  {"ncores",              'c'          , "CORES"   ,  0                  ,  "Number of threads to be used"               , 0 },
  {"nprocesses",          'p'          , "LPS"     ,  0                  ,  "Number of simulation objects"               , 0 },
  {"event-us",          1000, "TIME",  0, "Event granularity"               , 0 },
  {"fan-out",           1001, "VALUE", 0, "Event fan out"               , 0 },
  {"num-events",        1002, "VALUE", 0, "Number of event per process to end the simulation"               , 0 },
  {"hot-spots",         1003, "VALUE", 0, "Hot-spot LPs"               , 0 },
  {"prob-hot-hit",      1004, "VALUE", 0, "Prob. to hit a hot-spot"               , 0 },
  { 0, 0, 0, 0, 0, 0} 
};

error_t model_parse_opt(int key, char *arg, struct argp_state *state){
	(void)state;
	switch(key){
		case 1000:
			model_args.event_us = atoi(arg);
			break;
		case 1001:
			model_args.fan_out = atoi(arg);
			break;
		case 1002:
			model_args.num_events = atoi(arg);
			break;
		case 1003:
			model_args.hot_spots = atoi(arg);
			break;
		case 1004:
			model_args.prob_hit_hot = strtod(arg, NULL);
			break;
		case 'c':
			conf.n_threads = atoi(arg);
			break;
		case 'p':
			conf.lps = atoi(arg);
			break;
		case ARGP_KEY_END:
	      if(conf.lps == 0){
	        printf("Please set a non-zero number of lps\n");
	        argp_usage (state);  
	      }
	      if(conf.n_threads == 0){
	        printf("Please set a non-zero number of threads\n");
	        argp_usage (state);  
	      }
	}
	return 0;
}







/// This overflows if the machine is not restarted in about 50-100 years (on 64 bits archs)
#define RAW_CLOCK_READ() ({ \
			unsigned int lo; \
			unsigned int hi; \
			__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
			(unsigned long long)(((unsigned long long)hi) << 32 | lo); \
			})


static inline lp_id_t get_hot_spot(lp_id_t dest, void *seed){
	if(!model_args.hot_spots) return dest;
	if(Random(seed) < model_args.prob_hit_hot)
		dest = dest % model_args.hot_spots;
	else
		dest = model_args.hot_spots + (dest  % (conf.lps - model_args.hot_spots) );
}

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s)
{
	struct phold_message new_event = {0};
	lp_id_t dest;
	struct phold_state *state = (struct phold_state *)s;
	unsigned long long clock_at_start = RAW_CLOCK_READ();
	unsigned long long clock_at_now;
	unsigned int i;

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(*state));
			if(state == NULL)
				abort();
			initialize_stream(me, &state->seed);
			SetState(state);

			for(i = 0; i < model_args.start_events; i++)
				ScheduleNewEvent(me, Expent(&state->seed, model_args.mean) + model_args.lookahead, EVENT, &new_event, sizeof(new_event));
			break;

		case LP_FINI:
			break;

		case STERILE_EVENT:
		case EVENT:

			do{
			  clock_at_now = RAW_CLOCK_READ();
			}while( (clock_at_now-clock_at_start) < (model_args.event_us*model_args.clocks_per_us) );
			if(event_type == STERILE_EVENT) break;

			if(!model_args.fan_out && Random(&state->seed) <= model_args.p_remote){
				dest = (lp_id_t)(Random(&state->seed) * conf.lps);
				dest = get_hot_spot(dest,&state->seed);
				ScheduleNewEvent(dest, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, STERILE_EVENT, &new_event, sizeof(new_event));
			}

			for(i=0;i<model_args.fan_out;i++){
				dest = (lp_id_t)(Random(&state->seed) * conf.lps);
				dest = get_hot_spot(dest,&state->seed);
				ScheduleNewEvent(dest, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, STERILE_EVENT, &new_event, sizeof(new_event));
			}


			ScheduleNewEvent(me, now + Expent(&state->seed, model_args.mean) + model_args.lookahead, EVENT, &new_event, sizeof(new_event));
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


const char *argp_program_version = "Phold 1.0";
const char *argp_program_bug_address = "<bho.com>";
static char doc[] = "Phold";
static char args_doc[] = "-c CORES -p LPS";  


static struct argp argp       = { model_options,       model_parse_opt, args_doc, doc, NULL, 0, 0 };

void parse_options(int argn, char **argv){
  argp_parse(&argp, argn, argv, 0, NULL, NULL);
}



int main(int argn, char **argv)
{
	argp_parse(&argp, argn, argv, 0, NULL, NULL);
	RootsimInit(&conf);
	return RootsimRun();
}
