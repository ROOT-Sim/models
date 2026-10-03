/**
 * @file smmp/smmp.c
 *
 * @brief Discrete event shared-memory multiprocessor simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "smmp.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct smmp_config smmp_cfg = {
    .num_processors = 16,
    .num_banks = 16,
    .cache_hit_ratio = 0.85,
    .write_ratio = 0.20,
    .mean_think_time = 5.0,
    .bank_access_time = 10.0,
    .network_latency = 1.0,
    .check_theory = false
};


static void update_bank_metrics(struct bank_state *bank, simtime_t now)
{
    if (now > bank->last_stats_time) {
        double dt = now - bank->last_stats_time;
        bank->area_server_busy += (bank->server_busy ? 1.0 : 0.0) * dt;
        bank->area_queue_length += (double)bank->queue_len * dt;
        bank->last_stats_time = now;
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    union smmp_state *state = (union smmp_state *)s;
    bool is_processor = (me < smmp_cfg.num_processors);

    if (state != NULL && event_type != LP_FINI) {
        if (is_processor) {
            state->proc.lvt = now;
        } else {
            state->bank.lvt = now;
            update_bank_metrics(&state->bank, now);
        }
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            if (is_processor) {
                struct processor_state *proc = &state->proc;
                initialize_stream((unsigned int)me + 1000, &proc->seed);
                proc->lvt = now;
                proc->proc_id = me;
                SetState(state);

                // Schedule first compute think time
                double dt = Expent(&proc->seed, smmp_cfg.mean_think_time);
                ScheduleNewEvent(me, now + dt, EVENT_THINK_DONE, NULL, 0);
            } else {
                struct bank_state *bank = &state->bank;
                initialize_stream((unsigned int)me + 2000, &bank->seed);
                bank->lvt = now;
                bank->bank_id = me;
                bank->last_stats_time = now;
                SetState(state);
            }
            break;
        }

        case EVENT_THINK_DONE: {
            if (!is_processor) break;
            struct processor_state *proc = &state->proc;

            static _Atomic uint64_t global_req_counter = 0;
            uint64_t rid = ++global_req_counter;

            bool is_write = (Random(&proc->seed) < smmp_cfg.write_ratio);
            if (!is_write) {
                // Read request: check cache hit
                if (Random(&proc->seed) < smmp_cfg.cache_hit_ratio) {
                    proc->cache_hits++;
                    proc->reads_issued++;
                    proc->requests_completed++;
                    proc->total_access_latency += 1.0; // 1 cycle cache hit latency

                    double dt = Expent(&proc->seed, smmp_cfg.mean_think_time);
                    ScheduleNewEvent(me, now + dt, EVENT_THINK_DONE, NULL, 0);
                    break;
                }
                proc->cache_misses++;
                proc->reads_issued++;
            } else {
                proc->writes_issued++;
            }

            // Generate address and route to interleaved memory bank
            uint64_t addr = (uint64_t)(Random(&proc->seed) * 10000000.0);
            unsigned int bank_idx = (unsigned int)((addr / 64) % smmp_cfg.num_banks);
            lp_id_t bank_lp = smmp_cfg.num_processors + bank_idx;

            struct mem_request req;
            req.req_id = rid;
            req.processor_id = me;
            req.address = addr;
            req.is_write = is_write;
            req.issue_time = now;

            unsigned int ev = is_write ? EVENT_MEM_WRITE : EVENT_MEM_READ;
            simtime_t send_time = now + smmp_cfg.network_latency;
            ScheduleNewEvent(bank_lp, send_time, ev, &req, sizeof(req));
            break;
        }

        case EVENT_MEM_READ:
        case EVENT_MEM_WRITE: {
            if (is_processor) break;
            struct bank_state *bank = &state->bank;
            const struct mem_request *req = (const struct mem_request *)payload;
            if (!req) break;

            if (!bank->server_busy) {
                bank->server_busy = true;
                bank->busy_until = now + smmp_cfg.bank_access_time;
                bank->total_requests_serviced++;

                struct mem_response resp;
                resp.req_id = req->req_id;
                resp.is_write = req->is_write;
                resp.issue_time = req->issue_time;

                simtime_t resp_time = now + smmp_cfg.bank_access_time + smmp_cfg.network_latency;
                ScheduleNewEvent(req->processor_id, resp_time, EVENT_MEM_RESPONSE, &resp, sizeof(resp));

                ScheduleNewEvent(me, now + smmp_cfg.bank_access_time, EVENT_BANK_FREE, NULL, 0);
            } else {
                if (bank->queue_len < MAX_MEM_QUEUE_DEPTH) {
                    bank->queue[bank->queue_len++] = *req;
                } else {
                    bank->total_queue_stalls++;
                }
            }
            break;
        }

        case EVENT_BANK_FREE: {
            if (is_processor) break;
            struct bank_state *bank = &state->bank;

            if (bank->queue_len > 0) {
                struct mem_request next_req = bank->queue[0];
                bank->queue_len--;
                if (bank->queue_len > 0) {
                    memmove(&bank->queue[0], &bank->queue[1], sizeof(struct mem_request) * bank->queue_len);
                }

                bank->busy_until = now + smmp_cfg.bank_access_time;
                bank->total_requests_serviced++;

                struct mem_response resp;
                resp.req_id = next_req.req_id;
                resp.is_write = next_req.is_write;
                resp.issue_time = next_req.issue_time;

                simtime_t resp_time = now + smmp_cfg.bank_access_time + smmp_cfg.network_latency;
                ScheduleNewEvent(next_req.processor_id, resp_time, EVENT_MEM_RESPONSE, &resp, sizeof(resp));

                ScheduleNewEvent(me, now + smmp_cfg.bank_access_time, EVENT_BANK_FREE, NULL, 0);
            } else {
                bank->server_busy = false;
            }
            break;
        }

        case EVENT_MEM_RESPONSE: {
            if (!is_processor) break;
            struct processor_state *proc = &state->proc;
            const struct mem_response *resp = (const struct mem_response *)payload;
            if (!resp) break;

            proc->requests_completed++;
            proc->total_access_latency += (now - resp->issue_time);

            // Schedule next computation
            double dt = Expent(&proc->seed, smmp_cfg.mean_think_time);
            ScheduleNewEvent(me, now + dt, EVENT_THINK_DONE, NULL, 0);
            break;
        }

        case LP_FINI: {
            if (is_processor) {
                struct processor_state *proc = &state->proc;
                double total_time = proc->lvt > 0.0 ? proc->lvt : 1.0;
                double avg_lat = proc->requests_completed > 0 ? (proc->total_access_latency / (double)proc->requests_completed) : 0.0;
                double actual_hit_rate = (proc->reads_issued > 0) ? ((double)proc->cache_hits / (double)proc->reads_issued) : 0.0;

                printf("Processor %llu: Reads=%" PRIu64 ", Writes=%" PRIu64 ", HitRate=%.4f, AMAT=%.2f cycles, Completed=%" PRIu64 ", IPC=%.4f\n",
                       (unsigned long long)me, proc->reads_issued, proc->writes_issued,
                       actual_hit_rate, avg_lat, proc->requests_completed, (double)proc->requests_completed / total_time);
            } else {
                struct bank_state *bank = &state->bank;
                double total_time = bank->lvt > 0.0 ? bank->lvt : 1.0;
                double util = bank->area_server_busy / total_time;
                double mean_q = bank->area_queue_length / total_time;

                printf("Memory Bank %llu: Serviced=%" PRIu64 ", Utilization=%.4f, MeanQueue=%.2f, Stalls=%" PRIu64 "\n",
                       (unsigned long long)me, bank->total_requests_serviced, util, mean_q, bank->total_queue_stalls);
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
    .stats_file = "smmp",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1000.0,
};

static bool smmp_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --processors
            smmp_cfg.num_processors = (unsigned int)atoi(arg);
            return true;
        case 257: // --memory-banks
            smmp_cfg.num_banks = (unsigned int)atoi(arg);
            return true;
        case 258: // --hit-ratio
            smmp_cfg.cache_hit_ratio = atof(arg);
            return true;
        case 259: // --write-ratio
            smmp_cfg.write_ratio = atof(arg);
            return true;
        case 260: // --think-time
            smmp_cfg.mean_think_time = atof(arg);
            return true;
        case 261: // --bank-access-time
            smmp_cfg.bank_access_time = atof(arg);
            return true;
        case 262: // --network-latency
            smmp_cfg.network_latency = atof(arg);
            return true;
        case 263: // --check
            smmp_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void smmp_extra_usage(void)
{
    printf("\nShared-Memory Multiprocessor Specific Options:\n");
    printf("  --processors <P>               Number of processor nodes (default: 16)\n");
    printf("  --memory-banks <M>             Number of interleaved memory banks (default: 16)\n");
    printf("  --hit-ratio <H>                L1 cache hit ratio (default: 0.85)\n");
    printf("  --write-ratio <W>              Fraction of memory accesses that are writes (default: 0.20)\n");
    printf("  --think-time <T>               Mean computation cycles between memory references (default: 5.0)\n");
    printf("  --bank-access-time <T>         Memory bank cycle time (default: 10.0)\n");
    printf("  --network-latency <T>          Interconnect network transit delay (default: 1.0)\n");
    printf("  --check                        Verify analytical AMAT queueing formula\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, smmp_cfg.num_processors + smmp_cfg.num_banks, 1000.0);

    static const struct option smmp_options[] = {
        {"processors", required_argument, 0, 256},
        {"memory-banks", required_argument, 0, 257},
        {"hit-ratio", required_argument, 0, 258},
        {"write-ratio", required_argument, 0, 259},
        {"think-time", required_argument, 0, 260},
        {"bank-access-time", required_argument, 0, 261},
        {"network-latency", required_argument, 0, 262},
        {"check", no_argument, 0, 263},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   smmp_options, NULL,
                                   smmp_extra_handler, NULL,
                                   smmp_extra_usage);

    conf.lps = smmp_cfg.num_processors + smmp_cfg.num_banks;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "smmp";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
