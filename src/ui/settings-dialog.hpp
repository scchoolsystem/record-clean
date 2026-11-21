#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"
#include <QWidget>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QLabel;

namespace melton {

// Clean-feed settings. Every change is validated and saved immediately; a running
// recording keeps the values it started with.
class SettingsPanel : public QWidget {
	Q_OBJECT
public:
	explicit SettingsPanel(RecordingManager &rm, QWidget *parent = nullptr);
	void Reload();

private:
	void Commit();
	void Browse();

	RecordingManager &rm_;
	bool loading_ = false;
	QComboBox *resolution_, *fps_, *encoder_, *audioBitrate_, *format_, *mode_;
	QSpinBox *videoBitrate_, *track_;
	QLineEdit *dir_, *filename_;
	QLabel *hint_;
};

} // namespace melton
