#pragma once

#include "application.h"

enum role_t {
	role_psycho,
	role_rescuer,
	role_gleader,
	role_gfollower,
	role_on_my_own
};

typedef struct {
	enum role_t role;
	float emotion;
	float emotional_f;
	unsigned age;
	unsigned destination;
	unsigned group_i;
} person_t;

extern void person_env_init(region_t *region, unsigned region_id);

extern void person_refresh_all(simtime_t now, region_t *region, unsigned region_id);
extern void person_on_visit(simtime_t now, region_t *region, unsigned region_id, agent_t agent);
extern void person_on_leave(simtime_t now, region_t *region, unsigned region_id, agent_t agent);

extern void person_set_up(region_t *region, unsigned region_id, unsigned count);
extern void person_explosion(simtime_t now, region_t *region, unsigned region_id, unsigned explosion_cell);
