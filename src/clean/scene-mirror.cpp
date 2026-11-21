#include "scene-mirror.hpp"

namespace melton {

static constexpr int kMaxDepth = 12; // nested scene recursion guard
static std::atomic<unsigned> g_mirrorCounter{0};

SceneMirror::SceneMirror(ExclusionSet &exclusions) : exclusions_(exclusions)
{
	table_ = std::make_shared<Table>();
}

SceneMirror::~SceneMirror()
{
	Clear();
}

void SceneMirror::Clear()
{
	std::shared_ptr<Table> old;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		old = std::move(table_);
		table_ = std::make_shared<Table>();
	}
	// `old` (signals first, then scenes) is destroyed here, outside the lock.
	if (old)
		old->signals.clear();
}

OBSSource SceneMirror::Lookup(obs_source_t *original) const
{
	if (!original)
		return nullptr;
	const char *uuid = obs_source_get_uuid(original);
	if (!uuid)
		return nullptr;
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = table_->byUuid.find(uuid);
	return it == table_->byUuid.end() ? OBSSource() : it->second;
}

void SceneMirror::OnSceneEvent(void *param, calldata_t *)
{
	auto *self = static_cast<SceneMirror *>(param);
	if (self->onDirty)
		self->onDirty();
}

void SceneMirror::Watch(Table &t, obs_source_t *scene, bool geometry)
{
	signal_handler_t *sh = obs_source_get_signal_handler(scene);
	if (!sh)
		return;
	static const char *structural[] = {"item_add", "item_remove", "reorder", "refresh"};
	static const char *geo[] = {"item_transform", "item_visible"};
	auto add = [&](const char *sig) {
		auto s = std::make_unique<OBSSignal>();
		s->Connect(sh, sig, OnSceneEvent, this);
		t.signals.push_back(std::move(s));
	};
	for (auto *s : structural)
		add(s);
	if (geometry)
		for (auto *s : geo)
			add(s);
}

struct ContainsCtx {
	const SceneMirror *self;
	const ExclusionSet *ex;
	bool found = false;
	int depth = 0;
	bool (*recurse)(const SceneMirror *, const ExclusionSet *, obs_scene_t *, int);
};

static bool ContainsImpl(const ExclusionSet *ex, obs_scene_t *scene, int depth);

static bool ContainsEnum(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<ContainsCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (c->ex->Matches(src)) {
		c->found = true;
		return false;
	}
	obs_scene_t *nested = obs_scene_from_source(src); // scenes and groups
	if (nested && c->depth < kMaxDepth) {
		if (ContainsImpl(c->ex, nested, c->depth + 1)) {
			c->found = true;
			return false;
		}
	}
	return true;
}

static bool ContainsImpl(const ExclusionSet *ex, obs_scene_t *scene, int depth)
{
	ContainsCtx c;
	c.self = nullptr;
	c.ex = ex;
	c.depth = depth;
	c.recurse = nullptr;
	obs_scene_enum_items(scene, ContainsEnum, &c);
	return c.found;
}

bool SceneMirror::ContainsExcluded(obs_scene_t *scene, int depth) const
{
	return ContainsImpl(&exclusions_, scene, depth);
}

static void CopyItemProps(obs_sceneitem_t *from, obs_sceneitem_t *to)
{
	obs_transform_info ti;
	obs_sceneitem_get_info2(from, &ti);
	obs_sceneitem_set_info2(to, &ti);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(from, &crop);
	obs_sceneitem_set_crop(to, &crop);

	obs_sceneitem_set_scale_filter(to, obs_sceneitem_get_scale_filter(from));
	obs_sceneitem_set_blending_method(to, obs_sceneitem_get_blending_method(from));
	obs_sceneitem_set_blending_mode(to, obs_sceneitem_get_blending_mode(from));
	obs_sceneitem_set_visible(to, obs_sceneitem_visible(from));
}

struct CopyCtx {
	SceneMirror *self;
	obs_scene_t *dst;
	obs_sceneitem_t *dstGroup;
	int depth;
	std::function<void(obs_scene_t *, obs_scene_t *, obs_sceneitem_t *, int)> recurseGroup;
	std::function<OBSSource(obs_scene_t *, int)> copyNested;
	std::function<bool(obs_scene_t *)> containsExcluded;
	const ExclusionSet *ex;
};

static bool CopyEnum(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<CopyCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src || c->ex->Matches(src))
		return true; // excluded: simply not added to the mirror

	if (obs_sceneitem_is_group(item)) {
		unsigned id = ++g_mirrorCounter;
		std::string gname = "~melton-group-" + std::to_string(id);
		obs_sceneitem_t *gi = obs_scene_add_group2(c->dst, gname.c_str(), false);
		if (!gi) {
			MLOG(LOG_WARNING, "Could not mirror group '%s'; it is left out of the clean feed",
			     obs_source_get_name(src));
			return true;
		}
		c->recurseGroup(obs_scene_from_source(src), c->dst, gi, c->depth + 1);
		CopyItemProps(item, gi); // group transform last, after children are in place
		return true;
	}

	OBSSource use = src;
	obs_scene_t *nested = obs_scene_from_source(src);
	if (nested && c->depth < kMaxDepth && c->containsExcluded(nested)) {
		OBSSource m = c->copyNested(nested, c->depth + 1);
		if (m)
			use = m;
	}
	obs_sceneitem_t *ni = obs_scene_add(c->dst, use);
	if (!ni)
		return true;
	CopyItemProps(item, ni);
	if (c->dstGroup)
		obs_sceneitem_group_add_item(c->dstGroup, ni);
	return true;
}

void SceneMirror::CopyInto(Table &t, obs_scene_t *src, obs_scene_t *dst, obs_sceneitem_t *dstGroup, int depth)
{
	CopyCtx c;
	c.self = this;
	c.dst = dst;
	c.dstGroup = dstGroup;
	c.depth = depth;
	c.ex = &exclusions_;
	c.recurseGroup = [&](obs_scene_t *gsrc, obs_scene_t *gdst, obs_sceneitem_t *gi, int d) {
		CopyInto(t, gsrc, gdst, gi, d);
	};
	c.copyNested = [&](obs_scene_t *n, int d) { return CopyScene(t, n, d); };
	c.containsExcluded = [&](obs_scene_t *n) { return ContainsExcluded(n, 0); };
	obs_scene_enum_items(src, CopyEnum, &c); // enumerates bottom -> top, so z-order is preserved
}

OBSSource SceneMirror::CopyScene(Table &t, obs_scene_t *src, int depth)
{
	if (depth > kMaxDepth)
		return nullptr;
	std::string name = "~melton-mirror-" + std::to_string(++g_mirrorCounter);
	obs_scene_t *dst = obs_scene_create_private(name.c_str());
	if (!dst)
		return nullptr;
	obs_source_t *srcSource = obs_scene_get_source(src);
	Watch(t, srcSource, true);
	CopyInto(t, src, dst, nullptr, depth);
	OBSSource out = obs_scene_get_source(dst);
	obs_scene_release(dst);
	return out;
}

static bool CollectScenes(void *param, obs_source_t *scene)
{
	static_cast<std::vector<OBSSource> *>(param)->push_back(scene);
	return true;
}

void SceneMirror::RebuildAll()
{
	auto t = std::make_shared<Table>();
	std::vector<OBSSource> scenes;
	obs_enum_scenes(CollectScenes, &scenes);

	for (auto &scene : scenes) {
		const char *uuid = obs_source_get_uuid(scene);
		obs_scene_t *s = obs_scene_from_source(scene);
		if (!uuid || !s)
			continue;
		if (exclusions_.Matches(scene))
			continue; // an excluded scene is never the clean program
		Watch(*t, scene, false);
		if (ContainsExcluded(s, 0)) {
			OBSSource m = CopyScene(*t, s, 0);
			if (m) {
				t->byUuid[uuid] = m;
				t->mirrored++;
				continue;
			}
			MLOG(LOG_WARNING, "Mirror of scene '%s' failed; using original scene", obs_source_get_name(scene));
		}
		t->byUuid[uuid] = scene;
		t->passthrough++;
	}

	std::shared_ptr<Table> old;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		old = std::move(table_);
		table_ = t;
	}
	if (old)
		old->signals.clear();
	MLOG(LOG_INFO, "Clean mirror ready: %d scene(s) mirrored, %d used directly", t->mirrored, t->passthrough);
}

} // namespace melton
