/**
 * @file sharks/sector_geom.c
 *
 * @brief Torus distance and boundary crossing calculations for Sharks World
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "sector_geom.h"

#include <float.h>
#include <math.h>

double torus_wrap(double val, double max_val)
{
    double w = fmod(val, max_val);
    if (w < 0.0) w += max_val;
    return w;
}

double torus_dist_sq(double x1, double y1, double x2, double y2, double world_size)
{
    double dx = fabs(x1 - x2);
    if (dx > world_size * 0.5) dx = world_size - dx;
    double dy = fabs(y1 - y2);
    if (dy > world_size * 0.5) dy = world_size - dy;
    return dx * dx + dy * dy;
}

double torus_dist(double x1, double y1, double x2, double y2, double world_size)
{
    return sqrt(torus_dist_sq(x1, y1, x2, y2, world_size));
}

void creature_position_at(const struct creature *c, double t, double world_size,
                          double *out_x, double *out_y)
{
    double dt = t - c->t_ref;
    *out_x = torus_wrap(c->x + c->vx * dt, world_size);
    *out_y = torus_wrap(c->y + c->vy * dt, world_size);
}

void creature_advance_to(struct creature *c, double t, double world_size)
{
    creature_position_at(c, t, world_size, &c->x, &c->y);
    c->t_ref = t;
}

bool predict_creature_box_exit(const struct creature *c, double now,
                               double x0, double y0, double x1, double y1,
                               double *out_t, int *out_dir)
{
    double min_dt = DBL_MAX;
    int best_dir = -1;

    double dt_ref = now - c->t_ref;
    double cx = c->x + c->vx * dt_ref;
    double cy = c->y + c->vy * dt_ref;

    // Direction 0: West (crosses x0)
    if (c->vx < -1e-9) {
        double dt = (x0 - cx) / c->vx;
        if (dt > 1e-6 && dt < min_dt) {
            min_dt = dt;
            best_dir = 0;
        }
    }

    // Direction 1: East (crosses x1)
    if (c->vx > 1e-9) {
        double dt = (x1 - cx) / c->vx;
        if (dt > 1e-6 && dt < min_dt) {
            min_dt = dt;
            best_dir = 1;
        }
    }

    // Direction 2: South (crosses y0)
    if (c->vy < -1e-9) {
        double dt = (y0 - cy) / c->vy;
        if (dt > 1e-6 && dt < min_dt) {
            min_dt = dt;
            best_dir = 2;
        }
    }

    // Direction 3: North (crosses y1)
    if (c->vy > 1e-9) {
        double dt = (y1 - cy) / c->vy;
        if (dt > 1e-6 && dt < min_dt) {
            min_dt = dt;
            best_dir = 3;
        }
    }

    if (best_dir >= 0) {
        *out_t = now + min_dt;
        *out_dir = best_dir;
        return true;
    }
    return false;
}

bool predict_shark_fish_attack(const struct creature *shark,
                               const struct creature *fish,
                               double now, double world_size,
                               double attack_range,
                               double *out_t)
{
    double sx, sy, fx, fy;
    creature_position_at(shark, now, world_size, &sx, &sy);
    creature_position_at(fish, now, world_size, &fx, &fy);

    double dx = fx - sx;
    if (dx > world_size * 0.5) dx -= world_size;
    else if (dx < -world_size * 0.5) dx += world_size;

    double dy = fy - sy;
    if (dy > world_size * 0.5) dy -= world_size;
    else if (dy < -world_size * 0.5) dy += world_size;

    double d0_sq = dx * dx + dy * dy;
    double r_sq = attack_range * attack_range;

    if (d0_sq <= r_sq) {
        return false;
    }

    double dvx = fish->vx - shark->vx;
    double dvy = fish->vy - shark->vy;

    double a = dvx * dvx + dvy * dvy;
    if (a < 1e-12) {
        return false;
    }

    double b = 2.0 * (dx * dvx + dy * dvy);
    if (b >= 0.0) {
        // Diverging or relative stationary
        return false;
    }

    double c = d0_sq - r_sq;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        return false;
    }

    double dt = (-b - sqrt(disc)) / (2.0 * a);
    if (dt <= 1e-6) {
        return false;
    }

    *out_t = now + dt;
    return true;
}
