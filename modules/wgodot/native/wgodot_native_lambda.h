// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

#include "core/object/callable_mp.h"

namespace WGodotNative {

template <class Method>
struct LambdaMethod;

template <class Return, class... Params>
struct LambdaMethod<Return (*)(Params...)> {
	static constexpr int ARGUMENT_COUNT = sizeof...(Params);
	static constexpr bool HAS_SELF = false;
	using Method = Return (*)(Params...);
	static void call(Method p_method, Object *p_self, const Variant **p_args, int p_count, Variant &r_result, Callable::CallError &r_error, const Vector<Variant> &p_defaults) {
		if constexpr (std::is_void_v<Return>) {
			call_with_variant_args_static_dv(p_method, p_args, p_count, r_error, p_defaults);
		} else {
			call_with_variant_args_static_ret_dv(p_method, p_args, p_count, r_result, r_error, p_defaults);
		}
	}
};

template <class Owner, class Return, class... Params>
struct LambdaMethod<Return (Owner::*)(Params...)> {
	static constexpr int ARGUMENT_COUNT = sizeof...(Params);
	static constexpr bool HAS_SELF = true;
	using Method = Return (Owner::*)(Params...);
	static void call(Method p_method, Object *p_self, const Variant **p_args, int p_count, Variant &r_result, Callable::CallError &r_error, const Vector<Variant> &p_defaults) {
		if constexpr (std::is_void_v<Return>) {
			call_with_variant_args_dv(static_cast<Owner *>(p_self), p_method, p_args, p_count, r_error, p_defaults);
		} else {
			call_with_variant_args_ret_dv(static_cast<Owner *>(p_self), p_method, p_args, p_count, r_result, r_error, p_defaults);
		}
	}
};

// Captures are passed as value parameters on each invocation. Reassigning a
// captured scalar must not change the closure's saved value; shared containers
// still share their storage. Variant also keeps captured RefCounted objects alive.
template <int CaptureCount, class Method>
class LambdaCallable : public CallableCustom {
	using Invocation = LambdaMethod<Method>;
	static constexpr int ARGUMENT_COUNT = Invocation::ARGUMENT_COUNT - CaptureCount;
	Method method;
	Variant self;
	StringName name;
	Vector<Variant> captures;
	Vector<Variant> defaults;

	static bool compare_equal(const CallableCustom *p_left, const CallableCustom *p_right) { return p_left == p_right; }
	static bool compare_less(const CallableCustom *p_left, const CallableCustom *p_right) { return p_left < p_right; }

public:
	LambdaCallable(Method p_method, Object *p_self, const StringName &p_name, const Vector<Variant> &p_captures, const Vector<Variant> &p_defaults) : method(p_method), self(p_self), name(p_name), captures(p_captures), defaults(p_defaults) {}
	bool is_valid() const override { return !Invocation::HAS_SELF || self.get_validated_object() != nullptr; }
	uint32_t hash() const override { return hash_murmur3_one_64(uint64_t(this)); }
	String get_as_text() const override { return String(name) + "(native lambda)"; }
	CompareEqualFunc get_compare_equal_func() const override { return compare_equal; }
	CompareLessFunc get_compare_less_func() const override { return compare_less; }
	ObjectID get_object() const override { return self; }
	StringName get_method() const override { return name; }
	int get_argument_count(bool &r_valid) const override {
		r_valid = true;
		return ARGUMENT_COUNT;
	}
	void call(const Variant **p_arguments, int p_count, Variant &r_result, Callable::CallError &r_error) const override {
		Object *owner = self.get_validated_object();
		if (Invocation::HAS_SELF && !owner) {
			r_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
			return;
		}
		if (p_count > ARGUMENT_COUNT || p_count < ARGUMENT_COUNT - defaults.size()) {
			r_error.error = p_count > ARGUMENT_COUNT ? Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS : Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
			r_error.expected = ARGUMENT_COUNT;
			return;
		}
		std::array<const Variant *, Invocation::ARGUMENT_COUNT> arguments{};
		Variant nil;
		for (int i = 0; i < CaptureCount; i++) {
			arguments[i] = &captures[i];
			if (captures[i].get_type() == Variant::OBJECT) {
				bool freed = false;
				captures[i].get_validated_object_with_check(freed);
				if (freed) {
					ERR_PRINT(vformat("Native lambda capture %d was freed. Passing null.", i));
					arguments[i] = &nil;
				}
			}
		}
		for (int i = 0; i < p_count; i++) {
			arguments[CaptureCount + i] = p_arguments[i];
		}
		Invocation::call(method, owner, arguments.data(), CaptureCount + p_count, r_result, r_error, defaults);
		if (r_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
			r_error.argument -= CaptureCount;
		}
	}
};

template <int CaptureCount, class Method>
Callable lambda_callable(Method p_method, Object *p_self, const StringName &p_name, const Vector<Variant> &p_captures, const Vector<Variant> &p_defaults) {
	using Implementation = LambdaCallable<CaptureCount, Method>;
	return Callable(memnew(Implementation(p_method, p_self, p_name, p_captures, p_defaults)));
}

} // namespace WGodotNative
