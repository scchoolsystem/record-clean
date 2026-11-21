#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace melton {

struct Exclusion {
	std::string name;
	std::string uuid;
};

struct Settings {
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t fps = 30;
	std::string encoderId = "auto"; // "auto" = best available H.264, software fallback
	int videoKbps = 6000;
	int audioKbps = 160;
	int audioTrack = 0; // 0-based OBS audio track; must match the track the dirty recording uses
	std::string format = "mkv"; // mkv | mp4
	std::string baseDir;        // empty = <Documents>/Melton Recordings
	std::string filenameFormat = "%CCYY-%MM-%DD_%hh%mm%ss";
	bool linked = true;
	std::vector<Exclusion> exclusions;
};

class SettingsStore {
public:
	static SettingsStore &Get();
	Settings Snapshot();
	void Replace(const Settings &s);
	void Load();
	void Save();

private:
	std::mutex mutex_;
	Settings settings_;
};

std::string DefaultBaseDir();

} // namespace melton
