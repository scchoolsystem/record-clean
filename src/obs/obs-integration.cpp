#include "obs-integration.hpp"
#include "ui/recorder-dock.hpp"
#include <QCoreApplication>
#include <QMetaObject>

namespace melton {

std::atomic<bool> g_alive{false};
static std::unique_ptr<RecordingManager> g_manager;
static std::atomic<uint64_t> g_dirtyStartNs{0};
static std::mutex g_msgMutex;
static std::string g_startMessage;

void RunOnMainThread(std::function<void()> fn)
{
	if (!g_alive.load() || !QCoreApplication::instance())
		return;
	QMetaObject::invokeMethod(
		QCoreApplication::instance(),
		[fn = std::move(fn)]() {
			if (g_alive.load())
				fn();
		},
		Qt::QueuedConnection);
}

RecordingManager &Integration::Manager()
{
	return *g_manager;
}

RecordingManager *Integration::MaybeManager()
{
	return g_alive.load() ? g_manager.get() : nullptr;
}

uint64_t Integration::DirtyStartNs()
{
	return g_dirtyStartNs.load();
}

std::string Integration::TakeStartMessage()
{
	std::lock_guard<std::mutex> lock(g_msgMutex);
	std::string m;
	m.swap(g_startMessage);
	return m;
}

static void RefreshTransitionState()
{
	if (!g_manager)
		return;
	g_manager->Transitions().SetFrontendDuration(obs_frontend_get_transition_duration());
	if (g_manager->Renderer().Running())
		g_manager->Transitions().Refresh(g_manager->Renderer().BaseWidth(), g_manager->Renderer().BaseHeight());
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!g_manager)
		return;
	RecordingManager &m = *g_manager;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		m.Init();
		RefreshTransitionState();
		MLOG(LOG_INFO, "Melton Dual Feed Recorder ready");
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
		RefreshTransitionState();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		m.RequestRebuild();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		if (m.Status().state != CleanState::Stopped) {
			MLOG(LOG_WARNING, "Scene collection is changing: stopping clean recording");
			m.Stop();
		}
		m.Disarm();
		m.StopPreview();
		m.Mirror().Clear();
		m.Transitions().Clear();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		m.Init();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED: {
		g_dirtyStartNs = os_gettime_ns();
		Settings s = SettingsStore::Get().Snapshot();
		// Linked mode: one recording command (OBS's own button, hotkey or our dock) starts both.
		if (s.linked && m.Status().state == CleanState::Stopped) {
			std::string err;
			if (!m.Start(err)) {
				std::lock_guard<std::mutex> lock(g_msgMutex);
				g_startMessage = "Clean recording did not start: " + err;
			}
		}
		break;
	}
	case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		g_dirtyStartNs = 0;
		if (SettingsStore::Get().Snapshot().linked && m.Status().state != CleanState::Stopped)
			m.Stop(); // linked: stop both together
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		m.Shutdown(); // finalise the clean file before OBS tears the UI down
		break;
	default:
		break;
	}
}

void Integration::Init()
{
	SettingsStore::Get().Load();
	g_manager = std::make_unique<RecordingManager>();
	g_alive = true;
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

void Integration::CreateDock()
{
	auto *dock = new RecorderDock(*g_manager);
	if (!obs_frontend_add_dock_by_id("melton-dual-feed", "MELTON DUAL FEED", static_cast<QWidget *>(dock))) {
		MLOG(LOG_ERROR, "Could not register the MELTON DUAL FEED dock");
		delete dock;
	}
}

void Integration::Shutdown()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	if (g_manager)
		g_manager->Shutdown();
	g_alive = false; // pending queued lambdas become no-ops
	g_manager.reset();
}

} // namespace melton
