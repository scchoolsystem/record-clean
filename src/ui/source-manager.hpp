#pragma once
#include "common.hpp"
#include "recording/recording-manager.hpp"
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

namespace melton {

// Graphics Exclusion Manager: tick the sources that must NOT appear in the clean feed.
class SourceManagerPanel : public QWidget {
	Q_OBJECT
public:
	explicit SourceManagerPanel(RecordingManager &rm, QWidget *parent = nullptr);
	void Reload();

signals:
	void previewRequested();

private:
	void OnItemChanged(QTreeWidgetItem *item);
	void ApplyFilter(const QString &text);
	void SaveFromList();

	RecordingManager &rm_;
	QTreeWidget *list_;
	QTreeWidget *tree_;
	QLineEdit *filter_;
	bool loading_ = false;
};

} // namespace melton
