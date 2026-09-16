#include "robot_explore.h"
#include "topology_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "abm.h"
#include "argparse.h"

#define OBSTACLE_PROB 0.01

unsigned int n_prc_tot = 16;

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .write_enabled = false, .default_geometry = TOPOLOGY_HEXAGON};
struct _abm_settings_t abm_settings = {
    .neighbour_data_size = sizeof(unsigned), .traverse_handler = _TRAVERSE, .keep_history = false};

#define KEEP_ALIVE_INTERVAL (now + (simtime_t)Expent(50.0) + 0.001)

static void new_agent(unsigned me)
{
	agent_t agent = SpawnAgent(sizeof(agent_state_type) + n_prc_tot * sizeof(map_t));
	agent_state_type *agent_state = DataAgent(agent, NULL);
	if (!agent_state) return;
	memset(agent_state, 0, sizeof(agent_state_type) + n_prc_tot * sizeof(map_t));

	agent_state->current_cell = UINT_MAX;
	agent_state->target_cell = UINT_MAX;

	EnqueueVisit(agent, me, REGION_IN);
	simtime_t delay = 10.0 * Random() + 1.0;
	ScheduleNewLeaveEvent(delay, REGION_OUT, agent);
}

void ProcessEvent(lp_id_t me, simtime_t now, unsigned int event, const void *payload, unsigned int size, void *st)
{
	(void)size;
	cell_state_t *state = (cell_state_t *)st;
	agent_t robot;
	const agent_t *agent_p = (const agent_t *)payload;
	agent_state_type *agent_state, *robot_state;
	unsigned int i, j;
	simtime_t timestamp;

	if(event != LP_INIT && state != NULL)
		state->started = true;

	switch(event) {
		case LP_INIT: {
			state = rs_malloc(sizeof(cell_state_t));
			if(state == NULL) {
				fprintf(stderr, "Error allocating cell %llu state!\n", (unsigned long long)me);
				abort();
			}
			SetState(state);
			memset(state, 0, sizeof(cell_state_t));

			state->has_obstacles = false;
			for(i = 0; i < 6; i++) {
				if(GetReceiver((unsigned)me, i, false) != DIRECTION_INVALID) {
					if(!state->has_obstacles && Random() < OBSTACLE_PROB) {
						state->has_obstacles = true;
						state->neighbours[i] = (unsigned int)-1;
					} else {
						state->neighbours[i] = GetReceiver((unsigned)me, i, false);
					}
				} else {
					state->neighbours[i] = (unsigned int)-1;
				}
			}

			if (me == 0) {
				for(i = 0; i < ROBOTS; ++i) {
					unsigned int target = (unsigned int)(n_prc_tot * Random());
					if (target >= n_prc_tot) target = n_prc_tot - 1;
					ScheduleNewEvent(target, now + 0.001, NEW_ROBOT, NULL, 0);
				}
			}

			ScheduleNewEvent(me, KEEP_ALIVE_INTERVAL, KEEP_ALIVE, NULL, 0);
			break;
		}

		case NEW_ROBOT:
			new_agent((unsigned)me);
			break;

		case KEEP_ALIVE:
			ScheduleNewEvent(me, KEEP_ALIVE_INTERVAL, KEEP_ALIVE, NULL, 0);
			break;

		case REGION_IN: {
			if (!agent_p || !state) break;
			agent_state = DataAgent(*agent_p, NULL);
			if (!agent_state) break;

			agent_state->current_cell = (unsigned)me;
			state->present_agents++;

			if(agent_state->current_cell < n_prc_tot && !agent_state->visit_map[agent_state->current_cell].visited) {
				agent_state->visit_map[agent_state->current_cell].visited = true;
				agent_state->visited_cells++;
				memcpy(&agent_state->visit_map[agent_state->current_cell].neighbours, state->neighbours, sizeof(unsigned int) * 6);
			}

			if(agent_state->current_cell == agent_state->target_cell) {
				agent_state->target_cell = UINT_MAX;
			}

			if(CountAgents() > 1) {
				IterAgents(NULL);
				while(IterAgents(&robot)) {
					if (robot == *agent_p) continue;
					robot_state = DataAgent(robot, NULL);
					if (!robot_state) continue;

					for(j = 0; j < n_prc_tot; j++) {
						if(robot_state->visit_map[j].visited && !agent_state->visit_map[j].visited) {
							memcpy(&agent_state->visit_map[j], &robot_state->visit_map[j], sizeof(map_t));
							agent_state->visited_cells++;
						} else if (agent_state->visit_map[j].visited && !robot_state->visit_map[j].visited) {
							memcpy(&robot_state->visit_map[j], &agent_state->visit_map[j], sizeof(map_t));
							robot_state->visited_cells++;
						}
					}

					agent_state->met_robots++;
					robot_state->met_robots++;

					robot_state->target_cell = closest_frontier(agent_state, agent_state->current_cell, (unsigned int)-1);
					agent_state->target_cell = closest_frontier(agent_state, agent_state->current_cell, robot_state->current_cell);
					break;
				}
			} else {
				agent_state->target_cell = closest_frontier(agent_state, agent_state->current_cell, (unsigned int)-1);
			}

			if(Random() < 0.01) {
				agent_state->target_cell = UINT_MAX;
			}

			if(agent_state->target_cell == UINT_MAX || agent_state->target_cell >= n_prc_tot) {
				agent_state->target_cell = closest_frontier(agent_state, agent_state->current_cell, (unsigned int)-1);
				if(agent_state->target_cell == UINT_MAX || agent_state->target_cell >= n_prc_tot) {
					agent_state->target_cell = (unsigned int)RandomRange(0, (int)n_prc_tot - 1);
				}
			}

			agent_state->direction = compute_my_direction(agent_state);

			if(agent_state->direction >= 6 || GetReceiver(agent_state->current_cell, agent_state->direction, false) == DIRECTION_INVALID) {
				unsigned int valid_dirs[6];
				unsigned int valid_cnt = 0;
				for (unsigned int d = 0; d < 6; d++) {
					if (GetReceiver(agent_state->current_cell, d, false) != DIRECTION_INVALID) {
						valid_dirs[valid_cnt++] = d;
					}
				}
				if (valid_cnt > 0) {
					agent_state->direction = valid_dirs[RandomRange(0, (int)valid_cnt - 1)];
				} else {
					agent_state->direction = 0;
				}
			}

			unsigned int next_cell = GetReceiver(agent_state->current_cell, agent_state->direction, false);
			if (next_cell != DIRECTION_INVALID) {
				EnqueueVisit(*agent_p, next_cell, REGION_IN);
			} else {
				EnqueueVisit(*agent_p, (unsigned)me, REGION_IN);
			}

			timestamp = now + Expent(TIME_STEP) + 0.0001;
			state->max_ratio = (double)agent_state->visited_cells / (double)n_prc_tot;
			ScheduleNewLeaveEvent(timestamp, REGION_OUT, *agent_p);
			break;
		}

		case REGION_OUT:
			if (state && state->present_agents > 0)
				state->present_agents--;
			break;

		case _TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	if(!snapshot) return false;
	const cell_state_t *state = (const cell_state_t *)snapshot;
	return state->max_ratio >= 1.0;
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 100,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "robot_explore",
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
	n_prc_tot = (unsigned int)conf.lps;

	abm_init_simulation(&conf, TOPOLOGY_HEXAGON, side, side, sizeof(unsigned), ProcessEvent, CanEnd);

	RootsimInit(&conf);
	int ret = RootsimRun();
	printf("ROOT-Sim exited with code: %d\n", ret);
	return ret;
}
