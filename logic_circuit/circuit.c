/**
 * @file logic_circuit/circuit.c
 *
 * @brief Discrete event parallel digital logic simulator for ROOT-Sim
 *
 * SPDX-FileCopyrightText: 2008-2026 HPDCS Group <rootsim@googlegroups.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "circuit.h"
#include "bench_parser.h"
#include <argparse.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct circuit_config circuit_cfg = {
    .netlist_path = NULL,
    .num_vectors = 1000,
    .vector_interval = 10.0,
    .clock_period = 10.0,
    .gate_delay = 1.0,
    .wire_delay = 0.1,
    .delay_model = DELAY_TRANSPORT,
    .check_signature = false,
    .sequential = false
};

struct circuit_graph circuit_netlist;

uint8_t evaluate_gate(enum gate_type type, const uint8_t *inputs, uint32_t num_inputs)
{
    if (num_inputs == 0) return LOGIC_0;

    switch (type) {
        case GATE_INPUT:
        case GATE_BUF:
            return inputs[0];

        case GATE_NOT:
            if (inputs[0] == LOGIC_0) return LOGIC_1;
            if (inputs[0] == LOGIC_1) return LOGIC_0;
            return LOGIC_X;

        case GATE_AND: {
            bool has_x = false;
            for (uint32_t i = 0; i < num_inputs; i++) {
                if (inputs[i] == LOGIC_0) return LOGIC_0;
                if (inputs[i] == LOGIC_X || inputs[i] == LOGIC_Z) has_x = true;
            }
            return has_x ? LOGIC_X : LOGIC_1;
        }

        case GATE_NAND: {
            uint8_t a = evaluate_gate(GATE_AND, inputs, num_inputs);
            if (a == LOGIC_0) return LOGIC_1;
            if (a == LOGIC_1) return LOGIC_0;
            return LOGIC_X;
        }

        case GATE_OR: {
            bool has_x = false;
            for (uint32_t i = 0; i < num_inputs; i++) {
                if (inputs[i] == LOGIC_1) return LOGIC_1;
                if (inputs[i] == LOGIC_X || inputs[i] == LOGIC_Z) has_x = true;
            }
            return has_x ? LOGIC_X : LOGIC_0;
        }

        case GATE_NOR: {
            uint8_t o = evaluate_gate(GATE_OR, inputs, num_inputs);
            if (o == LOGIC_0) return LOGIC_1;
            if (o == LOGIC_1) return LOGIC_0;
            return LOGIC_X;
        }

        case GATE_XOR: {
            uint32_t ones = 0;
            for (uint32_t i = 0; i < num_inputs; i++) {
                if (inputs[i] == LOGIC_X || inputs[i] == LOGIC_Z) return LOGIC_X;
                if (inputs[i] == LOGIC_1) ones++;
            }
            return (ones % 2 != 0) ? LOGIC_1 : LOGIC_0;
        }

        case GATE_XNOR: {
            uint8_t x = evaluate_gate(GATE_XOR, inputs, num_inputs);
            if (x == LOGIC_0) return LOGIC_1;
            if (x == LOGIC_1) return LOGIC_0;
            return LOGIC_X;
        }

        case GATE_DFF:
            return inputs[0];
    }
    return LOGIC_X;
}

static void broadcast_signal(lp_id_t me, simtime_t send_time, uint8_t val)
{
    const struct gate_info *info = &circuit_netlist.nodes[me];
    for (uint32_t i = 0; i < info->num_fanouts; i++) {
        struct signal_payload sig;
        sig.dest_pin = info->fanouts[i].dest_pin;
        sig.logic_val = val;
        ScheduleNewEvent(info->fanouts[i].dest_lp, send_time, EVENT_SIGNAL, &sig, sizeof(sig));
    }
}

static void ProcessEvent(lp_id_t me, simtime_t now, unsigned event_type,
                         const void *payload, unsigned size, void *s)
{
    (void)size;
    struct gate_state *state = (struct gate_state *)s;
    if (state != NULL && event_type != LP_FINI) {
        state->lvt = now;
    }

    const struct gate_info *info = &circuit_netlist.nodes[me];

    switch (event_type) {
        case LP_INIT: {
            state = rs_malloc(sizeof(*state));
            if (!state) abort();
            memset(state, 0, sizeof(*state));

            initialize_stream((unsigned int)me + 101, &state->seed);
            state->lvt = now;
            state->me = me;
            state->type = info->type;
            state->is_primary_input = info->is_primary_input;
            state->is_primary_output = info->is_primary_output;
            state->num_inputs = info->num_inputs;

            for (uint32_t i = 0; i < MAX_GATE_INPUTS; i++) {
                state->input_vals[i] = LOGIC_0;
            }

            state->current_output = LOGIC_0;
            state->pending_output = LOGIC_0;
            state->pending_time = 0.0;
            state->total_transitions = 0;
            state->glitches_filtered = 0;
            state->output_signature = 2166136261U; // FNV-1 offset basis

            SetState(state);

            if (state->is_primary_input) {
                // Initialize primary input with a pseudo-random value
                state->current_output = (Random(&state->seed) > 0.5) ? LOGIC_1 : LOGIC_0;
                broadcast_signal(me, now + circuit_cfg.wire_delay, state->current_output);

                // Schedule first stimulus
                struct stimulus_payload stim = { .vector_idx = 1 };
                ScheduleNewEvent(me, now + circuit_cfg.vector_interval, EVENT_STIMULUS, &stim, sizeof(stim));
            } else if (state->type == GATE_DFF) {
                // Schedule first clock event for sequential flip-flop
                ScheduleNewEvent(me, now + circuit_cfg.clock_period, EVENT_CLOCK, NULL, 0);
            }
            break;
        }

        case EVENT_STIMULUS: {
            const struct stimulus_payload *stim = (const struct stimulus_payload *)payload;
            uint8_t new_val = (Random(&state->seed) > 0.5) ? LOGIC_1 : LOGIC_0;

            if (new_val != state->current_output) {
                state->current_output = new_val;
                state->total_transitions++;
                broadcast_signal(me, now + circuit_cfg.wire_delay, new_val);
            }

            if (stim && stim->vector_idx < circuit_cfg.num_vectors) {
                struct stimulus_payload next_stim = { .vector_idx = stim->vector_idx + 1 };
                ScheduleNewEvent(me, now + circuit_cfg.vector_interval, EVENT_STIMULUS, &next_stim, sizeof(next_stim));
            }
            break;
        }

        case EVENT_CLOCK: {
            // DFF clock trigger: evaluate D input
            uint8_t d_in = state->input_vals[0];
            if (d_in != state->current_output) {
                state->current_output = d_in;
                state->total_transitions++;

                if (state->is_primary_output) {
                    state->output_signature = (state->output_signature ^ (d_in + 1)) * 16777619U + (uint32_t)(now * 100);
                }

                broadcast_signal(me, now + circuit_cfg.gate_delay, d_in);
            }

            // Schedule next clock tick
            ScheduleNewEvent(me, now + circuit_cfg.clock_period, EVENT_CLOCK, NULL, 0);
            break;
        }

        case EVENT_SIGNAL: {
            const struct signal_payload *sig = (const struct signal_payload *)payload;
            if (!sig) break;

            if (sig->dest_pin < MAX_GATE_INPUTS) {
                state->input_vals[sig->dest_pin] = sig->logic_val;
            }

            if (state->type == GATE_DFF) {
                // DFF only updates on clock edge
                break;
            }

            uint8_t new_out = evaluate_gate(state->type, state->input_vals, state->num_inputs);

            if (circuit_cfg.delay_model == DELAY_TRANSPORT) {
                if (new_out != state->current_output) {
                    state->current_output = new_out;
                    state->total_transitions++;

                    if (state->is_primary_output) {
                        state->output_signature = (state->output_signature ^ (new_out + 1)) * 16777619U + (uint32_t)(now * 100);
                    }

                    broadcast_signal(me, now + circuit_cfg.gate_delay, new_out);
                }
            } else {
                // Inertial delay model: filter pulses shorter than gate_delay
                if (new_out != state->current_output) {
                    if (now < state->pending_time && new_out != state->pending_output) {
                        state->glitches_filtered++;
                    }
                    state->pending_output = new_out;
                    state->pending_time = now + circuit_cfg.gate_delay;
                    state->current_output = new_out;
                    state->total_transitions++;

                    if (state->is_primary_output) {
                        state->output_signature = (state->output_signature ^ (new_out + 1)) * 16777619U + (uint32_t)(now * 100);
                    }

                    broadcast_signal(me, now + circuit_cfg.gate_delay, new_out);
                }
            }
            break;
        }

        case LP_FINI: {
            if (state->is_primary_output) {
                printf("Primary Output '%s' (LP %llu): Transitions=%" PRIu64 ", Signature=0x%08X\n",
                       info->name, (unsigned long long)me, state->total_transitions, state->output_signature);
            } else if (state->is_primary_input) {
                printf("Primary Input '%s' (LP %llu): Stimulus Transitions=%" PRIu64 "\n",
                       info->name, (unsigned long long)me, state->total_transitions);
            } else {
                if (state->glitches_filtered > 0) {
                    printf("Gate '%s' (LP %llu): Transitions=%" PRIu64 ", GlitchesFiltered=%" PRIu64 "\n",
                           info->name, (unsigned long long)me, state->total_transitions, state->glitches_filtered);
                }
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
    .stats_file = "circuit",
    .ckpt_interval = 0,
    .core_binding = true,
    .serial = false,
    .synchronization = TIME_WARP,
    .dispatcher = ProcessEvent,
    .committed = CanEnd,
    .termination_time = 1000.0,
};

static bool circuit_extra_handler(int c, const char *arg, void *user_data)
{
    (void)user_data;
    switch (c) {
        case 256: // --circuit
            circuit_cfg.netlist_path = arg;
            return true;
        case 257: // --vectors
            circuit_cfg.num_vectors = (uint32_t)atoi(arg);
            return true;
        case 258: // --interval
            circuit_cfg.vector_interval = atof(arg);
            return true;
        case 259: // --clock-period
            circuit_cfg.clock_period = atof(arg);
            return true;
        case 260: // --gate-delay
            circuit_cfg.gate_delay = atof(arg);
            return true;
        case 261: // --wire-delay
            circuit_cfg.wire_delay = atof(arg);
            return true;
        case 262: // --delay-model
            if (strcasecmp(arg, "transport") == 0) {
                circuit_cfg.delay_model = DELAY_TRANSPORT;
            } else if (strcasecmp(arg, "inertial") == 0) {
                circuit_cfg.delay_model = DELAY_INERTIAL;
            } else {
                fprintf(stderr, "Unknown delay model: %s (choose transport, inertial)\n", arg);
                return false;
            }
            return true;
        case 263: // --check
            circuit_cfg.check_signature = true;
            return true;
        default:
            return false;
    }
}

static void circuit_extra_usage(void)
{
    printf("\nLogic Circuit Specific Options:\n");
    printf("  --circuit <path>               Path to ISCAS .bench netlist (default: bundled c17.bench)\n");
    printf("  --vectors <N>                  Number of stimulus test vectors (default: 1000)\n");
    printf("  --interval <T>                 Time interval between test vectors (default: 10.0)\n");
    printf("  --clock-period <T>             Clock period for DFF sequential elements (default: 10.0)\n");
    printf("  --gate-delay <D>               Gate propagation delay (default: 1.0)\n");
    printf("  --wire-delay <D>               Interconnect wire delay (default: 0.1)\n");
    printf("  --delay-model <transport|inertial> Delay model (default: transport)\n");
    printf("  --check                        Verify output signature\n");
}

int main(int argc, char **argv)
{
    struct model_cli_options opt;
    init_default_cli_options(&opt, 11, 1000.0);

    static const struct option circuit_options[] = {
        {"circuit", required_argument, 0, 256},
        {"vectors", required_argument, 0, 257},
        {"interval", required_argument, 0, 258},
        {"clock-period", required_argument, 0, 259},
        {"gate-delay", required_argument, 0, 260},
        {"wire-delay", required_argument, 0, 261},
        {"delay-model", required_argument, 0, 262},
        {"check", no_argument, 0, 263},
        {0, 0, 0, 0}
    };

    parse_model_cli_options_custom(argc, argv, &opt, &conf,
                                   circuit_options, NULL,
                                   circuit_extra_handler, NULL,
                                   circuit_extra_usage);

    bool loaded = false;
    if (circuit_cfg.netlist_path != NULL) {
        loaded = parse_bench_file(circuit_cfg.netlist_path, &circuit_netlist);
        if (!loaded) {
            fprintf(stderr, "Error loading netlist file: %s. Trying built-in fallback...\n", circuit_cfg.netlist_path);
        }
    }

    if (!loaded) {
        // Try local file benchmarks/c17.bench first
        loaded = parse_bench_file("benchmarks/c17.bench", &circuit_netlist);
    }
    if (!loaded) {
        // Try relative to ROOT-Sim models source
        loaded = parse_bench_file("/Users/pellegrini/git/ROOT-Sim/models/logic_circuit/benchmarks/c17.bench", &circuit_netlist);
    }
    if (!loaded) {
        // Built-in fallback
        loaded = load_builtin_c17(&circuit_netlist);
    }

    if (!loaded || circuit_netlist.num_nodes == 0) {
        fprintf(stderr, "Failed to load any valid netlist\n");
        return 1;
    }

    // Check if circuit contains sequential elements (DFF)
    for (uint32_t i = 0; i < circuit_netlist.num_nodes; i++) {
        if (circuit_netlist.nodes[i].type == GATE_DFF) {
            circuit_cfg.sequential = true;
            break;
        }
    }

    conf.lps = circuit_netlist.num_nodes;

    if (opt.stats_file != NULL) {
        conf.stats_file = opt.stats_file;
    } else {
        conf.stats_file = "circuit";
    }

    printf("Loaded circuit with %u nodes (%u inputs, %u outputs, sequential: %s)\n",
           circuit_netlist.num_nodes, circuit_netlist.num_inputs, circuit_netlist.num_outputs,
           circuit_cfg.sequential ? "yes" : "no");

    RootsimInit(&conf);
    int ret = RootsimRun();

    free_circuit_graph(&circuit_netlist);
    return ret;
}
