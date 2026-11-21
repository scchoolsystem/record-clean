#include "settings.hpp"
#include "common.hpp"
#include <QStandardPaths>
#include <QDir>

namespace melton {

SettingsStore &SettingsStore::Get()
{
	static SettingsStore store;
	return store;
}

std::string DefaultBaseDir()
{
	QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	return QDir(docs).filePath("Melton Recordings").toStdString();
}

Settings SettingsStore::Snapshot()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return settings_;
}

void SettingsStore::Replace(const Settings &s)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		settings_ = s;
	}
	Save();
}

static std::string ConfigFile()
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path("settings.json");
	std::string out = SafeStr(path);
	bfree(path);
	return out;
}

void SettingsStore::Load()
{
	Settings s;
	std::string file = ConfigFile();
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(file.c_str(), "bak");
	if (d) {
		auto geti = [&](const char *k, int def) {
			obs_data_set_default_int(d, k, def);
			return (int)obs_data_get_int(d, k);
		};
		s.width = (uint32_t)geti("width", 1920);
		s.height = (uint32_t)geti("height", 1080);
		s.fps = (uint32_t)geti("fps", 30);
		s.videoKbps = geti("video_kbps", 6000);
		s.audioKbps = geti("audio_kbps", 160);
		s.audioTrack = geti("audio_track", 0);
		obs_data_set_default_string(d, "encoder", "auto");
		obs_data_set_default_string(d, "format", "mkv");
		obs_data_set_default_string(d, "filename_format", "%CCYY-%MM-%DD_%hh%mm%ss");
		obs_data_set_default_bool(d, "linked", true);
		s.encoderId = obs_data_get_string(d, "encoder");
		s.format = obs_data_get_string(d, "format");
		s.baseDir = obs_data_get_string(d, "base_dir");
		s.filenameFormat = obs_data_get_string(d, "filename_format");
		s.linked = obs_data_get_bool(d, "linked");
		OBSDataArrayAutoRelease arr = obs_data_get_array(d, "exclusions");
		size_t n = arr ? obs_data_array_count(arr) : 0;
		for (size_t i = 0; i < n; i++) {
			OBSDataAutoRelease e = obs_data_array_item(arr, i);
			Exclusion ex;
			ex.name = obs_data_get_string(e, "name");
			ex.uuid = obs_data_get_string(e, "uuid");
			if (!ex.name.empty() || !ex.uuid.empty())
				s.exclusions.push_back(ex);
		}
	}
	// Clamp invalid values now rather than failing later.
	if (s.width < 320 || s.width > 3840 || (s.width & 1))
		s.width = 1920;
	if (s.height < 240 || s.height > 2160 || (s.height & 1))
		s.height = 1080;
	if (s.fps < 1 || s.fps > 120)
		s.fps = 30;
	if (s.videoKbps < 500 || s.videoKbps > 100000)
		s.videoKbps = 6000;
	if (s.audioKbps < 64 || s.audioKbps > 512)
		s.audioKbps = 160;
	if (s.audioTrack < 0 || s.audioTrack > 5)
		s.audioTrack = 0;
	if (s.format != "mkv" && s.format != "mp4")
		s.format = "mkv";
	if (s.baseDir.empty())
		s.baseDir = DefaultBaseDir();
	if (s.filenameFormat.empty())
		s.filenameFormat = "%CCYY-%MM-%DD_%hh%mm%ss";
	std::lock_guard<std::mutex> lock(mutex_);
	settings_ = s;
}

void SettingsStore::Save()
{
	Settings s = Snapshot();
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_int(d, "width", s.width);
	obs_data_set_int(d, "height", s.height);
	obs_data_set_int(d, "fps", s.fps);
	obs_data_set_int(d, "video_kbps", s.videoKbps);
	obs_data_set_int(d, "audio_kbps", s.audioKbps);
	obs_data_set_int(d, "audio_track", s.audioTrack);
	obs_data_set_string(d, "encoder", s.encoderId.c_str());
	obs_data_set_string(d, "format", s.format.c_str());
	obs_data_set_string(d, "base_dir", s.baseDir.c_str());
	obs_data_set_string(d, "filename_format", s.filenameFormat.c_str());
	obs_data_set_bool(d, "linked", s.linked);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (auto &ex : s.exclusions) {
		OBSDataAutoRelease e = obs_data_create();
		obs_data_set_string(e, "name", ex.name.c_str());
		obs_data_set_string(e, "uuid", ex.uuid.c_str());
		obs_data_array_push_back(arr, e);
	}
	obs_data_set_array(d, "exclusions", arr);
	std::string file = ConfigFile();
	if (!obs_data_save_json_safe(d, file.c_str(), "tmp", "bak"))
		MLOG(LOG_WARNING, "Could not save settings to %s", file.c_str());
}

} // namespace melton
