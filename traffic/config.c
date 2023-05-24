/**
 *
 * TRAFFIC is a simulation model for the ROme OpTimistic Simulator (ROOT-Sim)
 * which allows to simulate car traffic on generic routes, which can be
 * specified from text file.
 *
 * The software is provided as-is, with no guarantees, and is released under
 * the GNU GPL v3 (or higher).
 *
 * For any information, you can find contact information on my personal webpage:
 * http://www.dis.uniroma1.it/~pellegrini
 *
 * @file init.c
 * @brief This module implements the initialization functions
 * @author Alessandro Pellegrini
 * @date January 12, 2012
 *
 * SPDX-FileCopyrightText: 2008-2023 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "config.h"

#define JSMN_PARENT_LINKS
#define JSMN_STRICT
#define JSMN_STATIC
#include "jsmn.h"

#define PARSE_INT(str, strlen)                                                                                         \
	({                                                                                                             \
		char *endptr;                                                                                          \
		char *real_str = strndup(str, strlen);                                                                 \
		errno = 0;                                                                                             \
		long int parsed_value = strtol(real_str, &endptr, 10);                                                 \
		int result;                                                                                            \
		if(errno != 0 || *endptr != '\0') {                                                                    \
			result = -1; /* Return -1 to indicate an error */                                              \
		} else {                                                                                               \
			result = (int)parsed_value;                                                                    \
		}                                                                                                      \
		free(real_str);                                                                                        \
		result;                                                                                                \
	})

#define PARSE_DOUBLE(str, strlen)                                                                                      \
	({                                                                                                             \
		char *endptr;                                                                                          \
		char *real_str = strndup(str, strlen);                                                                 \
		errno = 0;                                                                                             \
		double parsed_value = strtod(real_str, &endptr);                                                       \
		double result;                                                                                         \
		if(errno != 0 || *endptr != '\0') {                                                                    \
			result = -1.0; /* Return -1.0 to indicate an error */                                          \
		} else {                                                                                               \
			result = parsed_value;                                                                         \
		}                                                                                                      \
		free(real_str);                                                                                        \
		result;                                                                                                \
	})

static jsmntok_t *tokens;
static int total_tokens;

uint64_t conf_num_nodes = 0;
static uint64_t conf_num_edges = 0;

static struct node_config *node_config;
static struct edge_config *edge_config;

static uint64_t count_configured_nodes = 0;
static uint64_t count_configured_edges = 0;

static inline void *xmalloc(size_t size)
{
	void *p = malloc(size);
	if(!p) {
		perror("malloc()");
		exit(EXIT_FAILURE);
	}
	return p;
}

static inline void *xrealloc(void *ptr, size_t size)
{
	void *p = realloc(ptr, size);
	if(!p) {
		free(ptr);
		perror("realloc()\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

static int jsoneq(const char *json, jsmntok_t *t, const char *s)
{
	if(t->type == JSMN_STRING && strlen(s) == t->end - t->start &&
	    strncmp(json + t->start, s, t->end - t->start) == 0) {
		return 0;
	}
	return -1;
}

static void process_nodes(const char *config, size_t tok, size_t count)
{
	int curr_node, processed = 0;
	double enter_freq, leave_prob;

	// nodes must be an object
	if(count < 1 || tokens[tok].type != JSMN_OBJECT) {
		fprintf(stderr, "Invalid nodes object: object expected.\n");
		exit(EXIT_FAILURE);
	}

	// Process each node
	size_t j;
	for(size_t i = tok + 1; processed < count; i = j, processed++) {
		// Ensure each node has the expected structure
		if(tokens[i].type != JSMN_STRING || tokens[i + 1].type != JSMN_OBJECT) {
			fprintf(stderr, "Invalid node: object expected.\n");
			exit(EXIT_FAILURE);
		}

		// Extract relevant information
		curr_node = PARSE_INT(config + tokens[i].start, tokens[i].end - tokens[i].start);
		for(j = i + 2; j < total_tokens && tokens[j].parent == i + 1; j += 2) {
			if(jsoneq(config, &tokens[j], "enter_freq") == 0) {
				enter_freq =
				    PARSE_DOUBLE(config + tokens[j + 1].start, tokens[j + 1].end - tokens[j + 1].start);
			} else if(jsoneq(config, &tokens[j], "leave_prob") == 0) {
				leave_prob =
				    PARSE_DOUBLE(config + tokens[j + 1].start, tokens[j + 1].end - tokens[j + 1].start);
			} else {
				fprintf(stderr, "Invalid node object: unexpected key '%.*s'.\n",
				    tokens[j].end - tokens[j].start, config + tokens[j].start);
				exit(EXIT_FAILURE);
			}
		}

		if(curr_node == -1 || enter_freq == -1.0 || leave_prob == -1.0) {
			fprintf(stderr, "Invalid node configuration (%d): enter=%f, leave=%f\n", curr_node, enter_freq,
			    leave_prob);
			exit(EXIT_FAILURE);
		}

		node_config[curr_node].enter_freq = enter_freq;
		node_config[curr_node].leave_prob = leave_prob;
	}
}

static void process_edges(const char *config, size_t tok, size_t count)
{
	int curr_edge, source, target, processed = 0;
	double length;

	// nodes must be an object
	if(count < 1 || tokens[tok].type != JSMN_OBJECT) {
		fprintf(stderr, "Invalid edges object: object expected.\n");
		exit(EXIT_FAILURE);
	}

	// Process each node
	size_t j;
	for(size_t i = tok + 1; processed < count; i = j, processed++) {
		// Ensure each node has the expected structure
		if(tokens[i].type != JSMN_STRING || tokens[i + 1].type != JSMN_OBJECT) {
			fprintf(stderr, "Invalid edge: object expected.\n");
			exit(EXIT_FAILURE);
		}

		// Extract relevant information
		curr_edge = PARSE_INT(config + tokens[i].start, tokens[i].end - tokens[i].start);
		for(j = i + 2; j < total_tokens && tokens[j].parent == i + 1; j += 2) {
			if(jsoneq(config, &tokens[j], "source") == 0) {
				source =
				    PARSE_INT(config + tokens[j + 1].start, tokens[j + 1].end - tokens[j + 1].start);
			} else if(jsoneq(config, &tokens[j], "target") == 0) {
				target =
				    PARSE_INT(config + tokens[j + 1].start, tokens[j + 1].end - tokens[j + 1].start);
			} else if(jsoneq(config, &tokens[j], "length") == 0) {
				length =
				    PARSE_DOUBLE(config + tokens[j + 1].start, tokens[j + 1].end - tokens[j + 1].start);
			} else {
				fprintf(stderr, "Invalid edge object: unexpected key '%.*s'.\n",
				    tokens[j].end - tokens[j].start, config + tokens[j].start);
				exit(EXIT_FAILURE);
			}
		}

		if(curr_edge == -1 || source == -1 || target == -1 || length == -1.0) {
			fprintf(stderr, "Invalid edge configuration (%d): source=%d, target=%d, length=%f\n", curr_edge,
			    source, target, length);
			exit(EXIT_FAILURE);
		}

		edge_config[curr_edge - conf_num_nodes].from = source;
		edge_config[curr_edge - conf_num_nodes].to = target;
		edge_config[curr_edge - conf_num_nodes].length = length;
	}
}

static void parse_main_level(const char *config, size_t count)
{
	// Top level must be an object
	jsmntok_t *t = tokens;
	if(count < 1 || t[0].type != JSMN_OBJECT) {
		fprintf(stderr, "Invalid configuration file: object expected.\n");
		exit(EXIT_FAILURE);
	}

	// Extract num_nodes and num_edges
	for(int i = 1; i < count; i++) {
		if(t[i].type == JSMN_STRING) {
			if(t[i].parent == 0 && jsoneq(config, &t[i], "num_nodes") == 0) {
				if(t[i + 1].type == JSMN_PRIMITIVE) {
					conf_num_nodes = PARSE_INT(config + tokens[i + 1].start,
					    tokens[i + 1].end - tokens[i + 1].start);
				}
				i++;
			} else if(t[i].parent == 0 && jsoneq(config, &t[i], "num_edges") == 0) {
				if(t[i + 1].type == JSMN_PRIMITIVE) {
					conf_num_edges = PARSE_INT(config + tokens[i + 1].start,
					    tokens[i + 1].end - tokens[i + 1].start);
				}
				i++;
			}
		}
	}

	// Check if num_nodes and num_edges were found
	if(conf_num_nodes == -1) {
		fprintf(stderr, "num_nodes not found.\n");
		exit(EXIT_FAILURE);
	}
	if(conf_num_edges == -1) {
		fprintf(stderr, "num_edges not found.\n");
		exit(EXIT_FAILURE);
	}

	// Allocate temporary configuration structures
	node_config = xmalloc(sizeof(*node_config) * conf_num_nodes);
	edge_config = xmalloc(sizeof(*edge_config) * conf_num_edges);

	// Find nodes and edges
	for(int i = 1; i < count; i++) {
		if(t[i].parent == 0 && t[i].type == JSMN_STRING) {
			if(jsoneq(config, &t[i], "nodes") == 0) {
				process_nodes(config, i + 1, t[i + 1].size);
				i += t[i + 1].size + 1;
			} else if(jsoneq(config, &t[i], "edges") == 0) {
				process_edges(config, i + 1, t[i + 1].size);
				i += t[i + 1].size + 1;
			}
		}
	}
}

uint64_t process_configuration_file(FILE *f)
{
	size_t read;
	int eof_expected = 0;
	char *conf_file = NULL;
	size_t conf_file_len = 0;
	char tmp_buf[BUFSIZ];
	jsmn_parser p;
	size_t tok_count = 256;

	jsmn_init(&p);

	printf("Parsing configuration file... ");
	fflush(stdout);

	tokens = xmalloc(sizeof(*tokens) * tok_count);

	while(true) {
		read = fread(tmp_buf, 1, sizeof(tmp_buf), f);
		if(read == 0) {
			if(feof(f)) {
				if(eof_expected != 0) {
					break;
				} else {
					fprintf(stderr, "fread(): unexpected EOF\n");
					exit(EXIT_FAILURE);
				}
			} else if(ferror(f)) {
				perror("fread() error");
				exit(EXIT_FAILURE);
			}
		}

		conf_file = xrealloc(conf_file, conf_file_len + read + 1);
		strncpy(conf_file + conf_file_len, tmp_buf, read);
		conf_file_len = conf_file_len + read;

again:
		total_tokens = jsmn_parse(&p, conf_file, conf_file_len, tokens, tok_count);
		if(total_tokens < 0) {
			if(total_tokens == JSMN_ERROR_NOMEM) {
				tok_count = tok_count * 2;
				tokens = xrealloc(tokens, sizeof(*tokens) * tok_count);
				goto again;
			}
		} else {
			parse_main_level(conf_file, p.toknext);
			eof_expected = 1;
		}
	}

	printf("done\n");

	free(tokens);
	free(conf_file);

	return conf_num_nodes + conf_num_edges;
}


void get_node_config(lp_id_t me, struct node_config *c)
{
	memcpy(c, &node_config[me], sizeof(node_config[me]));
	if(++count_configured_nodes == conf_num_nodes)
		free(node_config);
}

void get_edge_config(lp_id_t me, struct edge_config *c)
{
	memcpy(c, &edge_config[me], sizeof(edge_config[me]));
	if(++count_configured_edges == conf_num_edges)
		free(edge_config);
}
