#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"

namespace melton {

extern std::atomic<bool> g_alive;

class Integration {
public:
	static void Init();     // obs_module_load
	static void CreateDock(); // obs_module_post_load
	static void Shutdown(); // obs_module_unload
	static RecordingManager &Manager();
	static uint64_t DirtyStartNs();       // when OBS's native recording last started (0 if not recording)
	static std::string TakeStartMessage(); // error text from an automatic (linked) start, if any
};

} // namespace melton
