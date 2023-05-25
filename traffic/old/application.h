/**
 * SPDX-FileCopyrightText: 2008-2023 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 * TRAFFIC is a simulation model for the ROme OpTimistic Simulator (ROOT-Sim)
 * which allows to simulate car traffic on generic routes, which can be
 * specified from text file.

 * @file application.h
 * @brief Global simulation data types and definitions
 * @author Alessandro Pellegrini
 * @date January 12, 2012
 *
 */

#pragma once

#include "ROOT-Sim.h"
#include "ROOT-Sim/random.h"


#ifndef SIMPLE_TRAFFIC
#define SIMPLE_TRAFFIC 0
#endif


#define YEAR 31536000
#define WEEK 604800
#define DAY 86400


// Execution time must be specified in seconds
#ifndef EXECUTION_TIME
#define EXECUTION_TIME    (1 * DAY/8)
#endif


// In Km/h. Il simulatore li converte in Km/s
#ifndef AVERAGE_SPEED
#define AVERAGE_SPEED    110
#endif

// Lunghezza macchina: 4.20m + 0.80m distanza di sicurezza = 5m
// Unità di lunghezza in km
// Due corsie
#ifndef CARS_PER_UNIT_LENGTH
#define CARS_PER_UNIT_LENGTH    400
#endif

// A junction has no actual length, yet cars can be queued in it
#ifndef CARS_PER_JUNCTION
#define    CARS_PER_JUNCTION    10000
#endif
#ifndef JUNCTION_LENGTH
#define JUNCTION_LENGTH 0.1
#endif

// A junction has no actual length, yet cars take some time to pass in it
#ifndef JUNCTION_TRAVERSE_TIME
#define    JUNCTION_TRAVERSE_TIME  600    // 10 minutes on average
#endif

#if SIMPLE_TRAFFIC != 0
#define MIN_SPEED		AVERAGE_SPEED//15//20
#else
#define MIN_SPEED        20
#endif

// SIGMA is in Km/h
#define SPEED_SIGMA        20.0
#define ENTER_SIGMA        10

// accidents parameters
#define ACCIDENT_DURATION    3600    // one hour on average
#define ACCIDENT_SIGMA        30
#define ACCIDENT_LEAVE_TIME    20    // Exponential mean to compute the time increment to leave after an accident

#define KEEP_ALIVE_TIME        200 // Exponential mean of keep alive messages

// EVENTI
#define ARRIVAL        10
#define LEAVE        11
#define FINISH_ACCIDENT 12
#define KEEP_ALIVE    100

#define STOP_PROBABILITY    0.05

#define ACCIDENT_PROBABILITY    0.15

// LP Type
#define JUNCTION    123
#define SEGMENT        124

// Allowed length for an LP's name
#define NAME_LENGTH    32

struct event_content {
    int from;
    bool injection; // Tells whether the car is entering the highway or not
};

struct topology {
    int num_neighbours;
    unsigned int *neighbours;
};

struct vehicle {
    int from;
    simtime_t arrival;
    simtime_t leave;
    bool accident;
    bool stopped;
    double traveled;
    unsigned long long car_id;
    struct vehicle *next;
    double speed;
};

struct state {
    simtime_t lvt;            // Elapsed simulation time
    bool accident;
    unsigned int total_queue_slots;
    char name[NAME_LENGTH];    // Name of the cell
    int lp_type;        // Is it a junction or a segment?
    double segment_length;        // Length of this road segment. If set to 0, it's a junction
    double enter_prob;        // it is a frequency!
    double leave_prob;
    struct topology *topology;        // Each node can have an arbitrary number of neighbours
    unsigned int queued_elements;
    struct vehicle *queue;            // Cars passing through the node are stored here
    unsigned long long car_id;
};


extern struct vehicle *car_enqueue(int me, int from, struct state *state);

extern struct vehicle *car_dequeue(unsigned int me, struct state *state, unsigned long long *);

extern struct vehicle *car_dequeue_conditional(struct state *state, unsigned long long *);

extern void inject_new_cars(struct state *state, int me);

extern int check_car_leaving(struct state *state, int from, int me);

extern void check_accident_end(struct state *state);

extern void determine_stop(struct state *state);

extern void cause_accident(struct state *state, int me);

extern void release_cars(unsigned int me, struct state *state);

extern void update_car_leave(struct state *state, unsigned long long, simtime_t new);

extern unsigned long long get_mark(unsigned int k1, unsigned int k2);
