#include "sugarscape.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "abm.h"
#include "argparse.h"

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .write_enabled = false, .default_geometry = TOPOLOGY_SQUARE};
struct _abm_settings_t abm_settings = {
    .neighbour_data_size = 2 * sizeof(unsigned), .traverse_handler = _TRAVERSE, .keep_history = false};

static double ComputeMinTour(unsigned a, unsigned b, unsigned *unused)
{
	(void)unused;
	unsigned int regions = RegionsCount();
	if (regions == 0) regions = 16;
	unsigned int side = (unsigned int)sqrt((double)regions);
	if (side == 0) side = 1;
	int x1 = (int)(a % side);
	int y1 = (int)(a / side);
	int x2 = (int)(b % side);
	int y2 = (int)(b / side);
	int dx = x1 - x2;
	int dy = y1 - y2;
	return sqrt((double)(dx * dx + dy * dy));
}

static unsigned init_capacity(unsigned lp_id)
{
	static const unsigned sugar_sources[] = {5, 10};

	unsigned capacity = 0;
	unsigned i = sizeof(sugar_sources) / sizeof(unsigned);
	double distance;
	while(i--) {
		unsigned src = sugar_sources[i] % (RegionsCount() > 0 ? RegionsCount() : 16);
		if(lp_id == src) {
			capacity = 4;
			break;
		}
		distance = ComputeMinTour(lp_id, src, NULL);

		if(distance < SOURCEBASERADIUS) {
			capacity = 4;
			break;
		}
		if(distance < 2 * SOURCEBASERADIUS && capacity < 3)
			capacity = 3;

		if(distance < 3 * SOURCEBASERADIUS && capacity < 2)
			capacity = 2;

		if(distance < 4 * SOURCEBASERADIUS && capacity < 1)
			capacity = 1;
	}
	return capacity;
}

static void sugar_eater_new(unsigned me, simtime_t now)
{
	agent_t agent = SpawnAgent(sizeof(sugar_eater_t));
	sugar_eater_t *sugar_eater = DataAgent(agent, NULL);
	if (sugar_eater) {
		sugar_eater->wealth = (unsigned)RandomRange(MIN_INITIAL_WEALTH, MAX_INITIAL_WEALTH);
		sugar_eater->eat_rate = (unsigned)RandomRange(MIN_EAT_RATE, MAX_EAT_RATE);
		sugar_eater->remaining_steps = (unsigned)RandomRange(MIN_MAX_AGE, MAX_MAX_AGE);
	}
	simtime_t delay = TIME_STEP - 0.1 + Random() / 5.0;
	if (delay < 0.0001) delay = 0.0001;
	ScheduleNewEvent(me, now + delay, SUGAR_VISIT, &agent, sizeof(agent_t));
}

static void sugar_eater_on_visit(agent_t agent, region_t *region, simtime_t now)
{
	sugar_eater_t *sugar_eater = DataAgent(agent, NULL);
	if (!sugar_eater || !region) return;

	// get sugar
	sugar_eater->wealth += region->n.sugar;
	region->n.sugar = 0;
	// get older
	sugar_eater->remaining_steps--;
	// die :(
	if(sugar_eater->wealth == 0 || sugar_eater->wealth < sugar_eater->eat_rate || !sugar_eater->remaining_steps) {
		KillAgent(agent);
		return;
	}
	// increment eaters count the region
	region->n.eaters++;
	// eat
	sugar_eater->wealth -= sugar_eater->eat_rate;
	// prepare to leave
	simtime_t delay = TIME_STEP + Random() / 5.0;
	if (delay < 0.0001) delay = 0.0001;
	ScheduleNewLeaveEvent(now + delay, SUGAR_LEAVE, agent);
}

static void sugar_eater_on_leave(agent_t agent, unsigned me, region_t *region)
{
	if (!region) return;

	unsigned *info_p = NULL;
	unsigned receiver = me;
	unsigned max_sugar = region->n.sugar;

	// get the max sugar available in the neighbourhood
	unsigned dir_count = DirectionsCount();
	for (unsigned d = 0; d < dir_count; d++) {
		if(GetNeighbourInfo(d, &receiver, (void **)&info_p) == 0 && info_p && !info_p[1] &&
		    info_p[0] > max_sugar)
			max_sugar = info_p[0];
	}

	// decrement eaters
	if (region->n.eaters > 0)
		region->n.eaters--;

	// remain here if this is the richest region
	if(region->n.sugar == max_sugar) {
		EnqueueVisit(agent, me, SUGAR_VISIT);
		return;
	}

	// find candidates with max sugar and no eaters
	unsigned int valid_dests[8];
	unsigned int valid_count = 0;
	for (unsigned d = 0; d < dir_count && valid_count < 8; d++) {
		if (GetNeighbourInfo(d, &receiver, (void **)&info_p) == 0 && info_p && !info_p[1] &&
		    info_p[0] == max_sugar) {
			valid_dests[valid_count++] = receiver;
		}
	}

	if (valid_count > 0) {
		unsigned int pick = (unsigned int)RandomRange(0, (int)valid_count - 1);
		EnqueueVisit(agent, valid_dests[pick], SUGAR_VISIT);
	} else {
		EnqueueVisit(agent, me, SUGAR_VISIT);
	}
}

void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *event_content,
    unsigned event_size, void *ptr)
{
	(void)event_size;
	region_t *region = (region_t *)ptr;
	const agent_t *agent_p = (const agent_t *)event_content;

	switch(event_type) {
		case LP_INIT: {
			region = rs_malloc(sizeof(region_t));
			if(region == NULL) {
				printf("Out of memory!\n");
				exit(EXIT_FAILURE);
			}
			SetState(region);

			region->capacity = init_capacity((unsigned)me);
			region->n.sugar = region->capacity;
			region->n.eaters = 0;

			TrackNeighbourInfo(&(region->n));
			if(me == 0) {
				unsigned i = INIT_EATERS;
				unsigned regions = RegionsCount();
				if (regions == 0) regions = 16;
				while(i--) {
					unsigned dest = (unsigned)(Random() * regions);
					ScheduleNewEvent(dest, now + 0.1, SUGAR_INIT, NULL, 0);
				}
			}

			ScheduleNewEvent(me, now + TIME_STEP - 0.25 + Random() / 5.0, SUGAR_REFILL, NULL, 0);
			break;
		}

		case SUGAR_INIT:
			sugar_eater_new((unsigned)me, now);
			break;

		case SUGAR_VISIT:
			if (agent_p)
				sugar_eater_on_visit(*agent_p, region, now);
			break;

		case SUGAR_LEAVE:
			if (agent_p)
				sugar_eater_on_leave(*agent_p, (unsigned)me, region);
			break;

		case SUGAR_REFILL:
			if(region && region->n.sugar < region->capacity)
				region->n.sugar++;

			ScheduleNewEvent(me, now + TIME_STEP - 0.25 + Random() / 5.0, SUGAR_REFILL, NULL, 0);
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
	return (s->n.eaters == 0);
}

struct simulation_configuration conf = {
    .lps = 16,
    .n_threads = 0,
    .termination_time = 100,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "sugarscape",
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

	abm_init_simulation(&conf, TOPOLOGY_SQUARE, side, side, 2 * sizeof(unsigned), ProcessEvent, CanEnd);

	RootsimInit(&conf);
	int ret = RootsimRun();
	printf("ROOT-Sim exited with code: %d\n", ret);
	return ret;
}
