// wgodot-changes::file
#include "wgodot_native_task.h"

#include "core/object/class_db.h"
#include "core/os/mutex.h"

namespace WGodotNative {
namespace {

struct TaskRegistry {
	Mutex mutex;
	SelfList<NativeTask>::List tasks;
	bool closing = false;
};

TaskRegistry &registry() {
	static TaskRegistry value;
	return value;
}

} // namespace

void TaskOwner::clear() {
	MutexLock lock(registry().mutex);
	closing = true;
	while (tasks.first()) {
		Ref<NativeTask> task = tasks.first()->self();
		task->cancel();
	}
}

TaskOwner::~TaskOwner() {
	clear();
}

void NativeTask::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_resume", &NativeTask::signal_resume, MethodInfo("_resume"));
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

Variant NativeTask::start(TaskFrame *p_frame, TaskOwner *p_owner) {
	Ref<NativeTask> task = memnew(NativeTask);
	task->frame = p_frame;
	{
		MutexLock lock(registry().mutex);
		if (registry().closing || (p_owner && p_owner->closing)) {
			return Variant();
		}
		registry().tasks.add(&task->registry_link);
		if (p_owner) {
			p_owner->tasks.add(&task->owner_link);
		}
	}
	task->drive(Variant());
	MutexLock lock(registry().mutex);
	if (task->state == State::COMPLETED) {
		return task->result;
	}
	return task->state == State::CANCELLED ? Variant() : Variant(task);
}

void NativeTask::drive(const Variant &p_value) {
	Ref<NativeTask> keep_alive(this);
	{
		MutexLock lock(registry().mutex);
		if (state == State::COMPLETED || state == State::CANCELLED) {
			return;
		}
		inbox = p_value;
		resume_pending = true;
		if (driving) {
			return;
		}
		driving = true;
	}
	TaskFrame *finished_frame = nullptr;
	bool completed = false;
	while (true) {
		{
			MutexLock lock(registry().mutex);
			if (state == State::COMPLETED || state == State::CANCELLED || !resume_pending) {
				driving = false;
				completed = state == State::COMPLETED;
				if (completed || state == State::CANCELLED) {
					finished_frame = frame;
					frame = nullptr;
					inbox = Variant();
				}
				break;
			}
			resume_pending = false;
			state = State::RUNNING;
		}
		// Do not hold the registry lock across game/engine calls. A signal from
		// another thread may arrive before this resume method returns; it only
		// queues the next invocation while this one is still driving the frame.
		frame->resume(*this);
	}
	if (completed) {
		// GDScript resumes the caller before releasing the finished invocation's
		// parameters and remaining locals. Preserve that observable lifetime.
		emit_signal(SNAME("completed"), result);
	}
	memdelete(finished_frame);
}

bool NativeTask::wait(const Variant &p_value) {
	MutexLock lock(registry().mutex);
	if (state == State::CANCELLED) {
		return true;
	}
	Variant value = p_value;
	if (value.get_type() == Variant::OBJECT) {
		bool freed = false;
		Object *object = value.get_validated_object_with_check(freed);
		if (freed) {
			ERR_PRINT("Cannot await a freed native game object.");
			cancel();
			return true;
		}
		if (NativeTask *task = Object::cast_to<NativeTask>(object)) {
			value = task->state == State::COMPLETED ? task->result : Variant(Signal(task, SNAME("completed")));
		}
	}
	if (value.get_type() != Variant::SIGNAL) {
		inbox = value;
		return false;
	}
	state = State::WAITING;
	// The emitter's connection owns the task while waiting. The task does not
	// own that Callable, so a never-emitted signal creates no self-reference cycle.
	Ref<NativeTask> keep_alive(this);
	const Error error = Signal(value).connect(Callable(this, SNAME("_resume")).bind(keep_alive), Object::CONNECT_ONE_SHOT);
	if (error != OK) {
		ERR_PRINT("Cannot connect native game await to signal " + String(Signal(value).get_name()) + ".");
		cancel();
	}
	return true;
}

Variant NativeTask::signal_resume(const Variant **p_arguments, int p_count, Callable::CallError &r_error) {
	r_error.error = Callable::CallError::CALL_OK;
	ERR_FAIL_COND_V(p_count < 1, Variant()); // The final argument retains this task.
	Variant value;
	if (p_count == 2) {
		value = *p_arguments[0];
	} else if (p_count > 2) {
		Array values;
		values.resize(p_count - 1);
		for (int i = 0; i < p_count - 1; i++) {
			values[i] = *p_arguments[i];
		}
		value = values;
	}
	drive(value);
	return Variant();
}

Variant NativeTask::take_result() {
	MutexLock lock(registry().mutex);
	Variant value = std::move(inbox);
	inbox = Variant();
	return value;
}

void NativeTask::complete(const Variant &p_result) {
	MutexLock lock(registry().mutex);
	if (state == State::CANCELLED) {
		return;
	}
	result = p_result;
	state = State::COMPLETED;
	owner_link.remove_from_list();
	registry_link.remove_from_list();
}

void NativeTask::clear_connections() {
	List<Object::Connection> incoming_connections;
	get_signals_connected_to_this(&incoming_connections);
	for (Object::Connection &connection : incoming_connections) {
		connection.signal.disconnect(connection.callable);
	}
}

void NativeTask::cancel() {
	Ref<NativeTask> keep_alive(this);
	TaskFrame *cancelled_frame = nullptr;
	{
		MutexLock lock(registry().mutex);
		if (state == State::CANCELLED || state == State::COMPLETED) {
			return;
		}
		state = State::CANCELLED;
		owner_link.remove_from_list();
		registry_link.remove_from_list();
		clear_connections();
		if (!driving) {
			cancelled_frame = frame;
			frame = nullptr;
			inbox = Variant();
		}
	}
	memdelete(cancelled_frame);
}

bool NativeTask::is_cancelled() const {
	MutexLock lock(registry().mutex);
	return state == State::CANCELLED;
}

void NativeTask::clear_all() {
	MutexLock lock(registry().mutex);
	registry().closing = true;
	while (registry().tasks.first()) {
		Ref<NativeTask> task = registry().tasks.first()->self();
		task->cancel();
	}
}

NativeTask::~NativeTask() {
	MutexLock lock(registry().mutex);
	state = State::CANCELLED;
	owner_link.remove_from_list();
	registry_link.remove_from_list();
	memdelete(frame);
}

} // namespace WGodotNative
