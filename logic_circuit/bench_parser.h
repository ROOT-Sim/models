/**
 * @file logic_circuit/bench_parser.h
 *
 * @brief Parser for ISCAS-85/89 .bench netlists
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "circuit.h"
#include <stdbool.h>

bool parse_bench_file(const char *filepath, struct circuit_graph *graph);
bool load_builtin_c17(struct circuit_graph *graph);
void free_circuit_graph(struct circuit_graph *graph);
