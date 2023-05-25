#pragma once

#include <ROOT-Sim.h>

struct node_config {
	double enter_freq;
	double leave_prob;
};

struct edge_config {
	lp_id_t from;
	lp_id_t to;
	double length;
};

extern uint64_t conf_num_nodes;

#define IS_NODE(me) (me < conf_num_nodes)
#define IS_EDGE(me) !IS_NODE(me)

extern uint64_t process_configuration_file(FILE *f);
extern void get_node_config(lp_id_t me, struct node_config *c);
extern void get_edge_config(lp_id_t me, struct edge_config *c);
extern unsigned long count_neighbours(lp_id_t me);
