// wgodot-changes::file
#include "scene_work_queue.h"

#include "core/object/class_db.h"
#include "core/os/os.h"

void SceneWorkItem::_bind_methods() {
	ClassDB::bind_method(D_METHOD("cancel"), &SceneWorkItem::cancel);
	ClassDB::bind_method(D_METHOD("is_cancelled"), &SceneWorkItem::is_cancelled);
	ClassDB::bind_method(D_METHOD("is_completed"), &SceneWorkItem::is_completed);
	GDVIRTUAL_BIND(_step);
	ADD_SIGNAL(MethodInfo("completed"));
}

void SceneWorkItem::cancel() {
	cancelled = true;
}

void SceneWorkQueue::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add", "item"), &SceneWorkQueue::add);
	ClassDB::bind_method(D_METHOD("clear"), &SceneWorkQueue::clear);
	ClassDB::bind_method(D_METHOD("set_time_budget_msec", "value"), &SceneWorkQueue::set_time_budget_msec);
	ClassDB::bind_method(D_METHOD("get_time_budget_msec"), &SceneWorkQueue::get_time_budget_msec);
	ClassDB::bind_method(D_METHOD("get_pending_count"), &SceneWorkQueue::get_pending_count);
	ClassDB::bind_method(D_METHOD("get_last_process_usec"), &SceneWorkQueue::get_last_process_usec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_budget_msec", PROPERTY_HINT_RANGE, "0.1,10,0.1,or_greater,suffix:ms"), "set_time_budget_msec", "get_time_budget_msec");
}

void SceneWorkQueue::set_time_budget_msec(double p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value <= 0.0);
	time_budget_msec = p_value;
}

void SceneWorkQueue::add(const Ref<SceneWorkItem> &p_item) {
	ERR_FAIL_NULL(p_item);
	ERR_FAIL_COND(p_item->queued || p_item->completed || p_item->cancelled);
	p_item->queued = true;
	pending.push_back(p_item);
	set_process_internal(true);
}

void SceneWorkQueue::clear() {
	if (active.is_valid()) {
		active->cancel();
	}
	for (const Ref<SceneWorkItem> &item : pending) {
		item->queued = false;
		item->cancel();
	}
	pending.clear();
	set_process_internal(false);
}

void SceneWorkQueue::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		clear();
		return;
	}
	if (p_what != NOTIFICATION_INTERNAL_PROCESS) {
		return;
	}
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	const uint64_t deadline = started + uint64_t(time_budget_msec * 1000.0);
	while (!pending.is_empty()) {
		Ref<SceneWorkItem> item = pending.front()->get();
		pending.pop_front();
		if (item->cancelled) {
			item->queued = false;
		} else {
			bool finished = true;
			active = item;
			item->GDVIRTUAL_CALL(_step, finished);
			active.unref();
			if (item->cancelled) {
				item->queued = false;
			} else if (finished) {
				item->completed = true;
				item->queued = false;
				item->emit_signal(SNAME("completed"));
			} else if (!item->cancelled && is_inside_tree()) {
				// Round robin prevents one long catch-up from taking every slice.
				pending.push_back(item);
			} else {
				item->queued = false;
			}
		}
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			break;
		}
	}
	last_process_usec = OS::get_singleton()->get_ticks_usec() - started;
	set_process_internal(!pending.is_empty());
}

SceneWorkQueue::~SceneWorkQueue() {
	clear();
}
