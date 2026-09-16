#include <ROOT-Sim.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "tcar.h"
#include "abm.h"
#include "argparse.h"

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .write_enabled = false, .default_geometry = TOPOLOGY_SQUARE};
struct _abm_settings_t abm_settings = {sizeof(unsigned), _TRAVERSE, false};

static unsigned int n_prc_tot = 16;

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *event_content, unsigned event_size, void *ptr)
{
	(void)event_content;
	(void)event_size;

	lp_state_type *pointer = (lp_state_type *)ptr;
	unsigned i, j;
	unsigned receiver = 0;
	unsigned *trails_p;
	unsigned min_trails;
	simtime_t timestamp = 0;

	switch(event_type) {
		case LP_INIT:

			pointer = (lp_state_type *)rs_malloc(sizeof(lp_state_type));
			if(pointer == NULL) {
				printf("Out of memory!\n");
				exit(EXIT_FAILURE);
			}
			pointer->trails = 0;
			SetState(pointer);

			if(OCCUPIED_CELLS > n_prc_tot) {
				printf("We require more cells to start the simulation!\n");
				exit(EXIT_FAILURE);
			}

			// Occupy the "first" and "last" cells
			if(me < ((OCCUPIED_CELLS + 1) / 2) || me >= ((n_prc_tot) - (OCCUPIED_CELLS / 2))) {
				for(i = 0; i < ROBOTS_PER_CELL; i++) {
					simtime_t delay = (simtime_t)(20 * Random());
					if (delay < 0.0001) delay = 0.0001;
					ScheduleNewEvent(me, now + delay, REGION_IN, NULL, 0);
				}
			}

			TrackNeighbourInfo(&pointer->trails);
			ScheduleNewEvent(me, now + 10, PING, NULL, 0);
			break;

		case REGION_IN:
			if (pointer) {
				pointer->trails++;
			}
			ScheduleNewEvent(me, now + TIME_STEP / 100000, REGION_OUT, NULL, 0);
			break;

		case REGION_OUT:
			// Go to the neighbour who has the smallest trails count
			min_trails = UINT_MAX;
			i = DirectionsCount();
			while(i--) {
				if(GetNeighbourInfo(i, &j, (void **)&trails_p) != -1 && trails_p && min_trails > *trails_p) {
					min_trails = *trails_p;
					receiver = j;
				}
			}

			if (min_trails != UINT_MAX) {
				for (int tries = 0; tries < 20; tries++) {
					unsigned int dir = (unsigned int)(Random() * DirectionsCount());
					if(GetNeighbourInfo(dir, &receiver, (void **)&trails_p) != -1 &&
					   trails_p && min_trails == *trails_p)
						break;
				}
			}

			switch(DISTRIBUTION) {
				case UNIFORM:
					timestamp = now + (simtime_t)(TIME_STEP * Random());
					break;

				case EXPONENTIAL:
					timestamp = now + (simtime_t)(Expent(TIME_STEP));
					break;

				default:
					timestamp = now + (simtime_t)(TIME_STEP * Random());
					break;
			}

			if (timestamp <= now)
				timestamp = now + 0.0001;

			ScheduleNewEvent(receiver, timestamp, REGION_IN, NULL, 0);
			break;

		case PING:
			ScheduleNewEvent(me, now + 10, PING, NULL, 0);
			break;

		case _TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;

	if(snapshot && ((const lp_state_type *)snapshot)->trails >= MINIMUM_VISITS)
		return true;

	return false;
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "tcar",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
};

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, 16, 1000);
	parse_model_cli_options(argc, argv, &opt, &conf);

	unsigned int side = (unsigned int)sqrt((double)conf.lps);
	if (side * side != conf.lps) {
		side = (unsigned int)ceil(sqrt((double)conf.lps));
		conf.lps = side * side;
	}
	n_prc_tot = (unsigned int)conf.lps;

	abm_init_simulation(&conf, TOPOLOGY_SQUARE, side, side, sizeof(unsigned), ProcessEvent, CanEnd);

	RootsimInit(&conf);
	return RootsimRun();
}
