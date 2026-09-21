// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

#include <functional>
#include <memory>
#include <tuple>

namespace WGodotNative {

// Identity is separate from the invocation signature. Adapting a callback must
// not change whether a later disconnect/find refers to the same callback.
struct CallbackIdentity {
	virtual const CallbackIdentity &base() const { return *this; }
	virtual const void *type_tag() const { return nullptr; }
	virtual uint32_t hash() const { return hash_one_uint64(uint64_t(reinterpret_cast<uintptr_t>(this))); }
	virtual bool equals(const CallbackIdentity &p_other) const { return this == &p_other; }
	virtual ~CallbackIdentity() = default;
};

struct MethodIdentity : CallbackIdentity {
	inline static char tag = 0;
	ObjectID owner;
	uint64_t slot;
	MethodIdentity(ObjectID p_owner, uint64_t p_slot) : owner(p_owner), slot(p_slot) {}
	const void *type_tag() const override { return &tag; }
	uint32_t hash() const override { return hash_one_uint64(uint64_t(owner) ^ slot); }
	bool equals(const CallbackIdentity &p_other) const override {
		if (p_other.type_tag() != &tag) {
			return false;
		}
		const auto &other = static_cast<const MethodIdentity &>(p_other);
		return owner == other.owner && slot == other.slot;
	}
};

template <class Tuple>
struct BoundIdentity : CallbackIdentity {
	inline static char tag = 0;
	std::shared_ptr<CallbackIdentity> source;
	std::shared_ptr<const Tuple> arguments;
	BoundIdentity(std::shared_ptr<CallbackIdentity> p_source, std::shared_ptr<const Tuple> p_arguments) : source(std::move(p_source)), arguments(std::move(p_arguments)) {}
	const void *type_tag() const override { return &tag; }
	uint32_t hash() const override { return source->hash(); }
	const CallbackIdentity &base() const override { return source->base(); }
	bool equals(const CallbackIdentity &p_other) const override {
		if (p_other.type_tag() != &tag) {
			return false;
		}
		const auto &other = static_cast<const BoundIdentity &>(p_other);
		return source->equals(*other.source) && *arguments == *other.arguments;
	}
};

struct UnboundIdentity : CallbackIdentity {
	inline static char tag = 0;
	std::shared_ptr<CallbackIdentity> source;
	size_t count;
	UnboundIdentity(std::shared_ptr<CallbackIdentity> p_source, size_t p_count) : source(std::move(p_source)), count(p_count) {}
	const void *type_tag() const override { return &tag; }
	uint32_t hash() const override { return source->hash(); }
	const CallbackIdentity &base() const override { return source->base(); }
	bool equals(const CallbackIdentity &p_other) const override {
		if (p_other.type_tag() != &tag) {
			return false;
		}
		const auto &other = static_cast<const UnboundIdentity &>(p_other);
		return count == other.count && source->equals(*other.source);
	}
};

struct EngineCallbackIdentity : CallbackIdentity {
	inline static char tag = 0;
	Callable callback;
	explicit EngineCallbackIdentity(Callable p_callback) : callback(std::move(p_callback)) {}
	const void *type_tag() const override { return &tag; }
	uint32_t hash() const override { return callback.hash(); }
	bool equals(const CallbackIdentity &p_other) const override {
		return p_other.type_tag() == &tag && callback == static_cast<const EngineCallbackIdentity &>(p_other).callback;
	}
};

template <class Signature>
class WCallable;

template <class R, class... Args>
class WCallable<R(Args...)> {
	template <class>
	friend class WCallable;
	// Godot's Vector relocates elements as bytes. Keep both functions at a
	// stable address: std::function can point into its own inline storage.
	struct Invocation {
		std::function<R(Args...)> invoke;
		std::function<bool()> valid;
		Invocation(std::function<R(Args...)> p_invoke, std::function<bool()> p_valid) : invoke(std::move(p_invoke)), valid(std::move(p_valid)) {}
	};
	std::shared_ptr<const Invocation> invocation;
	std::shared_ptr<CallbackIdentity> identity;
	ObjectID owner;
	std::tuple<Args...> defaults{};
	int default_count = 0;

	template <size_t I, class Tuple>
	decltype(auto) argument(Tuple &p_args) const {
		if constexpr (I < std::tuple_size_v<Tuple>) {
			return convert<std::tuple_element_t<I, std::tuple<Args...>>>(std::get<I>(p_args));
		} else {
			return std::get<I>(defaults);
		}
	}
	template <class Tuple, size_t... I>
	R call_tuple(Tuple &p_args, std::index_sequence<I...>) const {
		return invocation->invoke(argument<I>(p_args)...);
	}
	template <class Source, class Tuple, size_t... I>
	static R call_unbound(const Source &p_source, Tuple &p_args, std::index_sequence<I...>) {
		return p_source.call(std::get<I>(p_args)...);
	}
	template <class Tuple, size_t... I>
	void set_defaults(Tuple &&p_defaults, std::index_sequence<I...>) {
		((std::get<sizeof...(Args) - sizeof...(I) + I>(defaults) = std::get<I>(std::forward<Tuple>(p_defaults))), ...);
		default_count = sizeof...(I);
	}
	template <class Tuple, size_t... I>
	auto bind_tuple(Tuple p_bound, std::index_sequence<I...>) const {
		using Bound = WCallable<R(std::tuple_element_t<I, std::tuple<Args...>>...)>;
		if (is_null()) {
			return Bound();
		}
		auto source = *this;
		auto bound = std::make_shared<const Tuple>(std::move(p_bound));
		auto result = Bound::make([source, bound](std::tuple_element_t<I, std::tuple<Args...>>... p_args) -> R {
			return std::apply([&](const auto &...p_tail) -> R { return source.call(p_args..., p_tail...); }, *bound);
		},
				invocation->valid, owner, std::make_shared<BoundIdentity<Tuple>>(identity, bound));
		result.defaults = std::make_tuple(std::get<I>(defaults)...);
		result.default_count = MAX(0, default_count - int(std::tuple_size_v<Tuple>));
		return result;
	}

public:
	using Result = R;
	using Arguments = std::tuple<Args...>;
	static constexpr size_t argument_count = sizeof...(Args);
	WCallable() = default;
	template <class F>
	static WCallable make(F p_function, std::function<bool()> p_valid = {}, ObjectID p_owner = ObjectID(), std::shared_ptr<CallbackIdentity> p_identity = {}) {
		WCallable result;
		result.invocation = std::make_shared<const Invocation>(std::move(p_function), std::move(p_valid));
		result.owner = p_owner;
		result.identity = p_identity ? std::move(p_identity) : std::make_shared<CallbackIdentity>();
		return result;
	}
	std::weak_ptr<CallbackIdentity> weak_identity() const { return identity; }
	std::shared_ptr<CallbackIdentity> base_identity() const {
		return identity ? std::shared_ptr<CallbackIdentity>(identity, const_cast<CallbackIdentity *>(&identity->base())) : nullptr;
	}
	template <class OtherSignature>
	bool same_connection(const WCallable<OtherSignature> &p_other) const {
		return identity && p_other.identity && identity->base().equals(p_other.identity->base());
	}
	static WCallable identity_key(std::shared_ptr<CallbackIdentity> p_identity, ObjectID p_owner) {
		WCallable result;
		result.identity = std::move(p_identity);
		result.owner = p_owner;
		return result;
	}
	bool is_null() const { return !invocation; }
	bool is_valid() const { return invocation && (!invocation->valid || invocation->valid()); }
	ObjectID get_object_id() const { return owner; }
	uint32_t hash() const { return identity ? identity->hash() : 0; }
	int get_argument_count() const { return sizeof...(Args); }
	int get_minimum_argument_count() const { return sizeof...(Args) - default_count; }
	template <class OtherSignature>
	bool operator==(const WCallable<OtherSignature> &p_other) const {
		return identity == p_other.identity || (identity && p_other.identity && identity->equals(*p_other.identity));
	}
	template <class OtherSignature>
	bool operator!=(const WCallable<OtherSignature> &p_other) const { return !(*this == p_other); }
	template <class... Values>
	R call(Values &&...p_args) const {
		static_assert(sizeof...(Values) <= sizeof...(Args), "Too many callback arguments.");
		if (!is_valid() || int(sizeof...(Values)) < get_minimum_argument_count()) {
			ERR_PRINT("Invalid native callback invocation.");
			if constexpr (!std::is_void_v<R>) {
				return R();
			} else {
				return;
			}
		}
		auto args = std::forward_as_tuple(p_args...);
		return call_tuple(args, std::index_sequence_for<Args...>());
	}
	template <class... Values>
	WCallable with_defaults(std::tuple<Values...> p_defaults) const {
		static_assert(sizeof...(Values) <= sizeof...(Args));
		WCallable result = *this;
		result.set_defaults(std::move(p_defaults), std::index_sequence_for<Values...>());
		return result;
	}
	template <class OtherR, class... OtherArgs>
	static WCallable adapt(const WCallable<OtherR(OtherArgs...)> &p_source) {
		if constexpr (std::is_same_v<WCallable, WCallable<OtherR(OtherArgs...)>>) {
			return p_source;
		}
		if (p_source.is_null()) {
			return {};
		}
		auto result = make([p_source](Args... p_args) -> R {
			if constexpr (std::is_void_v<R>) { p_source.call(p_args...); }
			else { return convert<R>(p_source.call(p_args...)); } }, [p_source]() { return p_source.is_valid(); }, p_source.owner, p_source.identity);
		if constexpr (std::is_same_v<Arguments, typename WCallable<OtherR(OtherArgs...)>::Arguments>) {
			result.defaults = p_source.defaults;
			result.default_count = p_source.default_count;
		}
		return result;
	}
	template <class... Bound>
	auto bind(Bound... p_bound) const {
		static_assert(sizeof...(Bound) <= sizeof...(Args));
		return bind_tuple(std::make_tuple(std::move(p_bound)...), std::make_index_sequence<sizeof...(Args) - sizeof...(Bound)>());
	}
	template <size_t Count, class SourceSignature>
	static WCallable unbind(const WCallable<SourceSignature> &p_source) {
		static_assert(Count > 0 && Count <= sizeof...(Args));
		if (p_source.is_null()) {
			return {};
		}
		return make([p_source](Args... p_args) -> R {
			auto args = std::forward_as_tuple(p_args...);
			return call_unbound(p_source, args, std::make_index_sequence<sizeof...(Args) - Count>());
		},
				[p_source]() { return p_source.is_valid(); }, p_source.owner, std::make_shared<UnboundIdentity>(p_source.identity, Count));
	}
	static WCallable from_callable(Callable p_callable) {
		return make([p_callable](Args... p_args) -> R {
			std::array<Variant, sizeof...(Args)> values{ Variant(p_args)... };
			std::array<const Variant *, sizeof...(Args)> pointers{};
			for (size_t i = 0; i < values.size(); i++) { pointers[i] = &values[i]; }
			Variant result;
			Callable::CallError error;
			p_callable.callp(pointers.data(), pointers.size(), result, error);
			if (error.error != Callable::CallError::CALL_OK) { ERR_PRINT("Engine callback invocation failed."); }
			if constexpr (!std::is_void_v<R>) { return convert<R>(result); } }, [p_callable]() { return p_callable.is_valid(); }, p_callable.get_object_id(), std::make_shared<EngineCallbackIdentity>(p_callable));
	}
	Callable to_callable() const;
	template <class... Values>
	void call_deferred(Values... p_args) const;
};

// Only official engine boundaries decode Variant arguments. Native connections
// and invocations never enter this adapter.
template <class R, class... Args>
class EngineCallback : public CallableCustom {
	WCallable<R(Args...)> callback;
	Callable base_comparator;
	template <size_t... I>
	void invoke(const Variant **p_args, Variant &r_result, std::index_sequence<I...>) const {
		if constexpr (std::is_void_v<R>) {
			callback.call(convert<std::tuple_element_t<I, std::tuple<Args...>>>(*p_args[I])...);
		} else {
			r_result = callback.call(convert<std::tuple_element_t<I, std::tuple<Args...>>>(*p_args[I])...);
		}
	}
	template <size_t Count = 0>
	void dispatch(const Variant **p_args, int p_count, Variant &r_result) const {
		if (p_count == Count) {
			invoke(p_args, r_result, std::make_index_sequence<Count>());
		} else if constexpr (Count < sizeof...(Args)) {
			dispatch<Count + 1>(p_args, p_count, r_result);
		}
	}
	static bool equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		return static_cast<const EngineCallback *>(p_a)->callback == static_cast<const EngineCallback *>(p_b)->callback;
	}
	static bool less(const CallableCustom *p_a, const CallableCustom *p_b) { return p_a < p_b; }

public:
	explicit EngineCallback(WCallable<R(Args...)> p_callback) : callback(std::move(p_callback)) {
		const auto base = callback.base_identity();
		if (base && base != callback.weak_identity().lock()) {
			base_comparator = WCallable<R(Args...)>::identity_key(base, callback.get_object_id()).to_callable();
		}
	}
	const Callable *get_base_comparator() const override { return base_comparator.is_null() ? nullptr : &base_comparator; }
	uint32_t hash() const override { return callback.hash(); }
	String get_as_text() const override { return "native callback"; }
	CompareEqualFunc get_compare_equal_func() const override { return &equal; }
	CompareLessFunc get_compare_less_func() const override { return &less; }
	ObjectID get_object() const override { return callback.get_object_id(); }
	bool is_valid() const override { return callback.is_valid(); }
	int get_argument_count(bool &r_valid) const override {
		r_valid = true;
		return sizeof...(Args);
	}
	void call(const Variant **p_args, int p_count, Variant &r_result, Callable::CallError &r_error) const override {
		r_error.error = Callable::CallError::CALL_OK;
		if (!callback.is_valid()) {
			r_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
			return;
		}
		if (p_count < callback.get_minimum_argument_count() || p_count > int(sizeof...(Args))) {
			r_error.error = p_count < callback.get_minimum_argument_count() ? Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS : Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
			r_error.expected = p_count < callback.get_minimum_argument_count() ? callback.get_minimum_argument_count() : sizeof...(Args);
			return;
		}
		dispatch(p_args, p_count, r_result);
	}
};

template <class R, class... Args>
Callable WCallable<R(Args...)>::to_callable() const {
	if (!identity) {
		return {};
	}
	return Callable(memnew((EngineCallback<R, Args...>)(*this)));
}

template <class R, class... Args>
template <class... Values>
void WCallable<R(Args...)>::call_deferred(Values... p_args) const {
	auto source = *this;
	auto args = std::make_tuple(std::move(p_args)...);
	WCallable<void()>::make([source, args]() { std::apply([&](const auto &...p_values) { source.call(p_values...); }, args); }).to_callable().call_deferred();
}

template <class Instance, class Owner, class R, class... Args>
auto method_callable(Instance *p_owner, R (Owner::*p_method)(Args...), uint64_t p_slot) {
	if (!p_owner) {
		return WCallable<R(Args...)>();
	}
	const ObjectID id = p_owner->get_instance_id();
	return WCallable<R(Args...)>::make([id, p_method](Args... p_args) -> R { return (static_cast<Owner *>(ObjectDB::get_instance(id))->*p_method)(p_args...); }, [id]() { return ObjectDB::get_instance(id) != nullptr; }, id, std::make_shared<MethodIdentity>(id, p_slot));
}

template <class R, class... Args>
auto method_callable(R (*p_method)(Args...), uint64_t p_slot) {
	return WCallable<R(Args...)>::make(p_method, {}, ObjectID(), std::make_shared<MethodIdentity>(ObjectID(), p_slot));
}

} // namespace WGodotNative
