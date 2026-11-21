#include "source-manager.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>

namespace melton {

static const int kUuidRole = Qt::UserRole + 1;
static const int kNameRole = Qt::UserRole + 2;

static bool CollectSources(void *param, obs_source_t *src)
{
	const char *name = obs_source_get_name(src);
	if (!name || name[0] == '~') // our private mirror/group helpers
		return true;
	if (obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION)
		return true;
	static_cast<std::vector<OBSSource> *>(param)->push_back(src);
	return true;
}

static bool CollectScenes(void *param, obs_source_t *src)
{
	static_cast<std::vector<OBSSource> *>(param)->push_back(src);
	return true;
}

struct TreeCtx {
	ExclusionSet *ex;
	QTreeWidgetItem *parent;
	int depth;
};

static void AddTreeItems(obs_scene_t *scene, ExclusionSet *ex, QTreeWidgetItem *parent, int depth);

static bool TreeEnum(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<TreeCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	QString name = QString::fromUtf8(obs_source_get_name(src));
	if (name.startsWith('~'))
		return true;
	bool excluded = c->ex->Matches(src);
	auto *node = new QTreeWidgetItem(c->parent);
	node->setText(0, name + (excluded ? "   [EXCLUDED]" : ""));
	if (excluded)
		node->setForeground(0, QBrush(QColor(255, 120, 120)));
	if (obs_scene_t *nested = obs_scene_from_source(src))
		if (c->depth < 8)
			AddTreeItems(nested, c->ex, node, c->depth + 1);
	return true;
}

static void AddTreeItems(obs_scene_t *scene, ExclusionSet *ex, QTreeWidgetItem *parent, int depth)
{
	TreeCtx c{ex, parent, depth};
	obs_scene_enum_items(scene, TreeEnum, &c);
}

SourceManagerPanel::SourceManagerPanel(RecordingManager &rm, QWidget *parent) : QWidget(parent), rm_(rm)
{
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(8, 8, 8, 8);
	auto *info = new QLabel("Tick every graphics source (logo, lower third, ticker, browser overlay...) "
				"that must be hidden from the CLEAN recording. Camera sources stay untouched.");
	info->setWordWrap(true);
	info->setObjectName("hint");
	lay->addWidget(info);

	filter_ = new QLineEdit;
	filter_->setPlaceholderText("Filter sources...");
	lay->addWidget(filter_);

	auto *tabs = new QTabWidget;
	list_ = new QTreeWidget;
	list_->setHeaderLabels({"Exclude from clean", "Type"});
	list_->setColumnWidth(0, 220);
	tree_ = new QTreeWidget;
	tree_->setHeaderLabel("Scene structure (nested scenes expanded)");
	tabs->addTab(list_, "All sources");
	tabs->addTab(tree_, "Scene structure");
	lay->addWidget(tabs, 1);

	auto *row = new QHBoxLayout;
	auto *refresh = new QPushButton("Refresh");
	auto *preview = new QPushButton("Test preview of clean feed");
	row->addWidget(refresh);
	row->addWidget(preview);
	lay->addLayout(row);

	connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int) { OnItemChanged(it); });
	connect(filter_, &QLineEdit::textChanged, this, [this](const QString &t) { ApplyFilter(t); });
	connect(refresh, &QPushButton::clicked, this, [this]() { Reload(); });
	connect(preview, &QPushButton::clicked, this, [this]() { emit previewRequested(); });
	Reload();
}

void SourceManagerPanel::Reload()
{
	loading_ = true;
	list_->clear();
	tree_->clear();

	std::vector<OBSSource> sources;
	obs_enum_all_sources(CollectSources, &sources);
	std::sort(sources.begin(), sources.end(), [](const OBSSource &a, const OBSSource &b) {
		return QString::fromUtf8(obs_source_get_name(a)).toLower() <
		       QString::fromUtf8(obs_source_get_name(b)).toLower();
	});

	std::set<std::string> live;
	for (auto &s : sources) {
		auto *it = new QTreeWidgetItem(list_);
		it->setText(0, QString::fromUtf8(obs_source_get_name(s)));
		const char *id = obs_source_get_id(s);
		const char *disp = obs_source_get_display_name(id);
		it->setText(1, QString::fromUtf8(disp ? disp : id));
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(0, rm_.Exclusions().Matches(s) ? Qt::Checked : Qt::Unchecked);
		it->setData(0, kUuidRole, QString::fromUtf8(obs_source_get_uuid(s)));
		it->setData(0, kNameRole, QString::fromUtf8(obs_source_get_name(s)));
		live.insert(SafeStr(obs_source_get_uuid(s)));
		live.insert("name:" + SafeStr(obs_source_get_name(s)));
	}
	// Excluded sources that no longer exist stay listed so the user can see and clear them.
	for (auto &e : rm_.Exclusions().Get()) {
		if (live.count(e.uuid) || live.count("name:" + e.name))
			continue;
		auto *it = new QTreeWidgetItem(list_);
		it->setText(0, QString::fromStdString(e.name) + "  (missing)");
		it->setText(1, "Source not found");
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(0, Qt::Checked);
		it->setData(0, kUuidRole, QString::fromStdString(e.uuid));
		it->setData(0, kNameRole, QString::fromStdString(e.name));
		it->setForeground(0, QBrush(QColor(255, 190, 90)));
	}

	std::vector<OBSSource> scenes;
	obs_enum_scenes(CollectScenes, &scenes);
	for (auto &sc : scenes) {
		auto *root = new QTreeWidgetItem(tree_);
		root->setText(0, QString::fromUtf8(obs_source_get_name(sc)));
		if (obs_scene_t *s = obs_scene_from_source(sc))
			AddTreeItems(s, &rm_.Exclusions(), root, 0);
	}
	tree_->expandAll();
	loading_ = false;
	ApplyFilter(filter_->text());
}

void SourceManagerPanel::ApplyFilter(const QString &text)
{
	for (int i = 0; i < list_->topLevelItemCount(); i++) {
		auto *it = list_->topLevelItem(i);
		it->setHidden(!text.isEmpty() && !it->text(0).contains(text, Qt::CaseInsensitive));
	}
}

void SourceManagerPanel::OnItemChanged(QTreeWidgetItem *)
{
	if (!loading_)
		SaveFromList();
}

void SourceManagerPanel::SaveFromList()
{
	std::vector<Exclusion> out;
	for (int i = 0; i < list_->topLevelItemCount(); i++) {
		auto *it = list_->topLevelItem(i);
		if (it->checkState(0) != Qt::Checked)
			continue;
		Exclusion e;
		e.uuid = it->data(0, kUuidRole).toString().toStdString();
		e.name = it->data(0, kNameRole).toString().toStdString();
		out.push_back(e);
	}
	Settings s = SettingsStore::Get().Snapshot();
	s.exclusions = out;
	SettingsStore::Get().Replace(s);
	rm_.ApplySettings(s); // live: mirrors are rebuilt and swapped in without interrupting a recording
	MLOG(LOG_INFO, "Clean-feed exclusions updated: %d source(s) excluded", (int)out.size());

	loading_ = true; // refresh the structure tree markers without re-saving
	tree_->clear();
	std::vector<OBSSource> scenes;
	obs_enum_scenes(CollectScenes, &scenes);
	for (auto &sc : scenes) {
		auto *root = new QTreeWidgetItem(tree_);
		root->setText(0, QString::fromUtf8(obs_source_get_name(sc)));
		if (obs_scene_t *s2 = obs_scene_from_source(sc))
			AddTreeItems(s2, &rm_.Exclusions(), root, 0);
	}
	tree_->expandAll();
	loading_ = false;
}

} // namespace melton
