#include "recording-manager.hpp"
#include "audio-router.hpp"
#include "file-manager.hpp"

namespace melton {

RecordingManager::RecordingManager() : mirror_(exclusions_), renderer_(mirror_, transitions_)
{
	mirror_.onDirty = [this]() { RequestRebuild(); };
	exclusions_.onChanged = [this]() { RequestRebuild(); };
}

RecordingManager::~RecordingManager() = default;

void RecordingManager::Init()
{
	exclusions_.Set(SettingsStore::Get().Snapshot().exclusions);
	exclusions_.Refresh();
	inited_ = true;
}

void RecordingManager::ApplySettings(const Settings &s)
{
	exclusions_.Set(s.exclusions);
	exclusions_.Refresh();
	RequestRebuild();
}

void RecordingManager::RequestRebuild()
{
	if (!inited_ || rebuildQueued_.exchange(true))
		return;
	RunOnMainThread([this]() {
		// Coalesce bursts (dragging an item fires many signals) into one rebuild.
		rebuildQueued_ = false;
		if (!renderer_.Running())
			return; // mirrors are rebuilt on every start; nothing is rendering now
		mirror_.RebuildAll();
		renderer_.MirrorsRebuilt();
	});
}

void RecordingManager::SetState(CleanState s)
{
	state_ = s;
	if (onStateChanged)
		onStateChanged();
}

bool RecordingManager::StartPreview(std::string &err)
{
	if (renderer_.Running())
		return true;
	// Low-cost preview mix; replaced by the full-size mix when recording starts.
	if (!renderer_.Start(640, 360, 30, err))
		return false;
	previewOnly_ = true;
	return true;
}

void RecordingManager::StopPreview()
{
	if (previewOnly_ && state_ == CleanState::Stopped) {
		renderer_.Stop();
		previewOnly_ = false;
	}
}

bool RecordingManager::Start(std::string &err)
{
	if (state_ != CleanState::Stopped) {
		err = "Clean recording is already running";
		return false;
	}
	Settings s = SettingsStore::Get().Snapshot();
	requestedEncoder_ = s.encoderId;
	retriedSoftware_ = false;
	if (previewOnly_) { // swap the preview mix for the full-size one
		renderer_.Stop();
		previewOnly_ = false;
	}
	EncoderInfo enc = ResolveEncoder(s.encoderId);
	bool ok = StartInternal(enc, err);
	if (!ok && enc.hardware && s.encoderId == "auto") {
		MLOG(LOG_WARNING, "Hardware encoder '%s' failed (%s); falling back to software x264", enc.id.c_str(),
		     err.c_str());
		retriedSoftware_ = true;
		err.clear();
		ok = StartInternal(SoftwareEncoder(), err);
	}
	if (!ok) {
		lastError_ = err;
		Teardown();
		SetState(CleanState::Stopped);
		MLOG(LOG_ERROR, "Clean recording failed to start: %s", err.c_str());
	}
	return ok;
}

bool RecordingManager::StartInternal(const EncoderInfo &enc, std::string &err)
{
	Settings s = SettingsStore::Get().Snapshot();
	Teardown();
	SetState(CleanState::Starting);

	PathResult pr = BuildCleanPath(s);
	if (!pr.ok) {
		err = pr.error;
		return false;
	}
	if (!renderer_.Start(s.width, s.height, s.fps, err))
		return false;

	venc_ = CreateVideoEncoder(enc, s.videoKbps, renderer_.EffectiveFps(), renderer_.Video());
	if (!venc_) {
		err = "Could not create video encoder '" + enc.id + "'";
		return false;
	}
	aenc_ = CreateAudioEncoder(s.audioKbps, s.audioTrack, err);
	if (!aenc_)
		return false;

	OBSDataAutoRelease os = obs_data_create();
	obs_data_set_string(os, "path", pr.path.c_str());
	if (s.format == "mp4") // fragmented MP4 survives a crash; MKV is the recommended default
		obs_data_set_string(os, "muxer_settings", "movflags=frag_keyframe+empty_moov+delay_moov");
	OBSOutputAutoRelease out = obs_output_create("ffmpeg_muxer", "melton-clean-output", os, nullptr);
	if (!out) {
		err = "Could not create the recording output";
		return false;
	}
	output_ = OBSOutput(out.Get());
	obs_output_set_video_encoder(output_, venc_);
	obs_output_set_audio_encoder(output_, aenc_, 0);
	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	startSig_.Connect(sh, "start", OnStart, this);
	stopSig_.Connect(sh, "stop", OnStop, this);

	path_ = pr.path;
	encoderName_ = enc.name;
	if (!obs_output_start(output_)) {
		const char *le = obs_output_get_last_error(output_);
		err = (le && *le) ? le : "The output refused to start";
		return false;
	}
	startNs_ = os_gettime_ns();
	MLOG(LOG_INFO, "Clean recording starting: %s (%s, %d kbps video, %d kbps audio, track %d)", path_.c_str(),
	     enc.id.c_str(), s.videoKbps, s.audioKbps, s.audioTrack + 1);
	return true; // state becomes Recording when the "start" signal arrives
}

void RecordingManager::OnStart(void *p, calldata_t *)
{
	auto *self = static_cast<RecordingManager *>(p);
	RunOnMainThread([self]() {
		if (self->state_ == CleanState::Starting) {
			self->startNs_ = os_gettime_ns();
			self->SetState(CleanState::Recording);
			MLOG(LOG_INFO, "Clean recording started");
		}
	});
}

void RecordingManager::OnStop(void *p, calldata_t *cd)
{
	auto *self = static_cast<RecordingManager *>(p);
	long long code = 0;
	calldata_get_int(cd, "code", &code);
	const char *le = nullptr;
	calldata_get_string(cd, "last_error", &le);
	std::string error = SafeStr(le);
	RunOnMainThread([self, code, error]() {
		bool wasStarting = self->state_ == CleanState::Starting;
		bool requested = self->state_ == CleanState::Stopping;
		if (code != OBS_OUTPUT_SUCCESS && !requested) {
			std::string msg = error.empty() ? ("output stopped with code " + std::to_string(code)) : error;
			self->lastError_ = msg;
			MLOG(LOG_ERROR, "Clean recording stopped unexpectedly: %s", msg.c_str());
			if (wasStarting && !self->retriedSoftware_ && self->requestedEncoder_ == "auto") {
				self->retriedSoftware_ = true;
				MLOG(LOG_WARNING, "Encoder failed during start; retrying with software x264");
				self->Teardown();
				std::string err;
				if (!self->StartInternal(SoftwareEncoder(), err)) {
					self->lastError_ = err;
					self->Teardown();
					self->SetState(CleanState::Stopped);
				}
				return;
			}
		}
		MLOG(LOG_INFO, "Clean recording finalised: %s", self->path_.c_str());
		self->Teardown();
		self->SetState(CleanState::Stopped);
	});
}

void RecordingManager::Stop()
{
	if (state_ == CleanState::Recording || state_ == CleanState::Starting) {
		SetState(CleanState::Stopping);
		if (output_)
			obs_output_stop(output_); // async: flushes encoders, finalises the file, then "stop" signal
		else
			Teardown(), SetState(CleanState::Stopped);
	}
}

void RecordingManager::Teardown()
{
	startSig_.Disconnect();
	stopSig_.Disconnect();
	if (output_ && obs_output_active(output_))
		obs_output_force_stop(output_);
	output_ = nullptr;
	// Encoders must be released before the view/mix that feeds them goes away.
	venc_ = nullptr;
	aenc_ = nullptr;
	renderer_.Stop();
}

void RecordingManager::Shutdown()
{
	inited_ = false;
	if (output_ && obs_output_active(output_)) {
		MLOG(LOG_INFO, "OBS is closing: finalising clean recording");
		state_ = CleanState::Stopping;
		startSig_.Disconnect();
		stopSig_.Disconnect();
		obs_output_stop(output_);
		for (int i = 0; i < 200 && obs_output_active(output_); i++) // up to 10 s
			os_sleep_ms(50);
		if (obs_output_active(output_)) {
			MLOG(LOG_WARNING, "Clean recording did not stop in time; forcing");
			obs_output_force_stop(output_);
		}
	}
	Teardown();
	mirror_.Clear();
	transitions_.Clear();
	state_ = CleanState::Stopped;
}

CleanStatus RecordingManager::Status()
{
	CleanStatus st;
	st.state = state_;
	st.path = path_;
	st.lastError = lastError_;
	st.encoderName = encoderName_;
	if (output_ && (state_ == CleanState::Recording || state_ == CleanState::Stopping)) {
		st.seconds = (double)(os_gettime_ns() - startNs_) / 1e9;
		st.bytes = obs_output_get_total_bytes(output_);
		st.droppedFrames = obs_output_get_frames_dropped(output_);
		st.totalFrames = (uint32_t)obs_output_get_total_frames(output_);
		if (renderer_.Video())
			st.skippedFrames = video_output_get_skipped_frames(renderer_.Video());
		Settings s = SettingsStore::Get().Snapshot();
		st.freeBytes = FreeSpaceFor(s.baseDir);
		double frameNs = 1e9 / (double)(renderer_.EffectiveFps() ? renderer_.EffectiveFps() : 30);
		if (st.freeBytes < kWarnFreeBytes)
			st.warning = "Low disk space";
		if (st.skippedFrames > 0 || st.droppedFrames > 0)
			st.warning = "Frames lost: encoder/GPU cannot sustain this setting";
		else if ((double)obs_get_average_frame_time_ns() > frameNs * 0.9)
			st.warning = "OBS render time is near the frame budget; lower resolution/FPS";
		if (st.freeBytes < kStopFreeBytes && state_ == CleanState::Recording) {
			MLOG(LOG_ERROR, "Disk almost full; stopping clean recording to keep the file intact");
			lastError_ = "Stopped: disk almost full";
			Stop();
		}
	} else {
		Settings s = SettingsStore::Get().Snapshot();
		st.freeBytes = FreeSpaceFor(s.baseDir);
	}
	return st;
}

} // namespace melton
