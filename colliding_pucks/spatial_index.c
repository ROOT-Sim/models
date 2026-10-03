/**
 * @file colliding_pucks/spatial_index.c
 *
 * @brief Candidate pair generator for naive, fixed, and dynamic quadtree
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "spatial_index.h"

#include <stdlib.h>
#include <string.h>

#define MAX_QUADTREE_DEPTH 8
#define FIXED_GRID_DIM 4

struct qnode {
    double x0, y0, x1, y1;
    unsigned int *indices;
    unsigned int count;
    unsigned int capacity;
    bool is_leaf;
    struct qnode *children[4];
};

static struct qnode *qnode_create(double x0, double y0, double x1, double y1)
{
    struct qnode *node = malloc(sizeof(*node));
    if (!node) return NULL;
    node->x0 = x0;
    node->y0 = y0;
    node->x1 = x1;
    node->y1 = y1;
    node->count = 0;
    node->capacity = 16;
    node->indices = malloc(sizeof(unsigned int) * node->capacity);
    node->is_leaf = true;
    for (int i = 0; i < 4; i++) {
        node->children[i] = NULL;
    }
    return node;
}

static void qnode_free(struct qnode *node)
{
    if (!node) return;
    if (!node->is_leaf) {
        for (int i = 0; i < 4; i++) {
            qnode_free(node->children[i]);
        }
    }
    free(node->indices);
    free(node);
}

static void qnode_add_puck(struct qnode *node, unsigned int idx)
{
    if (node->count >= node->capacity) {
        node->capacity *= 2;
        node->indices = realloc(node->indices, sizeof(unsigned int) * node->capacity);
    }
    node->indices[node->count++] = idx;
}

static void qnode_subdivide(struct qnode *node,
                            const struct puck *pucks,
                            double now,
                            unsigned int max_per_cell,
                            unsigned int depth,
                            struct spatial_stats *stats)
{
    if (depth >= MAX_QUADTREE_DEPTH || node->count <= max_per_cell) {
        return;
    }

    double mid_x = (node->x0 + node->x1) * 0.5;
    double mid_y = (node->y0 + node->y1) * 0.5;

    node->children[0] = qnode_create(node->x0, node->y0, mid_x, mid_y);       // SW
    node->children[1] = qnode_create(mid_x, node->y0, node->x1, mid_y);       // SE
    node->children[2] = qnode_create(node->x0, mid_y, mid_x, node->y1);       // NW
    node->children[3] = qnode_create(mid_x, mid_y, node->x1, node->y1);       // NE
    node->is_leaf = false;
    if (stats) stats->subdivisions++;

    for (unsigned int k = 0; k < node->count; k++) {
        unsigned int idx = node->indices[k];
        double px, py;
        puck_position_at(&pucks[idx], now, &px, &py);

        int child_idx = 0;
        if (px >= mid_x) child_idx |= 1;
        if (py >= mid_y) child_idx |= 2;

        qnode_add_puck(node->children[child_idx], idx);
    }

    // Recurse on children
    for (int i = 0; i < 4; i++) {
        if (node->children[i]->count > max_per_cell) {
            qnode_subdivide(node->children[i], pucks, now, max_per_cell, depth + 1, stats);
        }
    }
}

static void add_pair(struct candidate_pair *pairs, unsigned int *count, unsigned int max_pairs,
                     unsigned int i, unsigned int j)
{
    if (*count >= max_pairs) return;
    if (i > j) {
        unsigned int tmp = i;
        i = j;
        j = tmp;
    }
    // Prevent duplicate entries
    for (unsigned int k = 0; k < *count; k++) {
        if (pairs[k].i == i && pairs[k].j == j) {
            return;
        }
    }
    pairs[*count].i = i;
    pairs[*count].j = j;
    (*count)++;
}

static void collect_pairs_from_leaf(struct qnode *node,
                                    struct candidate_pair *out_pairs,
                                    unsigned int *pair_count,
                                    unsigned int max_pairs,
                                    struct spatial_stats *stats)
{
    if (node->is_leaf) {
        for (unsigned int i = 0; i < node->count; i++) {
            for (unsigned int j = i + 1; j < node->count; j++) {
                if (stats) stats->pair_checks++;
                add_pair(out_pairs, pair_count, max_pairs, node->indices[i], node->indices[j]);
            }
        }
    } else {
        for (int c = 0; c < 4; c++) {
            collect_pairs_from_leaf(node->children[c], out_pairs, pair_count, max_pairs, stats);
        }
    }
}

unsigned int find_collision_candidates(enum index_strategy strategy,
                                       const struct puck *pucks,
                                       unsigned int n_pucks,
                                       double now,
                                       double x_min, double y_min,
                                       double x_max, double y_max,
                                       unsigned int max_per_cell,
                                       struct candidate_pair *out_pairs,
                                       unsigned int max_pairs,
                                       struct spatial_stats *stats)
{
    unsigned int count = 0;
    if (n_pucks < 2) return 0;

    if (strategy == INDEX_NAIVE) {
        for (unsigned int i = 0; i < n_pucks; i++) {
            for (unsigned int j = i + 1; j < n_pucks; j++) {
                if (stats) stats->pair_checks++;
                if (count < max_pairs) {
                    out_pairs[count].i = i;
                    out_pairs[count].j = j;
                    count++;
                }
            }
        }
        if (stats) stats->candidates_found += count;
        return count;
    }

    if (strategy == INDEX_FIXED) {
        // Uniform grid of FIXED_GRID_DIM x FIXED_GRID_DIM cells
        unsigned int grid_dim = FIXED_GRID_DIM;
        double cell_w = (x_max - x_min) / (double)grid_dim;
        double cell_h = (y_max - y_min) / (double)grid_dim;
        if (cell_w <= 0.0) cell_w = 1.0;
        if (cell_h <= 0.0) cell_h = 1.0;

        unsigned int cell_counts[FIXED_GRID_DIM][FIXED_GRID_DIM];
        unsigned int *cell_pucks[FIXED_GRID_DIM][FIXED_GRID_DIM];
        memset(cell_counts, 0, sizeof(cell_counts));

        for (unsigned int r = 0; r < grid_dim; r++) {
            for (unsigned int c = 0; c < grid_dim; c++) {
                cell_pucks[r][c] = malloc(sizeof(unsigned int) * n_pucks);
            }
        }

        for (unsigned int k = 0; k < n_pucks; k++) {
            double px, py;
            puck_position_at(&pucks[k], now, &px, &py);
            int col = (int)((px - x_min) / cell_w);
            int row = (int)((py - y_min) / cell_h);
            if (col < 0) col = 0;
            if (col >= (int)grid_dim) col = (int)grid_dim - 1;
            if (row < 0) row = 0;
            if (row >= (int)grid_dim) row = (int)grid_dim - 1;

            cell_pucks[row][col][cell_counts[row][col]++] = k;
        }

        // Pair within same cell and forward adjacent cells
        int drow[4] = {0, 1, 1, 1};
        int dcol[4] = {1, 0, 1, -1};

        for (unsigned int r = 0; r < grid_dim; r++) {
            for (unsigned int c = 0; c < grid_dim; c++) {
                unsigned int n_in_cell = cell_counts[r][c];
                // Intra-cell pairs
                for (unsigned int i = 0; i < n_in_cell; i++) {
                    for (unsigned int j = i + 1; j < n_in_cell; j++) {
                        if (stats) stats->pair_checks++;
                        add_pair(out_pairs, &count, max_pairs,
                                 cell_pucks[r][c][i], cell_pucks[r][c][j]);
                    }
                }
                // Inter-cell pairs
                for (int dir = 0; dir < 4; dir++) {
                    int nr = (int)r + drow[dir];
                    int nc = (int)c + dcol[dir];
                    if (nr >= 0 && nr < (int)grid_dim && nc >= 0 && nc < (int)grid_dim) {
                        unsigned int n_neigh = cell_counts[nr][nc];
                        for (unsigned int i = 0; i < n_in_cell; i++) {
                            for (unsigned int j = 0; j < n_neigh; j++) {
                                if (stats) stats->pair_checks++;
                                add_pair(out_pairs, &count, max_pairs,
                                         cell_pucks[r][c][i], cell_pucks[nr][nc][j]);
                            }
                        }
                    }
                }
            }
        }

        for (unsigned int r = 0; r < grid_dim; r++) {
            for (unsigned int c = 0; c < grid_dim; c++) {
                free(cell_pucks[r][c]);
            }
        }
        if (stats) stats->candidates_found += count;
        return count;
    }

    // INDEX_DYNAMIC: adaptive quadtree
    struct qnode *root = qnode_create(x_min, y_min, x_max, y_max);
    for (unsigned int k = 0; k < n_pucks; k++) {
        qnode_add_puck(root, k);
    }
    qnode_subdivide(root, pucks, now, max_per_cell, 0, stats);
    collect_pairs_from_leaf(root, out_pairs, &count, max_pairs, stats);
    qnode_free(root);

    if (stats) stats->candidates_found += count;
    return count;
}
