/**
 * @file logic_circuit/bench_parser.c
 *
 * @brief Implementation of ISCAS-85/89 .bench netlist parser
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "bench_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

static char *trim_whitespace(char *str)
{
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int find_or_add_node(struct circuit_graph *graph, uint32_t *capacity, const char *name)
{
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        if (strcmp(graph->nodes[i].name, name) == 0) {
            return (int)i;
        }
    }

    if (graph->num_nodes >= *capacity) {
        *capacity *= 2;
        graph->nodes = realloc(graph->nodes, sizeof(struct gate_info) * (*capacity));
        if (!graph->nodes) {
            fprintf(stderr, "Out of memory allocating gate nodes\n");
            abort();
        }
    }

    uint32_t idx = graph->num_nodes++;
    memset(&graph->nodes[idx], 0, sizeof(struct gate_info));
    strncpy(graph->nodes[idx].name, name, MAX_NAME_LEN - 1);
    graph->nodes[idx].type = GATE_BUF; // Default
    return (int)idx;
}

static enum gate_type parse_type(const char *tname)
{
    if (strcasecmp(tname, "AND") == 0) return GATE_AND;
    if (strcasecmp(tname, "NAND") == 0) return GATE_NAND;
    if (strcasecmp(tname, "OR") == 0) return GATE_OR;
    if (strcasecmp(tname, "NOR") == 0) return GATE_NOR;
    if (strcasecmp(tname, "XOR") == 0) return GATE_XOR;
    if (strcasecmp(tname, "XNOR") == 0) return GATE_XNOR;
    if (strcasecmp(tname, "NOT") == 0) return GATE_NOT;
    if (strcasecmp(tname, "BUF") == 0 || strcasecmp(tname, "BUFF") == 0) return GATE_BUF;
    if (strcasecmp(tname, "DFF") == 0) return GATE_DFF;
    return GATE_BUF;
}

static bool parse_stream(FILE *fp, struct circuit_graph *graph)
{
    uint32_t capacity = INITIAL_CAPACITY;
    graph->num_nodes = 0;
    graph->num_inputs = 0;
    graph->num_outputs = 0;
    graph->nodes = malloc(sizeof(struct gate_info) * capacity);
    if (!graph->nodes) return false;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim_whitespace(line);
        if (*p == '#' || *p == '\0') continue;

        // Check for INPUT(...)
        if (strncasecmp(p, "INPUT", 5) == 0) {
            char *open_p = strchr(p, '(');
            char *close_p = strrchr(p, ')');
            if (open_p && close_p && close_p > open_p) {
                *close_p = '\0';
                char *name = trim_whitespace(open_p + 1);
                int idx = find_or_add_node(graph, &capacity, name);
                graph->nodes[idx].is_primary_input = true;
                graph->nodes[idx].type = GATE_INPUT;
            }
            continue;
        }

        // Check for OUTPUT(...)
        if (strncasecmp(p, "OUTPUT", 6) == 0) {
            char *open_p = strchr(p, '(');
            char *close_p = strrchr(p, ')');
            if (open_p && close_p && close_p > open_p) {
                *close_p = '\0';
                char *name = trim_whitespace(open_p + 1);
                int idx = find_or_add_node(graph, &capacity, name);
                graph->nodes[idx].is_primary_output = true;
            }
            continue;
        }

        // Check for Gate: out = TYPE(in1, in2, ...)
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *out_name = trim_whitespace(p);
            char *rhs = trim_whitespace(eq + 1);

            char *open_p = strchr(rhs, '(');
            char *close_p = strrchr(rhs, ')');
            if (!open_p || !close_p || close_p <= open_p) continue;

            *open_p = '\0';
            *close_p = '\0';
            char *type_name = trim_whitespace(rhs);
            char *args = open_p + 1;

            int out_idx = find_or_add_node(graph, &capacity, out_name);
            graph->nodes[out_idx].type = parse_type(type_name);

            // Parse inputs separated by commas
            char *token = strtok(args, ",");
            uint32_t in_count = 0;
            while (token && in_count < MAX_GATE_INPUTS) {
                char *in_name = trim_whitespace(token);
                if (*in_name) {
                    strncpy(graph->nodes[out_idx].input_names[in_count], in_name, MAX_NAME_LEN - 1);
                    find_or_add_node(graph, &capacity, in_name);
                    in_count++;
                }
                token = strtok(NULL, ",");
            }
            graph->nodes[out_idx].num_inputs = in_count;
        }
    }

    // Second pass: compute fanouts
    // For each node u, count how many gates have u in their input_names
    uint32_t *fanout_counts = calloc(graph->num_nodes, sizeof(uint32_t));
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        for (uint32_t p_in = 0; p_in < graph->nodes[i].num_inputs; p_in++) {
            const char *in_name = graph->nodes[i].input_names[p_in];
            for (uint32_t u = 0; u < graph->num_nodes; u++) {
                if (strcmp(graph->nodes[u].name, in_name) == 0) {
                    fanout_counts[u]++;
                    break;
                }
            }
        }
    }

    for (uint32_t u = 0; u < graph->num_nodes; u++) {
        graph->nodes[u].num_fanouts = 0;
        if (fanout_counts[u] > 0) {
            graph->nodes[u].fanouts = malloc(sizeof(struct gate_fanout) * fanout_counts[u]);
        } else {
            graph->nodes[u].fanouts = NULL;
        }
    }

    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        for (uint32_t p_in = 0; p_in < graph->nodes[i].num_inputs; p_in++) {
            const char *in_name = graph->nodes[i].input_names[p_in];
            for (uint32_t u = 0; u < graph->num_nodes; u++) {
                if (strcmp(graph->nodes[u].name, in_name) == 0) {
                    uint32_t f_idx = graph->nodes[u].num_fanouts++;
                    graph->nodes[u].fanouts[f_idx].dest_lp = (lp_id_t)i;
                    graph->nodes[u].fanouts[f_idx].dest_pin = p_in;
                    break;
                }
            }
        }
    }
    free(fanout_counts);

    // Build lists of primary inputs and outputs
    uint32_t pi_count = 0, po_count = 0;
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].is_primary_input) pi_count++;
        if (graph->nodes[i].is_primary_output) po_count++;
    }

    graph->num_inputs = pi_count;
    graph->num_outputs = po_count;
    graph->primary_inputs = malloc(sizeof(lp_id_t) * (pi_count > 0 ? pi_count : 1));
    graph->primary_outputs = malloc(sizeof(lp_id_t) * (po_count > 0 ? po_count : 1));

    uint32_t pi_idx = 0, po_idx = 0;
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].is_primary_input) {
            graph->primary_inputs[pi_idx++] = (lp_id_t)i;
        }
        if (graph->nodes[i].is_primary_output) {
            graph->primary_outputs[po_idx++] = (lp_id_t)i;
        }
    }

    return true;
}

bool parse_bench_file(const char *filepath, struct circuit_graph *graph)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        return false;
    }
    bool ok = parse_stream(fp, graph);
    fclose(fp);
    return ok;
}

static const char *c17_builtin_netlist =
    "# Built-in c17 benchmark\n"
    "INPUT(1)\n"
    "INPUT(2)\n"
    "INPUT(3)\n"
    "INPUT(6)\n"
    "INPUT(7)\n"
    "OUTPUT(22)\n"
    "OUTPUT(23)\n"
    "10 = NAND(1, 3)\n"
    "11 = NAND(2, 6)\n"
    "16 = NAND(10, 11)\n"
    "19 = NAND(11, 7)\n"
    "22 = NAND(10, 16)\n"
    "23 = NAND(16, 19)\n";

bool load_builtin_c17(struct circuit_graph *graph)
{
    FILE *fp = fmemopen((void *)c17_builtin_netlist, strlen(c17_builtin_netlist), "r");
    if (!fp) return false;
    bool ok = parse_stream(fp, graph);
    fclose(fp);
    return ok;
}

void free_circuit_graph(struct circuit_graph *graph)
{
    if (!graph || !graph->nodes) return;
    for (uint32_t i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].fanouts) {
            free(graph->nodes[i].fanouts);
            graph->nodes[i].fanouts = NULL;
        }
    }
    free(graph->nodes);
    graph->nodes = NULL;
    if (graph->primary_inputs) free(graph->primary_inputs);
    if (graph->primary_outputs) free(graph->primary_outputs);
    graph->primary_inputs = NULL;
    graph->primary_outputs = NULL;
    graph->num_nodes = 0;
}
