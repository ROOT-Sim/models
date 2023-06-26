#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>
#include <string.h>

#include "car.h"
#include "config.h"
#include "model.h"
#include "road.h"
#include "statistics.h"
#include "jam.h"

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
	if(state->accident || IS_JUNCTION(me)) {
		return;
	}

	// An accident happens depending on the amount of cars.
	// Accident probability is normal wrt the number of cars.
	// Derive a discrete probability from a normal distribution.
	if(state->enqueued_cars == 0) {
		min = 0.0;
		max = 0.5;
	} else if(state->enqueued_cars == state->total_queue_slots) {
		min = (double)state->total_queue_slots - 0.5;
		max = (double)state->total_queue_slots;
	} else {
		min = (double)state->enqueued_cars - 0.5;
		max = (double)state->enqueued_cars + 0.5;
	}

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
		involved_car = RandomRange(&state->seed, 0, (int)(state->enqueued_cars - 1));
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

static void reorder_queue(lp_id_t me, struct vehicle **head, struct state *state)
{
	simtime_t now = state->lvt;
	struct vehicle *curr;
	struct vehicle *prev;

	// Update speed and leave time
	curr = *head;
	while(curr != NULL) {
		update_car_speed(me, state, curr);
		curr = curr->next;
	}

	// Sort the queue by leave time
	bool didSwap = false;
	for(didSwap = true; didSwap;) {
		didSwap = false;
		prev = NULL; // Set prev to NULL for the first iteration
		curr = *head;
		while(curr != NULL && curr->next != NULL) {
			if(curr->leave_time > curr->next->leave_time) {
				if(*head == curr) {
					*head = curr->next;
					curr->next = (*head)->next;
					(*head)->next = curr;
				} else {
					prev->next = curr->next;
					curr->next = prev->next->next;
					prev->next->next = curr;
				}
				didSwap = true;
			}
			prev = curr;
			curr = curr->next;
		}
	}

	// Update the portion of traveled space
	curr = *head;
	while(curr != NULL) {
		curr->traveled = (now - curr->arrival_time) / (curr->leave_time - curr->arrival_time);
		curr = curr->next;
	}
}


static void car_enqueue(lp_id_t me, struct state *state, struct car_arrival_event *event)
{
	struct vehicle *new_car;

	// Create the car node
	new_car = populate_car(me, state, event);

	new_car->next = state->queue;
	state->queue = new_car;
	state->enqueued_cars++;
	reorder_queue(me, &state->queue, state);
}


void release_cars(unsigned int me, struct state *state)
{
	(void)me;
	struct vehicle *curr_car;

	curr_car = state->queue;
	while(curr_car != NULL) {
		if(curr_car->accident == true) {
			curr_car->accident = false;
		}

		curr_car = curr_car->next;
	}
}


void inject_new_car(lp_id_t me, struct state *state)
{
	simtime_t timestamp;
	struct car_arrival_event new_evt = {0};

	// If the node has an interarrival frequency == 0, then no car can enter the node
	if(D_EQUAL(state->enter_freq, 0)) {
		return;
	}

	// Entering timestamps distributed according to an Erlang distribution.
	timestamp = state->lvt + (simtime_t)(Expent(&state->seed, state->enter_freq));

	// Account for notified jams
	if(timestamp < state->slowdown_injection_until) {
		timestamp = state->slowdown_injection_until;
	}

	// Send me the inject event
	new_evt.from = me;
	new_evt.injection = true;
	ScheduleNewEvent(me, timestamp, ARRIVAL, &new_evt, sizeof(struct car_arrival_event));
}

void process_car_arrival(lp_id_t me, struct state *state, struct car_arrival_event *event)
{
	struct vehicle *car;

	if(!event->injection && check_car_leaving(state, event->from, me)) {
		return;
	}

	// If the arrival is related to a new car entering the road system, schedule the next car entering event
	if(event->injection && IS_JUNCTION(me)) {
		inject_new_car(me, state);

		if(state->slowdown_injection_until > 0)
			state->slowdown_injection_until = 0;
	}

	car_enqueue(me, state, event);
	ScheduleNewEvent(me, state->queue->leave_time, LEAVE, NULL, 0); // TODO: retractable events would be useful here

	if(state->enqueued_cars > JAM_START_FACTOR * state->total_queue_slots) {
		handle_detected_jam(me, state, event);
	}

	//	cause_accident(state, me);
}

void process_car_leave(lp_id_t me, struct state *state)
{
	struct car_arrival_event new_event = {0};
	lp_id_t receiver;

	struct vehicle **curr_car_ptr = &(state->queue);

	while(*curr_car_ptr != NULL && (*curr_car_ptr)->leave_time == state->lvt) {
		if((*curr_car_ptr)->accident) {
			curr_car_ptr = &((*curr_car_ptr)->next);
			continue;
		}

		struct vehicle *dequeued_car = *curr_car_ptr;
		*curr_car_ptr = dequeued_car->next;
		state->enqueued_cars--;

		update_mean(state->leave_mean, state->lvt);

		new_event.injection = false;
		new_event.car_id = dequeued_car->car_id;

		if(IS_JUNCTION(me)) {
			new_event.from = me;

			do {
				new_event.to = get_random_destination(me);
			} while(new_event.to == dequeued_car->from);

			if(new_event.to == INVALID_DIRECTION) {
				// If a topology has sink nodes with non-zero enter probability, the spurious car cannot
				// move.
				rs_free(dequeued_car);
				return;
			}

			// Must pass through the connecting edge
			receiver = new_event.road = get_path_towards(me, new_event.to);
		} else {
			new_event.from = dequeued_car->from;
			receiver = dequeued_car->to;
		}

		ScheduleNewEvent(receiver, dequeued_car->leave_time, ARRIVAL, &new_event, sizeof(new_event));
		rs_free(dequeued_car);
	}
}

void slowdown_cars_for_jam(lp_id_t me, struct state *state, simtime_t until)
{
	struct vehicle *curr_car = state->queue;
	simtime_t delay = until - curr_car->leave_time;

	if(delay > 0) {
		while(curr_car != NULL) {
			curr_car->leave_time += delay;
			curr_car = curr_car->next;
		}

		ScheduleNewEvent(me, state->queue->leave_time, LEAVE, NULL,
		    0); // TODO: retractable events would be useful here
	}
}
