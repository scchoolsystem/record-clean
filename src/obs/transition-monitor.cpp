#include "transition-monitor.hpp"

namespace melton {

TransitionCache::TransitionCache() = default;
TransitionCache::~TransitionCache()
{
	Clear();
}

void TransitionCache::Clear()
{
	std::map<std::string, Entry> old;
	OBSSource cut;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		old.swap(clones_);
		cut = std::move(cut_);
		cut_ = nullptr;
	}
}

bool TransitionCache::IsCut(obs_source_t *t)
{
	const char *id = t ? obs_source_get_id(t) : nullptr;
	return id && strcmp(id, "cut_transition") == 0;
}

static void ConfigureClone(obs_source_t *t, uint32_t w, uint32_t h)
{
	obs_transition_set_size(t, w, h);
	obs_transition_set_alignment(t, OBS_ALIGN_CENTER);
	obs_transition_set_scale_type(t, OBS_TRANSITION_SCALE_MAX_ONLY);
}

OBSSource TransitionCache::MakeClone(obs_source_t *original, uint32_t w, uint32_t h)
{
	OBSDataAutoRelease settings = obs_source_get_settings(original);
	OBSSourceAutoRelease clone =
		obs_source_create_private(obs_source_get_id(original), "melton-clean-transition", settings);
	if (!clone) {
		MLOG(LOG_WARNING, "Transition '%s' cannot be mirrored; the clean feed will cut instead",
		     obs_source_get_name(original));
		return nullptr;
	}
	ConfigureClone(clone, w, h);
	return OBSSource(clone.Get());
}

void TransitionCache::Refresh(uint32_t w, uint32_t h)
{
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);

	std::map<std::string, Entry> next;
	std::map<std::string, Entry> prev;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		prev = clones_;
	}
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *orig = list.sources.array[i];
		const char *uuid = obs_source_get_uuid(orig);
		if (!uuid)
			continue;
		OBSDataAutoRelease settings = obs_source_get_settings(orig);
		std::string json = SafeStr(obs_data_get_json(settings));
		auto it = prev.find(uuid);
		if (it != prev.end() && it->second.settingsJson == json) {
			next[uuid] = it->second; // unchanged: reuse (re-updating a stinger would reload its media)
			continue;
		}
		Entry e;
		e.settingsJson = json;
		e.clone = MakeClone(orig, w, h);
		if (e.clone)
			next[uuid] = e;
	}
	obs_frontend_source_list_free(&list);

	OBSSource cut;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		cut = cut_;
	}
	if (!cut) {
		OBSSourceAutoRelease c = obs_source_create_private("cut_transition", "melton-clean-cut", nullptr);
		if (c) {
			ConfigureClone(c, w, h);
			cut = OBSSource(c.Get());
		}
	}
	std::map<std::string, Entry> old;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		old.swap(clones_);
		clones_ = std::move(next);
		cut_ = cut;
	}
}

OBSSource TransitionCache::Get(obs_source_t *original)
{
	if (!original)
		return Cut();
	const char *uuid = obs_source_get_uuid(original);
	std::lock_guard<std::mutex> lock(mutex_);
	if (uuid) {
		auto it = clones_.find(uuid);
		if (it != clones_.end())
			return it->second.clone;
	}
	return cut_;
}

OBSSource TransitionCache::Cut()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return cut_;
}

int TransitionCache::DurationFor(obs_source_t *scene) const
{
	if (scene) {
		// Same private-setting keys the OBS frontend uses for per-scene transition overrides.
		OBSDataAutoRelease data = obs_source_get_private_settings(scene);
		const char *name = obs_data_get_string(data, "transition");
		if (name && *name) {
			obs_data_set_default_int(data, "transition_duration", 300);
			return (int)obs_data_get_int(data, "transition_duration");
		}
	}
	return frontendDuration_.load();
}

} // namespace melton
