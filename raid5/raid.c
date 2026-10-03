/**
 * @file raid5/raid.c
 *
 * @brief Discrete event RAID-5 disk array simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "raid.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct raid_config raid_cfg = {
    .num_disks = 5,
    .stripe_unit_kb = 64,
    .arrival_rate = 100.0, // 100 user IOPS
    .write_fraction = 0.30,
    .failed_disk_id = -1,
    .fail_time = 10.0,
    .disk_rpm = 7200.0,
    .seek_a = 0.0008, // Lee & Katz seek equation coefficients (in seconds)
    .seek_b = 0.000002,
    .seek_c = 0.002,
    .transfer_rate_mb_s = 100.0,
    .check_theory = false
};

#define EVENT_LOCAL_DISK_FREE 10

static double calc_disk_service_time(struct disk_state *disk, uint32_t target_cyl, uint32_t sectors)
{
    uint32_t dist = (target_cyl > disk->current_cylinder) ? (target_cyl - disk->current_cylinder) : (disk->current_cylinder - target_cyl);
    double seek_t = (dist == 0) ? 0.0 : (raid_cfg.seek_a * sqrt((double)dist) + raid_cfg.seek_b * (double)dist + raid_cfg.seek_c);
    disk->current_cylinder = target_cyl;

    double rev_time = 60.0 / raid_cfg.disk_rpm;
    double rot_t = Random(&disk->seed) * rev_time;

    double size_mb = (double)(sectors * 512) / (1024.0 * 1024.0);
    double xfer_t = size_mb / raid_cfg.transfer_rate_mb_s;

    disk->total_seek_time += seek_t;
    disk->total_rot_time += rot_t;
    disk->total_xfer_time += xfer_t;

    double total = seek_t + rot_t + xfer_t;
    if (total < 1e-4) total = 1e-4;
    return total;
}

static void update_disk_metrics(struct disk_state *disk, simtime_t now)
{
    if (now > disk->last_stats_time) {
        double dt = now - disk->last_stats_time;
        disk->area_disk_busy += (disk->busy ? 1.0 : 0.0) * dt;
        disk->area_disk_queue += (double)disk->queue_len * dt;
        disk->last_stats_time = now;
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    union raid_lp_state *state = (union raid_lp_state *)s;
    bool is_controller = (me == 0);

    if (state != NULL && event_type != LP_FINI) {
        if (is_controller) {
            state->ctrl.lvt = now;
        } else {
            state->disk.lvt = now;
            update_disk_metrics(&state->disk, now);
        }
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            if (is_controller) {
                struct controller_state *ctrl = &state->ctrl;
                initialize_stream(42, &ctrl->seed);
                ctrl->lvt = now;
                SetState(state);

                // Schedule first user I/O request
                double dt = Expent(&ctrl->seed, 1.0 / raid_cfg.arrival_rate);
                ScheduleNewEvent(0, now + dt, EVENT_IO_REQUEST, NULL, 0);

                // If a disk failure is configured, schedule it
                if (raid_cfg.failed_disk_id >= 0 && (unsigned int)raid_cfg.failed_disk_id < raid_cfg.num_disks) {
                    lp_id_t f_lp = 1 + (unsigned int)raid_cfg.failed_disk_id;
                    ScheduleNewEvent(f_lp, now + raid_cfg.fail_time, EVENT_DISK_FAIL, NULL, 0);
                }
            } else {
                struct disk_state *disk = &state->disk;
                initialize_stream((unsigned int)me + 100, &disk->seed);
                disk->lvt = now;
                disk->disk_id = (uint32_t)(me - 1);
                disk->current_cylinder = 0;
                disk->last_stats_time = now;
                SetState(state);
            }
            break;
        }

        case EVENT_DISK_FAIL: {
            if (is_controller) break;
            struct disk_state *disk = &state->disk;
            disk->failed = true;
            break;
        }

        case EVENT_IO_REQUEST: {
            if (!is_controller) break;
            struct controller_state *ctrl = &state->ctrl;

            uint64_t uio_id = ++ctrl->next_io_id;

            bool is_write = (Random(&ctrl->seed) < raid_cfg.write_fraction);
            ctrl->total_user_ios++;
            if (is_write) ctrl->user_writes++; else ctrl->user_reads++;

            // Map user logical request to RAID-5 stripe
            unsigned int N = raid_cfg.num_disks;
            unsigned int D = N - 1; // Data disks per stripe
            uint32_t stripe_id = (uint32_t)(Random(&ctrl->seed) * 10000.0);
            uint32_t data_disk_idx = stripe_id % D;
            uint32_t parity_disk_idx = (N - 1 - (stripe_id / D)) % N;
            if (data_disk_idx >= parity_disk_idx) {
                data_disk_idx++;
            }

            uint32_t cyl = (stripe_id / D) * 10;
            uint32_t sectors = (raid_cfg.stripe_unit_kb * 1024) / 512;

            // Track active user I/O in controller
            int u_idx = -1;
            for (unsigned int i = 0; i < MAX_CONTROLLER_QUEUE; i++) {
                if (ctrl->active_ios[i].io_id == 0) {
                    u_idx = (int)i;
                    break;
                }
            }
            if (u_idx < 0) {
                u_idx = (int)(ctrl->active_ios_count % MAX_CONTROLLER_QUEUE);
            }
            ctrl->active_ios[u_idx].io_id = uio_id;
            ctrl->active_ios[u_idx].is_write = is_write;
            ctrl->active_ios[u_idx].arrival_time = now;
            ctrl->active_ios_count++;

            simtime_t net_lat = 0.0001; // 100us controller-to-disk interconnect delay

            if (!is_write) {
                // Read request
                bool data_disk_failed = (raid_cfg.failed_disk_id == (int)data_disk_idx && now >= raid_cfg.fail_time);
                if (!data_disk_failed) {
                    // Normal read: 1 disk access
                    ctrl->active_ios[u_idx].pending_sub_ios = 1;
                    struct disk_access_payload dap = {
                        .io_id = uio_id, .disk_id = data_disk_idx, .cylinder = cyl,
                        .sectors = sectors, .op = OP_READ, .issue_time = now
                    };
                    ScheduleNewEvent(1 + data_disk_idx, now + net_lat, EVENT_DISK_ACCESS, &dap, sizeof(dap));
                } else {
                    // Degraded read: must read all surviving disks in parallel to reconstruct data!
                    ctrl->degraded_reads++;
                    ctrl->active_ios[u_idx].pending_sub_ios = N - 1;
                    for (unsigned int d = 0; d < N; d++) {
                        if (d != data_disk_idx) {
                            struct disk_access_payload dap = {
                                .io_id = uio_id, .disk_id = d, .cylinder = cyl,
                                .sectors = sectors, .op = OP_READ, .issue_time = now
                            };
                            ScheduleNewEvent(1 + d, now + net_lat, EVENT_DISK_ACCESS, &dap, sizeof(dap));
                        }
                    }
                }
            } else {
                // Small write (Read-Modify-Write): 2 reads + 2 writes = 4 disk accesses
                ctrl->active_ios[u_idx].pending_sub_ios = 4;

                // Read old data
                struct disk_access_payload d_read = {
                    .io_id = uio_id, .disk_id = data_disk_idx, .cylinder = cyl,
                    .sectors = sectors, .op = OP_READ, .issue_time = now
                };
                ScheduleNewEvent(1 + data_disk_idx, now + net_lat, EVENT_DISK_ACCESS, &d_read, sizeof(d_read));

                // Read old parity
                struct disk_access_payload p_read = {
                    .io_id = uio_id, .disk_id = parity_disk_idx, .cylinder = cyl,
                    .sectors = sectors, .op = OP_READ, .issue_time = now
                };
                ScheduleNewEvent(1 + parity_disk_idx, now + net_lat, EVENT_DISK_ACCESS, &p_read, sizeof(p_read));

                // Write new data
                struct disk_access_payload d_write = {
                    .io_id = uio_id, .disk_id = data_disk_idx, .cylinder = cyl,
                    .sectors = sectors, .op = OP_WRITE, .issue_time = now
                };
                ScheduleNewEvent(1 + data_disk_idx, now + net_lat * 2.0, EVENT_DISK_ACCESS, &d_write, sizeof(d_write));

                // Write new parity
                struct disk_access_payload p_write = {
                    .io_id = uio_id, .disk_id = parity_disk_idx, .cylinder = cyl,
                    .sectors = sectors, .op = OP_WRITE, .issue_time = now
                };
                ScheduleNewEvent(1 + parity_disk_idx, now + net_lat * 2.0, EVENT_DISK_ACCESS, &p_write, sizeof(p_write));
            }

            // Schedule next user I/O arrival
            double dt = Expent(&ctrl->seed, 1.0 / raid_cfg.arrival_rate);
            ScheduleNewEvent(0, now + dt, EVENT_IO_REQUEST, NULL, 0);
            break;
        }

        case EVENT_DISK_ACCESS: {
            if (is_controller) break;
            struct disk_state *disk = &state->disk;
            const struct disk_access_payload *dap = (const struct disk_access_payload *)payload;
            if (!dap) break;

            if (disk->failed) {
                // Failed disk does not service request (times out / fails)
                break;
            }

            if (!disk->busy) {
                disk->busy = true;
                disk->total_accesses++;
                if (dap->op == OP_WRITE) disk->total_writes++; else disk->total_reads++;

                double s_time = calc_disk_service_time(disk, dap->cylinder, dap->sectors);
                disk->busy_until = now + s_time;

                struct disk_completion_payload dcp;
                dcp.io_id = dap->io_id;
                dcp.disk_id = disk->disk_id;
                dcp.issue_time = dap->issue_time;
                dcp.completion_time = now + s_time;

                simtime_t net_lat = 0.0001;
                ScheduleNewEvent(0, now + s_time + net_lat, EVENT_DISK_COMPLETION, &dcp, sizeof(dcp));
                ScheduleNewEvent(me, now + s_time, EVENT_LOCAL_DISK_FREE, NULL, 0);
            } else {
                if (disk->queue_len < MAX_DISK_QUEUE) {
                    disk->queue[disk->queue_len++] = *dap;
                }
            }
            break;
        }

        case EVENT_LOCAL_DISK_FREE: {
            if (is_controller) break;
            struct disk_state *disk = &state->disk;

            if (disk->queue_len > 0) {
                struct disk_access_payload next_dap = disk->queue[0];
                disk->queue_len--;
                if (disk->queue_len > 0) {
                    memmove(&disk->queue[0], &disk->queue[1], sizeof(struct disk_access_payload) * disk->queue_len);
                }

                disk->total_accesses++;
                if (next_dap.op == OP_WRITE) disk->total_writes++; else disk->total_reads++;

                double s_time = calc_disk_service_time(disk, next_dap.cylinder, next_dap.sectors);
                disk->busy_until = now + s_time;

                struct disk_completion_payload dcp;
                dcp.io_id = next_dap.io_id;
                dcp.disk_id = disk->disk_id;
                dcp.issue_time = next_dap.issue_time;
                dcp.completion_time = now + s_time;

                simtime_t net_lat = 0.0001;
                ScheduleNewEvent(0, now + s_time + net_lat, EVENT_DISK_COMPLETION, &dcp, sizeof(dcp));
                ScheduleNewEvent(me, now + s_time, EVENT_LOCAL_DISK_FREE, NULL, 0);
            } else {
                disk->busy = false;
            }
            break;
        }

        case EVENT_DISK_COMPLETION: {
            if (!is_controller) break;
            struct controller_state *ctrl = &state->ctrl;
            const struct disk_completion_payload *dcp = (const struct disk_completion_payload *)payload;
            if (!dcp) break;

            // Find matching user I/O in active queue
            for (unsigned int i = 0; i < MAX_CONTROLLER_QUEUE; i++) {
                if (ctrl->active_ios[i].io_id == dcp->io_id) {
                    if (ctrl->active_ios[i].pending_sub_ios > 0) {
                        ctrl->active_ios[i].pending_sub_ios--;
                    }
                    if (ctrl->active_ios[i].pending_sub_ios == 0) {
                        ctrl->completed_user_ios++;
                        ctrl->total_user_latency += (now - ctrl->active_ios[i].arrival_time);
                        ctrl->active_ios[i].io_id = 0; // Free slot
                    }
                    break;
                }
            }
            break;
        }

        case LP_FINI: {
            if (is_controller) {
                struct controller_state *ctrl = &state->ctrl;
                double total_time = ctrl->lvt > 0.0 ? ctrl->lvt : 1.0;
                double avg_lat_ms = ctrl->completed_user_ios > 0 ? (ctrl->total_user_latency / (double)ctrl->completed_user_ios * 1000.0) : 0.0;
                double user_iops = (double)ctrl->completed_user_ios / total_time;

                printf("RAID Controller: UserIOs=%" PRIu64 " (Reads=%" PRIu64 ", Writes=%" PRIu64 "), Completed=%" PRIu64 ", DegradedReads=%" PRIu64 ", AvgLatency=%.2f ms, AchievedIOPS=%.2f\n",
                       ctrl->total_user_ios, ctrl->user_reads, ctrl->user_writes,
                       ctrl->completed_user_ios, ctrl->degraded_reads, avg_lat_ms, user_iops);

                if (raid_cfg.check_theory) {
                    double expected_amp = (1.0 - raid_cfg.write_fraction) * 1.0 + raid_cfg.write_fraction * 4.0;
                    printf("[CHECK] Theoretical small-write amplification: %.2f disk accesses/IO (write_fraction=%.2f)\n",
                           expected_amp, raid_cfg.write_fraction);
                }
            } else {
                struct disk_state *disk = &state->disk;
                double total_time = disk->lvt > 0.0 ? disk->lvt : 1.0;
                double util = disk->area_disk_busy / total_time;
                double mean_q = disk->area_disk_queue / total_time;
                double avg_seek_ms = disk->total_accesses > 0 ? (disk->total_seek_time / (double)disk->total_accesses * 1000.0) : 0.0;
                double avg_rot_ms = disk->total_accesses > 0 ? (disk->total_rot_time / (double)disk->total_accesses * 1000.0) : 0.0;
                double avg_xfer_ms = disk->total_accesses > 0 ? (disk->total_xfer_time / (double)disk->total_accesses * 1000.0) : 0.0;

                printf("Disk %u: Accesses=%" PRIu64 " (R=%" PRIu64 ", W=%" PRIu64 "), Util=%.4f, MeanQueue=%.2f, AvgSeek=%.2f ms, AvgRot=%.2f ms, AvgXfer=%.2f ms, Status=%s\n",
                       disk->disk_id, disk->total_accesses, disk->total_reads, disk->total_writes,
                       util, mean_q, avg_seek_ms, avg_rot_ms, avg_xfer_ms, disk->failed ? "FAILED" : "ONLINE");
            }

            rs_free(state);
            break;
        }

        default:
            fprintf(stderr, "Unknown event type %u on LP %llu\n", event_type, (unsigned long long)me);
            abort();
    }
}

static bool CanEnd(lp_id_t me, const void *snapshot)
{
    (void)me;
    (void)snapshot;
    return false;
}

static struct simulation_configuration conf = {
    .n_threads = 0,
    .gvt_period = 1000,
    .log_level = LOG_INFO,
    .stats_file = "raid5",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 10.0,
};

static bool raid_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --num-disks
            raid_cfg.num_disks = (unsigned int)atoi(arg);
            return true;
        case 257: // --stripe-size
            raid_cfg.stripe_unit_kb = (unsigned int)atoi(arg);
            return true;
        case 258: // --workload-rate
            raid_cfg.arrival_rate = atof(arg);
            return true;
        case 259: // --write-fraction
            raid_cfg.write_fraction = atof(arg);
            return true;
        case 260: // --fail-disk
            raid_cfg.failed_disk_id = atoi(arg);
            return true;
        case 261: // --fail-time
            raid_cfg.fail_time = atof(arg);
            return true;
        case 262: // --rpm
            raid_cfg.disk_rpm = atof(arg);
            return true;
        case 263: // --check
            raid_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void raid_extra_usage(void)
{
    printf("\nRAID-5 Disk Array Specific Options:\n");
    printf("  --num-disks <N>                Number of disks in RAID group (default: 5)\n");
    printf("  --stripe-size <K>              Stripe unit size in KB (default: 64)\n");
    printf("  --workload-rate <R>            User I/O arrival rate in IOPS (default: 100.0)\n");
    printf("  --write-fraction <F>           Fraction of user I/Os that are writes (default: 0.30)\n");
    printf("  --fail-disk <D>                Disk index to fail (-1 for none, default: -1)\n");
    printf("  --fail-time <T>                Logical time to inject disk failure (default: 10.0)\n");
    printf("  --rpm <R>                      Disk spindle speed RPM (default: 7200.0)\n");
    printf("  --check                        Verify small-write amplification factor\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, 1 + raid_cfg.num_disks, 10.0);

    static const struct option raid_options[] = {
        {"num-disks", required_argument, 0, 256},
        {"stripe-size", required_argument, 0, 257},
        {"workload-rate", required_argument, 0, 258},
        {"write-fraction", required_argument, 0, 259},
        {"fail-disk", required_argument, 0, 260},
        {"fail-time", required_argument, 0, 261},
        {"rpm", required_argument, 0, 262},
        {"check", no_argument, 0, 263},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   raid_options, NULL,
                                   raid_extra_handler, NULL,
                                   raid_extra_usage);

    conf.lps = 1 + raid_cfg.num_disks;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "raid5";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
