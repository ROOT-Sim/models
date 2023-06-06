/**
 * @file model.h
 *
 * @brief Model state and configuration
 *
 * Model state and configuration.
 *
 * SPDX-FileCopyrightText: 2008-2023 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <ROOT-Sim/random.h>
#include <float.h>
#include <math.h>

#define YEAR 31536000
#define WEEK 604800
#define DAY 86400

// Execution time must be specified in seconds
#define TOTAL_SIMULATION_TIME (1 * DAY)

// Lunghezza macchina: 4.20m + 0.80m distanza di sicurezza = 5m
// Unità di lunghezza in km
// Due corsie
#define CARS_PER_UNIT_LENGTH 400

// A junction has no actual length, yet cars can be queued in it
#define CARS_PER_JUNCTION 10

// In Km/h.
#define AVERAGE_SPEED 60
#define SPEED_SIGMA 20.0

#define ACCIDENT_PROBABILITY 0.15
#define ACCIDENT_DURATION 3600 // one hour on average
#define ACCIDENT_SIGMA 30

#define JUNCTION_TRAVERSE_MIN 1 // in seconds
#define JUNCTION_TRAVERSE 60    // in seconds

#define ACCIDENT_LEAVE_TIME 20  // Exponential mean to compute the time increment to leave after an accident

#define D_EQUAL(a, b) (fabs((a) - (b)) < DBL_EPSILON)
#define D_EQUAL_ZERO(a) (fabs(a) < DBL_EPSILON)
#define D_DIFFER(a, b) (fabs((a) - (b)) >= DBL_EPSILON)
#define D_DIFFER_ZERO(a) (fabs(a) >= DBL_EPSILON)

#define JAM_START_FACTOR 0.9
#define JAM_END_FACTOR 0.75

enum events { JAM, ARRIVAL, FINISH_ACCIDENT, LEAVE };

struct mean_estimator;

struct state {
	simtime_t lvt;
	struct rng_t seed;
	bool accident;
	struct mean_estimator *leave_mean;
	union {
		// Road-specific simulation state
		struct {
			double road_len;
		};
		// Junction-specific simulation state
		struct {
			double enter_freq;
			double leave_prob;
			simtime_t slowdown_injection_until;
		};
	};
	unsigned int total_queue_slots;
	unsigned int enqueued_cars;
	unsigned long long int car_monotonic_counter;
	struct vehicle *queue;
};

struct car_arrival_event {
	lp_id_t from;              // The node the car is coming from.
	lp_id_t road;              // The edge used to reach the destination from the source.
	lp_id_t to;                // Used to tell an edge what is the node we are heading to.
	unsigned long long car_id; // Unique car id in the entire simulation.
	bool injection;            // Tells whether the car is entering the road network or not.
};

struct jam_notify_event {
	lp_id_t from;    // The LP notifying the jam
	simtime_t until; // An estimated time when the jam will end
};
