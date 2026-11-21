#include "source-exclusion.hpp"

namespace melton {

ExclusionSet::ExclusionSet()
{
	signal_handler_t *sh = obs_get_signal_handler();
	renameSignal_.Connect(sh, "source_rename", OnRename, this);
	createSignal_.Connect(sh, "source_create", OnSourceChange, this);
	removeSignal_.Connect(sh, "source_remove", OnSourceChange, this);
}

ExclusionSet::~ExclusionSet()
{
	renameSignal_.Disconnect();
	createSignal_.Disconnect();
	removeSignal_.Disconnect();
}

void ExclusionSet::Set(const std::vector<Exclusion> &list)
{
	std::lock_guard<std::mutex> lock(mutex_);
	list_ = list;
}

std::vector<Exclusion> ExclusionSet::Get() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return list_;
}

bool ExclusionSet::Empty() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return list_.empty();
}

bool ExclusionSet::Matches(obs_source_t *source) const
{
	if (!source)
		return false;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto &e : list_) {
		if (!e.uuid.empty() && uuid && e.uuid == uuid)
			return true;
		// Name fallback only when the stored UUID no longer exists (see Refresh()).
		if (e.uuid.empty() && name && e.name == name)
			return true;
	}
	return false;
}

bool ExclusionSet::Refresh()
{
	bool changed = false;
	std::vector<Exclusion> list = Get();
	for (auto &e : list) {
		OBSSourceAutoRelease byUuid = e.uuid.empty() ? nullptr : obs_get_source_by_uuid(e.uuid.c_str());
		if (byUuid) {
			const char *n = obs_source_get_name(byUuid);
			if (n && e.name != n) {
				e.name = n; // renamed
				changed = true;
			}
			continue;
		}
		OBSSourceAutoRelease byName = e.name.empty() ? nullptr : obs_get_source_by_name(e.name.c_str());
		if (byName) {
			const char *u = obs_source_get_uuid(byName);
			if (u && e.uuid != u) {
				e.uuid = u; // re-created under the same name
				changed = true;
			}
		}
		// else: source missing; keep the entry so it re-applies if the source returns.
	}
	if (changed) {
		std::lock_guard<std::mutex> lock(mutex_);
		list_ = list;
	}
	return changed;
}

void ExclusionSet::OnRename(void *param, calldata_t *)
{
	auto *self = static_cast<ExclusionSet *>(param);
	RunOnMainThread([self]() {
		if (self->Refresh())
			MLOG(LOG_INFO, "Exclusion list updated after a source rename");
		if (self->onChanged)
			self->onChanged();
	});
}

void ExclusionSet::OnSourceChange(void *param, calldata_t *)
{
	auto *self = static_cast<ExclusionSet *>(param);
	RunOnMainThread([self]() {
		self->Refresh();
		if (self->onChanged)
			self->onChanged();
	});
}

} // namespace melton
