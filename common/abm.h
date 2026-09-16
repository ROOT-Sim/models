#pragma once

#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>
#include <ROOT-Sim/topology.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t agent_t;

#define DIRECTION_INVALID ((unsigned int)-1)
#define INIT LP_INIT

enum _abm_internal_events {
    ABM_UPDATE = 65000,
    ABM_LEAVING = 65001,
    ABM_VISITING = 65002
};

struct _topology_settings_t {
    int type;
    int default_geometry;
    bool write_enabled;
};

struct _abm_settings_t {
    size_t neighbour_data_size;
    unsigned int traverse_handler;
    bool keep_history;
};

#define TOPOLOGY_OBSTACLES 1
#define TOPOLOGY_PROBABILITIES 2
#define TOPOLOGY_COSTS 3

// Function prototypes for model code
void abm_set_state(void *user_state);
void TrackNeighbourInfo(void *neighbour_data);
int GetNeighbourInfo(unsigned int dir, unsigned int *region_id, void **data_p);
unsigned int DirectionsCount(void);
unsigned int RegionsCount(void);

agent_t SpawnAgent(size_t user_data_size);
void *DataAgent(agent_t agent_id, unsigned int *data_size_p);
void KillAgent(agent_t agent_id);
unsigned int CountAgents(void);
bool IterAgents(agent_t *agent_p);

void ScheduleNewLeaveEvent(simtime_t time, unsigned int event_type, agent_t agent_id);
void EnqueueVisit(agent_t agent_id, unsigned int region, unsigned int event_type);

unsigned int CountPastVisits(agent_t agent_id);
void GetPastVisit(agent_t agent_id, unsigned int *region_p, unsigned int *event_type_p, simtime_t *time_p, unsigned int i);
unsigned int CountVisits(agent_t agent_id);
void GetVisit(agent_t agent_id, unsigned int *region_p, unsigned int *event_type_p, unsigned int i);
void SetVisit(agent_t agent_id, unsigned int region, unsigned int event_type, unsigned int i);
void AddVisit(agent_t agent_id, unsigned int region, unsigned int event_type, unsigned int i);
void RemoveVisit(agent_t agent_id, unsigned int i);

unsigned int FindReceiver(void);
unsigned int FindReceiverToward(unsigned int to);
unsigned int abm_get_receiver(unsigned int me, unsigned int dir, bool allow_obstacles);

double abm_random(void);
int abm_random_range(int min, int max);
double abm_expent(double mean);
double abm_normal(void);

// Initialize ABM simulation system
void abm_init_simulation(struct simulation_configuration *conf,
                         enum topology_geometry geom,
                         unsigned int width,
                         unsigned int height,
                         size_t neighbour_data_size,
                         ProcessEvent_t user_handler,
                         CanEnd_t user_can_end);

#define SetState(s) abm_set_state(s)
#define CountAgentsABM() CountAgents()
#define Random() abm_random()
#define RandomRange(min, max) abm_random_range(min, max)
#undef Expent
#define Expent(mean) abm_expent(mean)
#define Normal() abm_normal()
#define GetReceiver(me, dir, obs) abm_get_receiver(me, dir, obs)

#ifdef __cplusplus
}
#endif
