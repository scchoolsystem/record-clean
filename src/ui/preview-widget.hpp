#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"
#include <QWidget>

class QScreen;
class QKeyEvent;
class QMouseEvent;

namespace melton {

// Native-window OBS display that shows the clean composition (Windows only).
class PreviewWidget : public QWidget {
	Q_OBJECT
public:
	explicit PreviewWidget(RecordingManager &rm, QWidget *parent = nullptr);
	~PreviewWidget() override;
	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void CreateDisplay();
	void DestroyDisplay();
	static void Draw(void *param, uint32_t cx, uint32_t cy);

	RecordingManager &rm_;
	obs_display_t *display_ = nullptr;
};

// Frameless window showing the clean feed, meant to be put fullscreen on another monitor
// or projector. Press Esc or double-click to close.
class ProjectorWindow : public QWidget {
	Q_OBJECT
public:
	ProjectorWindow(RecordingManager &rm, QScreen *screen, bool fullscreen);

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
};

} // namespace melton
