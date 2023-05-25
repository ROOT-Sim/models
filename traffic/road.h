#pragma once
#include <ROOT-Sim.h>

extern void inject_new_car(lp_id_t me, struct state *state);
extern void process_car_arrival(lp_id_t me, struct state *state, struct car_arrival_event *event);
extern simtime_t compute_traverse_time(struct state *state, struct vehicle *car);
extern void process_car_leave(lp_id_t me, struct state *state, struct car_leave_event *event);
