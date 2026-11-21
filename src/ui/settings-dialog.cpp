#include "settings-dialog.hpp"
#include "recording/video-encoder.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace melton {

SettingsPanel::SettingsPanel(RecordingManager &rm, QWidget *parent) : QWidget(parent), rm_(rm)
{
	auto *form = new QFormLayout(this);
	form->setContentsMargins(8, 8, 8, 8);

	resolution_ = new QComboBox;
	resolution_->addItem("1920 x 1080 (1080p)", QSize(1920, 1080));
	resolution_->addItem("1280 x 720 (720p)", QSize(1280, 720));
	resolution_->addItem("2560 x 1440 (1440p)", QSize(2560, 1440));
	resolution_->addItem("854 x 480 (480p)", QSize(854, 480));
	form->addRow("Resolution", resolution_);

	fps_ = new QComboBox;
	for (int f : {24, 25, 30, 50, 60})
		fps_->addItem(QString::number(f) + " fps", f);
	form->addRow("Frame rate", fps_);

	encoder_ = new QComboBox;
	encoder_->addItem("Auto (hardware if available, else x264)", "auto");
	for (auto &e : ListH264Encoders())
		encoder_->addItem(QString::fromStdString(e.name) + (e.hardware ? " [HW]" : " [SW]"),
				  QString::fromStdString(e.id));
	form->addRow("Video encoder", encoder_);

	videoBitrate_ = new QSpinBox;
	videoBitrate_->setRange(500, 100000);
	videoBitrate_->setSuffix(" kbps");
	videoBitrate_->setSingleStep(500);
	form->addRow("Video bitrate", videoBitrate_);

	audioBitrate_ = new QComboBox;
	for (int b : {96, 128, 160, 192, 256, 320})
		audioBitrate_->addItem(QString::number(b) + " kbps", b);
	form->addRow("Audio bitrate", audioBitrate_);

	track_ = new QSpinBox;
	track_->setRange(1, 6);
	form->addRow("OBS audio track", track_);

	format_ = new QComboBox;
	format_->addItem("MKV (recommended)", "mkv");
	format_->addItem("MP4 (fragmented)", "mp4");
	form->addRow("Format", format_);

	dir_ = new QLineEdit;
	auto *browse = new QPushButton("Browse...");
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(dir_, 1);
	dirRow->addWidget(browse);
	form->addRow("Destination", dirRow);

	filename_ = new QLineEdit;
	filename_->setToolTip("OBS filename tokens, e.g. %CCYY-%MM-%DD_%hh%mm%ss. \"CLEAN_\" is added in front.");
	form->addRow("Filename format", filename_);

	mode_ = new QComboBox;
	mode_->addItem("Linked: OBS recording and clean recording start/stop together", true);
	mode_->addItem("Independent: clean recording is started/stopped on its own", false);
	form->addRow("Mode", mode_);

	hint_ = new QLabel("Changes are saved automatically and apply to the next clean recording.");
	hint_->setWordWrap(true);
	hint_->setObjectName("hint");
	form->addRow(hint_);

	Reload();

	auto commit = [this]() { Commit(); };
	connect(resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(fps_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(encoder_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(audioBitrate_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(format_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commit);
	connect(videoBitrate_, &QSpinBox::editingFinished, this, commit);
	connect(track_, QOverload<int>::of(&QSpinBox::valueChanged), this, commit);
	connect(dir_, &QLineEdit::editingFinished, this, commit);
	connect(filename_, &QLineEdit::editingFinished, this, commit);
	connect(browse, &QPushButton::clicked, this, [this]() { Browse(); });
}

void SettingsPanel::Reload()
{
	loading_ = true;
	Settings s = SettingsStore::Get().Snapshot();
	int i = resolution_->findData(QSize(int(s.width), int(s.height)));
	resolution_->setCurrentIndex(i < 0 ? 0 : i);
	i = fps_->findData(int(s.fps));
	fps_->setCurrentIndex(i < 0 ? 2 : i);
	i = encoder_->findData(QString::fromStdString(s.encoderId));
	encoder_->setCurrentIndex(i < 0 ? 0 : i);
	videoBitrate_->setValue(s.videoKbps);
	i = audioBitrate_->findData(s.audioKbps);
	audioBitrate_->setCurrentIndex(i < 0 ? 2 : i);
	track_->setValue(s.audioTrack + 1);
	i = format_->findData(QString::fromStdString(s.format));
	format_->setCurrentIndex(i < 0 ? 0 : i);
	dir_->setText(QString::fromStdString(s.baseDir));
	filename_->setText(QString::fromStdString(s.filenameFormat));
	mode_->setCurrentIndex(s.linked ? 0 : 1);
	loading_ = false;
}

void SettingsPanel::Commit()
{
	if (loading_)
		return;
	Settings s = SettingsStore::Get().Snapshot(); // keeps the exclusion list
	QSize r = resolution_->currentData().toSize();
	s.width = uint32_t(r.width());
	s.height = uint32_t(r.height());
	s.fps = uint32_t(fps_->currentData().toInt());
	s.encoderId = encoder_->currentData().toString().toStdString();
	s.videoKbps = videoBitrate_->value();
	s.audioKbps = audioBitrate_->currentData().toInt();
	s.audioTrack = track_->value() - 1;
	s.format = format_->currentData().toString().toStdString();
	QString d = dir_->text().trimmed();
	s.baseDir = d.isEmpty() ? DefaultBaseDir() : d.toUtf8().toStdString();
	QString f = filename_->text().trimmed();
	s.filenameFormat = f.isEmpty() ? "%CCYY-%MM-%DD_%hh%mm%ss" : f.toUtf8().toStdString();
	s.linked = mode_->currentData().toBool();
	SettingsStore::Get().Replace(s);

	// Warn early when the chosen output is more than the main canvas can supply.
	obs_video_info ovi;
	QString warn = "Changes are saved automatically and apply to the next clean recording.";
	if (obs_get_video_info(&ovi)) {
		uint32_t mainFps = ovi.fps_den ? ovi.fps_num / ovi.fps_den : 30;
		if (s.fps > mainFps)
			warn = QString("OBS runs at %1 fps; the clean feed will be limited to that.").arg(mainFps);
	}
	hint_->setText(warn);
}

void SettingsPanel::Browse()
{
	QString d = QFileDialog::getExistingDirectory(this, "Clean recording folder", dir_->text());
	if (!d.isEmpty()) {
		dir_->setText(d);
		Commit();
	}
}

} // namespace melton
