#include "application.h"

#include "abm.h"
#include "argparse.h"
#include "person.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _topology_settings_t topology_settings = {
    .type = TOPOLOGY_OBSTACLES, .write_enabled = false, .default_geometry = TOPOLOGY_HEXAGON};
struct _abm_settings_t abm_settings = {0, _TRAVERSE, false};

static _Atomic unsigned total_saved = 0;
static _Atomic unsigned total_deaths = 0;
static const char *topology_path = NULL;
static const char *escape_stats_path = NULL;

static unsigned *lp_saved = NULL;
static unsigned *lp_deaths = NULL;
static unsigned lp_stats_count = 0;

static pthread_mutex_t outcome_lock = PTHREAD_MUTEX_INITIALIZER;
static simtime_t *outcome_times = NULL;
static size_t outcome_count = 0;
static size_t outcome_cap = 0;
static simtime_t max_outcome_time = 0.0;

enum { OUT_OUTCOME = 1 };

void egress_record_outcome(simtime_t now)
{
	ScheduleOutput(OUT_OUTCOME, &now, sizeof(now));
}

static void egress_output_callback(lp_id_t me, unsigned output_type, const void *output_content, unsigned output_size)
{
	(void)me;
	if (output_type != OUT_OUTCOME || !output_content || output_size != sizeof(simtime_t))
		return;

	simtime_t t = *(const simtime_t *)output_content;
	pthread_mutex_lock(&outcome_lock);
	if (outcome_count == outcome_cap) {
		size_t new_cap = (outcome_cap == 0) ? 1024 : outcome_cap * 2;
		simtime_t *tmp = realloc(outcome_times, new_cap * sizeof(simtime_t));
		if (tmp) {
			outcome_times = tmp;
			outcome_cap = new_cap;
		}
	}
	if (outcome_count < outcome_cap) {
		outcome_times[outcome_count++] = t;
	}
	pthread_mutex_unlock(&outcome_lock);
}

static int cmp_simtime(const void *a, const void *b)
{
	simtime_t va = *(const simtime_t *)a;
	simtime_t vb = *(const simtime_t *)b;
	return (va > vb) - (va < vb);
}

static struct simulation_configuration conf = {
    .lps = EDGE_LEN * EDGE_LEN,
    .n_threads = 0,
    .termination_time = 10000,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "egress",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .output_callback = egress_output_callback,
};

// From Luc Devroye's book "Non-Uniform Random Variate Generation." p. 522
unsigned random_binomial(unsigned trials, double p)
{
	if (p >= 1.0 || !trials)
		return trials;
	unsigned x = 0;
	double sum = 0, log_q = log(1.0 - p);
	while (1) {
		sum += log(Random()) / (trials - x);
		if (sum < log_q || trials == x) {
			return x;
		}
		x++;
	}
	return 0;
}

static char *read_entire_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long len = ftell(f);
	if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	char *buf = malloc((size_t)len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t nread = fread(buf, 1, (size_t)len, f);
	buf[nread] = '\0';
	fclose(f);
	return buf;
}

static void load_topology_json(const char *path, double **obstacles_out, unsigned int *regions_out,
                               enum topology_geometry *geom_out)
{
	char *json = NULL;
	if (path != NULL) {
		json = read_entire_file(path);
		if (!json) {
			fprintf(stderr, "Error opening topology file '%s'\n", path);
			exit(EXIT_FAILURE);
		}
	} else {
		json = read_entire_file("topology.json");
		if (!json) {
			json = read_entire_file("egress/topology.json");
		}
		if (!json) {
			fprintf(stderr, "Error: Could not find 'topology.json' or 'egress/topology.json'. "
			                "Specify with -T <FILE>.\n");
			exit(EXIT_FAILURE);
		}
	}

	enum topology_geometry geom = TOPOLOGY_HEXAGON;
	const char *geom_key = strstr(json, "\"geometry\"");
	if (geom_key) {
		const char *colon = strchr(geom_key, ':');
		const char *q1 = colon ? strchr(colon, '"') : NULL;
		const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
		if (q1 && q2) {
			size_t glen = (size_t)(q2 - (q1 + 1));
			if (strncmp(q1 + 1, "squares", glen) == 0 || strncmp(q1 + 1, "square", glen) == 0) {
				geom = TOPOLOGY_SQUARE;
			} else if (strncmp(q1 + 1, "torus", glen) == 0) {
				geom = TOPOLOGY_TORUS;
			} else {
				geom = TOPOLOGY_HEXAGON;
			}
		}
	}

	unsigned int regions_count = 0;
	const char *rc_key = strstr(json, "\"regions_count\"");
	if (rc_key) {
		const char *colon = strchr(rc_key, ':');
		if (colon) {
			regions_count = (unsigned int)strtoul(colon + 1, NULL, 10);
		}
	}

	const char *vals_key = strstr(json, "\"values\"");
	const char *arr_start = vals_key ? strchr(vals_key, '[') : NULL;
	if (!arr_start) {
		fprintf(stderr, "Error: Invalid topology JSON (missing \"values\" array)\n");
		free(json);
		exit(EXIT_FAILURE);
	}

	size_t cap = (regions_count > 0) ? regions_count : 1024;
	double *values = malloc(cap * sizeof(double));
	if (!values) {
		perror("malloc");
		free(json);
		exit(EXIT_FAILURE);
	}

	size_t count = 0;
	const char *p = arr_start + 1;
	while (*p && *p != ']') {
		while (*p && (isspace((unsigned char)*p) || *p == ','))
			p++;
		if (*p == ']' || *p == '\0')
			break;
		char *endptr = NULL;
		double v = strtod(p, &endptr);
		if (endptr == p) {
			fprintf(stderr, "Error: Failed parsing number in topology JSON\n");
			free(values);
			free(json);
			exit(EXIT_FAILURE);
		}
		if (count >= cap) {
			cap *= 2;
			double *tmp = realloc(values, cap * sizeof(double));
			if (!tmp) {
				perror("realloc");
				free(values);
				free(json);
				exit(EXIT_FAILURE);
			}
			values = tmp;
		}
		values[count++] = v;
		p = endptr;
	}

	free(json);

	if (count == 0) {
		fprintf(stderr, "Error: Empty \"values\" array in topology JSON\n");
		free(values);
		exit(EXIT_FAILURE);
	}
	if (regions_count == 0) {
		regions_count = (unsigned int)count;
	}

	*obstacles_out = values;
	*regions_out = regions_count;
	*geom_out = geom;
}

void ProcessEvent(lp_id_t me, simtime_t now, unsigned int event_type, const void *event_content,
                  unsigned int event_size, void *ptr)
{
	(void)event_size;
	region_t *state = (region_t *)ptr;

	switch (event_type) {
		case LP_INIT: {
			if (GetValueTopology((unsigned)me, (unsigned)me) != 0.0)
				break;

			state = rs_malloc(sizeof(region_t));
			if (!state)
				abort();
			memset(state, 0, sizeof(region_t));
			state->expl_dist = UINT_MAX;
			memset(state->group_d, 0xFF, sizeof(state->group_d));
			SetState(state);

			person_env_init(state, (unsigned)me);
			person_set_up(state, (unsigned)me, 120);

			unsigned i = EXPLOSIONS_CNT;
			while (i--) {
				ScheduleNewEvent(me, explosions[i].time, EXPLOSION, &explosions[i].cell,
				                 sizeof(unsigned));
			}
			ScheduleNewEvent(me, now + 0.0001 + Random() * 10.0, REFRESH, NULL, 0);
			break;
		}

		case LP_FINI:
			if (state) {
				atomic_fetch_add_explicit(&total_saved, state->saved, memory_order_relaxed);
				atomic_fetch_add_explicit(&total_deaths, state->deaths, memory_order_relaxed);
				if (lp_saved && lp_deaths && me < lp_stats_count) {
					lp_saved[me] = state->saved;
					lp_deaths[me] = state->deaths;
				}
				pthread_mutex_lock(&outcome_lock);
				if (state->last_outcome_time > max_outcome_time)
					max_outcome_time = state->last_outcome_time;
				pthread_mutex_unlock(&outcome_lock);
				rs_free(state);
			}
			break;

		case VISIT:
			if (state && event_content)
				person_on_visit(now, state, (unsigned)me, *(const agent_t *)event_content);
			break;

		case LEAVE:
			if (state && event_content)
				person_on_leave(now, state, (unsigned)me, *(const agent_t *)event_content);
			break;

		case SAVE:
			if (state && event_content) {
				KillAgent(*(const agent_t *)event_content);
				state->saved++;
				if (now > state->last_outcome_time)
					state->last_outcome_time = now;
				egress_record_outcome(now);
			}
			break;

		case REFRESH:
			if (!state)
				break;
			person_refresh_all(now, state, (unsigned)me);
			if (now > 10000.0 || (conf.termination_time > 0.0 && now >= conf.termination_time)) {
				state->exit = true;
			} else {
				ScheduleNewEvent(me, now + 0.0001 + Random() * 10.0, REFRESH, NULL, 0);
			}
			break;

		case EXPLOSION:
			if (state && event_content)
				person_explosion(now, state, (unsigned)me, *(const unsigned *)event_content);
			break;

		case _TRAVERSE:
		default:
			break;
	}
}

bool CanEnd(lp_id_t me, const void *snapshot)
{
	(void)me;
	if (!snapshot)
		return true;

	return ((const region_t *)snapshot)->exit;
}

enum {
	OPT_CONFP = 2000,
	OPT_AVGAGE,
	OPT_GROUPP,
	OPT_ENVP,
	OPT_TRUSTP,
	OPT_PSYCHOP,
	OPT_TOPOLOGY,
	OPT_ESCAPE_STATS
};

static bool egress_extra_handler(int c, const char *arg, void *user_data)
{
	(void)user_data;
	switch (c) {
		case OPT_CONFP:
			confusion_p = strtod(arg, NULL);
			return true;
		case OPT_AVGAGE:
			avg_age = strtod(arg, NULL);
			return true;
		case OPT_GROUPP:
			group_p = strtod(arg, NULL);
			return true;
		case OPT_ENVP:
			env_knowledge_p = strtod(arg, NULL);
			return true;
		case OPT_TRUSTP:
			trust_p = strtod(arg, NULL);
			return true;
		case OPT_PSYCHOP:
			psycho_infl_p = strtod(arg, NULL);
			return true;
		case OPT_TOPOLOGY:
		case 'T':
			topology_path = arg;
			return true;
		case OPT_ESCAPE_STATS:
			escape_stats_path = arg;
			return true;
		default:
			return false;
	}
}

static void egress_extra_usage(void)
{
	printf("\nEgress Specific Options:\n");
	printf("  --confusion-p <DOUBLE>     Confusion probability (default: %.2f)\n", DEF_CONFUSION_P);
	printf("  --avg-age <DOUBLE>         Average agent age (default: %.1f)\n", (double)DEF_AVG_AGE);
	printf("  --group-p <DOUBLE>         Group membership probability (default: %.2f)\n", DEF_GROUP_P);
	printf("  --env-p <DOUBLE>           Environment knowledge probability (default: %.2f)\n",
	       DEF_ENV_KNOWLEDGE_P);
	printf("  --trust-p <DOUBLE>         Trust probability (default: %.2f)\n", DEF_TRUST_P);
	printf("  --psycho-p <DOUBLE>        Psycho influence probability (default: %.2f)\n", DEF_PSYCHO_INFL_P);
	printf("  -T, --topology <FILE>      Path to JSON obstacles topology file\n");
	printf("  --escape-stats <FILE>      Write per-LP saved/deaths statistics to FILE\n");
}

int main(int argc, char **argv)
{
	struct model_cli_options opt;
	init_default_cli_options(&opt, EDGE_LEN * EDGE_LEN, 10000);

	static const struct option egress_options[] = {
	    {"confusion-p", required_argument, 0, OPT_CONFP},
	    {"avg-age", required_argument, 0, OPT_AVGAGE},
	    {"group-p", required_argument, 0, OPT_GROUPP},
	    {"env-p", required_argument, 0, OPT_ENVP},
	    {"trust-p", required_argument, 0, OPT_TRUSTP},
	    {"psycho-p", required_argument, 0, OPT_PSYCHOP},
	    {"topology", required_argument, 0, OPT_TOPOLOGY},
	    {"escape-stats", required_argument, 0, OPT_ESCAPE_STATS},
	    {0, 0, 0, 0}
	};

	parse_model_cli_options_custom(argc, argv, &opt, &conf, egress_options, "T:", egress_extra_handler, NULL,
	                               egress_extra_usage);

	if (topology_path == NULL && optind < argc) {
		topology_path = argv[optind];
	}

	double *obstacles = NULL;
	unsigned int regions_count = 0;
	enum topology_geometry geom = TOPOLOGY_HEXAGON;
	load_topology_json(topology_path, &obstacles, &regions_count, &geom);

	conf.lps = regions_count;
	unsigned int side = (unsigned int)sqrt((double)conf.lps);
	if (side * side != conf.lps) {
		side = (unsigned int)ceil(sqrt((double)conf.lps));
		conf.lps = side * side;
	}

	if (escape_stats_path != NULL) {
		lp_stats_count = (unsigned)conf.lps;
		lp_saved = calloc(lp_stats_count, sizeof(unsigned));
		lp_deaths = calloc(lp_stats_count, sizeof(unsigned));
		if (!lp_saved || !lp_deaths) {
			perror("calloc");
			exit(EXIT_FAILURE);
		}
	}

	abm_init_simulation(&conf, geom, side, side, 0, ProcessEvent, CanEnd);
	abm_set_obstacles(obstacles, regions_count);
	free(obstacles);

	RootsimInit(&conf);
	int ret = RootsimRun();

	unsigned saved = atomic_load_explicit(&total_saved, memory_order_relaxed);
	unsigned deaths = atomic_load_explicit(&total_deaths, memory_order_relaxed);
	unsigned total = saved + deaths;

	simtime_t t_end = max_outcome_time;
	simtime_t t95 = t_end;
	if (outcome_count > 0) {
		qsort(outcome_times, outcome_count, sizeof(simtime_t), cmp_simtime);
		size_t target95 = (size_t)ceil(0.95 * (double)total);
		if (target95 > 0 && target95 <= outcome_count) {
			t95 = outcome_times[target95 - 1];
		} else {
			t95 = outcome_times[(size_t)(0.95 * (double)outcome_count)];
		}
	}

	if (escape_stats_path != NULL && lp_saved && lp_deaths) {
		FILE *f = fopen(escape_stats_path, "w");
		if (f) {
			for (unsigned i = 0; i < lp_stats_count; i++) {
				fprintf(f, "%u %u %u\n", i, lp_saved[i], lp_deaths[i]);
			}
			fclose(f);
		} else {
			perror(escape_stats_path);
		}
	}

	free(lp_saved);
	free(lp_deaths);
	free(outcome_times);

	printf("Egress simulation finished: saved=%u, deaths=%u, total=%u, t95=%.6f, t_end=%.6f (exit code %d)\n",
	       saved, deaths, total, t95, t_end, ret);
	return ret;
}
