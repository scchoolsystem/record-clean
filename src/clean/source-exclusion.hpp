#pragma once
#include "common.hpp"
#include "config/settings.hpp"

namespace melton {

// Thread-safe set of sources hidden from the clean output. Matching is by UUID
// first (survives renames) and by name second (survives delete + re-create).
class ExclusionSet {
public:
	ExclusionSet();
	~ExclusionSet();

	void Set(const std::vector<Exclusion> &list);
	std::vector<Exclusion> Get() const;
	bool Matches(obs_source_t *source) const;
	bool Empty() const;

	// Main thread: re-sync stored names/UUIDs against live sources. Returns true if anything changed.
	bool Refresh();

	std::function<void()> onChanged; // called after rename / remove / create events

private:
	static void OnRename(void *param, calldata_t *cd);
	static void OnSourceChange(void *param, calldata_t *cd);

	mutable std::mutex mutex_;
	std::vector<Exclusion> list_;
	OBSSignal renameSignal_, createSignal_, removeSignal_;
};

} // namespace melton
