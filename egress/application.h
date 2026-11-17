#pragma once

#include <ROOT-Sim.h>

#include "abm.h"
#include "parameters.h"

typedef struct _region_t {
	unsigned saved;
	unsigned deaths;
	simtime_t last_outcome_time;
	unsigned psycho_count;
	unsigned expl_dist;
	unsigned nearest_exit;
	unsigned room_id;
	unsigned exit_dis;
	bool can_see_exit;
	unsigned group_d[GROUPS_CNT];
	bool exit;
} region_t;

extern void egress_record_outcome(simtime_t now);

enum _event_t {
	VISIT = 1,
	LEAVE,
	SAVE,
	REFRESH,
	EXPLOSION,
	_TRAVERSE
};
