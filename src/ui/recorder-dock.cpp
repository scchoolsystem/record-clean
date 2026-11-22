#include "recorder-dock.hpp"
#include "obs/obs-integration.hpp"
#include "preview-widget.hpp"
#include "recording/file-manager.hpp"
#include "settings-dialog.hpp"
#include "source-manager.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QScreen>
#include <QDialog>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace melton {

static const char *kStyle = R"(
QWidget { background:#15181d; color:#d7dce3; font-size:12px; }
QLabel#title { font-weight:700; letter-spacing:2px; color:#ffffff; font-size:13px; }
QLabel#hint { color:#8a93a0; }
QLabel#ind { font-weight:700; padding:3px 8px; border-radius:3px; background:#2a2f37; }
QLabel#warn { color:#ffcc66; font-weight:600; }
QLabel#err { color:#ff6b6b; font-weight:600; }
QPushButton { background:#2a2f37; border:1px solid #3a414c; padding:5px 8px; border-radius:3px; }
QPushButton:hover { background:#343a44; }
QPushButton:disabled { color:#5d6672; background:#1d2127; }
QPushButton#go { background:#b3261e; border-color:#d93a31; color:white; font-weight:700; }
QPushButton#go:disabled { background:#4a2724; color:#8a6b69; }
QTabWidget::pane { border:1px solid #2a2f37; }
QTabBar::tab { background:#1d2127; padding:5px 10px; }
QTabBar::tab:selected { background:#2a2f37; color:white; }
QLineEdit, QComboBox, QSpinBox, QTreeWidget { background:#0f1216; border:1px solid #2f3640; padding:2px; }
)";

static QString Hms(double sec)
{
	int s = int(sec);
	return QString("%1:%2:%3")
		.arg(s / 3600, 2, 10, QChar('0'))
		.arg(s / 60 % 60, 2, 10, QChar('0'))
		.arg(s % 60, 2, 10, QChar('0'));
}

static void SetInd(QLabel *l, const QString &text, const QString &color)
{
	l->setText(text);
	l->setStyleSheet(QString("background:%1; color:white;").arg(color));
}

RecorderDock::RecorderDock(RecordingManager &rm, QWidget *parent) : QWidget(parent), rm_(rm)
{
	setStyleSheet(kStyle);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);

	auto *title = new QLabel("MELTON DUAL FEED");
	title->setObjectName("title");
	root->addWidget(title);

	auto *indRow = new QGridLayout;
	dirtyInd_ = new QLabel;
	cleanInd_ = new QLabel;
	dirtyInd_->setObjectName("ind");
	cleanInd_->setObjectName("ind");
	dirtyInd_->setAlignment(Qt::AlignCenter);
	cleanInd_->setAlignment(Qt::AlignCenter);
	indRow->addWidget(dirtyInd_, 0, 0);
	indRow->addWidget(cleanInd_, 0, 1);
	root->addLayout(indRow);

	auto *grid = new QGridLayout;
	grid->setColumnStretch(1, 1);
	auto addRow = [&](int r, const char *name, QLabel *&value) {
		grid->addWidget(new QLabel(name), r, 0);
		value = new QLabel("-");
		value->setTextInteractionFlags(Qt::TextSelectableByMouse);
		value->setWordWrap(true);
		grid->addWidget(value, r, 1);
	};
	addRow(0, "OBS", obsStatus_);
	addRow(1, "Program scene", program_);
	addRow(2, "Dirty time", dirtyTime_);
	addRow(3, "Clean time", cleanTime_);
	addRow(4, "Storage", storage_);
	addRow(5, "Clean file", file_);
	root->addLayout(grid);

	warning_ = new QLabel;
	warning_->setWordWrap(true);
	root->addWidget(warning_);

	auto *btns = new QGridLayout;
	startBoth_ = new QPushButton("Start Both");
	stopBoth_ = new QPushButton("Stop Both");
	startClean_ = new QPushButton("Start Clean");
	stopClean_ = new QPushButton("Stop Clean");
	auto *folder = new QPushButton("Open Recording Folder");
	startBoth_->setObjectName("go");
	btns->addWidget(startBoth_, 0, 0);
	btns->addWidget(stopBoth_, 0, 1);
	btns->addWidget(startClean_, 1, 0);
	btns->addWidget(stopClean_, 1, 1);
	btns->addWidget(folder, 2, 0, 1, 2);
	root->addLayout(btns);

	// Show the clean feed somewhere else (projector / second monitor).
	auto *out = new QGridLayout;
	arm_ = new QPushButton("Arm clean feed");
	arm_->setCheckable(true);
	arm_->setToolTip("Keep the clean feed running without recording, so it can be projected or used as a source");
	screen_ = new QComboBox;
	auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++)
		screen_->addItem(QString("Monitor %1: %2 (%3x%4)")
					 .arg(i + 1)
					 .arg(screens[i]->name())
					 .arg(screens[i]->geometry().width())
					 .arg(screens[i]->geometry().height()),
				 i);
	auto *projFull = new QPushButton("Fullscreen projector");
	auto *projWin = new QPushButton("Windowed projector");
	out->addWidget(arm_, 0, 0, 1, 2);
	out->addWidget(screen_, 1, 0, 1, 2);
	out->addWidget(projFull, 2, 0);
	out->addWidget(projWin, 2, 1);
	root->addLayout(out);
	connect(arm_, &QPushButton::clicked, this, [this]() { ToggleArm(); });
	connect(projFull, &QPushButton::clicked, this, [this]() { OpenProjector(true); });
	connect(projWin, &QPushButton::clicked, this, [this]() { OpenProjector(false); });

	auto *tabs = new QTabWidget;
	settings_ = new SettingsPanel(rm_);
	sources_ = new SourceManagerPanel(rm_);
	tabs->addTab(settings_, "Clean feed settings");
	tabs->addTab(sources_, "Graphics exclusion");
	root->addWidget(tabs, 1);

	connect(startBoth_, &QPushButton::clicked, this, [this]() { StartBoth(); });
	connect(stopBoth_, &QPushButton::clicked, this, [this]() { StopBoth(); });
	connect(startClean_, &QPushButton::clicked, this, [this]() { StartClean(); });
	connect(stopClean_, &QPushButton::clicked, this, [this]() { StopClean(); });
	connect(folder, &QPushButton::clicked, this, [this]() { OpenFolder(); });
	connect(sources_, &SourceManagerPanel::previewRequested, this, [this]() { ShowPreview(); });

	rm_.onStateChanged = [this]() {
		Refresh();
	};
	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, [this]() { Refresh(); });
	timer_->start(500);
	Refresh();
}

RecorderDock::~RecorderDock()
{
	timer_->stop();
	rm_.onStateChanged = nullptr;
	if (previewDlg_)
		rm_.StopPreview();
}

void RecorderDock::ShowError(const QString &msg)
{
	errorText_ = msg;
	errorUntilNs_ = os_gettime_ns() + 15ULL * 1000000000ULL;
	Refresh();
}

void RecorderDock::StartBoth()
{
	if (rm_.Status().state != CleanState::Stopped) {
		ShowError("Clean recording is already running.");
		return;
	}
	bool wasActive = obs_frontend_recording_active();
	if (!wasActive)
		obs_frontend_recording_start(); // OBS's own recorder; unchanged, untouched
	bool linked = SettingsStore::Get().Snapshot().linked;
	// In linked mode the clean feed starts from OBS's "recording started" event, so the OBS
	// button and hotkey work too. If OBS was already recording, start the clean feed now.
	if (wasActive || !linked) {
		std::string err;
		if (!rm_.Start(err))
			ShowError(QString::fromStdString("Clean recording did not start: " + err));
	}
}

void RecorderDock::StopBoth()
{
	if (obs_frontend_recording_active())
		obs_frontend_recording_stop();
	rm_.Stop();
}

void RecorderDock::StartClean()
{
	std::string err;
	if (!rm_.Start(err))
		ShowError(QString::fromStdString("Clean recording did not start: " + err));
}

void RecorderDock::StopClean()
{
	rm_.Stop();
}

void RecorderDock::OpenFolder()
{
	Settings s = SettingsStore::Get().Snapshot();
	QString dir = QString::fromStdString(s.baseDir);
	QDir().mkpath(dir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void RecorderDock::ShowPreview()
{
	std::string err;
	if (!rm_.StartPreview(err)) {
		ShowError(QString::fromStdString("Preview unavailable: " + err));
		return;
	}
	if (!previewDlg_) {
		previewDlg_ = new QDialog(this, Qt::Tool);
		previewDlg_->setWindowTitle("Clean feed preview");
		auto *lay = new QVBoxLayout(previewDlg_);
		lay->setContentsMargins(0, 0, 0, 0);
#ifdef _WIN32
		lay->addWidget(new PreviewWidget(rm_, previewDlg_));
#else
		lay->addWidget(new QLabel("The clean preview is available on Windows builds."));
#endif
		previewDlg_->resize(640, 360);
		connect(previewDlg_, &QDialog::finished, this, [this]() { rm_.StopPreview(); });
	}
	previewDlg_->show();
	previewDlg_->raise();
}

void RecorderDock::ToggleArm()
{
	if (rm_.Armed()) {
		rm_.Disarm();
	} else {
		std::string err;
		if (!rm_.Arm(err))
			ShowError(QString::fromStdString("Cannot arm clean feed: " + err));
	}
	Refresh();
}

void RecorderDock::OpenProjector(bool fullscreen)
{
	if (!rm_.Armed()) {
		std::string err;
		if (!rm_.Arm(err)) {
			ShowError(QString::fromStdString("Cannot arm clean feed: " + err));
			return;
		}
	}
#ifdef _WIN32
	auto screens = QGuiApplication::screens();
	int i = screen_->currentData().toInt();
	QScreen *sc = (i >= 0 && i < screens.size()) ? screens[i] : QGuiApplication::primaryScreen();
	new ProjectorWindow(rm_, sc, fullscreen); // deletes itself on close
#else
	(void)fullscreen;
	ShowError("The projector window is available on Windows builds.");
#endif
	Refresh();
}

void RecorderDock::Refresh()
{
	arm_->setChecked(rm_.Armed());
	arm_->setText(rm_.Armed() ? "Clean feed ARMED (click to disarm)" : "Arm clean feed");
	CleanStatus st = rm_.Status();
	bool dirty = obs_frontend_recording_active();
	bool clean = st.state == CleanState::Recording;

	SetInd(dirtyInd_, dirty ? "DIRTY: RECORDING" : "DIRTY: STOPPED", dirty ? "#b3261e" : "#2a2f37");
	switch (st.state) {
	case CleanState::Recording:
		SetInd(cleanInd_, "CLEAN: RECORDING", "#b3261e");
		break;
	case CleanState::Starting:
		SetInd(cleanInd_, "CLEAN: STARTING", "#9a6b00");
		break;
	case CleanState::Stopping:
		SetInd(cleanInd_, "CLEAN: STOPPING", "#9a6b00");
		break;
	default:
		SetInd(cleanInd_, "CLEAN: STOPPED", "#2a2f37");
	}

	obsStatus_->setText(obs_video_active() ? "Connected (running inside OBS)" : "Video not active");
	std::string prog = rm_.Renderer().ProgramSceneName();
	if (prog.empty()) {
		obs_source_t *cur = obs_frontend_get_current_scene();
		prog = SafeStr(cur ? obs_source_get_name(cur) : nullptr);
		obs_source_release(cur);
	}
	program_->setText(QString::fromStdString(prog));

	uint64_t ds = Integration::DirtyStartNs();
	dirtyTime_->setText(dirty && ds ? Hms(double(os_gettime_ns() - ds) / 1e9) : "-");
	cleanTime_->setText(clean ? Hms(st.seconds) : "-");

	Settings s = SettingsStore::Get().Snapshot();
	uint64_t freeBytes = st.freeBytes ? st.freeBytes : FreeSpaceFor(s.baseDir);
	storage_->setText(
		QString("%1 GB free   %2")
			.arg(double(freeBytes) / (1024.0 * 1024 * 1024), 0, 'f', 1)
			.arg(clean ? QString("(clean file %1 MB)").arg(st.bytes / (1024 * 1024)) : QString()));
	file_->setText(st.state != CleanState::Stopped ? QString::fromStdString(st.path) : "-");

	QString msg;
	QString objName = "warn";
	std::string auto_ = Integration::TakeStartMessage();
	if (!auto_.empty())
		ShowError(QString::fromStdString(auto_));
	if (!errorText_.isEmpty() && os_gettime_ns() < errorUntilNs_) {
		msg = errorText_;
		objName = "err";
	} else if (!st.lastError.empty() && st.state == CleanState::Stopped) {
		msg = QString::fromStdString("Last clean recording problem: " + st.lastError);
		objName = "err";
	} else if (clean && !dirty) {
		msg = "OBS native (dirty) recording is NOT running while the clean recording is active.";
	} else if (!st.warning.empty()) {
		msg = QString::fromStdString(st.warning);
	} else if (clean && st.state == CleanState::Recording) {
		msg = QString("Encoder: %1").arg(QString::fromStdString(st.encoderName));
		objName = "hint";
	}
	warning_->setObjectName(objName);
	warning_->setStyleSheet(objName == "err"    ? "color:#ff6b6b;font-weight:600;"
				: objName == "hint" ? "color:#8a93a0;"
						    : "color:#ffcc66;font-weight:600;");
	warning_->setText(msg);

	bool idle = st.state == CleanState::Stopped;
	startBoth_->setEnabled(idle || !dirty);
	stopBoth_->setEnabled(dirty || !idle);
	startClean_->setEnabled(idle);
	stopClean_->setEnabled(st.state == CleanState::Recording || st.state == CleanState::Starting);
}

} // namespace melton
