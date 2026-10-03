/**
 * @file raid5/raid.h
 *
 * @brief Discrete event RAID-5 disk array simulation model
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_DISKS 32
#define MAX_DISK_QUEUE 128
#define MAX_CONTROLLER_QUEUE 512

enum raid_event_type {
    EVENT_DISK_COMPLETION = 1,
    EVENT_DISK_ACCESS = 2,
    EVENT_IO_REQUEST = 3,
    EVENT_DISK_FAIL = 4
};

enum disk_op_type {
    OP_READ = 0,
    OP_WRITE = 1
};

struct disk_access_payload {
    uint64_t io_id;
    uint32_t disk_id;
    uint32_t cylinder;
    uint32_t sectors;
    enum disk_op_type op;
    simtime_t issue_time;
};

struct disk_completion_payload {
    uint64_t io_id;
    uint32_t disk_id;
    simtime_t issue_time;
    simtime_t completion_time;
};

struct raid_config {
    unsigned int num_disks;
    unsigned int stripe_unit_kb;
    double arrival_rate; // User IOPS
    double write_fraction;
    int failed_disk_id; // -1 for none
    double fail_time;
    double disk_rpm;
    double seek_a; // Lee & Katz seek curve parameters
    double seek_b;
    double seek_c;
    double transfer_rate_mb_s;
    bool check_theory;
};

struct disk_state {
    struct rng_t seed;
    simtime_t lvt;
    uint32_t disk_id;
    bool failed;

    uint32_t current_cylinder;
    bool busy;
    simtime_t busy_until;

    unsigned int queue_len;
    struct disk_access_payload queue[MAX_DISK_QUEUE];

    uint64_t total_accesses;
    uint64_t total_reads;
    uint64_t total_writes;
    double total_seek_time;
    double total_rot_time;
    double total_xfer_time;

    simtime_t last_stats_time;
    double area_disk_busy;
    double area_disk_queue;
};

struct user_io {
    uint64_t io_id;
    bool is_write;
    simtime_t arrival_time;
    uint32_t pending_sub_ios;
};

struct controller_state {
    struct rng_t seed;
    simtime_t lvt;

    uint64_t next_io_id;
    uint64_t total_user_ios;
    uint64_t user_reads;
    uint64_t user_writes;
    uint64_t completed_user_ios;
    uint64_t degraded_reads;
    double total_user_latency;

    unsigned int active_ios_count;
    struct user_io active_ios[MAX_CONTROLLER_QUEUE];
};

union raid_lp_state {
    struct controller_state ctrl;
    struct disk_state disk;
};

extern struct raid_config raid_cfg;
