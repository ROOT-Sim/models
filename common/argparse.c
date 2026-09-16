#include "argparse.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_default_cli_options(struct model_cli_options *opt, lp_id_t default_lps, simtime_t default_term_time)
{
    opt->n_threads = 0; // 0 = auto in ROOT-Sim
    opt->lps = default_lps;
    opt->termination_time = default_term_time;
    opt->stats_file = NULL;
    opt->log_level = LOG_INFO;
}

static void print_usage(const char *progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -c, --ncores <N>           Number of worker threads (default: auto)\n");
    printf("  -p, --nprocesses <N>       Number of logical processes (LPs)\n");
    printf("  -t, --termination-time <T> Simulation termination time\n");
    printf("  -s, --stats <FILE>         Path to stats output file\n");
    printf("  -l, --log-level <LEVEL>    Logging level (trace, debug, info, warn, error, fatal, silent)\n");
    printf("  --serial                   Run in serial mode\n");
    printf("  --timewarp                 Run in Time Warp optimistic mode (default)\n");
    printf("  -h, --help                 Display this help message\n");
}

bool parse_model_cli_options_custom(int argc, char **argv,
                                    struct model_cli_options *opt,
                                    struct simulation_configuration *conf,
                                    const struct option *extra_long_options,
                                    const char *extra_short_options,
                                    bool (*extra_handler)(int c, const char *arg, void *user_data),
                                    void *user_data,
                                    void (*extra_usage)(void))
{
    static const struct option base_long_options[] = {
        {"ncores", required_argument, 0, 'c'},
        {"nprocesses", required_argument, 0, 'p'},
        {"termination-time", required_argument, 0, 't'},
        {"stats", required_argument, 0, 's'},
        {"log-level", required_argument, 0, 'l'},
        {"serial", no_argument, 0, 1001},
        {"timewarp", no_argument, 0, 1002},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    size_t base_count = sizeof(base_long_options) / sizeof(base_long_options[0]) - 1;

    size_t extra_count = 0;
    if (extra_long_options != NULL) {
        while (extra_long_options[extra_count].name != NULL) {
            extra_count++;
        }
    }

    struct option *all_options = malloc(sizeof(struct option) * (base_count + extra_count + 1));
    if (all_options == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memcpy(all_options, base_long_options, sizeof(struct option) * base_count);
    if (extra_count > 0) {
        memcpy(all_options + base_count, extra_long_options, sizeof(struct option) * extra_count);
    }
    memset(all_options + base_count + extra_count, 0, sizeof(struct option));

    char short_options[128] = "c:p:t:s:l:h";
    if (extra_short_options != NULL) {
        strncat(short_options, extra_short_options, sizeof(short_options) - strlen(short_options) - 1);
    }

    if (conf != NULL) {
        conf->synchronization = TIME_WARP;
    }

    optind = 1; // Reset getopt
    int c;
    while ((c = getopt_long(argc, argv, short_options, all_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                opt->n_threads = (unsigned int)strtoul(optarg, NULL, 10);
                break;
            case 'p':
                opt->lps = (lp_id_t)strtoull(optarg, NULL, 10);
                break;
            case 't':
                opt->termination_time = strtod(optarg, NULL);
                break;
            case 's':
                opt->stats_file = optarg;
                break;
            case 'l':
                if (strcasecmp(optarg, "trace") == 0) opt->log_level = LOG_TRACE;
                else if (strcasecmp(optarg, "debug") == 0) opt->log_level = LOG_DEBUG;
                else if (strcasecmp(optarg, "info") == 0) opt->log_level = LOG_INFO;
                else if (strcasecmp(optarg, "warn") == 0) opt->log_level = LOG_WARN;
                else if (strcasecmp(optarg, "error") == 0) opt->log_level = LOG_ERROR;
                else if (strcasecmp(optarg, "fatal") == 0) opt->log_level = LOG_FATAL;
                else if (strcasecmp(optarg, "silent") == 0) opt->log_level = LOG_SILENT;
                break;
            case 1001:
                if (conf != NULL) conf->synchronization = SERIAL;
                break;
            case 1002:
                if (conf != NULL) conf->synchronization = TIME_WARP;
                break;
            case 'h':
                print_usage(argv[0]);
                if (extra_usage != NULL) {
                    extra_usage();
                }
                free(all_options);
                exit(EXIT_SUCCESS);
            default:
                if (extra_handler != NULL && extra_handler(c, optarg, user_data)) {
                    break;
                }
                break;
        }
    }

    free(all_options);

    if (conf != NULL) {
        conf->lps = opt->lps;
        conf->n_threads = opt->n_threads;
        conf->termination_time = opt->termination_time;
        conf->log_level = opt->log_level;
        conf->stats_file = opt->stats_file;
    }

    return true;
}

bool parse_model_cli_options(int argc, char **argv, struct model_cli_options *opt, struct simulation_configuration *conf)
{
    return parse_model_cli_options_custom(argc, argv, opt, conf, NULL, NULL, NULL, NULL, NULL);
}
