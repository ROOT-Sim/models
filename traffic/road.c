#include <ROOT-Sim.h>
#include <ROOT-Sim/random.h>

#include "model.h"
#include "road.h"

void inject_new_cars(lp_id_t me, struct state *state)
{
	simtime_t timestamp;
	struct car_arrival_event new_evt;

	// If the node has an interarrival frequency == 0, then no car can enter the node
	if(D_EQUAL(state->enter_freq, 0)) {
		return;
	}

	// Entering timestamps ditributed according to an Erlang distribution
	timestamp = state->lvt + (simtime_t)(Expent(&state->seed, state->enter_freq));

	// Send me the inject event
	new_evt.from = me;
	new_evt.injection = true;
	ScheduleNewEvent(me, timestamp, ARRIVAL, &new_evt, sizeof(struct car_arrival_event));
}
