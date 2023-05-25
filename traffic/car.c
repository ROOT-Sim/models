#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <string.h>

#include "car.h"
#include "config.h"
#include "model.h"
#include "road.h"
#include "statistics.h"

static unsigned long long get_mark(unsigned long long k1, unsigned long long k2)
{
	return (((k1 + k2) * (k1 + k2 + 1) / 2) + k2);
}

struct vehicle *populate_car(lp_id_t me, lp_id_t from, struct state *state)
{
	struct vehicle *new_car = rs_malloc(sizeof(*new_car));
	memset(new_car, 0, sizeof(*new_car));
	new_car->from = from;
	new_car->arrival = state->lvt;
	new_car->leave = state->lvt + compute_traverse_time(state, new_car);
	new_car->car_id = get_mark(me, state->car_monotonic_counter++);
	if(state->accident)
		new_car->accident = true;
	return new_car;
}

void update_car_state(struct state *state, struct vehicle *car)
{
	car->traveled = (state->lvt - car->arrival) / (car->leave - car->arrival);
	car->speed = fabs(Gaussian(&state->seed, state->road_len / (car->leave - car->arrival), SPEED_SIGMA));
	car->leave = state->lvt + compute_traverse_time(state, car);
}

bool check_car_leaving(struct state *state, lp_id_t from, lp_id_t me)
{
	double coin;

	// Cars can only leave from junctions with a non-zero leave probability.
	if(IS_EDGE(me) || D_EQUAL(state->leave_prob, 0.0)) {
		return false;
	}

	// If this is an end node, and the car did not join the highway from here...
	if(count_neighbours(me) == 1 && from != me) {
		// ...then the car has left.
		return true;
	}

	// If the car arrived here (and did not join from here) then check if it is leaving.
	if(from != me) {
		coin = Random(&state->seed);
		if(coin <= state->leave_prob) {
			// The car has left.
			return true;
		}
	}

	return false;
}
