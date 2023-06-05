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

struct vehicle *populate_car(lp_id_t me, lp_id_t from, struct state *state, struct car_arrival_event *event)
{
	struct vehicle *new_car = rs_malloc(sizeof(*new_car));
	memset(new_car, 0, sizeof(*new_car));
	new_car->from = from;
	new_car->arrival_time = state->lvt;

	if(event->injection)
		new_car->car_id = event->car_id;
	else
		new_car->car_id = get_mark(me, state->car_monotonic_counter++);

	if(state->accident)
		new_car->accident = true;

	return new_car;
}

void update_car_speed(lp_id_t me, struct state *state, struct vehicle *car)
{
	double traffic;
	double speed, real_speed;
	simtime_t traverse_time;

	// Junctions do not have an actual "size"
	if(IS_JUNCTION(me)) {
		car->leave_time = state->lvt + JUNCTION_TRAVERSE_MIN + Expent(&state->seed, JUNCTION_TRAVERSE);
	}

	// Compute the traffic scaling factor
	traffic = (double)state->enqueued_cars / (double)state->total_queue_slots;

	// Compute the speed
	if(car->speed == 0) {
		car->speed = AVERAGE_SPEED;
	}
	speed = car->speed * (1 - traffic);
	do {
		// Compute traverse time according to a Normal distribution
		real_speed = fabs(Gaussian(&state->seed, speed, SPEED_SIGMA));
		car->speed = real_speed;
		traverse_time = (simtime_t)(state->road_len * (1 - car->traveled) / real_speed);
	} while(isinf(traverse_time));

	// Convert the traverse time in seconds
	traverse_time *= 3600.0;

	car->leave_time = state->lvt + traverse_time;
}

bool check_car_leaving(struct state *state, lp_id_t from, lp_id_t me)
{
	double coin;

	// Cars can only leave from junctions with a non-zero leave probability.
	if(IS_ROAD(me) || D_EQUAL(state->leave_prob, 0.0)) {
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
