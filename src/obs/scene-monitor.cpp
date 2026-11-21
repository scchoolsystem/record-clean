#include "scene-monitor.hpp"

namespace melton {

ProgramState ReadProgram()
{
	ProgramState st;
	// OBS puts the active transition on output channel 0; its "active source" is the
	// scene the Program is on (or the destination while a transition is running).
	obs_source_t *prog = obs_get_output_source(0);
	if (!prog)
		return st;
	if (obs_source_get_type(prog) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_t *t = obs_transition_get_active_source(prog);
		st.target = OBSSource(t); // OBSSource takes its own ref
		obs_source_release(t);
		st.idle = obs_transition_get_time(prog) >= 1.0f;
		st.transition = OBSSource(prog);
	} else {
		st.target = OBSSource(prog);
	}
	obs_source_release(prog);
	return st;
}

} // namespace melton
