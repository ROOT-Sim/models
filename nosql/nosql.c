#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>

#include "argparse.h"
#include "nosql.h"

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *payload, unsigned size, void *st);
static bool CanEnd(lp_id_t me, const void *snapshot);

static struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "nosql",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *payload, unsigned size, void *st)
{
	(void)size;
	int i;
	event_content_type new_event_content;
	const event_content_type *event_content = (const event_content_type *)payload;
	simtime_t timestamp;

	memset(&new_event_content, 0, sizeof(new_event_content));

	lp_state_type *state = (lp_state_type *)st;

	if(state != NULL) {
		state->lvt = now;
	}

	switch(event_type) {
		case LP_INIT:
			state = rs_malloc(sizeof(lp_state_type));
			if(state == NULL) {
				printf("Out of memory!\n");
				abort();
			}

			initialize_stream((unsigned int)me, &state->seed);
			SetState(state);
			memset(state, 0, sizeof(lp_state_type));
			initialize_stream((unsigned int)me, &state->seed);

			timestamp = (simtime_t)(20 * Random(&state->seed)) + 0.0001;
			ScheduleNewEvent(me, timestamp, START_TX, NULL, 0);
			break;

		case LP_FINI:
			break;

		case START_TX:
			state->residual_tx_ops = (unsigned int)RandomRange(&state->seed, MIN_OP_COUNT, MAX_OP_COUNT);
			state->read_set_size = RandomRange(&state->seed, MAX_RS_SIZE / 2, MAX_RS_SIZE);
			state->tx_ops_displacement = 0;

			timestamp = now + 0.0001 + (simtime_t)Expent(&state->seed, TX_OP_ARRIVAL);
			ScheduleNewEvent(me, timestamp, TX_OP, NULL, 0);
			break;

		case TX_OP:
			state->residual_tx_ops--;
			timestamp = now + 0.0001 + (simtime_t)Expent(&state->seed, 10.0);

			if(state->residual_tx_ops > 0) {
				ScheduleNewEvent(me, timestamp, TX_OP, NULL, 0);
				if(state->tx_ops_displacement < MAX_RS_SIZE) {
					state->read_set[state->tx_ops_displacement] = RandomRange(&state->seed, 10, 10000);
					state->tx_ops_displacement++;
				}
			} else {
				lp_id_t recv = (lp_id_t)(Random(&state->seed) * conf.lps);
				lp_id_t recv2 = (lp_id_t)(Random(&state->seed) * conf.lps);
				while(recv2 == recv && conf.lps > 1) {
					recv2 = (lp_id_t)(Random(&state->seed) * conf.lps);
				}

				new_event_content.from = me;
				new_event_content.size = state->read_set_size;
				memcpy(new_event_content.read_set, state->read_set, sizeof(int) * state->read_set_size);

				new_event_content.second = true;
				ScheduleNewEvent(recv2, timestamp, PREPARE, &new_event_content, sizeof(new_event_content));

				ScheduleNewEvent(me, timestamp + 0.0001 + (simtime_t)Expent(&state->seed, 10.0), START_TX, NULL, 0);
				state->committed_tx++;
			}
			break;

		case PREPARE:
			if(event_content != NULL) {
				for(i = 0; i < event_content->size && i < MAX_WS_SIZE; i++) {
					state->write_set[i] = event_content->read_set[i];
				}
				new_event_content.from = event_content->from;
				new_event_content.size = event_content->size;
				new_event_content.second = event_content->second;
				memcpy(new_event_content.read_set, event_content->read_set, sizeof(int) * event_content->size);

				timestamp = now + 0.0001 + (simtime_t)Expent(&state->seed, 50);
				ScheduleNewEvent(event_content->from, timestamp, COMMIT, &new_event_content, sizeof(new_event_content));
			}
			break;

		case COMMIT:
			state->committed_tx++;
			if(event_content && event_content->second) {
				timestamp = now + 0.0001 + (simtime_t)Expent(&state->seed, 10.0);
				ScheduleNewEvent(me, timestamp, START_TX, NULL, 0);
			}
			break;

		default:
			fprintf(stderr, "NoSQL: Unknown event type %u on LP %llu\n", event_type, (unsigned long long)me);
			abort();
	}
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	const lp_state_type *state = (const lp_state_type *)snapshot;
	if(state && state->committed_tx >= TOTAL_COMMITTED_TX)
		return true;
	return false;
}

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, 16, 5000);
	parse_model_cli_options(argc, argv, &opt, &conf);

	RootsimInit(&conf);
	return RootsimRun();
}
