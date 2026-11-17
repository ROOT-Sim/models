/**
 * @file colliding_pucks/geometry.h
 *
 * @brief 2D geometric and kinematic operations for Colliding Pucks
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdbool.h>
#include <math.h>

struct vector2d {
    double x;
    double y;
};

struct puck {
    unsigned int id;
    double x;
    double y;
    double vx;
    double vy;
    double radius;
    double mass;
    double t_ref;
    unsigned int owner_lp;
};

struct line_segment {
    double x1, y1;
    double x2, y2;
};

struct point_obstacle {
    double x, y;
};

static inline struct vector2d v2d_add(struct vector2d a, struct vector2d b)
{
    return (struct vector2d){a.x + b.x, a.y + b.y};
}

static inline struct vector2d v2d_sub(struct vector2d a, struct vector2d b)
{
    return (struct vector2d){a.x - b.x, a.y - b.y};
}

static inline struct vector2d v2d_scale(struct vector2d a, double s)
{
    return (struct vector2d){a.x * s, a.y * s};
}

static inline double v2d_dot(struct vector2d a, struct vector2d b)
{
    return a.x * b.x + a.y * b.y;
}

static inline double v2d_length_sq(struct vector2d a)
{
    return a.x * a.x + a.y * a.y;
}

static inline double v2d_length(struct vector2d a)
{
    return sqrt(v2d_length_sq(a));
}

void puck_position_at(const struct puck *p, double t, double *out_x, double *out_y);
void puck_advance_to(struct puck *p, double t);

bool predict_puck_puck_collision(const struct puck *p1, const struct puck *p2, double now, double *out_t);

void resolve_elastic_collision(struct puck *p1, struct puck *p2, double t);

bool predict_puck_cushion_collision(const struct puck *p, double now,
                                    double x_min, double y_min, double x_max, double y_max,
                                    double *out_t, int *out_wall);

void resolve_cushion_bounce(struct puck *p, double t, int wall);

bool predict_sector_exit(const struct puck *p, double now,
                         double sec_x_min, double sec_y_min, double sec_x_max, double sec_y_max,
                         double *out_t, int *out_direction);
