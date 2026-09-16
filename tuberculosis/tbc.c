#include "tbc.h"
#include "guy.h"
#include "guy_init.h"
#include "abm.h"
#include "argparse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .write_enabled = false, .default_geometry = TOPOLOGY_SQUARE};
struct _abm_settings_t abm_settings = {0, _TRAVERSE, false};

// From Luc Devroye's book "Non-Uniform Random Variate Generation." p. 522
unsigned random_binomial(unsigned trials, double p)
{
	if(p >= 1.0 || !trials)
		return trials;
	unsigned x = 0;
	double sum = 0, log_q = log(1.0 - p);
	while(1) {
		sum += log(Random()) / (trials - x);
		if(sum < log_q || trials == x) {
			return x;
		}
		x++;
	}
	return 0;
}

static void move_healthy_people(unsigned me, region_t *region, simtime_t now)
{
	const unsigned neighbours = DirectionsCount();
	unsigned i = neighbours, to_send;
	rootsim_bitmap explored[bitmap_required_size(neighbours)];
	bitmap_initialize(explored, neighbours);

	unsigned actual_neighbours = 0;
	while(i--) {
		if(GetReceiver(me, i, false) != DIRECTION_INVALID)
			actual_neighbours++;
		else
			bitmap_set(explored, i);
	}

	while(actual_neighbours) {
		i = (unsigned)(Random() * neighbours);
		if(bitmap_check(explored, i))
			continue;
		bitmap_set(explored, i);
		to_send = random_binomial(region->healthy, 1.0 / actual_neighbours);
		ScheduleNewEvent(GetReceiver(me, i, false), now + 0.0001, RECEIVE_HEALTHY, &to_send, sizeof(unsigned));
		region->healthy -= to_send;
		actual_neighbours--;
	}
}

void ProcessEvent(lp_id_t me, simtime_t now, unsigned int event_type, const void *event_content,
    unsigned int event_size, void *ptr)
{
	(void)event_size;
	region_t *state = (region_t *)ptr;

	if(me == 0 && event_type != LP_INIT && state != NULL)
		state->now = now;

	switch(event_type) {
		case LP_INIT: {
			region_t *region = rs_malloc(sizeof(region_t));
			if (!region) abort();
			SetState(region);
			region->healthy = 0;
			region->now = now;

			if(me == 0) {
				guy_init();
			}
			ScheduleNewEvent(me, now + 1.25 + Random() / 2.0, MIDNIGHT, NULL, 0);
			break;
		}

		case MIDNIGHT:
			if (state) {
				move_healthy_people((unsigned)me, state, now);
			}
			ScheduleNewEvent(me, now + 0.25 + Random() / 2.0, MIDNIGHT, NULL, 0);
			break;

		case RECEIVE_HEALTHY:
			if (state && event_content)
				state->healthy += *(const unsigned *)event_content;
			break;

		case INFECTION:
			if (state && event_content)
				guy_on_infection((infection_t *)event_content, state, now);
			break;

		case GUY_VISIT:
			if (state && event_content)
				guy_on_visit(*(const agent_t *)event_content, (unsigned)me, state, now);
			break;

		case GUY_LEAVE:
			if (event_content)
				guy_on_leave(*(const agent_t *)event_content, now);
			break;

		case GUY_INIT:
			if (state && event_content)
				guy_on_init((init_t *)event_content, state);
			break;

		case _TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	if(!snapshot) return false;
	const region_t *s = (const region_t *)snapshot;
	if(me == 0) {
		return s->now > END_TIME;
	}
	return false;
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 10,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "tuberculosis",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
};

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, 16, 10);
	parse_model_cli_options(argc, argv, &opt, &conf);

	unsigned int side = (unsigned int)sqrt((double)conf.lps);
	if (side * side != conf.lps) {
		side = (unsigned int)ceil(sqrt((double)conf.lps));
		conf.lps = side * side;
	}

	abm_init_simulation(&conf, TOPOLOGY_SQUARE, side, side, 0, ProcessEvent, CanEnd);

	RootsimInit(&conf);
	int ret = RootsimRun();
	printf("ROOT-Sim exited with code: %d\n", ret);
	return ret;
}
