#include "video-encoder.hpp"

namespace melton {

static bool IsHardwareId(const std::string &id)
{
	return id.find("nvenc") != std::string::npos || id.find("amf") != std::string::npos ||
	       id.find("qsv") != std::string::npos || id.find("vaapi") != std::string::npos ||
	       id.find("videotoolbox") != std::string::npos;
}

std::vector<EncoderInfo> ListH264Encoders()
{
	std::vector<EncoderInfo> out;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (!id)
			continue;
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0)
			continue;
		uint32_t caps = obs_get_encoder_caps(id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		EncoderInfo e;
		e.id = id;
		e.name = SafeStr(obs_encoder_get_display_name(id));
		e.hardware = IsHardwareId(e.id);
		out.push_back(e);
	}
	return out;
}

EncoderInfo SoftwareEncoder()
{
	EncoderInfo e;
	e.id = "obs_x264";
	e.name = SafeStr(obs_encoder_get_display_name("obs_x264"));
	e.hardware = false;
	return e;
}

EncoderInfo ResolveEncoder(const std::string &requested)
{
	auto list = ListH264Encoders();
	auto find = [&](const std::string &id) -> const EncoderInfo * {
		for (auto &e : list)
			if (e.id == id)
				return &e;
		return nullptr;
	};
	if (requested != "auto")
		if (auto *e = find(requested))
			return *e;
	// Preference order: NVIDIA, AMD, Intel, then software.
	static const char *prefer[] = {"obs_nvenc_h264_tex", "h264_texture_amf", "obs_qsv11_v2"};
	for (auto *id : prefer)
		if (auto *e = find(id))
			return *e;
	return SoftwareEncoder();
}

OBSEncoder CreateVideoEncoder(const EncoderInfo &info, int kbps, uint32_t fps, video_t *video)
{
	OBSDataAutoRelease s = obs_encoder_defaults(info.id.c_str());
	if (!s)
		s = obs_data_create();
	obs_data_set_int(s, "bitrate", kbps);
	obs_data_set_string(s, "rate_control", "CBR");
	obs_data_set_int(s, "keyint_sec", 2);
	if (info.id == "obs_x264") {
		obs_data_set_string(s, "preset", "veryfast"); // modest CPU budget
		obs_data_set_int(s, "bitrate", kbps);
	}
	(void)fps;
	OBSEncoderAutoRelease enc = obs_video_encoder_create(info.id.c_str(), "melton-clean-video", s, nullptr);
	if (!enc)
		return nullptr;
	obs_encoder_set_video(enc, video);
	return OBSEncoder(enc.Get());
}

} // namespace melton
