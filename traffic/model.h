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
#ifndef TOTAL_SIMULATION_TIME
#define TOTAL_SIMULATION_TIME (1 * DAY)
#endif

// Lunghezza macchina: 4.20m + 0.80m distanza di sicurezza = 5m
// Unità di lunghezza in km
// Due corsie
#ifndef CARS_PER_UNIT_LENGTH
#define CARS_PER_UNIT_LENGTH 400
#endif

// A junction has no actual length, yet cars can be queued in it
#ifndef CARS_PER_JUNCTION
#define CARS_PER_JUNCTION 10
#endif

#define D_EQUAL(a, b) (fabs((a) - (b)) < DBL_EPSILON)
#define D_EQUAL_ZERO(a) (fabs(a) < DBL_EPSILON)
#define D_DIFFER(a, b) (fabs((a) - (b)) >= DBL_EPSILON)
#define D_DIFFER_ZERO(a) (fabs(a) >= DBL_EPSILON)

enum events { ARRIVAL };

struct state {
	simtime_t lvt;
	struct rng_t seed;
	bool accident;
	union {
		double road_len;
		struct {
			double enter_freq;
			double leave_prob;
		};
	};
	unsigned int total_queue_slots;
	unsigned int queued_elements;
	struct vehicle *queue;
};

struct car_arrival_event {
	lp_id_t from;
	bool injection; // Tells whether the car is entering the road network or not
};
