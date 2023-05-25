#pragma once

#include <ROOT-Sim.h>
#include "model.h"

struct vehicle {
	struct vehicle *next;
	lp_id_t from;
	simtime_t arrival;
	simtime_t leave;
	bool accident;
	bool stopped;
	double traveled;
	unsigned long long car_id;
	double speed;
};

extern bool check_car_leaving(struct state *state, lp_id_t from, lp_id_t me);
extern struct vehicle *populate_car(lp_id_t me, lp_id_t from, struct state *state);
extern void update_car_state(struct state *state, struct vehicle *car);
