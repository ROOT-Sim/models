#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <math.h>
#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>

#include "argparse.h"
#include "pcs.h"

bool pcs_statistics = false;
bool fading_check = false;
bool variable_ta = false;
unsigned complete_calls = COMPLETE_CALLS;
unsigned channels_per_cell = CHANNELS_PER_CELL;
double ref_ta = TA;
double ta_duration = TA_DURATION;
double ta_change = TA_CHANGE;

static struct topology *pcs_topology = NULL;

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *ptr);
static bool CanEnd(lp_id_t me, const void *snapshot);

static struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "pcs",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *ptr)
{
	(void)size;
	unsigned int w;
	event_content_type new_event_content = {0};
	const event_content_type *event_content = (const event_content_type *)content;

	new_event_content.cell = -1;
	new_event_content.channel = -1;
	new_event_content.call_term_time = -1;

	simtime_t handoff_time;
	simtime_t timestamp = 0;

	lp_state_type *state = (lp_state_type *)ptr;

	if(state != NULL) {
		state->lvt = now;
		state->executed_events++;
	}

	switch(event_type) {
		case LP_INIT:
			state = (lp_state_type *)rs_malloc(sizeof(lp_state_type));
			if(state == NULL) {
				printf("Out of memory!\n");
				abort();
			}

			initialize_stream((unsigned int)me, &state->seed);
			SetState(state);

			memset(state, 0, sizeof(lp_state_type));
			initialize_stream((unsigned int)me, &state->seed);

			state->channel_counter = channels_per_cell;
			state->ta = ref_ta;
			state->me = (unsigned int)me;

			state->channel_state = rs_malloc(sizeof(unsigned int) * 2 * (CHANNELS_PER_CELL / BITS + 1));
			if(state->channel_state == NULL) {
				abort();
			}
			for(w = 0; w < state->channel_counter / (sizeof(int) * 8) + 1; w++)
				state->channel_state[w] = 0;

			timestamp = (simtime_t)(20 * Random(&state->seed));
			ScheduleNewEvent(me, timestamp, START_CALL, NULL, 0);

			timestamp = (simtime_t)(FADING_RECHECK_FREQUENCY * Random(&state->seed));
			ScheduleNewEvent(me, timestamp, FADING_RECHECK, NULL, 0);
			break;

		case LP_FINI:
			break;

		case START_CALL:
			state->arriving_calls++;

			if(state->channel_counter == 0) {
				state->blocked_on_setup++;
			} else {
				state->channel_counter--;

				new_event_content.channel = allocation(state, channels_per_cell);
				new_event_content.from = (unsigned int)me;
				new_event_content.sent_at = now;

				switch(DURATION_DISTRIBUTION) {
					case UNIFORM:
						new_event_content.call_term_time =
						    now + 0.0001 + (simtime_t)(ta_duration * Random(&state->seed));
						break;
					case EXPONENTIAL:
						new_event_content.call_term_time =
						    now + 0.0001 + (simtime_t)(Expent(&state->seed, ta_duration));
						break;
					default:
						new_event_content.call_term_time = now + 0.0001 + (simtime_t)(5 * Random(&state->seed));
				}

				switch(CELL_CHANGE_DISTRIBUTION) {
					case UNIFORM:
						handoff_time = now + 0.0001 + (simtime_t)((ta_change)*Random(&state->seed));
						break;
					case EXPONENTIAL:
						handoff_time = now + 0.0001 + (simtime_t)(Expent(&state->seed, ta_change));
						break;
					default:
						handoff_time = now + 0.0001 + (simtime_t)(5 * Random(&state->seed));
				}

				if(new_event_content.call_term_time < handoff_time) {
					ScheduleNewEvent(me, new_event_content.call_term_time, END_CALL,
					    &new_event_content, sizeof(new_event_content));
				} else {
					lp_id_t r = GetReceiver(pcs_topology, me, DIRECTION_RANDOM);
					new_event_content.cell = (r != INVALID_DIRECTION) ? (int)r : (int)me;
					ScheduleNewEvent(me, handoff_time, HANDOFF_LEAVE, &new_event_content,
					    sizeof(new_event_content));
				}
			}

			if(variable_ta)
				state->ta = recompute_ta(ref_ta, now);

			switch(DISTRIBUTION) {
				case UNIFORM:
					timestamp = now + 0.0001 + (simtime_t)(state->ta * Random(&state->seed));
					break;
				case EXPONENTIAL:
					timestamp = now + 0.0001 + (simtime_t)(Expent(&state->seed, state->ta));
					break;
				default:
					timestamp = now + 0.0001 + (simtime_t)(5 * Random(&state->seed));
			}

			ScheduleNewEvent(me, timestamp, START_CALL, NULL, 0);
			break;

		case END_CALL:
			state->channel_counter++;
			state->complete_calls++;
			if(event_content != NULL) {
				deallocation((unsigned int)me, state, event_content->channel, now);
			}
			break;

		case HANDOFF_LEAVE:
			state->channel_counter++;
			state->leaving_handoffs++;
			if(event_content != NULL) {
				deallocation((unsigned int)me, state, event_content->channel, now);
				new_event_content.call_term_time = event_content->call_term_time;
				new_event_content.from = (unsigned int)me;
				new_event_content.dummy = &(state->dummy);
				ScheduleNewEvent((lp_id_t)event_content->cell, now + 0.0001, HANDOFF_RECV, &new_event_content,
				    sizeof(new_event_content));
			}
			break;

		case HANDOFF_RECV:
			state->arriving_handoffs++;
			state->arriving_calls++;

			if(state->channel_counter == 0) {
				state->blocked_on_handoff++;
			} else {
				state->channel_counter--;

				new_event_content.channel = allocation(state, channels_per_cell);
				if(event_content != NULL) {
					new_event_content.call_term_time = event_content->call_term_time;
				}

				switch(CELL_CHANGE_DISTRIBUTION) {
					case UNIFORM:
						handoff_time = now + 0.0001 + (simtime_t)((ta_change)*Random(&state->seed));
						break;
					case EXPONENTIAL:
						handoff_time = now + 0.0001 + (simtime_t)(Expent(&state->seed, ta_change));
						break;
					default:
						handoff_time = now + 0.0001 + (simtime_t)(5 * Random(&state->seed));
				}

				if(new_event_content.call_term_time < handoff_time) {
					ScheduleNewEvent(me, new_event_content.call_term_time, END_CALL,
					    &new_event_content, sizeof(new_event_content));
				} else {
					lp_id_t r = GetReceiver(pcs_topology, me, DIRECTION_RANDOM);
					new_event_content.cell = (r != INVALID_DIRECTION) ? (int)r : (int)me;
					ScheduleNewEvent(me, handoff_time, HANDOFF_LEAVE, &new_event_content,
					    sizeof(new_event_content));
				}
			}
			break;

		case FADING_RECHECK:
			fading_recheck(state);
			timestamp = now + (simtime_t)(FADING_RECHECK_FREQUENCY);
			ScheduleNewEvent(me, timestamp, FADING_RECHECK, NULL, 0);
			break;

		default:
			fprintf(stdout, "PCS: Unknown event type! (me = %llu - event type = %u)\n", (unsigned long long)me, event_type);
			abort();
	}
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	const lp_state_type *s = (const lp_state_type *)snapshot;
	if (s && s->complete_calls >= complete_calls)
		return true;
	return false;
}

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, 16, 500);
	parse_model_cli_options(argc, argv, &opt, &conf);

	unsigned int width = (unsigned int)ceil(sqrt((double)conf.lps));
	if (width == 0) width = 1;
	unsigned int height = (unsigned int)ceil((double)conf.lps / width);
	if (height == 0) height = 1;

	pcs_topology = InitializeTopology(TOPOLOGY_HEXAGON, width, height);

	RootsimInit(&conf);
	int ret = RootsimRun();

	if (pcs_topology) {
		ReleaseTopology(pcs_topology);
		pcs_topology = NULL;
	}

	return ret;
}
