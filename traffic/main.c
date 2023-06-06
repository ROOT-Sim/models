#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ROOT-Sim.h>
#include <assert.h>

#include "config.h"
#include "model.h"
#include "road.h"
#include "jam.h"

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *payload, unsigned size, void *s)
{
	struct state *state = (struct state *)s;
	if(state != NULL) {
		state->lvt = now;
	}

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(*state));
			if(state == NULL) {
				fprintf(stderr, "ERROR: Unable to allocate simulation state!\n");
				exit(EXIT_FAILURE);
			}
			memset(state, 0, sizeof(*state));

			initialize_stream(me, &state->seed);

			state->leave_mean = rs_malloc(sizeof(*state->leave_mean));
			memset(state->leave_mean, 0, sizeof(*state->leave_mean));

			SetState(state);

			// Configure the LP from the parsed JSON file
			if(IS_JUNCTION(me)) {
				struct node_config c;
				get_node_config(me, &c);
				state->enter_freq = c.enter_freq;
				state->leave_prob = c.leave_prob;
				state->total_queue_slots = CARS_PER_JUNCTION;

				inject_new_car(me, state);
			} else {
				struct edge_config c;
				get_edge_config(me, &c);
				assert(me == c.id);

				state->road_len = c.length;
				state->total_queue_slots = (int)(state->road_len * CARS_PER_UNIT_LENGTH);
			}

			break;

		case LP_FINI:
			rs_free(state->leave_mean);
			rs_free(state);
			break;

		case ARRIVAL:
			process_car_arrival(me, state, (struct car_arrival_event *)payload);
			break;

		case LEAVE:
			process_car_leave(me, state);
			break;

		case JAM:
			handle_notified_jam(me, state, (struct jam_notify_event *)payload);
			break;

		case FINISH_ACCIDENT:
			state->accident = false;
			release_cars(me, state);
			break;

		default:
			printf("Simulation error: unexpected event (me=%lu - event type=%d)\n", me, event_type);
			abort();
	}
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	(void)snapshot;
	return false;
}

struct simulation_configuration conf = {
    .n_threads = 0,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = true,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

int main(int argc, char **argv)
{
	int ret;
	FILE *conf_file;

	// Set the locale to use dots as the decimal separator, as the json config file should adhere to it.
	setlocale(LC_NUMERIC, "C");

	if(argc != 2) {
		printf("Usage: %s topology.json\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	conf_file = fopen(argv[1], "rb");
	if(conf_file == NULL) {
		fprintf(stderr, "Error opening file: %s. ", argv[1]);
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	printf("Parsing configuration file: %s... ", argv[1]);
	fflush(stdout);
	conf.lps = process_configuration_file(conf_file);
	puts("done.");
	conf.stats_file = argv[0];
	conf.termination_time = TOTAL_SIMULATION_TIME;

	RootsimInit(&conf);
	ret = RootsimRun();

	cleanup_config();

	return ret;
}
