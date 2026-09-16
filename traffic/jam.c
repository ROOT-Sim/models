#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <ROOT-Sim.h>
#include <ROOT-Sim/topology.h>
#include "config.h"
#include "jam.h"
#include "model.h"
#include "road.h"

void update_mean(struct mean_estimator *calculator, simtime_t timestamp)
{
	assert(calculator != NULL);

	if(calculator->count < ESTIMATOR_WINDOW_SIZE) {
		calculator->count++;
	} else {
		// Subtract the oldest separation from the mean calculation
		double oldest_separation = timestamp - calculator->timestamps[calculator->head];
		calculator->mean -= oldest_separation / ESTIMATOR_WINDOW_SIZE;
		calculator->head = (calculator->head + 1) % ESTIMATOR_WINDOW_SIZE;
	}

	// Calculate the separation from the previous timestamp
	double separation = timestamp - calculator->timestamps[calculator->tail];

	calculator->tail = (calculator->tail + 1) % ESTIMATOR_WINDOW_SIZE;
	calculator->timestamps[calculator->tail] = timestamp;
	calculator->mean += separation / ESTIMATOR_WINDOW_SIZE;
}


static double calculate_waiting_time(double leave_mean, unsigned curr_elements, unsigned desired_elements)
{
	if(leave_mean <= 0.0 || isnan(leave_mean) || isinf(leave_mean)) {
		return 60.0;
	}

	double lambda = 1.0 / leave_mean;
	double waiting_time = 60.0;

	if(curr_elements > desired_elements && curr_elements > 0) {
		double p = (double)(curr_elements - desired_elements) / (double)curr_elements;
		if(p > 0.0 && p < 1.0) {
			waiting_time = -log(p) / lambda;
		}
	}

	if(isnan(waiting_time) || isinf(waiting_time) || waiting_time <= 0.0) {
		waiting_time = 60.0;
	}

	return waiting_time;
}


void handle_detected_jam(lp_id_t me, struct state *state, struct car_arrival_event *event)
{
	lp_id_t receiver;
	struct jam_notify_event jam_event;

	if(event == NULL || event->injection) {
		return;
	}

	if(state->leave_mean == NULL) {
		return;
	}

	double estimated_time_to_free = calculate_waiting_time(state->leave_mean->mean, state->total_queue_slots,
	    (unsigned)(JAM_END_FACTOR * state->total_queue_slots));

	if(IS_ROAD(me)) {
		receiver = event->from;
	} else {
		receiver = event->road;
	}

	if(receiver == INVALID_DIRECTION) {
		return;
	}

	jam_event.from = me;
	jam_event.until = state->lvt + estimated_time_to_free;
	ScheduleNewEvent(receiver, state->lvt, JAM, &jam_event, sizeof(jam_event));
}


void handle_notified_jam(lp_id_t me, struct state *state, struct jam_notify_event *event)
{
	if(IS_JUNCTION(me)) {
		state->slowdown_injection_until = event->until;
		slowdown_cars_for_jam(me, state, event->until);
	} else {
		slowdown_cars_for_jam(me, state, event->until);
	}
}
