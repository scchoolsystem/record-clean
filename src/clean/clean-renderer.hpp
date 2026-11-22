#pragma once
#include "common.hpp"
#include "scene-mirror.hpp"
#include "obs/scene-monitor.hpp"
#include "obs/transition-monitor.hpp"

namespace melton {

// Owns the private obs_view_t + video mix that renders the clean composition.
// It follows OBS's real Program output every graphics frame and mirrors scene
// changes (and transitions) onto the clean mirror scenes.
class CleanRenderer {
public:
	CleanRenderer(SceneMirror &mirror, TransitionCache &transitions);
	~CleanRenderer();

	bool Start(uint32_t outW, uint32_t outH, uint32_t fps, std::string &error);
	void Stop();
	bool Running() const { return running_.load(); }

	video_t *Video() const { return video_; }
	OBSSource Output() const; // source currently drawn by the clean view (for the preview)
	uint32_t BaseWidth() const { return baseW_; }
	uint32_t BaseHeight() const { return baseH_; }
	std::string ProgramSceneName() const;
	uint32_t EffectiveFps() const { return fps_; }
	uint32_t OutWidth() const { return outW_; }
	uint32_t OutHeight() const { return outH_; }

	// Main thread: scene mirrors were rebuilt; re-apply the current scene once Program is idle.
	void MirrorsRebuilt();

	// Number of times a scene was shown late because its mirror was not ready (diagnostic).
	uint64_t HeldFrames() const { return heldFrames_.load(); }

private:
	static void TickThunk(void *param, float seconds);
	void Tick();
	void Apply(const ProgramState &st, const OBSSource &mirror);

	SceneMirror &mirror_;
	TransitionCache &transitions_;

	OBSView view_;
	video_t *video_ = nullptr;
	uint32_t baseW_ = 0, baseH_ = 0, fps_ = 30, outW_ = 0, outH_ = 0;
	std::atomic<bool> running_{false};
	std::atomic<bool> buildRequested_{false};
	std::atomic<bool> reapply_{false};
	std::atomic<uint64_t> heldFrames_{0};

	mutable std::mutex mutex_; // guards everything below
	OBSSource activeT_;        // transition clone currently on view channel 0
	OBSSource shownMirror_;    // mirror scene currently shown
	OBSSource lastTarget_;     // original Program scene last applied
	std::string lastTargetUuid_;
	std::string lastTargetName_;
};

} // namespace melton
