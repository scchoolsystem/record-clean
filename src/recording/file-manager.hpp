#pragma once
#include "common.hpp"
#include "config/settings.hpp"

namespace melton {

struct PathResult {
	bool ok = false;
	std::string path; // full file path (UTF-8)
	std::string dir;
	std::string error;
};

constexpr uint64_t kMinFreeBytesToStart = 2ULL * 1024 * 1024 * 1024;
constexpr uint64_t kWarnFreeBytes = 5ULL * 1024 * 1024 * 1024;
constexpr uint64_t kStopFreeBytes = 300ULL * 1024 * 1024;

// <base>/<YYYY>/<MM>/<DD>/CLEAN_<formatted-time>.<ext>; never overwrites an existing file.
PathResult BuildCleanPath(const Settings &s);
uint64_t FreeSpaceFor(const std::string &dir);

} // namespace melton
