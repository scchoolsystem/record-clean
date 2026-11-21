#include "common.hpp"
#include "obs/obs-integration.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("melton-dual-feed-recorder", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("MeltonDualFeed.Description");
}

bool obs_module_load(void)
{
	melton::Integration::Init();
	obs_log(LOG_INFO, "Melton Dual Feed Recorder %s loaded", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	melton::Integration::CreateDock();
}

void obs_module_unload(void)
{
	melton::Integration::Shutdown();
	obs_log(LOG_INFO, "Melton Dual Feed Recorder unloaded");
}
