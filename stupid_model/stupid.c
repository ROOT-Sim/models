#include "stupid.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "abm.h"
#include "argparse.h"

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .default_geometry = TOPOLOGY_HEXAGON, .write_enabled = false};
struct _abm_settings_t abm_settings = {
    .neighbour_data_size = sizeof(size_t), .traverse_handler = BUG_TRAVERSE, .keep_history = false};

typedef struct _region_t {
	simtime_t lvt;
	double food_available; // cell's amount of food
	double last_bug_size;  // last bug size
	size_t bugs;
	unsigned is_explored;
	unsigned violation;
} region_t;

typedef struct _bug_t {
	double size;
	bool first; // to render the runs consistent in time we render the first spawned bug immortal
} bug_t;

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *event_content,
    unsigned event_size, void *ptr)
{
	(void)event_size;
	region_t *state = (region_t *)ptr;
	const agent_t *agent_p = (const agent_t *)event_content;

	unsigned i, dest, tries, directions;
	bug_t *this_bug;
	double consumption;
	size_t *bugs_count;
	agent_t this_agent;

	if(state != NULL)
		state->lvt = now;

	switch(event_type) {
		case LP_INIT: {
			region_t *region = rs_malloc(sizeof(region_t));
			if (!region) abort();

			region->is_explored = 0;
			region->last_bug_size = 0;
			region->food_available = RandomRange(0, MAX_FOOD_PRODUCTION_RATE);
			region->bugs = 0;
			region->violation = 0;
			region->lvt = 0;

			SetState(region);
			TrackNeighbourInfo(&region->bugs);

			if(me < NUM_OCCUPIED_CELLS) {
				ProcessEvent(me, 0, SPAWN_BUG, NULL, 0, region);
			}

			ScheduleNewEvent(me, now + TIME_STEP, PRODUCE_FOOD, NULL, 0);
			break;
		}

		case BUG_DELAYED_VISIT:
			if (agent_p)
				ScheduleNewEvent(me, now + 0.0001, BUG_VISIT, agent_p, sizeof(agent_t));
			break;

		case BUG_VISIT: {
			if (!agent_p || !state) break;
			state->is_explored = 1;
			state->bugs++;

			this_bug = DataAgent(*agent_p, NULL);
			if (!this_bug) break;

			if(CountAgents() > BUG_PER_CELL) {
				state->violation++;
				KillAgent(*agent_p);
				break;
			}

			consumption =
			    state->food_available > MAX_CONSUMPTION_RATE ? MAX_CONSUMPTION_RATE : state->food_available;

			this_bug->size += consumption;
			state->food_available -= consumption;
			if(state->food_available < 0)
				state->food_available = 0;

			state->last_bug_size = this_bug->size;

			if(this_bug->size >= REPRODUCTION_SIZE) {
				// reproduce
				for(i = 0; i < CHILD_COUNT; ++i) {
					directions = DirectionsCount();
					tries = directions + 1;
					while(tries--) {
						unsigned int j = (unsigned int)RandomRange(0, (int)directions - 1);
						if(GetNeighbourInfo(j, &dest, (void **)&bugs_count) < 0 || !bugs_count)
							continue;
						if(*bugs_count < BUG_PER_CELL) {
							simtime_t delay = (simtime_t)(TIME_STEP * Random());
							if (delay < 0.0001) delay = 0.0001;
							ScheduleNewEvent(dest, now + delay, SPAWN_BUG, NULL, 0);
							(*bugs_count)++;
							break;
						}
					}
				}
				// the father bug dies...
				KillAgent(*agent_p);
				state->bugs--;

			} else {
				simtime_t delay = (simtime_t)(TIME_STEP * Random());
				if (delay < 0.0001) delay = 0.0001;
				ScheduleNewLeaveEvent(now + delay, BUG_LEAVING, *agent_p);
			}
			break;
		}

		case PRODUCE_FOOD:
			if (state) {
				state->food_available += RandomRange(0, MAX_FOOD_PRODUCTION_RATE);
			}
			ScheduleNewEvent(me, now + TIME_STEP, PRODUCE_FOOD, NULL, 0);
			break;

		case SPAWN_BUG:
			if(CountAgents() < BUG_PER_CELL) {
				this_agent = SpawnAgent(sizeof(bug_t));
				this_bug = DataAgent(this_agent, NULL);
				if (this_bug) {
					this_bug->size = 1;
					this_bug->first = now <= 0.0;
				}
				ProcessEvent(me, now, BUG_VISIT, &this_agent, sizeof(this_agent), state);
			}
			break;

		case BUG_LEAVING: {
			if (!agent_p || !state) break;
			state->bugs--;

			if(RandomRange(0, 100) >= SURVIVAL_PROBABILITY) {
				this_bug = DataAgent(*agent_p, NULL);
				if(this_bug && !this_bug->first) {
					KillAgent(*agent_p);
					break;
				}
			}

			directions = DirectionsCount();
			unsigned int valid_dests[8];
			unsigned int valid_count = 0;
			for(i = 0; i < directions && valid_count < 8; ++i) {
				if(GetNeighbourInfo(i, &dest, (void **)&bugs_count) < 0 || !bugs_count)
					continue;

				if(*bugs_count < BUG_PER_CELL) {
					valid_dests[valid_count++] = dest;
				}
			}

			if(valid_count > 0) {
				unsigned int pick = (unsigned int)RandomRange(0, (int)valid_count - 1);
				EnqueueVisit(*agent_p, valid_dests[pick], BUG_DELAYED_VISIT);
			} else {
				KillAgent(*agent_p);
			}
			break;
		}

		case BUG_TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	if (!snapshot) return false;
	return ((const region_t *)snapshot)->is_explored != 0;
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 100,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "stupid",
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

	abm_init_simulation(&conf, TOPOLOGY_HEXAGON, side, side, sizeof(size_t), ProcessEvent, CanEnd);

	RootsimInit(&conf);
	int ret = RootsimRun();
	printf("ROOT-Sim exited with code: %d\n", ret);
	return ret;
}
