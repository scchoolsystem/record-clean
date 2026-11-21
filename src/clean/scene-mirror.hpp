#pragma once
#include "common.hpp"
#include "source-exclusion.hpp"

namespace melton {

// Builds "clean" counterparts of OBS scenes without touching the originals.
//
// * A scene that (directly or through nested scenes/groups) contains an excluded
//   source is rebuilt as a private mirror scene: same items, same order, same
//   transforms/crops/visibility, minus the excluded sources.
// * A scene that contains nothing excluded is used as-is (zero copy, always exact).
// * Mirror scenes reference the *same* underlying sources, so no camera, capture
//   device or browser source is ever opened a second time.
class SceneMirror {
public:
	explicit SceneMirror(ExclusionSet &exclusions);
	~SceneMirror();

	// Main thread only. Builds the new mirror table, then swaps it in atomically.
	void RebuildAll();
	void Clear();

	// Any thread. Returns the source to render in place of `original`, or null if
	// that scene has not been processed yet.
	OBSSource Lookup(obs_source_t *original) const;

	// Invoked (from any thread) when an original scene changed in a way that can
	// invalidate a mirror. The owner should coalesce and call RebuildAll().
	std::function<void()> onDirty;

private:
	struct Table {
		std::map<std::string, OBSSource> byUuid;
		std::vector<std::unique_ptr<OBSSignal>> signals;
		int mirrored = 0;
		int passthrough = 0;
	};

	static void OnSceneEvent(void *param, calldata_t *cd);
	void Watch(Table &t, obs_source_t *scene, bool geometry);
	OBSSource CopyScene(Table &t, obs_scene_t *src, int depth);
	void CopyInto(Table &t, obs_scene_t *src, obs_scene_t *dst, obs_sceneitem_t *dstGroup, int depth);
	bool ContainsExcluded(obs_scene_t *scene, int depth) const;

	ExclusionSet &exclusions_;
	mutable std::mutex mutex_;
	std::shared_ptr<Table> table_;
};

} // namespace melton
