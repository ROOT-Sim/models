#include "segregation.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "abm.h"
#include "argparse.h"

struct n_data {
	bool has_engineer;
	unsigned agents;
};

typedef struct _region_t {
	struct n_data n;
	unsigned violation;
	bool happy, started;
} region_t;

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .default_geometry = TOPOLOGY_HEXAGON, .write_enabled = false};
struct _abm_settings_t abm_settings = {
    .neighbour_data_size = sizeof(struct n_data), .traverse_handler = _TRAVERSE, .keep_history = false};

typedef struct _guy_t {
	bool engineer;
} guy_t;

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *event_content,
    unsigned event_size, void *ptr)
{
	(void)event_size;
	region_t *state = (region_t *)ptr;
	const agent_t *agent_p = (const agent_t *)event_content;

	unsigned i, directions, dest;
	float unlike;
	agent_t this_agent;
	guy_t *guy;
	struct n_data *neighbour_data;
	bool can_exit;

	if(event_type != LP_INIT && state != NULL)
		state->started = true;

	switch(event_type) {
		case LP_INIT: {
			region_t *region = rs_malloc(sizeof(region_t));
			if (!region) abort();

			region->n.agents = 0;
			region->n.has_engineer = false;
			region->violation = 0;
			region->happy = false;
			region->started = false;

			SetState(region);
			TrackNeighbourInfo(&region->n);

			if(Random() < AGENT_SPAWN_PROBABILITY) {
				region->n.agents++;
				this_agent = SpawnAgent(sizeof(guy_t));
				guy = DataAgent(this_agent, NULL);
				guy->engineer = Random() < AGENT_IS_ENGINEER_PROBABILITY;
				ScheduleNewLeaveEvent(now + Random() * TIME_STEP + 0.0001, GUY_LEAVE, this_agent);
			}

			ScheduleNewEvent(me, now + 10 * Random() * TIME_STEP + 0.001, KEEP_ALIVE, NULL, 0);
			break;
		}

		case KEEP_ALIVE:
			ScheduleNewEvent(me, now + 10 * Random() * TIME_STEP + 0.001, KEEP_ALIVE, NULL, 0);
			break;

		case GUY_DELAYED_VISIT:
			ScheduleNewEvent(me, now + 0.0001, GUY_VISIT, agent_p, sizeof(agent_t));
			break;

		case GUY_VISIT:
			if (!agent_p || !state) break;
			guy = DataAgent(*agent_p, NULL);
			state->n.agents++;
			if (guy) {
				state->n.has_engineer = guy->engineer;
			}

			if(CountAgents() > 1) {
				state->violation++;
				KillAgent(*agent_p);
				break;
			}

			ScheduleNewLeaveEvent(now + Random() * TIME_STEP + 0.0001, GUY_LEAVE, *agent_p);
			break;

		case GUY_LEAVE:
			if (!agent_p || !state) break;
			guy = DataAgent(*agent_p, NULL);
			unlike = 0.0;
			directions = DirectionsCount();
			can_exit = false;
			for(i = 0; i < directions; ++i) {
				if(GetNeighbourInfo(i, &dest, (void **)&neighbour_data) < 0 || !neighbour_data)
					continue;

				if(neighbour_data->agents && guy && guy->engineer != neighbour_data->has_engineer)
					unlike += 1.0f;

				if(neighbour_data->agents < 1) {
					can_exit = true;
				}
			}

			state->happy = (unlike / (float)directions) < AGENT_THRESHOLD;

			if(!state->happy && can_exit) {
				unsigned int valid_dests[8];
				unsigned int valid_count = 0;
				for (unsigned int d = 0; d < directions && valid_count < 8; ++d) {
					if(GetNeighbourInfo(d, &dest, (void **)&neighbour_data) == 0 &&
					   neighbour_data && neighbour_data->agents < 1) {
						valid_dests[valid_count++] = dest;
					}
				}
				if (valid_count > 0) {
					unsigned int pick = (unsigned int)RandomRange(0, (int)valid_count - 1);
					state->n.agents--;
					EnqueueVisit(*agent_p, valid_dests[pick], GUY_DELAYED_VISIT);
				} else {
					ScheduleNewLeaveEvent(now + Random() * TIME_STEP + 0.0001, GUY_LEAVE, *agent_p);
				}
			} else {
				ScheduleNewLeaveEvent(now + Random() * TIME_STEP + 0.0001, GUY_LEAVE, *agent_p);
			}
			break;

		case _TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	if (!snapshot) return false;
	const region_t *s = (const region_t *)snapshot;
	return s->started && (s->happy || s->n.agents == 0);
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 100,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "segregation",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
};

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, 16, 100);
	parse_model_cli_options(argc, argv, &opt, &conf);

	unsigned int side = (unsigned int)sqrt((double)conf.lps);
	if (side * side != conf.lps) {
		side = (unsigned int)ceil(sqrt((double)conf.lps));
		conf.lps = side * side;
	}

	abm_init_simulation(&conf, TOPOLOGY_HEXAGON, side, side, sizeof(struct n_data), ProcessEvent, CanEnd);

	RootsimInit(&conf);
	int ret = RootsimRun();
	printf("ROOT-Sim exited with code: %d\n", ret);
	return ret;
}
