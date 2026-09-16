// wgodot-changes::file
#pragma once

#include "wgodot_native_callback.h"
#include "wgodot_native_connections.h"

#include "core/os/mutex.h"

namespace WGodotNative {

template <class... Args>
class SignalSource;

template <class... Args>
class WSignal {
	friend class SignalSource<Args...>;
	using Callback = WCallable<void(Args...)>;
	struct Connection {
		uint64_t id = 0;
		Callback callback;
		uint32_t flags = 0;
		int references = 0;
		std::shared_ptr<NativeConnections::Lease> receiver_lifetime;
	};
	struct State {
		Mutex mutex;
		ObjectID owner;
		bool active = true;
		uint64_t next_id = 1;
		Vector<Connection> connections;
	};
	std::shared_ptr<State> state;
	mutable Signal engine_signal;
	static Callable engine_callback(const Callback &p_callback) {
		if constexpr ((std::is_constructible_v<Variant, Args> && ...)) {
			return p_callback.to_callable();
		} else {
			ERR_FAIL_V_MSG(Callable(), "Native signal arguments require an explicit engine boundary adapter.");
		}
	}
	static std::shared_ptr<NativeConnections::Lease> watch_receiver(const std::shared_ptr<State> &p_state, uint64_t p_id, ObjectID p_receiver) {
		std::weak_ptr<State> weak = p_state;
		return NativeConnections::watch(p_receiver, [weak, p_id]() { if (auto live = weak.lock()) { remove(live, p_id); } });
	}
	static void remove(const std::shared_ptr<State> &p_state, uint64_t p_id, bool p_force = true) {
		MutexLock lock(p_state->mutex);
		for (int i = 0; i < p_state->connections.size(); i++) {
			if (p_state->connections[i].id == p_id) {
				if (!p_force && --p_state->connections.write[i].references > 0) {
					return;
				}
				p_state->connections.remove_at(i);
				return;
			}
		}
	}

public:
	using Arguments = std::tuple<Args...>;
	WSignal() = default;
	explicit WSignal(Signal p_signal) : engine_signal(std::move(p_signal)) {}
	bool is_null() const { return !state && engine_signal.is_null(); }
	ObjectID get_object_id() const { return state ? state->owner : engine_signal.get_object_id(); }
	bool operator==(const WSignal &p_other) const { return state == p_other.state && engine_signal == p_other.engine_signal; }
	bool operator!=(const WSignal &p_other) const { return !(*this == p_other); }
	template <class Signature>
	Error connect(const WCallable<Signature> &p_callback, int64_t p_flags = 0) const {
		Callback callback = Callback::adapt(p_callback);
		if (!state) {
			return engine_signal.connect(engine_callback(callback), p_flags);
		}
		MutexLock lock(state->mutex);
		ERR_FAIL_COND_V(!state->active || !callback.is_valid(), ERR_INVALID_PARAMETER);
		ERR_FAIL_COND_V(p_flags & ~(Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT | Object::CONNECT_REFERENCE_COUNTED | Object::CONNECT_PERSIST), ERR_INVALID_PARAMETER);
		for (auto &connection : state->connections) {
			if (connection.callback.same_connection(callback)) {
				ERR_FAIL_COND_V(!(p_flags & Object::CONNECT_REFERENCE_COUNTED), ERR_INVALID_PARAMETER);
				connection.references++;
				return OK;
			}
		}
		const uint64_t id = state->next_id++;
		state->connections.push_back({ id, callback, uint32_t(p_flags), (p_flags & Object::CONNECT_REFERENCE_COUNTED) ? 1 : 0, watch_receiver(state, id, callback.get_object_id()) });
		return OK;
	}
	template <class Signature>
	bool is_connected(const WCallable<Signature> &p_callback) const {
		Callback callback = Callback::identity_key(p_callback.base_identity(), p_callback.get_object_id());
		if (!state) {
			return engine_signal.is_connected(engine_callback(callback));
		}
		MutexLock lock(state->mutex);
		for (const auto &connection : state->connections) {
			if (connection.callback.same_connection(callback)) {
				return true;
			}
		}
		return false;
	}
	template <class Signature>
	void disconnect(const WCallable<Signature> &p_callback) const {
		Callback callback = Callback::identity_key(p_callback.base_identity(), p_callback.get_object_id());
		if (!state) {
			engine_signal.disconnect(engine_callback(callback));
			return;
		}
		MutexLock lock(state->mutex);
		for (int i = 0; i < state->connections.size(); i++) {
			if (state->connections[i].callback.same_connection(callback)) {
				if (--state->connections.write[i].references <= 0) {
					state->connections.remove_at(i);
				}
				return;
			}
		}
		ERR_PRINT("Native signal callback is not connected.");
	}
	void emit(Args... p_args) const {
		if (!state) {
			if constexpr ((std::is_constructible_v<Variant, Args> && ...)) {
				std::array<Variant, sizeof...(Args)> values{ Variant(p_args)... };
				std::array<const Variant *, sizeof...(Args)> pointers{};
				for (size_t i = 0; i < values.size(); i++) {
					pointers[i] = &values[i];
				}
				(void)engine_signal.emit(pointers.data(), pointers.size());
			} else {
				ERR_PRINT("This native signal payload cannot cross an engine boundary.");
			}
			return;
		}
		auto keep_alive = state;
		Object *owner = ObjectDB::get_instance(state->owner);
		if (owner && owner->is_blocking_signals()) {
			return;
		}
		Ref<RefCounted> retained_owner(Object::cast_to<RefCounted>(owner));
		Vector<Connection> snapshot;
		{
			MutexLock lock(state->mutex);
			if (!state->active) {
				return;
			}
			snapshot = state->connections;
			// Match Object::emit_signalp: remove every one-shot before calling
			// any handler, then finish the snapshot even if a handler disconnects.
			for (const auto &connection : snapshot) {
				if (!connection.callback.is_valid()) {
					remove(state, connection.id);
				} else if (connection.flags & Object::CONNECT_ONE_SHOT) {
					remove(state, connection.id, false);
				}
			}
		}
		for (const auto &connection : snapshot) {
			if (!connection.callback.is_valid()) {
				continue;
			}
			if (connection.flags & Object::CONNECT_DEFERRED) {
				connection.callback.call_deferred(p_args...);
			} else {
				connection.callback.call(p_args...);
			}
		}
	}
	// Used by task cancellation. The returned disposer retains neither the task
	// callback nor its emitter, avoiding a task -> connection -> task cycle.
	std::function<void()> subscribe(Callback p_callback) const {
		if (!state) {
			Callable callback = engine_callback(p_callback);
			if (engine_signal.connect(callback, Object::CONNECT_ONE_SHOT) != OK) {
				return {};
			}
			Signal source = engine_signal;
			const auto identity = p_callback.weak_identity();
			const ObjectID owner = p_callback.get_object_id();
			return [source, identity, owner]() mutable {
				if (auto live = identity.lock()) {
					const Callable key = engine_callback(Callback::identity_key(std::move(live), owner));
					if (source.is_connected(key)) {
						source.disconnect(key);
					}
				}
			};
		}
		MutexLock lock(state->mutex);
		if (!state->active) {
			return {};
		}
		const uint64_t id = state->next_id++;
		state->connections.push_back({ id, p_callback, Object::CONNECT_ONE_SHOT, 0, watch_receiver(state, id, p_callback.get_object_id()) });
		std::weak_ptr<State> weak = state;
		return [weak, id]() { if (auto live = weak.lock()) { remove(live, id); } };
	}
};

template <class... Args>
class SignalSource {
	WSignal<Args...> value;

public:
	explicit SignalSource(Object *p_owner = nullptr) {
		value.state = std::make_shared<typename WSignal<Args...>::State>();
		if (p_owner) {
			value.state->owner = p_owner->get_instance_id();
		}
	}
	SignalSource(const SignalSource &) = delete;
	SignalSource &operator=(const SignalSource &) = delete;
	WSignal<Args...> signal() const { return value; }
	~SignalSource() {
		MutexLock lock(value.state->mutex);
		value.state->active = false;
		value.state->connections.clear();
	}
};

} // namespace WGodotNative
