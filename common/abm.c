#include "abm.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_ABM_DIRECTIONS 8

static enum topology_direction hex_directions[6] = {
    DIRECTION_E, DIRECTION_W, DIRECTION_NE, DIRECTION_NW, DIRECTION_SE, DIRECTION_SW
};

static enum topology_direction square_directions[4] = {
    DIRECTION_E, DIRECTION_W, DIRECTION_N, DIRECTION_S
};

struct abm_agent {
    agent_t id;
    unsigned int user_data_size;
    void *user_data;
    simtime_t leave_time;
    unsigned int next_region;
    unsigned int next_event;
    bool has_next_visit;
    struct abm_agent *next;
};

struct abm_neighbour {
    unsigned int lp;
    void *data;
};

struct abm_lp_context {
    lp_id_t me;
    void *user_state;
    struct rng_t seed;
    uint64_t agent_counter;

    void *tracked_data;
    void *published_data;
    size_t neighbour_data_size;

    unsigned int n_directions;
    enum topology_direction *directions;
    struct abm_neighbour neighbours[MAX_ABM_DIRECTIONS];

    struct abm_agent *agents;
    unsigned int agent_count;
};

static struct topology *abm_topology = NULL;
static enum topology_geometry abm_geometry = TOPOLOGY_HEXAGON;
static size_t abm_neighbour_data_size = 0;
static ProcessEvent_t abm_user_handler = NULL;
static CanEnd_t abm_user_can_end = NULL;
static double *abm_obstacles = NULL;
static unsigned int abm_obstacles_count = 0;
static unsigned int *abm_adj = NULL;
static unsigned int abm_adj_regions = 0;
static unsigned int abm_adj_dirs = 0;

static _Thread_local struct abm_lp_context *current_abm_ctx = NULL;
static _Thread_local struct abm_agent *iter_next = NULL;
static _Thread_local bool iter_active = false;

struct abm_leave_msg {
    agent_t agent_id;
    unsigned int event_type;
};

struct abm_update_msg {
    unsigned int from_lp;
    unsigned char data[];
};

struct abm_visit_msg {
    agent_t id;
    unsigned int event_type;
    unsigned int user_data_size;
    unsigned char user_data[];
};

void abm_set_state(void *user_state)
{
    if (current_abm_ctx) {
        current_abm_ctx->user_state = user_state;
    }
}

void TrackNeighbourInfo(void *neighbour_data)
{
    if (current_abm_ctx) {
        current_abm_ctx->tracked_data = neighbour_data;
    }
}

int GetNeighbourInfo(unsigned int dir, unsigned int *region_id, void **data_p)
{
    if (!current_abm_ctx || dir >= current_abm_ctx->n_directions)
        return -1;

    if (current_abm_ctx->neighbours[dir].lp == (unsigned int)DIRECTION_INVALID)
        return -1;

    if (region_id)
        *region_id = current_abm_ctx->neighbours[dir].lp;
    if (data_p)
        *data_p = current_abm_ctx->neighbours[dir].data;

    return 0;
}

unsigned int DirectionsCount(void)
{
    if (current_abm_ctx)
        return current_abm_ctx->n_directions;
    return (abm_geometry == TOPOLOGY_HEXAGON) ? 6 : 4;
}

unsigned int RegionsCount(void)
{
    if (abm_topology)
        return (unsigned int)CountRegions(abm_topology);
    return 1;
}

agent_t SpawnAgent(size_t user_data_size)
{
    if (!current_abm_ctx)
        return 0;

    struct abm_agent *ag = rs_malloc(sizeof(struct abm_agent));
    if (!ag) abort();

    current_abm_ctx->agent_counter++;
    ag->id = ((uint64_t)current_abm_ctx->me << 32) | (current_abm_ctx->agent_counter & 0xFFFFFFFF);
    ag->user_data_size = (unsigned int)user_data_size;
    ag->user_data = user_data_size > 0 ? rs_malloc(user_data_size) : NULL;
    if (user_data_size > 0 && !ag->user_data) abort();
    ag->leave_time = 0;
    ag->has_next_visit = false;
    ag->next_region = (unsigned int)DIRECTION_INVALID;
    ag->next_event = 0;

    ag->next = current_abm_ctx->agents;
    current_abm_ctx->agents = ag;
    current_abm_ctx->agent_count++;

    return ag->id;
}

void *DataAgent(agent_t agent_id, unsigned int *data_size_p)
{
    if (!current_abm_ctx)
        return NULL;

    struct abm_agent *curr = current_abm_ctx->agents;
    while (curr) {
        if (curr->id == agent_id) {
            if (data_size_p)
                *data_size_p = curr->user_data_size;
            return curr->user_data;
        }
        curr = curr->next;
    }
    return NULL;
}

void KillAgent(agent_t agent_id)
{
    if (!current_abm_ctx)
        return;

    struct abm_agent **curr = &current_abm_ctx->agents;
    while (*curr) {
        if ((*curr)->id == agent_id) {
            struct abm_agent *to_del = *curr;
            if (iter_active && iter_next == to_del) {
                iter_next = to_del->next;
            }
            *curr = to_del->next;
            if (to_del->user_data)
                rs_free(to_del->user_data);
            rs_free(to_del);
            current_abm_ctx->agent_count--;
            return;
        }
        curr = &(*curr)->next;
    }
}

unsigned int CountAgents(void)
{
    return current_abm_ctx ? current_abm_ctx->agent_count : 0;
}

bool IterAgents(agent_t *agent_p)
{
    if (!current_abm_ctx || !agent_p) {
        iter_active = false;
        iter_next = NULL;
        return false;
    }
    struct abm_agent *curr;
    if (!iter_active) {
        curr = current_abm_ctx->agents;
        iter_active = true;
    } else {
        curr = iter_next;
    }
    if (curr) {
        iter_next = curr->next;
        *agent_p = curr->id;
        return true;
    }
    iter_active = false;
    iter_next = NULL;
    return false;
}

void ScheduleNewLeaveEvent(simtime_t time, unsigned int event_type, agent_t agent_id)
{
    if (!current_abm_ctx)
        return;

    struct abm_agent *ag = current_abm_ctx->agents;
    while (ag) {
        if (ag->id == agent_id) {
            ag->leave_time = time;
            break;
        }
        ag = ag->next;
    }

    struct abm_leave_msg msg = { .agent_id = agent_id, .event_type = event_type };
    ScheduleNewEvent(current_abm_ctx->me, time, ABM_LEAVING, &msg, sizeof(msg));
}

void EnqueueVisit(agent_t agent_id, unsigned int region, unsigned int event_type)
{
    if (!current_abm_ctx)
        return;

    struct abm_agent *ag = current_abm_ctx->agents;
    while (ag) {
        if (ag->id == agent_id) {
            ag->next_region = region;
            ag->next_event = event_type;
            ag->has_next_visit = true;
            return;
        }
        ag = ag->next;
    }
}

unsigned int CountPastVisits(agent_t agent_id) { (void)agent_id; return 0; }
void GetPastVisit(agent_t agent_id, unsigned int *region_p, unsigned int *event_type_p, simtime_t *time_p, unsigned int i) {
    (void)agent_id; (void)region_p; (void)event_type_p; (void)time_p; (void)i;
}
unsigned int CountVisits(agent_t agent_id) { (void)agent_id; return 0; }
void GetVisit(agent_t agent_id, unsigned int *region_p, unsigned int *event_type_p, unsigned int i) {
    (void)agent_id; (void)region_p; (void)event_type_p; (void)i;
}
void SetVisit(agent_t agent_id, unsigned int region, unsigned int event_type, unsigned int i) {
    (void)agent_id; (void)region; (void)event_type; (void)i;
}
void AddVisit(agent_t agent_id, unsigned int region, unsigned int event_type, unsigned int i) {
    (void)agent_id; (void)region; (void)event_type; (void)i;
}
void RemoveVisit(agent_t agent_id, unsigned int i) {
    (void)agent_id; (void)i;
}

static bool is_obstacle(unsigned int lp)
{
    return abm_obstacles != NULL && lp < abm_obstacles_count && abm_obstacles[lp] != 0.0;
}

static void rebuild_adjacency(void)
{
    free(abm_adj);
    abm_adj = NULL;
    abm_adj_regions = 0;
    abm_adj_dirs = 0;

    if (!abm_topology)
        return;

    if (abm_geometry != TOPOLOGY_HEXAGON && abm_geometry != TOPOLOGY_SQUARE && abm_geometry != TOPOLOGY_TORUS)
        return;

    unsigned int n = (unsigned int)CountRegions(abm_topology);
    unsigned int n_dirs = (abm_geometry == TOPOLOGY_HEXAGON) ? 6 : 4;
    const enum topology_direction *dirs = (abm_geometry == TOPOLOGY_HEXAGON) ? hex_directions : square_directions;

    abm_adj = malloc((size_t)n * n_dirs * sizeof(unsigned int));
    if (!abm_adj) abort();

    abm_adj_regions = n;
    abm_adj_dirs = n_dirs;

    for (unsigned int u = 0; u < n; u++) {
        for (unsigned int d = 0; d < n_dirs; d++) {
            if (is_obstacle(u)) {
                abm_adj[u * n_dirs + d] = (unsigned int)DIRECTION_INVALID;
                continue;
            }
            lp_id_t r = (GetReceiver)(abm_topology, u, dirs[d]);
            if (r == INVALID_DIRECTION || is_obstacle((unsigned int)r)) {
                abm_adj[u * n_dirs + d] = (unsigned int)DIRECTION_INVALID;
            } else {
                abm_adj[u * n_dirs + d] = (unsigned int)r;
            }
        }
    }
}

void abm_set_obstacles(const double *obstacles, unsigned int count)
{
    free(abm_obstacles);
    abm_obstacles = NULL;
    abm_obstacles_count = 0;

    if (obstacles && count > 0) {
        abm_obstacles = malloc((size_t)count * sizeof(double));
        if (!abm_obstacles) abort();
        memcpy(abm_obstacles, obstacles, (size_t)count * sizeof(double));
        abm_obstacles_count = count;
    }

    rebuild_adjacency();
}

double GetValueTopology(unsigned int from, unsigned int to)
{
    (void)from;
    return is_obstacle(to) ? abm_obstacles[to] : 0.0;
}

unsigned int FindReceiver(void)
{
    if (!abm_topology || !current_abm_ctx)
        return 0;

    if (abm_geometry == TOPOLOGY_HEXAGON || abm_geometry == TOPOLOGY_SQUARE || abm_geometry == TOPOLOGY_TORUS) {
        unsigned int valid[MAX_ABM_DIRECTIONS];
        unsigned int valid_cnt = 0;
        for (unsigned int i = 0; i < current_abm_ctx->n_directions; i++) {
            unsigned int nb = current_abm_ctx->neighbours[i].lp;
            if (nb != (unsigned int)DIRECTION_INVALID) {
                valid[valid_cnt++] = nb;
            }
        }
        if (valid_cnt > 0) {
            return valid[abm_random_range(0, (int)valid_cnt - 1)];
        }
        return (unsigned int)current_abm_ctx->me;
    }

    lp_id_t r = (GetReceiver)(abm_topology, current_abm_ctx->me, DIRECTION_RANDOM);
    return (r != INVALID_DIRECTION) ? (unsigned int)r : (unsigned int)current_abm_ctx->me;
}

unsigned int FindReceiverToward(unsigned int to)
{
    if (!current_abm_ctx)
        return 0;

    unsigned int me = (unsigned int)current_abm_ctx->me;
    if (!abm_adj || me >= abm_adj_regions || to >= abm_adj_regions || me == to || is_obstacle(me) || is_obstacle(to)) {
        return FindReceiver();
    }

    unsigned int n = abm_adj_regions;
    unsigned int n_dirs = abm_adj_dirs;

    unsigned int stack_fh[4096];
    unsigned int stack_q[4096];
    unsigned int *first_hop = (n <= 4096) ? stack_fh : malloc((size_t)n * sizeof(unsigned int));
    unsigned int *queue = (n <= 4096) ? stack_q : malloc((size_t)n * sizeof(unsigned int));
    if (!first_hop || !queue) abort();

    for (unsigned int i = 0; i < n; i++) {
        first_hop[i] = UINT_MAX;
    }

    first_hop[me] = me;
    unsigned int head = 0;
    unsigned int tail = 0;
    unsigned int ret = UINT_MAX;

    unsigned int start_d = (unsigned int)abm_random_range(0, (int)n_dirs - 1);
    for (unsigned int k = 0; k < n_dirs; k++) {
        unsigned int d = (start_d + k) % n_dirs;
        unsigned int nb = abm_adj[me * n_dirs + d];
        if (nb != (unsigned int)DIRECTION_INVALID) {
            if (nb == to) {
                ret = nb;
                goto done;
            }
            if (first_hop[nb] == UINT_MAX) {
                first_hop[nb] = nb;
                queue[tail++] = nb;
            }
        }
    }

    while (head < tail) {
        unsigned int u = queue[head++];
        unsigned int fh = first_hop[u];
        for (unsigned int d = 0; d < n_dirs; d++) {
            unsigned int nb = abm_adj[u * n_dirs + d];
            if (nb != (unsigned int)DIRECTION_INVALID && first_hop[nb] == UINT_MAX) {
                if (nb == to) {
                    ret = fh;
                    goto done;
                }
                first_hop[nb] = fh;
                queue[tail++] = nb;
            }
        }
    }

done:
    if (n > 4096) {
        free(first_hop);
        free(queue);
    }
    return (ret != UINT_MAX) ? ret : FindReceiver();
}

unsigned int abm_compute_min_tour(unsigned int from, unsigned int to, unsigned int *path)
{
    if (!abm_adj || from >= abm_adj_regions || to >= abm_adj_regions || is_obstacle(from) || is_obstacle(to)) {
        return UINT_MAX;
    }
    if (from == to) {
        if (path)
            path[0] = from;
        return 0;
    }

    unsigned int n = abm_adj_regions;
    unsigned int n_dirs = abm_adj_dirs;

    unsigned int stack_parent[4096];
    unsigned int stack_q[4096];
    unsigned int *parent = (n <= 4096) ? stack_parent : malloc((size_t)n * sizeof(unsigned int));
    unsigned int *queue = (n <= 4096) ? stack_q : malloc((size_t)n * sizeof(unsigned int));
    if (!parent || !queue) abort();

    for (unsigned int i = 0; i < n; i++) {
        parent[i] = UINT_MAX;
    }

    parent[from] = from;
    queue[0] = from;
    unsigned int head = 0;
    unsigned int tail = 1;
    bool found = false;

    while (head < tail && !found) {
        unsigned int u = queue[head++];
        for (unsigned int d = 0; d < n_dirs; d++) {
            unsigned int nb = abm_adj[u * n_dirs + d];
            if (nb != (unsigned int)DIRECTION_INVALID && parent[nb] == UINT_MAX) {
                parent[nb] = u;
                if (nb == to) {
                    found = true;
                    break;
                }
                queue[tail++] = nb;
            }
        }
    }

    unsigned int hops = UINT_MAX;
    if (found) {
        hops = 0;
        unsigned int cur = to;
        while (cur != from) {
            hops++;
            cur = parent[cur];
        }
        if (path) {
            cur = to;
            for (unsigned int i = hops; i > 0; i--) {
                path[i] = cur;
                cur = parent[cur];
            }
            path[0] = from;
        }
    }

    if (n > 4096) {
        free(parent);
        free(queue);
    }
    return hops;
}

unsigned int abm_get_receiver(unsigned int me, unsigned int dir, bool allow_obstacles)
{
    if (!abm_topology) return (unsigned int)DIRECTION_INVALID;

    enum topology_direction d = DIRECTION_E;
    if (abm_geometry == TOPOLOGY_HEXAGON && dir < 6) {
        d = hex_directions[dir];
    } else if (dir < 4) {
        d = square_directions[dir];
    }

    lp_id_t r = (GetReceiver)(abm_topology, me, d);
    if (r == INVALID_DIRECTION) return (unsigned int)DIRECTION_INVALID;
    if (!allow_obstacles && is_obstacle((unsigned int)r)) return (unsigned int)DIRECTION_INVALID;
    return (unsigned int)r;
}

double abm_random(void)
{
    if (current_abm_ctx) {
        return (Random)(&current_abm_ctx->seed);
    }
    return (double)rand() / (double)RAND_MAX;
}

int abm_random_range(int min, int max)
{
    if (current_abm_ctx) {
        return (RandomRange)(&current_abm_ctx->seed, min, max);
    }
    return min + (rand() % (max - min + 1));
}

double abm_expent(double mean)
{
    if (current_abm_ctx) {
        return -mean * log(1.0 - (Random)(&current_abm_ctx->seed));
    }
    return -mean * log(1.0 - abm_random());
}

double abm_normal(void)
{
    if (current_abm_ctx) {
        return (Normal)(&current_abm_ctx->seed);
    }
    return 0.0;
}

static void broadcast_neighbour_update(struct abm_lp_context *ctx, simtime_t now)
{
    if (!ctx->tracked_data || ctx->neighbour_data_size == 0)
        return;

    if (ctx->published_data == NULL) {
        ctx->published_data = rs_malloc(ctx->neighbour_data_size);
        if (!ctx->published_data) abort();
        memset(ctx->published_data, 0xFF, ctx->neighbour_data_size); // Force difference on first update
    }

    if (memcmp(ctx->published_data, ctx->tracked_data, ctx->neighbour_data_size) == 0)
        return;

    memcpy(ctx->published_data, ctx->tracked_data, ctx->neighbour_data_size);

    size_t msg_size = sizeof(struct abm_update_msg) + ctx->neighbour_data_size;
    struct abm_update_msg *msg = rs_malloc(msg_size);
    if (!msg) abort();

    msg->from_lp = (unsigned int)ctx->me;
    memcpy(msg->data, ctx->published_data, ctx->neighbour_data_size);

    for (unsigned int i = 0; i < ctx->n_directions; i++) {
        if (ctx->neighbours[i].lp != (unsigned int)DIRECTION_INVALID) {
            ScheduleNewEvent(ctx->neighbours[i].lp, now + 0.0001, ABM_UPDATE, msg, (unsigned int)msg_size);
        }
    }
    rs_free(msg);
}

static void abm_dispatcher(lp_id_t me, simtime_t now, unsigned int event_type, const void *event_content,
                           unsigned int event_size, void *st)
{
    struct abm_lp_context *ctx = (struct abm_lp_context *)st;
    current_abm_ctx = ctx;

    switch (event_type) {
        case LP_INIT: {
            ctx = rs_malloc(sizeof(struct abm_lp_context));
            if (!ctx) abort();
            memset(ctx, 0, sizeof(struct abm_lp_context));

            ctx->me = me;
            ctx->neighbour_data_size = abm_neighbour_data_size;
            initialize_stream((unsigned int)me, &ctx->seed);

            if (abm_geometry == TOPOLOGY_HEXAGON) {
                ctx->n_directions = 6;
                ctx->directions = hex_directions;
            } else {
                ctx->n_directions = 4;
                ctx->directions = square_directions;
            }

            for (unsigned int i = 0; i < ctx->n_directions; i++) {
                lp_id_t n = (GetReceiver)(abm_topology, me, ctx->directions[i]);
                if (n != INVALID_DIRECTION && is_obstacle((unsigned int)n))
                    n = INVALID_DIRECTION;
                ctx->neighbours[i].lp = (n != INVALID_DIRECTION) ? (unsigned int)n : (unsigned int)DIRECTION_INVALID;
                if (ctx->neighbour_data_size > 0) {
                    ctx->neighbours[i].data = rs_malloc(ctx->neighbour_data_size);
                    if (ctx->neighbours[i].data)
                        memset(ctx->neighbours[i].data, 0, ctx->neighbour_data_size);
                }
            }

            (SetState)(ctx);
            current_abm_ctx = ctx;

            // Forward LP_INIT to model
            if (abm_user_handler) {
                abm_user_handler(me, now, LP_INIT, event_content, event_size, ctx->user_state);
            }
            broadcast_neighbour_update(ctx, now);
            break;
        }

        case ABM_UPDATE: {
            const struct abm_update_msg *msg = (const struct abm_update_msg *)event_content;
            if (ctx && msg && ctx->neighbour_data_size > 0) {
                for (unsigned int i = 0; i < ctx->n_directions; i++) {
                    if (ctx->neighbours[i].lp == msg->from_lp) {
                        if (ctx->neighbours[i].data) {
                            memcpy(ctx->neighbours[i].data, msg->data, ctx->neighbour_data_size);
                        }
                        break;
                    }
                }
            }
            break;
        }

        case ABM_LEAVING: {
            if (!ctx || !event_content) break;
            const struct abm_leave_msg *msg = (const struct abm_leave_msg *)event_content;
            agent_t agent_id = msg->agent_id;

            // Skip leave if the agent has already been killed
            struct abm_agent *ag = ctx->agents;
            while (ag) {
                if (ag->id == agent_id) break;
                ag = ag->next;
            }
            if (!ag) break;

            // Call model's leave handler
            if (abm_user_handler) {
                abm_user_handler(me, now, msg->event_type, &agent_id, sizeof(agent_id), ctx->user_state);
            }

            // Check if agent is still alive and has a planned destination
            struct abm_agent **prev = &ctx->agents;
            ag = ctx->agents;
            while (ag) {
                if (ag->id == agent_id) break;
                prev = &ag->next;
                ag = ag->next;
            }

            if (ag && ag->has_next_visit) {
                ag->has_next_visit = false;
                if (ag->next_region == me) {
                    // Stay in same region
                    if (abm_user_handler) {
                        abm_user_handler(me, now, ag->next_event, &ag->id, sizeof(ag->id), ctx->user_state);
                    }
                } else {
                    // Transfer to remote region
                    size_t visit_size = sizeof(struct abm_visit_msg) + ag->user_data_size;
                    struct abm_visit_msg *vmsg = rs_malloc(visit_size);
                    if (!vmsg) abort();

                    vmsg->id = ag->id;
                    vmsg->event_type = ag->next_event;
                    vmsg->user_data_size = ag->user_data_size;
                    if (ag->user_data_size > 0)
                        memcpy(vmsg->user_data, ag->user_data, ag->user_data_size);

                    ScheduleNewEvent(ag->next_region, now + 0.0001, ABM_VISITING, vmsg, (unsigned int)visit_size);
                    rs_free(vmsg);

                    // Remove from local LP
                    *prev = ag->next;
                    if (ag->user_data) rs_free(ag->user_data);
                    rs_free(ag);
                    ctx->agent_count--;
                }
            }
            broadcast_neighbour_update(ctx, now);
            break;
        }

        case ABM_VISITING: {
            const struct abm_visit_msg *vmsg = (const struct abm_visit_msg *)event_content;

            // Instantiate agent in this LP
            struct abm_agent *ag = rs_malloc(sizeof(struct abm_agent));
            if (!ag) abort();
            ag->id = vmsg->id;
            ag->user_data_size = vmsg->user_data_size;
            ag->user_data = vmsg->user_data_size > 0 ? rs_malloc(vmsg->user_data_size) : NULL;
            if (vmsg->user_data_size > 0 && !ag->user_data) abort();
            if (vmsg->user_data_size > 0)
                memcpy(ag->user_data, vmsg->user_data, vmsg->user_data_size);
            ag->leave_time = 0;
            ag->has_next_visit = false;
            ag->next_region = (unsigned int)DIRECTION_INVALID;
            ag->next_event = 0;

            ag->next = ctx->agents;
            ctx->agents = ag;
            ctx->agent_count++;

            // Call model's visit handler
            if (abm_user_handler) {
                abm_user_handler(me, now, vmsg->event_type, &ag->id, sizeof(ag->id), ctx->user_state);
            }
            broadcast_neighbour_update(ctx, now);
            break;
        }

        case LP_FINI: {
            if (abm_user_handler && ctx) {
                abm_user_handler(me, now, event_type, event_content, event_size, ctx->user_state);
            }
            break;
        }

        default: {
            // Forward event to model
            if (abm_user_handler && ctx) {
                abm_user_handler(me, now, event_type, event_content, event_size, ctx->user_state);
                broadcast_neighbour_update(ctx, now);
            }
            break;
        }
    }
}

static bool abm_committed(lp_id_t me, const void *snapshot)
{
    const struct abm_lp_context *ctx = (const struct abm_lp_context *)snapshot;
    if (abm_user_can_end && ctx) {
        return abm_user_can_end(me, ctx->user_state);
    }
    return false;
}

void abm_init_simulation(struct simulation_configuration *conf,
                         enum topology_geometry geom,
                         unsigned int width,
                         unsigned int height,
                         size_t neighbour_data_size,
                         ProcessEvent_t user_handler,
                         CanEnd_t user_can_end)
{
    abm_geometry = geom;
    abm_neighbour_data_size = neighbour_data_size;
    abm_user_handler = user_handler;
    abm_user_can_end = user_can_end;

    free(abm_obstacles);
    abm_obstacles = NULL;
    abm_obstacles_count = 0;

    if (abm_topology != NULL) {
        ReleaseTopology(abm_topology);
        abm_topology = NULL;
    }

    if (geom == TOPOLOGY_HEXAGON || geom == TOPOLOGY_SQUARE || geom == TOPOLOGY_TORUS) {
        abm_topology = InitializeTopology(geom, width, height);
    } else {
        abm_topology = InitializeTopology(geom, width * height);
    }

    rebuild_adjacency();

    conf->dispatcher = abm_dispatcher;
    conf->committed = abm_committed;
}
