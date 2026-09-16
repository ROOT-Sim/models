#pragma once
#ifndef MODELS_ROBOT_EXPLORE_ABM_TOPOLOGY_UTILS_H_
#define MODELS_ROBOT_EXPLORE_ABM_TOPOLOGY_UTILS_H_

#include "robot_explore.h"

extern unsigned int n_prc_tot;

unsigned int compute_my_direction(agent_state_type *state);
unsigned int closest_frontier(agent_state_type *state, unsigned int curr_cell, unsigned int exclude);

#endif /* MODELS_ROBOT_EXPLORE_ABM_TOPOLOGY_UTILS_H_ */
