#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <ROOT-Sim.h>
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
	double lambda = 1.0 / leave_mean;
	double waiting_time = 0.0;

	if(lambda > 0.0) {
		double p = (double)(curr_elements - desired_elements) / curr_elements;
		waiting_time = -log(p) / lambda;
	}

	return waiting_time;
}


void handle_detected_jam(lp_id_t me, struct state *state, struct car_arrival_event *event)
{
	lp_id_t receiver;
	struct jam_notify_event jam_event;

	double estimated_time_to_free = calculate_waiting_time(state->leave_mean->mean, state->total_queue_slots,
	    JAM_END_FACTOR * state->total_queue_slots);

	//    printf("%lu has a jam at time %f until %f\n", me, state->lvt, state->lvt + estimated_time_to_free);

	if(IS_ROAD(me)) {
		receiver = event->from;
	} else {
		receiver = event->road;
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
