#include "person.h"

#include "application.h"
#include "parameters.h"
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_E
#define M_E 2.7182818284590452354 /* e */
#endif

void person_set_up(region_t *region, unsigned region_id, unsigned count)
{
	if (region->room_id != 3 || count == 0)
		return;

	agent_t agent = SpawnAgent(sizeof(person_t));
	person_t *p = DataAgent(agent, NULL);
	memset(p, 0, sizeof(*p));

	p->role = role_gleader;
	p->age = MIN_AGE + (unsigned)((avg_age - MIN_AGE) * 2.0 * Random());
	p->emotional_f = (float)Random();
	p->group_i = region_id + 1;
	p->destination = FindReceiver();
	count--;
	ScheduleNewLeaveEvent(0.0001 + (Random() * p->age / 5.0), LEAVE, agent);

	double prob = PSYCHO_P + RESCUER_P + group_p;

	while (count--) {
		agent = SpawnAgent(sizeof(person_t));
		p = DataAgent(agent, NULL);
		memset(p, 0, sizeof(*p));
		p->role = role_on_my_own;
		p->age = MIN_AGE + (unsigned)((avg_age - MIN_AGE) * 2.0 * Random());
		p->emotional_f = (float)Random();
		p->destination = FindReceiver();
		if (Random() < prob) {
			double r = Random();
			if (r < PSYCHO_P / prob) {
				p->role = role_psycho;
				p->destination = exits[(unsigned)(Random() * EXITS_CNT)];
				region->psycho_count++;
			} else if (r < (PSYCHO_P + RESCUER_P) / prob) {
				p->destination = region->nearest_exit;
				p->role = role_rescuer;
				p->group_i = 0;
			} else {
				p->role = role_gfollower;
				p->group_i = region_id + 1;
			}
		}

		ScheduleNewLeaveEvent(0.0001 + (Random() * p->age / 5.0), LEAVE, agent);
	}
}

void person_env_init(region_t *region, unsigned region_id)
{
	unsigned y = region_id / EDGE_LEN;
	unsigned x = region_id - y * EDGE_LEN;
	unsigned i = ROOMS_CNT;
	while (i--) {
		if (x >= rooms[i].x_min &&
		    x <= rooms[i].x_max &&
		    y >= rooms[i].y_min &&
		    y <= rooms[i].y_max) {
			break;
		}
	}
	if (i >= ROOMS_CNT) {
		i = 0;
	}
	region->room_id = i;
	region->can_see_exit = false;

	const struct _r_t *r = &rooms[i];

	unsigned reg_min = region_id;
	unsigned hops_min = UINT_MAX;

	i = EXITS_CNT;
	while (i--) {
		const unsigned t_exit = exits[i];
		y = t_exit / EDGE_LEN;
		x = t_exit - y * EDGE_LEN;

		if (x >= r->x_min &&
		    x <= r->x_max &&
		    y >= r->y_min &&
		    y <= r->y_max) {
			region->can_see_exit = true;
		}

		if (region_id == t_exit) {
			reg_min = region_id;
			hops_min = 0;
			continue;
		}
		unsigned hops = abm_compute_min_tour(region_id, t_exit, NULL);
		if (hops < hops_min) {
			hops_min = hops;
			reg_min = t_exit;
		}
	}

	region->nearest_exit = reg_min;
	region->exit_dis = hops_min;
}

void person_refresh_all(simtime_t now, region_t *region, unsigned region_id)
{
	(void)region_id;
	unsigned hops = region->expl_dist;
	double death_p = 0.0;
	if (hops == 0) {
		death_p = 1.0;
	} else if (hops != UINT_MAX) {
		death_p = 10.0 / (double)hops / (double)hops;
	}

	agent_t agent;
	IterAgents(NULL);

	double psy_p = pow(1.0 - psycho_infl_p, region->psycho_count);

	while (IterAgents(&agent)) {
		person_t *p = DataAgent(agent, NULL);
		if (!p)
			continue;

		if (Random() < death_p) {
			if (p->role == role_psycho && region->psycho_count > 0)
				region->psycho_count--;
			KillAgent(agent);
			region->deaths++;
			if (now > region->last_outcome_time)
				region->last_outcome_time = now;
			egress_record_outcome(now);
			continue;
		}

		switch (p->role) {
			case role_psycho:
			case role_rescuer:
				break;

			case role_on_my_own:
			case role_gfollower:
			case role_gleader: {
				double f;
				if (Random() > psy_p) {
					p->destination = FindReceiver();
					f = 1.0;
				} else {
					unsigned int ag_cnt = CountAgents();
					if (region->can_see_exit && hops != UINT_MAX && hops > 0 && ag_cnt > 0) {
						f = p->emotional_f *
						    (double)region->exit_dis / ((double)ag_cnt * (double)hops);
						f = (f > 0.0) ? (pow(1.0 + f, 1.0 / f) / M_E) : 1.0;
					} else {
						f = 1.0;
					}
				}
				double emo = EMO_MVG_AVG * f + (1.0 - EMO_MVG_AVG) * p->emotion;
				p->emotion = (float)emo;
				break;
			}
		}
	}
}

void person_on_visit(simtime_t now, region_t *region, unsigned region_id, agent_t agent)
{
	(void)region_id;
	person_t *p = DataAgent(agent, NULL);
	if (!p)
		return;

	ScheduleNewLeaveEvent(now + 0.0001 + (Random() * p->age / 5.0), LEAVE, agent);

	if (p->role == role_psycho)
		region->psycho_count++;
}

static void check_memory(region_t *region, person_t *p, unsigned region_id)
{
	unsigned r_id = region->room_id;
	bool memory = Random() < MEMORY_P;
	while (memory && p->destination == region_id) {
		p->destination = rooms[r_id].doors[(unsigned)(Random() * DOORS_LEN)];
	}
}

void person_on_leave(simtime_t now, region_t *region, unsigned region_id, agent_t agent)
{
	person_t *p = DataAgent(agent, NULL);
	if (!p)
		return;

	if (Random() < ((double)p->age * (double)p->age / (10.0 * MAX_AGE * MAX_AGE))) {
		ScheduleNewLeaveEvent(now + 0.0001 + (Random() * p->age / 5.0), LEAVE, agent);
		return;
	}

	if (Random() < confusion_p) {
		ScheduleNewLeaveEvent(now + 0.0001 + (Random() * p->age / 5.0), LEAVE, agent);
		return;
	}

	unsigned dest = UINT_MAX;
	if (p->emotion > EMO_THRESH) {
		dest = FindReceiver();
		if (p->role == role_gleader && p->group_i < GROUPS_CNT) {
			region->group_d[p->group_i] = dest;
		}
		if (p->role == role_psycho && region->psycho_count > 0) {
			region->psycho_count--;
		}
	} else {
		switch (p->role) {
			case role_psycho:
				if (Random() < 0.3)
					dest = FindReceiver();
				else
					dest = FindReceiverToward(p->destination);
				if (region->psycho_count > 0)
					region->psycho_count--;
				break;
			case role_gleader:
				if (region->group_d[0] != UINT_MAX && Random() < trust_p) {
					p->destination = region->group_d[0];
				} else {
					if (p->destination == region_id) {
						if (Random() < env_knowledge_p)
							p->destination = exits[(unsigned)(Random() * EXITS_CNT)];
						else
							check_memory(region, p, region_id);
					}
				}
				/* fallthrough */
			case role_rescuer:
				dest = FindReceiverToward(p->destination);
				if (p->group_i < GROUPS_CNT)
					region->group_d[p->group_i] = dest;
				break;
			case role_on_my_own:
				if (region->group_d[0] != UINT_MAX && Random() < trust_p) {
					p->destination = region->group_d[0];
				} else {
					if (p->destination == region_id) {
						if (Random() < env_knowledge_p)
							p->destination = exits[(unsigned)(Random() * EXITS_CNT)];
						else
							check_memory(region, p, region_id);
					}
				}
				dest = FindReceiverToward(p->destination);
				break;
			case role_gfollower:
				if (p->group_i < GROUPS_CNT)
					dest = region->group_d[p->group_i];
				p->role = role_on_my_own;
				break;
		}
	}

	if (dest == UINT_MAX)
		dest = FindReceiver();

	unsigned i = EXITS_CNT;
	if (region->expl_dist != UINT_MAX) {
		while (i--) {
			if (exits[i] == dest) {
				EnqueueVisit(agent, dest, SAVE);
				return;
			}
		}
	}
	EnqueueVisit(agent, dest, VISIT);
}

void person_explosion(simtime_t now, region_t *region, unsigned region_id, unsigned explosion_cell)
{
	unsigned hops = (region_id != explosion_cell) ? abm_compute_min_tour(region_id, explosion_cell, NULL) : 0;
	hops = (hops < region->expl_dist) ? hops : region->expl_dist;
	region->expl_dist = hops;

	person_refresh_all(now, region, region_id);
}

