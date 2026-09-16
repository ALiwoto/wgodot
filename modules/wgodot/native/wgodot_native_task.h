// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/self_list.h"

namespace WGodotNative {

class NativeTask;

// The generated frame contains the function's saved values and native resume
// method. Destroying it releases those values without running the remaining body.
class TaskFrame {
public:
	virtual void resume(NativeTask &p_task) = 0;
	virtual ~TaskFrame() = default;
};

class TaskOwner {
	friend class NativeTask;
	SelfList<NativeTask>::List tasks;
	bool closing = false;

public:
	void clear();
	~TaskOwner();
};

class NativeTask : public RefCounted {
	GDCLASS(NativeTask, RefCounted);

	enum class State { CREATED,
		RUNNING,
		WAITING,
		COMPLETED,
		CANCELLED };
	State state = State::CREATED;
	TaskFrame *frame = nullptr;
	Variant inbox;
	Variant result;
	bool driving = false;
	bool resume_pending = false;
	SelfList<NativeTask> owner_link{ this };
	SelfList<NativeTask> registry_link{ this };

	void drive(const Variant &p_value);
	void clear_connections();
	Variant signal_resume(const Variant **p_arguments, int p_count, Callable::CallError &r_error);

protected:
	static void _bind_methods();

public:
	static Variant start(TaskFrame *p_frame, TaskOwner *p_owner = nullptr);
	static void clear_all();
	// Returns true when the generated body must return to the engine. An ordinary
	// value (including a task which completed immediately) requires no suspension.
	bool wait(const Variant &p_value);
	Variant take_result();
	void complete(const Variant &p_result = Variant());
	void cancel();
	bool is_cancelled() const;
	~NativeTask();
};

} // namespace WGodotNative
