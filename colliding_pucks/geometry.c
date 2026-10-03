/**
 * @file colliding_pucks/geometry.c
 *
 * @brief Kinematics and collision physics for Colliding Pucks
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "geometry.h"

#include <float.h>
#include <math.h>

void puck_position_at(const struct puck *p, double t, double *out_x, double *out_y)
{
    double dt = t - p->t_ref;
    *out_x = p->x + p->vx * dt;
    *out_y = p->y + p->vy * dt;
}

void puck_advance_to(struct puck *p, double t)
{
    puck_position_at(p, t, &p->x, &p->y);
    p->t_ref = t;
}

bool predict_puck_puck_collision(const struct puck *p1, const struct puck *p2, double now, double *out_t)
{
    double x1, y1, x2, y2;
    puck_position_at(p1, now, &x1, &y1);
    puck_position_at(p2, now, &x2, &y2);

    double dx = x1 - x2;
    double dy = y1 - y2;
    double dvx = p1->vx - p2->vx;
    double dvy = p1->vy - p2->vy;
    double radius_sum = p1->radius + p2->radius;

    double a = dvx * dvx + dvy * dvy;
    if (a < 1e-12) {
        return false;
    }

    double b = 2.0 * (dx * dvx + dy * dvy);
    if (b >= 0.0) {
        // Pucks are separating or stationary relative to each other
        return false;
    }

    double c = dx * dx + dy * dy - radius_sum * radius_sum;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        return false;
    }

    double dt = (-b - sqrt(disc)) / (2.0 * a);
    if (dt < -1e-6) {
        return false;
    }
    if (dt < 0.0) {
        dt = 0.0;
    }

    *out_t = now + dt;
    return true;
}

void resolve_elastic_collision(struct puck *p1, struct puck *p2, double t)
{
    puck_advance_to(p1, t);
    puck_advance_to(p2, t);

    double nx = p1->x - p2->x;
    double ny = p1->y - p2->y;
    double dist = sqrt(nx * nx + ny * ny);

    if (dist < 1e-12) {
        nx = 1.0;
        ny = 0.0;
    } else {
        nx /= dist;
        ny /= dist;
    }

    double v_rel = (p1->vx - p2->vx) * nx + (p1->vy - p2->vy) * ny;
    if (v_rel >= 0.0) {
        // Already separating
        return;
    }

    double impulse = -2.0 * v_rel / ((1.0 / p1->mass) + (1.0 / p2->mass));

    p1->vx += (impulse / p1->mass) * nx;
    p1->vy += (impulse / p1->mass) * ny;

    p2->vx -= (impulse / p2->mass) * nx;
    p2->vy -= (impulse / p2->mass) * ny;
}

bool predict_puck_cushion_collision(const struct puck *p, double now,
                                    double x_min, double y_min, double x_max, double y_max,
                                    double *out_t, int *out_wall)
{
    double x, y;
    puck_position_at(p, now, &x, &y);

    double min_dt = DBL_MAX;
    int best_wall = -1;

    // Wall 0: Left (x = x_min)
    if (p->vx < -1e-9) {
        double target_x = x_min + p->radius;
        double dt = (target_x - x) / p->vx;
        if (dt >= -1e-6 && dt < min_dt) {
            min_dt = (dt < 0.0) ? 0.0 : dt;
            best_wall = 0;
        }
    }

    // Wall 1: Right (x = x_max)
    if (p->vx > 1e-9) {
        double target_x = x_max - p->radius;
        double dt = (target_x - x) / p->vx;
        if (dt >= -1e-6 && dt < min_dt) {
            min_dt = (dt < 0.0) ? 0.0 : dt;
            best_wall = 1;
        }
    }

    // Wall 2: Bottom (y = y_min)
    if (p->vy < -1e-9) {
        double target_y = y_min + p->radius;
        double dt = (target_y - y) / p->vy;
        if (dt >= -1e-6 && dt < min_dt) {
            min_dt = (dt < 0.0) ? 0.0 : dt;
            best_wall = 2;
        }
    }

    // Wall 3: Top (y = y_max)
    if (p->vy > 1e-9) {
        double target_y = y_max - p->radius;
        double dt = (target_y - y) / p->vy;
        if (dt >= -1e-6 && dt < min_dt) {
            min_dt = (dt < 0.0) ? 0.0 : dt;
            best_wall = 3;
        }
    }

    if (best_wall >= 0) {
        *out_t = now + min_dt;
        *out_wall = best_wall;
        return true;
    }
    return false;
}

void resolve_cushion_bounce(struct puck *p, double t, int wall)
{
    puck_advance_to(p, t);
    switch (wall) {
        case 0: // Left
        case 1: // Right
            p->vx = -p->vx;
            break;
        case 2: // Bottom
        case 3: // Top
            p->vy = -p->vy;
            break;
        default:
            break;
    }
}

bool predict_sector_exit(const struct puck *p, double now,
                         double sec_x_min, double sec_y_min, double sec_x_max, double sec_y_max,
                         double *out_t, int *out_direction)
{
    double x, y;
    puck_position_at(p, now, &x, &y);

    double min_dt = DBL_MAX;
    int best_dir = -1;

    // West: x crosses sec_x_min
    if (p->vx < -1e-9) {
        double dt = (sec_x_min - x) / p->vx;
        if (dt > 1e-9 && dt < min_dt) {
            min_dt = dt;
            best_dir = 0; // West
        }
    }

    // East: x crosses sec_x_max
    if (p->vx > 1e-9) {
        double dt = (sec_x_max - x) / p->vx;
        if (dt > 1e-9 && dt < min_dt) {
            min_dt = dt;
            best_dir = 1; // East
        }
    }

    // South: y crosses sec_y_min
    if (p->vy < -1e-9) {
        double dt = (sec_y_min - y) / p->vy;
        if (dt > 1e-9 && dt < min_dt) {
            min_dt = dt;
            best_dir = 2; // South
        }
    }

    // North: y crosses sec_y_max
    if (p->vy > 1e-9) {
        double dt = (sec_y_max - y) / p->vy;
        if (dt > 1e-9 && dt < min_dt) {
            min_dt = dt;
            best_dir = 3; // North
        }
    }

    if (best_dir >= 0) {
        *out_t = now + min_dt;
        *out_direction = best_dir;
        return true;
    }
    return false;
}
