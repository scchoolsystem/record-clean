#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"
#include <QWidget>

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

} // namespace melton
