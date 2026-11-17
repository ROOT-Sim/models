/**
 * @file sharks/sector_geom.h
 *
 * @brief Torus geometry and boundary projection for Sharks World
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CREATURE_FISH  0
#define CREATURE_SHARK 1

struct creature {
    uint32_t id;
    uint8_t type;            // CREATURE_FISH or CREATURE_SHARK
    double x;
    double y;
    double vx;
    double vy;
    double speed;
    int angle_deg;           // 0..359
    double t_ref;
    uint32_t version;
    bool alive;
    uint32_t home_sector;
};

struct torus_box {
    double x_min;
    double y_min;
    double x_max;
    double y_max;
};

double torus_wrap(double val, double max_val);
double torus_dist_sq(double x1, double y1, double x2, double y2, double world_size);
double torus_dist(double x1, double y1, double x2, double y2, double world_size);

void creature_position_at(const struct creature *c, double t, double world_size,
                          double *out_x, double *out_y);
void creature_advance_to(struct creature *c, double t, double world_size);

bool predict_creature_box_exit(const struct creature *c, double now,
                               double x0, double y0, double x1, double y1,
                               double *out_t, int *out_dir);

bool predict_shark_fish_attack(const struct creature *shark,
                               const struct creature *fish,
                               double now, double world_size,
                               double attack_range,
                               double *out_t);
