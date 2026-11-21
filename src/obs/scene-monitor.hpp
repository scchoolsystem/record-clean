#pragma once
#include "common.hpp"

namespace melton {

struct ProgramState {
	OBSSource transition; // the transition source OBS currently has on Program channel 0
	OBSSource target;     // scene the Program is on, or is transitioning TO
	bool idle = true;     // true when OBS's own Program transition is not running
};

// Reads the real Program output (not Preview). Safe to call from the graphics thread:
// it only takes reference-counted snapshots of libobs state.
ProgramState ReadProgram();

} // namespace melton
