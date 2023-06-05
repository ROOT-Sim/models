#pragma once

#include <ROOT-Sim.h>

struct node_config {
	double enter_freq;
	double leave_prob;
};

struct edge_config {
	lp_id_t id;
	lp_id_t from;
	lp_id_t to;
	double length;
};

extern uint64_t conf_num_nodes;

#define IS_JUNCTION(me) (me < conf_num_nodes)
#define IS_ROAD(me) !IS_JUNCTION(me)

extern uint64_t process_configuration_file(FILE *f);
extern void get_node_config(lp_id_t me, struct node_config *c);
extern void get_edge_config(lp_id_t me, struct edge_config *c);
extern unsigned long count_neighbours(lp_id_t me);
extern lp_id_t get_random_destination(lp_id_t me);
extern lp_id_t get_path_towards(lp_id_t me, lp_id_t to);
extern void cleanup_config(void);
