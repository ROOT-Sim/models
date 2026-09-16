#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <stdbool.h>

#define TX_OP_ARRIVAL	250
#define MIN_OP_COUNT	10
#define MAX_OP_COUNT	50
#define MAX_RS_SIZE		50
#define MAX_WS_SIZE		50
#define TOTAL_COMMITTED_TX 100

#define START_TX	10
#define TX_OP		20
#define PREPARE		30
#define COMMIT		40

typedef struct _event_content_type {
	int read_set[MAX_RS_SIZE];
	int size;
	bool second;
	lp_id_t from;
} event_content_type;

typedef struct _lp_state_type {
	struct rng_t seed;
	unsigned int committed_tx;
	unsigned int conflicted_tx;
	unsigned int residual_tx_ops;
	int tx_ops_displacement;
	int read_set[MAX_RS_SIZE];
	int write_set[MAX_WS_SIZE];
	int read_set_size;
	int write_set_size;
	simtime_t lvt;
} lp_state_type;
