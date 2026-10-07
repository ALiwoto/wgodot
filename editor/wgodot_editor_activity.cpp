// wgodot-changes::file

#include "wgodot_editor_activity.h"

#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

namespace {
struct Activity {
	uint64_t id;
	uint64_t started_msec;
	String stage;
	String path;
};
Mutex activity_mutex;
Vector<Activity> activities;
uint64_t next_activity_id = 0;
} // namespace

WGodotEditorActivity::WGodotEditorActivity(const String &p_stage, const String &p_path) {
	MutexLock lock(activity_mutex);
	id = ++next_activity_id;
	activities.push_back({ id, OS::get_singleton()->get_ticks_msec(), p_stage, p_path });
}

WGodotEditorActivity::~WGodotEditorActivity() {
	MutexLock lock(activity_mutex);
	for (int i = activities.size() - 1; i >= 0; i--) {
		if (activities[i].id == id) {
			activities.remove_at(i);
			break;
		}
	}
}

WGodotEditorActivity::Snapshot WGodotEditorActivity::get_snapshot() {
	MutexLock lock(activity_mutex);
	Snapshot snapshot;
	if (!activities.is_empty()) {
		const Activity &current = activities[activities.size() - 1];
		snapshot.busy = true;
		snapshot.stage = current.stage;
		snapshot.path = current.path;
		snapshot.elapsed_msec = OS::get_singleton()->get_ticks_msec() - activities[0].started_msec;
	}
	return snapshot;
}
