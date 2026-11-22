#pragma once
#include "common.hpp"
#include "config/settings.hpp"
#include "clean/clean-renderer.hpp"
#include "clean/scene-mirror.hpp"
#include "clean/source-exclusion.hpp"
#include "obs/transition-monitor.hpp"
#include "video-encoder.hpp"

namespace melton {

enum class CleanState { Stopped, Starting, Recording, Stopping };

struct CleanStatus {
	CleanState state = CleanState::Stopped;
	std::string path;
	std::string lastError;
	std::string warning;
	std::string encoderName;
	double seconds = 0;
	uint64_t bytes = 0;
	int droppedFrames = 0;
	uint32_t skippedFrames = 0;
	uint32_t totalFrames = 0;
	uint64_t freeBytes = 0;
};

// Owns the whole clean pipeline: exclusions, scene mirror, transition clones, the
// renderer, the encoders and the ffmpeg_muxer output. Main-thread object.
class RecordingManager {
public:
	RecordingManager();
	~RecordingManager();

	void Init();                  // after OBS finished loading
	void Shutdown();              // OBS exiting: stops cleanly, blocks until the file is finalised
	bool Start(std::string &err); // clean recording
	void Stop();
	bool StartPreview(std::string &err); // low-cost 640x360 mix, only for the in-dock test preview
	void StopPreview();
	bool PreviewRunning() const { return previewOnly_; }
	// "Arm": keep the clean feed running at the configured size/fps without recording, so it can be
	// shown on another monitor (projector window) or used as the "Melton Clean Feed" source.
	bool Arm(std::string &err);
	void Disarm();
	bool Armed() const { return armed_; }

	CleanStatus Status();
	ExclusionSet &Exclusions() { return exclusions_; }
	SceneMirror &Mirror() { return mirror_; }
	CleanRenderer &Renderer() { return renderer_; }
	TransitionCache &Transitions() { return transitions_; }
	void ApplySettings(const Settings &s);
	void RequestRebuild(); // coalesced, any thread

	std::function<void()> onStateChanged; // main thread

private:
	bool StartInternal(const EncoderInfo &enc, std::string &err);
	void Teardown();
	void SetState(CleanState s);
	static void OnStart(void *p, calldata_t *);
	static void OnStop(void *p, calldata_t *cd);

	ExclusionSet exclusions_;
	SceneMirror mirror_;
	TransitionCache transitions_;
	CleanRenderer renderer_;

	OBSOutput output_;
	OBSEncoder venc_, aenc_;
	OBSSignal startSig_, stopSig_;
	std::atomic<CleanState> state_{CleanState::Stopped};
	bool previewOnly_ = false;
	bool armed_ = false;
	bool retriedSoftware_ = false;
	std::string path_, lastError_, encoderName_, requestedEncoder_;
	uint64_t startNs_ = 0;
	std::atomic<bool> rebuildQueued_{false};
	std::atomic<bool> inited_{false};
};

} // namespace melton
