// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"
#include "core/object/gdvirtual.gen.h"
#include "core/templates/list.h"
#include "scene/main/node.h"

// Small scene/physics operations that must run on the owning scene thread.
// Resource loading and private scene preparation belong on WorkerThreadPool.
class SceneWorkItem : public RefCounted {
	GDCLASS(SceneWorkItem, RefCounted);
	friend class SceneWorkQueue;

	bool cancelled = false;
	bool completed = false;
	bool queued = false;
	uint64_t resume_frame = 0;

protected:
	static void _bind_methods();
	GDVIRTUAL0R(bool, _step);

public:
	void cancel();
	// Waiting for rendering or another frame cannot make progress by spinning.
	void yield_until_next_frame();
	bool is_cancelled() const { return cancelled; }
	bool is_completed() const { return completed; }
};

class SceneWorkQueue : public Node {
	GDCLASS(SceneWorkQueue, Node);

	List<Ref<SceneWorkItem>> pending;
	Ref<SceneWorkItem> active;
	double time_budget_msec = 1.0;
	double frame_budget_fraction = 0.0;
	uint64_t last_process_usec = 0;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void add(const Ref<SceneWorkItem> &p_item);
	void clear();
	void set_time_budget_msec(double p_value);
	void set_frame_budget_fraction(double p_value);
	double get_time_budget_msec() const { return time_budget_msec; }
	double get_frame_budget_fraction() const { return frame_budget_fraction; }
	int get_pending_count() const { return pending.size(); }
	uint64_t get_last_process_usec() const { return last_process_usec; }
	~SceneWorkQueue();
};
