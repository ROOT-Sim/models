#pragma once

#include <ROOT-Sim.h>
#include <stdbool.h>

#define MIN_AGE 30
#define MAX_AGE 100
#define EXPLOSIONS_CNT 4
#define EXITS_CNT 8
#define EDGE_LEN 32
#define DOORS_LEN 3
#define ROOMS_CNT 6
#define GROUPS_CNT (EDGE_LEN * EDGE_LEN + 1)
#define DEF_GROUP_P 0.1
#define PEOPLE_CNT 10000
#define EMO_MVG_AVG 0.1
#define EMO_THRESH 0.8
#define MEMORY_P 0.2
#define DEF_ENV_KNOWLEDGE_P 0.1
#define DEF_TRUST_P 0.1
#define PSYCHO_P 0.1
#define DEF_PSYCHO_INFL_P 0.1
#define SOCIAL_NET_P 0.01
#define REMOTE_G_P 0.01
#define RESCUER_P 0.1
#define DEF_CONFUSION_P 0.1
#define DIE_P 0.8
#define DEF_AVG_AGE 50

extern double group_p;
extern double env_knowledge_p;
extern double trust_p;
extern double psycho_infl_p;
extern double confusion_p;
extern double avg_age;

extern const struct _expl_t {
	simtime_t time;
	unsigned cell;
} explosions[EXPLOSIONS_CNT];

extern const unsigned exits[EXITS_CNT];

extern const struct _r_t {
	unsigned x_min, y_min, x_max, y_max;
	unsigned doors[DOORS_LEN];
} rooms[ROOMS_CNT];
