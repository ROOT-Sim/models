#include <strings.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include "topology_utils.h"

static void map_linear_to_hexagon(unsigned int linear, unsigned int *x, unsigned int *y)
{
	unsigned int edge = (unsigned int)sqrt((double)n_prc_tot);
	if(edge == 0) edge = 1;
	if(linear >= n_prc_tot) {
		*x = 0;
		*y = 0;
		return;
	}

	*x = linear % edge;
	*y = linear / edge;
}

static unsigned int opposite_direction_of(unsigned int direction)
{
	static const unsigned int opp[6] = { 1, 0, 5, 4, 3, 2 };
	if(direction < 6)
		return opp[direction];
	return 0;
}

static double a_star(agent_state_type *state, unsigned int current_cell, unsigned int *good_direction)
{
	unsigned int i;
	double min_distance = DBL_MAX;
	double current_distance;
	double dx, dy;
	unsigned int x1, y1, x2, y2;
	unsigned int tentative_cell;

	*good_direction = UINT_MAX;

	if(current_cell >= n_prc_tot || state->target_cell >= n_prc_tot)
		return DBL_MAX;

	state->visit_map[current_cell].a_star_f = true;

	map_linear_to_hexagon(state->target_cell, &x1, &y1);

	for(i = 0; i < 6; i++) {
		if((tentative_cell = GetReceiver(current_cell, i, false)) == DIRECTION_INVALID)
			continue;

		if(tentative_cell >= n_prc_tot || state->visit_map[tentative_cell].a_star_f) {
			continue;
		}

		if(state->visit_map[current_cell].neighbours[i] != (unsigned int)-1) {
			if(current_cell == state->target_cell) {
				*good_direction = i;
				return 0.0;
			}

			map_linear_to_hexagon(tentative_cell, &x2, &y2);
			dx = (double)x1 - (double)x2;
			dy = (double)y2 - (double)y1;
			current_distance = sqrt(dx * dx + dy * dy);

			if(current_distance < min_distance) {
				min_distance = current_distance;
				*good_direction = i;
			}
		}
	}

	return min_distance;
}

unsigned int compute_my_direction(agent_state_type *state)
{
	unsigned int good_direction = UINT_MAX;

	unsigned i = n_prc_tot;
	while(i--) {
		state->visit_map[i].a_star_f = false;
	}

	a_star(state, state->current_cell, &good_direction);

	if(good_direction < 6)
		return good_direction;

	unsigned int x1, y1;
	unsigned int x2, y2;
	double min_distance = DBL_MAX;
	double distance;
	double dx, dy;
	unsigned receiver;

	map_linear_to_hexagon(state->target_cell, &x1, &y1);

	for(i = 0; i < 6; i++) {
		if((receiver = GetReceiver(state->current_cell, i, false)) != DIRECTION_INVALID && receiver < n_prc_tot) {
			map_linear_to_hexagon(receiver, &x2, &y2);

			dx = (double)x1 - (double)x2;
			dy = (double)y2 - (double)y1;
			distance = sqrt(dx * dx + dy * dy);

			if(distance < min_distance) {
				min_distance = distance;
				good_direction = i;
			}
		}
	}

	return good_direction;
}

unsigned int closest_frontier(agent_state_type *state, unsigned int curr_cell, unsigned int exclude)
{
	unsigned int i, j;
	unsigned int x, y, curr_x, curr_y;
	bool is_reachable;
	double distance;
	double min_distance = DBL_MAX;
	unsigned int target = UINT_MAX;
	unsigned receiver;

	map_linear_to_hexagon(curr_cell, &curr_x, &curr_y);

	for(i = 0; i < n_prc_tot; i++) {
		if(!state->visit_map[i].visited) {
			if(i == exclude) {
				continue;
			}

			is_reachable = false;
			for(j = 0; j < 6; j++) {
				if((receiver = GetReceiver(i, j, false)) != DIRECTION_INVALID && receiver < n_prc_tot &&
				    state->visit_map[receiver].visited &&
				    state->visit_map[receiver].neighbours[opposite_direction_of(j)] != (unsigned int)-1) {
					is_reachable = true;
				}
			}

			if(is_reachable) {
				map_linear_to_hexagon(i, &x, &y);
				distance = sqrt(((double)curr_x - (double)x) * ((double)curr_x - (double)x) +
				                ((double)curr_y - (double)y) * ((double)curr_y - (double)y));

				if(distance < min_distance) {
					min_distance = distance;
					target = i;
				}
			}
		}
	}

	return target;
}
