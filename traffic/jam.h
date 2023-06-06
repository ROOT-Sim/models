#pragma once

#include "model.h"

#define ESTIMATOR_WINDOW_SIZE 60

struct mean_estimator {
	size_t head;
	size_t tail;
	size_t count;
	double mean;
	simtime_t timestamps[ESTIMATOR_WINDOW_SIZE];
};

extern void update_mean(struct mean_estimator *calculator, simtime_t timestamp);
void handle_detected_jam(lp_id_t me, struct state *state, struct car_arrival_event *event);
void handle_notified_jam(lp_id_t me, struct state *state, struct jam_notify_event *event);
