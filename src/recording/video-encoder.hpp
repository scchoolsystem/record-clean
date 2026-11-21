#pragma once
#include "common.hpp"

namespace melton {

struct EncoderInfo {
	std::string id;
	std::string name;
	bool hardware = false;
};

std::vector<EncoderInfo> ListH264Encoders();
// "auto" -> best available hardware encoder, else x264. Unknown ids fall back to auto.
EncoderInfo ResolveEncoder(const std::string &requested);
EncoderInfo SoftwareEncoder();
OBSEncoder CreateVideoEncoder(const EncoderInfo &info, int kbps, uint32_t fps, video_t *video);

} // namespace melton
