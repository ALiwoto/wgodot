// wgodot-changes::file
#pragma once

#include "wgodot_native_signal.h"

#include "core/templates/self_list.h"

namespace WGodotNative {

struct Unit {};
class NativeTask;
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

// Scheduling and ownership are shared; each TaskState<T> owns a real T result.
class NativeTask : public RefCounted {
	GDCLASS(NativeTask, RefCounted);
	enum class State { CREATED,
		RUNNING,
		WAITING,
		COMPLETED,
		CANCELLED };
	State state = State::CREATED;
	TaskFrame *frame = nullptr;
	bool driving = false;
	bool resume_pending = false;
	std::function<void()> disconnect_wait;
	SelfList<NativeTask> owner_link{ this };
	SelfList<NativeTask> registry_link{ this };
	void drive();

protected:
	static void _bind_methods() {}
	virtual void publish() = 0;

public:
	void start(TaskFrame *p_frame, TaskOwner *p_owner);
	static void clear_all();
	static Mutex &coordination_mutex();
	bool suspend(const std::function<std::function<void()>()> &p_subscribe);
	void deliver(const std::function<void()> &p_store);
	void finish(const std::function<void()> &p_store);
	void cancel();
	bool is_cancelled() const;
	bool is_completed() const;
	~NativeTask();
};

template <class T>
class TaskState : public NativeTask {
public:
	using Value = std::conditional_t<std::is_void_v<T>, Unit, T>;
	Value result{};
	SignalSource<Value> completed;
	void complete(Value p_result = Value()) {
		finish([&]() { result = std::move(p_result); });
	}

protected:
	void publish() override { completed.signal().emit(result); }
};

template <class T>
class Task {
	Ref<TaskState<T>> state;

public:
	using Value = typename TaskState<T>::Value;
	Task() = default;
	static Task start(TaskFrame *p_frame, TaskOwner *p_owner = nullptr) {
		Task result;
		result.state = memnew(TaskState<T>);
		result.state->start(p_frame, p_owner);
		return result;
	}
	bool is_null() const { return state.is_null(); }
	bool is_completed() const { return state.is_valid() && state->is_completed(); }
	bool is_cancelled() const { return state.is_null() || state->is_cancelled(); }
	Value result() const { return state->result; }
	WSignal<Value> completed() const { return state->completed.signal(); }
};

template <class Result, class... Args>
bool await_signal(NativeTask &p_task, const WSignal<Args...> &p_signal, Result &r_result) {
	Ref<NativeTask> task(&p_task);
	return p_task.suspend([task, p_signal, &r_result]() {
		return p_signal.subscribe(WCallable<void(Args...)>::make([task, &r_result](Args... p_args) {
			task->deliver([&]() {
				if constexpr (std::is_same_v<Result, Unit>) {
					r_result = Unit();
				} else if constexpr (sizeof...(Args) == 1) {
					r_result = convert<Result>(std::get<0>(std::forward_as_tuple(p_args...)));
				} else {
					r_result = std::make_tuple(p_args...);
				}
			});
		},
				{}, task->get_instance_id()));
	});
}

template <class Result, class T>
bool await_task(NativeTask &p_task, const Task<T> &p_value, Result &r_result) {
	MutexLock lock(NativeTask::coordination_mutex());
	if (p_value.is_cancelled()) {
		p_task.cancel();
		return true;
	}
	if (p_value.is_completed()) {
		if constexpr (!std::is_same_v<Result, Unit>) {
			r_result = convert<Result>(p_value.result());
		}
		return false;
	}
	return await_signal(p_task, p_value.completed(), r_result);
}

} // namespace WGodotNative
