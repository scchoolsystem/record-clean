#include "clean-renderer.hpp"
#include "obs/scene-monitor.hpp"

namespace melton {

CleanRenderer::CleanRenderer(SceneMirror &mirror, TransitionCache &transitions)
	: mirror_(mirror), transitions_(transitions)
{
}

CleanRenderer::~CleanRenderer()
{
	Stop();
}

OBSSource CleanRenderer::Output() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return activeT_;
}

std::string CleanRenderer::ProgramSceneName() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return lastTargetName_;
}

bool CleanRenderer::Start(uint32_t outW, uint32_t outH, uint32_t fps, std::string &error)
{
	if (running_)
		return true;

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		error = "OBS video is not initialised";
		return false;
	}
	baseW_ = ovi.base_width;
	baseH_ = ovi.base_height;
	uint32_t mainFps = ovi.fps_den ? ovi.fps_num / ovi.fps_den : 30;
	if (fps > mainFps) {
		MLOG(LOG_WARNING, "Clean FPS %u exceeds OBS FPS %u; clamping to %u", fps, mainFps, mainFps);
		fps = mainFps;
	}
	fps_ = fps;

	// Same canvas as OBS (so scene transforms match exactly), own output size/fps,
	// always 8-bit NV12 for H.264.
	ovi.output_width = outW;
	ovi.output_height = outH;
	ovi.fps_num = fps;
	ovi.fps_den = 1;
	ovi.output_format = VIDEO_FORMAT_NV12;
	if (ovi.colorspace != VIDEO_CS_601 && ovi.colorspace != VIDEO_CS_709 && ovi.colorspace != VIDEO_CS_SRGB)
		ovi.colorspace = VIDEO_CS_709;

	mirror_.RebuildAll();
	transitions_.Refresh(baseW_, baseH_);

	ProgramState st = ReadProgram();
	if (!st.target) {
		error = "No Program scene is active in OBS";
		return false;
	}
	OBSSource mirrorScene = mirror_.Lookup(st.target);
	OBSSource clone = transitions_.Get(st.transition);
	if (!mirrorScene || !clone) {
		MLOG(LOG_WARNING, "Clean start diag: target='%s' uuid=%s mirror=%s transition='%s' id=%s clone=%s",
		     SafeStr(obs_source_get_name(st.target)).c_str(), SafeStr(obs_source_get_uuid(st.target)).c_str(),
		     mirrorScene ? "ok" : "MISSING", SafeStr(obs_source_get_name(st.transition)).c_str(),
		     SafeStr(obs_source_get_id(st.transition)).c_str(), clone ? "ok" : "MISSING");
		error = "Could not prepare the clean scene or transition";
		return false;
	}

	OBSView view(obs_view_create());
	video_t *video = obs_view_add2(view, &ovi);
	if (!video) {
		error = "Could not create the clean video mix (GPU/encoder limit?)";
		return false;
	}
	view_ = std::move(view);
	video_ = video;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		activeT_ = clone;
		shownMirror_ = mirrorScene;
		lastTarget_ = st.target;
		lastTargetUuid_ = SafeStr(obs_source_get_uuid(st.target));
		lastTargetName_ = SafeStr(obs_source_get_name(st.target));
		obs_transition_set(clone, mirrorScene);
		obs_view_set_source(view_, 0, clone);
	}
	running_ = true;
	obs_add_tick_callback(TickThunk, this);
	MLOG(LOG_INFO, "Clean renderer started: %ux%u @ %u fps (canvas %ux%u), Program scene '%s'", outW, outH, fps,
	     baseW_, baseH_, lastTargetName_.c_str());
	return true;
}

void CleanRenderer::Stop()
{
	if (!running_.exchange(false) && !view_)
		return;
	// After this returns no tick is in flight.
	obs_remove_tick_callback(TickThunk, this);
	if (view_) {
		obs_view_set_source(view_, 0, nullptr);
		obs_view_remove(view_); // mix is freed by the graphics thread; callers must have released encoders first
		view_ = nullptr;        // OBSView -> obs_view_destroy
	}
	video_ = nullptr;
	std::lock_guard<std::mutex> lock(mutex_);
	activeT_ = nullptr;
	shownMirror_ = nullptr;
	lastTarget_ = nullptr;
	lastTargetUuid_.clear();
}

void CleanRenderer::TickThunk(void *param, float)
{
	static_cast<CleanRenderer *>(param)->Tick();
}

void CleanRenderer::Tick()
{
	if (!running_)
		return;
	ProgramState st = ReadProgram();
	if (!st.target)
		return;
	const char *uuid = obs_source_get_uuid(st.target);
	if (!uuid)
		return;

	bool changed;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		changed = lastTargetUuid_ != uuid;
	}

	if (!changed) {
		// Mirrors were rebuilt: swap in the new one once OBS's Program transition has finished.
		if (reapply_ && st.idle) {
			OBSSource m = mirror_.Lookup(st.target);
			if (m) {
				std::lock_guard<std::mutex> lock(mutex_);
				if (activeT_ && m != shownMirror_) {
					obs_transition_set(activeT_, m);
					shownMirror_ = m;
				}
				reapply_ = false;
			}
		}
		return;
	}

	OBSSource m = mirror_.Lookup(st.target);
	if (!m) {
		// Scene unknown to the mirror table (just created?). Keep showing the previous
		// scene (never black) and ask the main thread to rebuild.
		heldFrames_++;
		if (!buildRequested_.exchange(true)) {
			RunOnMainThread([this]() {
				if (running_) {
					mirror_.RebuildAll();
					transitions_.Refresh(baseW_, baseH_);
				}
				buildRequested_ = false;
			});
		}
		return;
	}
	Apply(st, m);
}

void CleanRenderer::Apply(const ProgramState &st, const OBSSource &m)
{
	std::lock_guard<std::mutex> lock(mutex_);
	OBSSource clone = transitions_.Get(st.transition);
	if (!clone)
		clone = transitions_.Cut();
	if (!clone)
		return;

	int duration = transitions_.DurationFor(st.target);
	bool cut = TransitionCache::IsCut(clone) || duration <= 0;

	if (clone != activeT_) {
		// Hand the picture currently on screen to the new transition before it takes the
		// channel, so there is never a black frame.
		obs_transition_set(clone, shownMirror_);
		obs_view_set_source(view_, 0, clone);
		activeT_ = clone;
	}
	if (cut) {
		obs_transition_set(clone, m);
	} else if (!obs_transition_start(clone, OBS_TRANSITION_MODE_AUTO, (uint32_t)duration, m)) {
		MLOG(LOG_WARNING, "Clean transition could not start; cutting instead");
		obs_transition_set(clone, m);
	}
	shownMirror_ = m;
	lastTarget_ = st.target;
	lastTargetUuid_ = SafeStr(obs_source_get_uuid(st.target));
	lastTargetName_ = SafeStr(obs_source_get_name(st.target));
}

void CleanRenderer::MirrorsRebuilt()
{
	reapply_ = true;
}

} // namespace melton
