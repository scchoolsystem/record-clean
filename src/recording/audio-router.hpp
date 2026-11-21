#pragma once
#include "common.hpp"

namespace melton {

// The clean feed's audio is the OBS programme mix: an AAC encoder attached to the
// same global audio pipeline (and the same mixer track) that the dirty recording
// uses. No audio device is opened a second time.
OBSEncoder CreateAudioEncoder(int kbps, int trackIndex, std::string &error);

} // namespace melton
