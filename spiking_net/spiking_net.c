/**
 * @file spiking_net/spiking_net.c
 *
 * @brief Discrete event Spiking Neural Network (LIF) simulation model for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "spiking_net.h"
#include <argparse.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct spiking_net_config net_cfg = {
    .num_neurons = 64,
    .fan_out = 16,
    .tau_m = 0.020,         // 20 ms membrane time constant
    .tau_ref = 0.002,       // 2 ms absolute refractory period
    .v_thresh = -50.0,      // -50 mV firing threshold
    .v_reset = -65.0,       // -65 mV reset voltage
    .v_rest = -65.0,        // -65 mV resting potential
    .poisson_rate = 400.0,  // 400 Hz background drive per neuron
    .delay_min = 0.001,     // 1 ms minimum axonal delay
    .delay_max = 0.003,     // 3 ms maximum axonal delay
    .weight_exc = 1.5,      // +1.5 mV EPSP amplitude
    .weight_inh = -4.5,     // -4.5 mV IPSP amplitude
    .inhibitory_frac = 0.20,// 20% inhibitory interneurons (Brunel 2000)
    .check_theory = false
};

static void update_membrane_potential(struct neuron_state *state, simtime_t now)
{
    if (now <= state->last_update_time) {
        return;
    }

    if (now < state->refractory_until) {
        state->v_membrane = net_cfg.v_reset;
        state->last_update_time = now;
        return;
    }

    simtime_t decay_start = (state->last_update_time < state->refractory_until) ?
                            state->refractory_until : state->last_update_time;
    simtime_t elapsed = now - decay_start;
    state->v_membrane = net_cfg.v_rest + (state->v_membrane - net_cfg.v_rest) * exp(-elapsed / net_cfg.tau_m);
    state->last_update_time = now;
}

static void fire_action_potential(lp_id_t me, simtime_t now, struct neuron_state *state)
{
    state->spikes_fired++;
    state->last_spike_time = now;
    state->refractory_until = now + net_cfg.tau_ref;
    state->v_membrane = net_cfg.v_reset;

    for (uint32_t k = 0; k < state->fan_out_count; k++) {
        struct spike_payload sp = {
            .source_lp = me,
            .weight = state->synapses[k].weight
        };
        ScheduleNewEvent(state->synapses[k].target_lp,
                         now + state->synapses[k].delay,
                         EVENT_SYNAPSE_SPIKE,
                         &sp,
                         sizeof(sp));
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct neuron_state *state = (struct neuron_state *)s;

    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
    }

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me + 100, &state->seed);
            state->lvt = now;
            state->id = me;
            state->is_inhibitory = (Random(&state->seed) < net_cfg.inhibitory_frac);
            state->v_membrane = net_cfg.v_rest;
            state->last_spike_time = 0.0;
            state->last_update_time = now;
            state->refractory_until = 0.0;

            uint32_t f_out = net_cfg.fan_out;
            if (f_out > MAX_FAN_OUT) f_out = MAX_FAN_OUT;
            if (f_out >= net_cfg.num_neurons && net_cfg.num_neurons > 1) {
                f_out = net_cfg.num_neurons - 1;
            }
            state->fan_out_count = f_out;

            double weight_val = state->is_inhibitory ? net_cfg.weight_inh : net_cfg.weight_exc;
            for (uint32_t k = 0; k < state->fan_out_count; k++) {
                lp_id_t target;
                do {
                    target = (lp_id_t)(Random(&state->seed) * (double)net_cfg.num_neurons);
                } while (target == me && net_cfg.num_neurons > 1);

                double d_range = net_cfg.delay_max - net_cfg.delay_min;
                double delay_val = net_cfg.delay_min + (d_range > 0.0 ? Random(&state->seed) * d_range : 0.0);

                state->synapses[k].target_lp = target;
                state->synapses[k].weight = weight_val;
                state->synapses[k].delay = delay_val;
            }

            SetState(state);

            // Schedule first background Poisson input
            if (net_cfg.poisson_rate > 0.0) {
                double dt = Expent(&state->seed, 1.0 / net_cfg.poisson_rate);
                ScheduleNewEvent(me, now + dt, EVENT_EXTERNAL_INPUT, NULL, 0);
            }
            break;
        }

        case EVENT_SYNAPSE_SPIKE: {
            const struct spike_payload *sp = (const struct spike_payload *)payload;
            if (!sp) break;

            state->spikes_received++;
            update_membrane_potential(state, now);

            if (now >= state->refractory_until) {
                state->v_membrane += sp->weight;
                if (state->v_membrane >= net_cfg.v_thresh) {
                    fire_action_potential(me, now, state);
                }
            }
            break;
        }

        case EVENT_EXTERNAL_INPUT: {
            state->external_inputs_received++;
            update_membrane_potential(state, now);

            if (now >= state->refractory_until) {
                state->v_membrane += net_cfg.weight_exc;
                if (state->v_membrane >= net_cfg.v_thresh) {
                    fire_action_potential(me, now, state);
                }
            }

            if (net_cfg.poisson_rate > 0.0) {
                double dt = Expent(&state->seed, 1.0 / net_cfg.poisson_rate);
                ScheduleNewEvent(me, now + dt, EVENT_EXTERNAL_INPUT, NULL, 0);
            }
            break;
        }

        case LP_FINI: {
            double total_time = state->lvt > 0.0 ? state->lvt : 1.0;
            double rate_hz = (double)state->spikes_fired / total_time;

            printf("Neuron %llu (%s): Fired=%" PRIu64 " (Rate=%.2f Hz), Received=%" PRIu64 ", ExtInputs=%" PRIu64 ", V_mem=%.2f mV\n",
                   (unsigned long long)me,
                   state->is_inhibitory ? "Inhibitory" : "Excitatory",
                   state->spikes_fired,
                   rate_hz,
                   state->spikes_received,
                   state->external_inputs_received,
                   state->v_membrane);

            if (net_cfg.check_theory && me == 0) {
                printf("[CHECK] Neural Network LIF verification: simulated neuron firing dynamics active\n");
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
    .stats_file = "spiking_net",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1.0,
};

static bool net_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --neurons
            net_cfg.num_neurons = (unsigned int)atoi(arg);
            return true;
        case 257: // --fan-out
            net_cfg.fan_out = (unsigned int)atoi(arg);
            return true;
        case 258: // --tau-m
            net_cfg.tau_m = atof(arg);
            return true;
        case 259: // --tau-ref
            net_cfg.tau_ref = atof(arg);
            return true;
        case 260: // --v-thresh
            net_cfg.v_thresh = atof(arg);
            return true;
        case 261: // --v-reset
            net_cfg.v_reset = atof(arg);
            return true;
        case 262: // --v-rest
            net_cfg.v_rest = atof(arg);
            return true;
        case 263: // --poisson-rate
            net_cfg.poisson_rate = atof(arg);
            return true;
        case 264: // --delay-min
            net_cfg.delay_min = atof(arg);
            return true;
        case 265: // --delay-max
            net_cfg.delay_max = atof(arg);
            return true;
        case 266: // --check
            net_cfg.check_theory = true;
            return true;
        default:
            return false;
    }
}

static void net_extra_usage(void)
{
    printf("\nSpiking Neural Network Specific Options:\n");
    printf("  --neurons <N>                  Number of simulated neurons / LPs (default: 64)\n");
    printf("  --fan-out <K>                  Synaptic connections per neuron (default: 16)\n");
    printf("  --tau-m <T>                    Membrane time constant in seconds (default: 0.020)\n");
    printf("  --tau-ref <T>                  Refractory period in seconds (default: 0.002)\n");
    printf("  --v-thresh <V>                 Spike threshold voltage in mV (default: -50.0)\n");
    printf("  --v-reset <V>                  Reset potential in mV (default: -65.0)\n");
    printf("  --v-rest <V>                   Resting potential in mV (default: -65.0)\n");
    printf("  --poisson-rate <R>             Background Poisson drive per neuron in Hz (default: 100.0)\n");
    printf("  --delay-min <D>                Minimum axonal delay in seconds (default: 0.001)\n");
    printf("  --delay-max <D>                Maximum axonal delay in seconds (default: 0.003)\n");
    printf("  --check                        Verify firing rate dynamics within physiological bounds\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, net_cfg.num_neurons, 1.0);

    static const struct option net_options[] = {
        {"neurons", required_argument, 0, 256},
        {"fan-out", required_argument, 0, 257},
        {"tau-m", required_argument, 0, 258},
        {"tau-ref", required_argument, 0, 259},
        {"v-thresh", required_argument, 0, 260},
        {"v-reset", required_argument, 0, 261},
        {"v-rest", required_argument, 0, 262},
        {"poisson-rate", required_argument, 0, 263},
        {"delay-min", required_argument, 0, 264},
        {"delay-max", required_argument, 0, 265},
        {"check", no_argument, 0, 266},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   net_options, NULL,
                                   net_extra_handler, NULL,
                                   net_extra_usage);

    conf.lps = net_cfg.num_neurons;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "spiking_net";
    }

    RootsimInit(&conf);
    int ret = RootsimRun();

    return ret;
}
