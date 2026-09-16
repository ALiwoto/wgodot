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

void NativeTask::start(TaskFrame *p_frame, TaskOwner *p_owner) {
	frame = p_frame;
	{
		MutexLock lock(registry().mutex);
		if (registry().closing || (p_owner && p_owner->closing)) {
			state = State::CANCELLED;
			memdelete(frame);
			frame = nullptr;
			return;
		}
		registry().tasks.add(&registry_link);
		if (p_owner) {
			p_owner->tasks.add(&owner_link);
		}
		resume_pending = true;
	}
	drive();
}

void NativeTask::drive() {
	Ref<NativeTask> keep_alive(this);
	{
		MutexLock lock(registry().mutex);
		if (state == State::COMPLETED || state == State::CANCELLED) {
			return;
		}
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
		publish();
	}
	memdelete(finished_frame);
}

bool NativeTask::suspend(const std::function<std::function<void()>()> &p_subscribe) {
	MutexLock lock(registry().mutex);
	if (state == State::CANCELLED) {
		return true;
	}
	state = State::WAITING;
	disconnect_wait = p_subscribe();
	if (!disconnect_wait) {
		cancel();
	}
	return true;
}

void NativeTask::deliver(const std::function<void()> &p_store) {
	{
		MutexLock lock(registry().mutex);
		if (state == State::CANCELLED || state == State::COMPLETED) {
			return;
		}
		p_store();
		disconnect_wait = {};
		resume_pending = true;
	}
	drive();
}

void NativeTask::finish(const std::function<void()> &p_store) {
	MutexLock lock(registry().mutex);
	if (state == State::CANCELLED) {
		return;
	}
	p_store();
	state = State::COMPLETED;
	owner_link.remove_from_list();
	registry_link.remove_from_list();
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
		if (disconnect_wait) {
			auto disconnect = std::move(disconnect_wait);
			disconnect();
		}
		if (!driving) {
			cancelled_frame = frame;
			frame = nullptr;
		}
	}
	memdelete(cancelled_frame);
}

bool NativeTask::is_cancelled() const {
	MutexLock lock(registry().mutex);
	return state == State::CANCELLED;
}

bool NativeTask::is_completed() const {
	MutexLock lock(registry().mutex);
	return state == State::COMPLETED;
}

Mutex &NativeTask::coordination_mutex() {
	return registry().mutex;
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
