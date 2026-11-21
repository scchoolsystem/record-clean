#include "audio-router.hpp"

namespace melton {

OBSEncoder CreateAudioEncoder(int kbps, int trackIndex, std::string &error)
{
	audio_t *audio = obs_get_audio();
	if (!audio) {
		error = "OBS audio is not initialised";
		return nullptr;
	}
	obs_audio_info oai;
	if (obs_get_audio_info(&oai) && oai.samples_per_sec != 44100 && oai.samples_per_sec != 48000)
		MLOG(LOG_WARNING, "OBS audio sample rate is %u Hz; AAC works best at 44.1/48 kHz", oai.samples_per_sec);

	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_int(s, "bitrate", kbps);
	OBSEncoderAutoRelease enc =
		obs_audio_encoder_create("ffmpeg_aac", "melton-clean-audio", s, (size_t)trackIndex, nullptr);
	if (!enc) {
		error = "Could not create the AAC audio encoder";
		return nullptr;
	}
	obs_encoder_set_audio(enc, audio);
	return OBSEncoder(enc.Get());
}

} // namespace melton
