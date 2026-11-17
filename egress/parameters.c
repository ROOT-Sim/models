#include "parameters.h"

const struct _expl_t explosions[EXPLOSIONS_CNT] = {
		{.time = 10, .cell = 412},
		{.time = 100, .cell = 634},
		{.time = 200, .cell = 644},
		{.time = 300, .cell = 399}
};

const unsigned exits[EXITS_CNT] = {2, 3, 4, 5, 995, 996, 997, 998};

const struct _r_t rooms[ROOMS_CNT] = {
		{.x_min = 0, .y_min = 0, .x_max = 31, .y_max = 7, .doors = {5, 309, 310}},
		{.x_min = 0, .y_min = 8, .x_max = 31, .y_max = 15, .doors = {277, 554, 566}},
		{.x_min = 0, .y_min = 16, .x_max = 19, .y_max = 23, .doors = {522, 818, 817}},
		{.x_min = 20, .y_min = 16, .x_max = 31, .y_max = 23, .doors = {534, 830, 831}},
		{.x_min = 0, .y_min = 24, .x_max = 19, .y_max = 31, .doors = {786, 787, 996}},
		{.x_min = 20, .y_min = 24, .x_max = 31, .y_max = 31, .doors = {798, 797, 799}}
};

double group_p = DEF_GROUP_P;
double env_knowledge_p = DEF_ENV_KNOWLEDGE_P;
double trust_p = DEF_TRUST_P;
double psycho_infl_p = DEF_PSYCHO_INFL_P;
double confusion_p = DEF_CONFUSION_P;
double avg_age = DEF_AVG_AGE;
