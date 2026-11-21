#pragma once
#include "common.hpp"

namespace melton {

// Private clones of OBS's transitions, used by the clean feed so it can run the same
// transition (type + settings + duration) without touching the Program transition.
class TransitionCache {
public:
	TransitionCache();
	~TransitionCache();

	// Main thread: create clones for every frontend transition, refresh changed settings.
	void Refresh(uint32_t baseW, uint32_t baseH);
	void Clear();

	// Any thread. Clone for an OBS transition, or the cut clone if unknown/unsupported.
	OBSSource Get(obs_source_t *original);
	OBSSource Cut();

	// Duration OBS would use for a transition to `scene` (honours per-scene overrides).
	int DurationFor(obs_source_t *scene) const;
	void SetFrontendDuration(int ms) { frontendDuration_ = ms; }
	static bool IsCut(obs_source_t *transition);

private:
	struct Entry {
		OBSSource clone;
		std::string settingsJson;
	};
	OBSSource MakeClone(obs_source_t *original, uint32_t w, uint32_t h);

	mutable std::mutex mutex_;
	std::map<std::string, Entry> clones_;
	OBSSource cut_;
	std::atomic<int> frontendDuration_{300};
};

} // namespace melton
