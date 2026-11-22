#include "preview-widget.hpp"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

namespace melton {

PreviewWidget::PreviewWidget(RecordingManager &rm, QWidget *parent) : QWidget(parent), rm_(rm)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NoSystemBackground);
	setMinimumSize(320, 180);
}

PreviewWidget::~PreviewWidget()
{
	DestroyDisplay();
}

void PreviewWidget::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	CreateDisplay();
}

void PreviewWidget::hideEvent(QHideEvent *e)
{
	QWidget::hideEvent(e);
	DestroyDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	if (display_) {
		qreal dpr = devicePixelRatioF();
		obs_display_resize(display_, uint32_t(e->size().width() * dpr), uint32_t(e->size().height() * dpr));
	}
}

void PreviewWidget::CreateDisplay()
{
#ifdef _WIN32
	if (display_ || !isVisible())
		return;
	qreal dpr = devicePixelRatioF();
	gs_init_data info = {};
	info.cx = uint32_t(width() * dpr);
	info.cy = uint32_t(height() * dpr);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(winId());
	display_ = obs_display_create(&info, 0xFF101010);
	if (display_)
		obs_display_add_draw_callback(display_, Draw, this);
	else
		MLOG(LOG_WARNING, "Could not create the clean preview display");
#endif
}

void PreviewWidget::DestroyDisplay()
{
	if (display_) {
		obs_display_remove_draw_callback(display_, Draw, this);
		obs_display_destroy(display_);
		display_ = nullptr;
	}
}

void PreviewWidget::Draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PreviewWidget *>(param);
	CleanRenderer &r = self->rm_.Renderer();
	OBSSource out = r.Output();
	uint32_t bw = r.BaseWidth(), bh = r.BaseHeight();
	if (!out || !bw || !bh || !cx || !cy)
		return;
	float scale = std::min(float(cx) / bw, float(cy) / bh);
	int w = int(bw * scale), h = int(bh * scale);
	int x = (int(cx) - w) / 2, y = (int(cy) - h) / 2;
	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(bw), 0.0f, float(bh), -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);
	obs_source_video_render(out);
	gs_projection_pop();
	gs_viewport_pop();
}

ProjectorWindow::ProjectorWindow(RecordingManager &rm, QScreen *screen, bool fullscreen)
	: QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Melton Clean Feed");
	setStyleSheet("background:black;");
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->addWidget(new PreviewWidget(rm, this));
	if (screen) {
		setScreen(screen);
		move(screen->geometry().topLeft());
	}
	if (fullscreen) {
		if (screen)
			resize(screen->geometry().size());
		showFullScreen();
	} else {
		resize(960, 540);
		show();
	}
}

void ProjectorWindow::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Escape)
		close();
	else
		QWidget::keyPressEvent(e);
}

void ProjectorWindow::mouseDoubleClickEvent(QMouseEvent *)
{
	close();
}

} // namespace melton
