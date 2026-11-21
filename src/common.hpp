#pragma once
// Shared includes and helpers for Melton Dual Feed Recorder.
#include <obs.h>
#include <obs.hpp>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/base.h>
#include <util/platform.h>
#include <callback/signal.h>
#include <plugin-support.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define MLOG(level, ...) obs_log(level, __VA_ARGS__)

namespace melton {

// Run a callable on the Qt main thread (defined in obs-integration.cpp).
void RunOnMainThread(std::function<void()> fn);

inline std::string SafeStr(const char *s)
{
	return s ? std::string(s) : std::string();
}

} // namespace melton
