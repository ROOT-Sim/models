#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <string.h>

#include "car.h"
#include "config.h"
#include "model.h"
#include "road.h"
#include "statistics.h"

// With a certain probability, cause an accident encompassing a random amount of travelling cars.
static void cause_accident(struct state *state, lp_id_t me)
{
	double min;
	double max;
	double mean;
	double var;
	double prob;
	double coin;
	int involved_car;
	int i;
	struct vehicle *curr_car;
	simtime_t duration;

	// if there is already an accident, don't cause another one. We don't model accidents in junctions.
	if(state->accident || IS_NODE(me)) {
		return;
	}

	// An accident happens depending on the amount of cars.
	// Accident probability is normal wrt the number of cars.
	// Derive a discrete probability from a normal distribution.
	if(state->queued_elements == 0) {
		min = 0.0;
		max = 0.5;
	} else if(state->queued_elements == state->total_queue_slots) {
		min = (double)state->total_queue_slots - 0.5;
		max = (double)state->total_queue_slots;
	} else {
		min = (double)state->queued_elements - 0.5;
		max = (double)state->queued_elements + 0.5;
	}

	// TODO come calcolare qui la varianza?!
	// When there are many cars but not that much, accidents are more likely to occur
	mean = (double)state->total_queue_slots / 3.0;
	var = RandomRange(&state->seed, 0, 100);


	prob = contour_cdf(min, max, mean, var); // <- TORNA SEMPRE 0

	prob *= (double)ACCIDENT_PROBABILITY;

	// Toss a coin to check whether an accident occured or not
	coin = Random(&state->seed);

	// If there is an accident, set the parameters accordingly and determine how long the accident will last
	if(coin <= prob) {
		state->accident = true;

		// Compute when the road will be freed, according to the node accident duration.
		do {
			duration = Gaussian(&state->seed, ACCIDENT_DURATION, ACCIDENT_SIGMA);
		} while(duration <= 0);

		ScheduleNewEvent(me, state->lvt + duration, FINISH_ACCIDENT, NULL, 0);

		// Select cars involved in the accident
		involved_car = RandomRange(&state->seed, 0, (int)(state->queued_elements - 1));
		curr_car = state->queue;
		i = 0;
		while(i < involved_car) {
			curr_car = curr_car->next;
			i++;
		}

		while(curr_car != NULL) {
			curr_car->accident = true;
			curr_car = curr_car->next;
		}
	}
}

simtime_t compute_traverse_time(struct state *state, struct vehicle *car)
{
	double traffic;
	double speed, real_speed;
	simtime_t traverse_time;

	// Compute the traffic scaling factor
	traffic = (double)state->queued_elements / (double)state->total_queue_slots;

	// Compute the speed
	if(car->speed == 0) {
		car->speed = AVERAGE_SPEED;
	}
	speed = car->speed * (1 - traffic);
	do {
		// Compute traverse time according to a Normal distribution
		real_speed = fabs(Gaussian(&state->seed, speed, SPEED_SIGMA));
		car->speed = real_speed;
		traverse_time = (simtime_t)(car->traveled / real_speed);
	} while(isinf(traverse_time));

	// Convert the traverse time in seconds
	traverse_time *= 3600.0;

	return traverse_time;
}


static struct vehicle *reorder_queue(struct vehicle *head, struct state *state)
{
	simtime_t now = state->lvt;
	struct vehicle *curr;
	struct vehicle *prev;

	// Update speed and leave time
	curr = head;
	while(curr != NULL) {
		update_car_state(state, curr);
		curr = curr->next;
	}

	for(curr = head; (curr != NULL && curr->next != NULL); curr = curr->next) {
		curr->speed =
		    fabs(Gaussian(&state->seed, state->road_len / (curr->leave - curr->arrival), SPEED_SIGMA));
	}

	// Sort the queue by leave time
	bool didSwap = false;
	for(didSwap = true; didSwap;) {
		didSwap = false;
		prev = head;
		for(curr = head; (curr != NULL && curr->next != NULL); curr = curr->next) {
			if(curr->leave > curr->next->leave) {
				if(head == curr) {
					head = curr->next;
					curr->next = head->next;
					head->next = curr;
				} else {
					prev->next = curr->next;
					curr->next = prev->next->next;
					prev->next->next = curr;
				}
				didSwap = true;
			}

			prev = curr;
		}
	}

	// Update the portion of traveled space
	curr = head;
	while(curr != NULL) {
		curr->traveled = (now - curr->arrival) / (curr->leave - curr->arrival);
		curr = curr->next;
	}

	return head;
}


static struct vehicle *car_enqueue(lp_id_t me, lp_id_t from, struct state *state)
{
	struct vehicle *new_car;

	// Create the car node
	new_car = populate_car(me, from, state);

	new_car->next = state->queue;
	state->queue = new_car;
	state->queued_elements++;
	state->queue = reorder_queue(state->queue, state);

	return new_car;
}

// Find car with ID *mark and pop it. Update speeds of preceding cars
static struct vehicle *car_dequeue(unsigned int me, struct state *state, struct car_leave_event *event)
{
	struct vehicle *curr_car;
	struct vehicle *ret_car;

	curr_car = state->queue;

	if(curr_car == NULL) {
		printf("ERROR_1: car %llu not found in LP %u\n", event->car_id, me);
		abort();
	}

	if(curr_car->car_id == event->car_id) {
		if(curr_car->accident || curr_car->stopped) {
			return NULL;
		}

		state->queue = curr_car->next;
		state->queued_elements--;
		return curr_car;
	}

	while(curr_car->next != NULL && curr_car->next->car_id != event->car_id) {
		curr_car = curr_car->next;
		curr_car->speed = fabs(Gaussian(&state->seed, state->road_len / (curr_car->leave - curr_car->arrival),
		    SPEED_SIGMA)); // TODO
	}

	if(curr_car->next == NULL) {
		printf("ERROR_2: car %llu not found in LP %u\n", event->car_id, me);
		abort();
	}

	ret_car = curr_car->next;
	if(ret_car->accident || ret_car->stopped) {
		return NULL;
	}

	curr_car->next = curr_car->next->next;

	state->queued_elements--;

	return ret_car;
}


void inject_new_car(lp_id_t me, struct state *state)
{
	simtime_t timestamp;
	struct car_arrival_event new_evt;

	// If the node has an interarrival frequency == 0, then no car can enter the node
	if(D_EQUAL(state->enter_freq, 0)) {
		return;
	}

	// Entering timestamps distributed according to an Erlang distribution.
	timestamp = state->lvt + (simtime_t)(Expent(&state->seed, state->enter_freq));

	// Send me the inject event
	new_evt.from = me;
	new_evt.injection = true;
	ScheduleNewEvent(me, timestamp, ARRIVAL, &new_evt, sizeof(struct car_arrival_event));
}

void process_car_arrival(lp_id_t me, struct state *state, struct car_arrival_event *event)
{
	struct vehicle *car;
	struct car_leave_event car_leave_evt;

	if(!event->injection && check_car_leaving(state, event->from, me)) {
		return;
	}

	if(state->queued_elements < state->total_queue_slots) {
		car = car_enqueue(me, event->from, state);
		car_leave_evt.car_id = car->car_id;
		ScheduleNewEvent(me, car->leave, LEAVE, &car_leave_evt, sizeof(car_leave_evt));
	} else {
		fprintf(stderr, "Object queue full: %s\n", IS_NODE(me) ? "JUNCTION" : "ROAD");
		abort();
	}

	cause_accident(state, me);

	// If the arrival is related to a new car entering the road system, schedule the next car entering event
	if(event->injection && IS_NODE(me)) {
		inject_new_car(me, state);
	}
}

void process_car_leave(lp_id_t me, struct state *state, struct car_leave_event *event)
{
	struct car_arrival_event new_event = {0};
	lp_id_t receiver;
	struct vehicle *car = car_dequeue(me, state, event);

	if(car != NULL) {
		new_event.from = me;
		new_event.injection = false;

		if(IS_EDGE(me)) {
			if(count_neighbours(me) > 1) {
				do {
					receiver = get_random_destination(me);
				} while(receiver == car->from);
			} else {
				receiver = get_random_destination(me);
			}
		} else {
			receiver = event->destination;
		}

		ScheduleNewEvent(receiver, car->leave, ARRIVAL, &new_event, sizeof(new_event));
		free(car);
	} else {
		// MUST HANDLE "RETRACTABILITY" OF LEAVE EVENTS
		abort();
	}
}
