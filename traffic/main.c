#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <ROOT-Sim.h>

#include "config.h"

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type, const void *content, unsigned size, void *s) {}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
	return false;
}

struct simulation_configuration conf = {
    .n_threads = 0,
    .termination_time = 1000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
};

int main(int argc, char **argv)
{
	FILE *conf_file;

	// Set the locale to use dots as the decimal separator, as the json config file should adhere to it.
	setlocale(LC_NUMERIC, "C");

	if(argc != 2) {
		printf("Usage: %s topology.json\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	conf_file = fopen(argv[1], "rb");
	if(conf_file == NULL) {
		fprintf(stderr, "Error opening file: %s. ", argv[1]);
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	conf.lps = process_configuration_file(conf_file);
	conf.stats_file = argv[0];

	printf("Total number of LPs needed: %lu", conf.lps);
}
