/**
 * @file colliding_pucks/spatial_index.h
 *
 * @brief Spatial indexing strategies (naive, fixed-sector, dynamic quadtree)
 *
 * Implements the algorithms evaluated in Harless & Rogers (WSC 1995)
 * for spatial candidate pruning.
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "geometry.h"

enum index_strategy {
    INDEX_NAIVE = 0,
    INDEX_FIXED = 1,
    INDEX_DYNAMIC = 2
};

struct candidate_pair {
    unsigned int i;
    unsigned int j;
};

struct spatial_stats {
    unsigned long long pair_checks;
    unsigned long long candidates_found;
    unsigned long long subdivisions;
};

unsigned int find_collision_candidates(enum index_strategy strategy,
                                       const struct puck *pucks,
                                       unsigned int n_pucks,
                                       double now,
                                       double x_min, double y_min,
                                       double x_max, double y_max,
                                       unsigned int max_per_cell,
                                       struct candidate_pair *out_pairs,
                                       unsigned int max_pairs,
                                       struct spatial_stats *stats);
