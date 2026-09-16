#pragma once

#include <ROOT-Sim.h>
#include <getopt.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct model_cli_options {
    unsigned int n_threads;
    lp_id_t lps;
    simtime_t termination_time;
    const char *stats_file;
    enum log_level log_level;
};

void init_default_cli_options(struct model_cli_options *opt, lp_id_t default_lps, simtime_t default_term_time);
bool parse_model_cli_options(int argc, char **argv, struct model_cli_options *opt, struct simulation_configuration *conf);

bool parse_model_cli_options_custom(int argc, char **argv,
                                    struct model_cli_options *opt,
                                    struct simulation_configuration *conf,
                                    const struct option *extra_long_options,
                                    const char *extra_short_options,
                                    bool (*extra_handler)(int c, const char *arg, void *user_data),
                                    void *user_data,
                                    void (*extra_usage)(void));

#ifdef __cplusplus
}
#endif

