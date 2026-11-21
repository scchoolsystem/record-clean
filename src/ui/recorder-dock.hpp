#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QDialog;

namespace melton {

class SettingsPanel;
class SourceManagerPanel;

// The MELTON DUAL FEED dock.
class RecorderDock : public QWidget {
	Q_OBJECT
public:
	explicit RecorderDock(RecordingManager &rm, QWidget *parent = nullptr);
	~RecorderDock() override;

private:
	void StartBoth();
	void StopBoth();
	void StartClean();
	void StopClean();
	void OpenFolder();
	void ShowPreview();
	void Refresh();
	void ShowError(const QString &msg);

	RecordingManager &rm_;
	QTimer *timer_;
	QLabel *dirtyInd_, *cleanInd_;
	QLabel *obsStatus_, *program_, *dirtyTime_, *cleanTime_, *storage_, *file_, *warning_;
	QPushButton *startBoth_, *stopBoth_, *startClean_, *stopClean_;
	SettingsPanel *settings_;
	SourceManagerPanel *sources_;
	QDialog *previewDlg_ = nullptr;
	QString errorText_;
	uint64_t errorUntilNs_ = 0;
};

} // namespace melton
