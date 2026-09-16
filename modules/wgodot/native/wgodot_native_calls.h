// wgodot-changes::file
#pragma once

#include "wgodot_native_object.h"
#include "wgodot_native_packed.h"

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant_caster.h"

#include <array>
#include <type_traits>
#include <utility>

namespace WGodotNative {

template <class T>
auto instantiate() {
	if constexpr (std::is_abstract_v<T>) {
		// Crypto and other backend interfaces register a concrete engine factory.
		T *instance = Object::cast_to<T>(ClassDB::instantiate(T::get_class_static()));
		if constexpr (std::is_base_of_v<RefCounted, T>) {
			return Ref<T>(instance);
		} else {
			return instance;
		}
	} else {
		return memnew(T);
	}
}

template <class T>
T *object_pointer(T *p_value) {
	return p_value;
}

template <class T>
T *object_pointer(const Ref<T> &p_value) {
	return p_value.ptr();
}

template <class T>
T *object_pointer(const ObjectValue<T> &p_value) {
	return p_value.ptr();
}

inline Object *object_pointer(const Variant &p_value) {
	return p_value.get_validated_object();
}

inline void free_object(Object *p_object) {
	ERR_FAIL_NULL(p_object);
	Callable::CallError error;
	// Object handles free specially, including RefCounted and locked-object
	// checks. It has no MethodBind and must not be replaced with raw memdelete.
	p_object->callp(SNAME("free"), nullptr, 0, error);
	ERR_FAIL_COND_MSG(error.error != Callable::CallError::CALL_OK, "Native game free() failed.");
}

template <class T>
struct IsRef : std::false_type {};

template <class T>
struct IsRef<Ref<T>> : std::true_type {};

// Keep exact native arguments borrowed. In particular, obtaining an Object pointer
// from Ref<T> must not temporarily increase the object's visible reference count.
template <class To, class From>
decltype(auto) convert(From &&p_value) {
	using Target = std::decay_t<To>;
	using Source = std::decay_t<From>;
	if constexpr (std::is_same_v<Target, Source>) {
		return std::forward<From>(p_value);
	} else if constexpr (std::is_convertible_v<From &&, Target>) {
		return Target(std::forward<From>(p_value));
	} else if constexpr (IsPacked<Source>::value) {
		if constexpr (std::is_same_v<Target, typename Source::Native>) {
			return p_value.native();
		} else {
			return VariantCaster<Target>::cast(static_cast<const Variant &>(p_value));
		}
	} else if constexpr (std::is_pointer_v<Target> && std::is_base_of_v<Object, std::remove_pointer_t<Target>> && IsObjectValue<Source>::value) {
		return Object::cast_to<std::remove_pointer_t<Target>>(p_value.ptr());
	} else if constexpr (std::is_enum_v<Target>) {
		return static_cast<Target>(p_value);
	} else if constexpr (std::is_pointer_v<Target> && std::is_base_of_v<Object, std::remove_pointer_t<Target>> && IsRef<Source>::value) {
		return Object::cast_to<std::remove_pointer_t<Target>>(p_value.ptr());
	} else {
		return VariantCaster<Target>::cast(Variant(p_value));
	}
}

template <class Result, class Function, class... Args>
Result invoke_result(Function &&p_function, Args &&...p_args) {
	if constexpr (std::is_void_v<Result>) {
		std::forward<Function>(p_function)(std::forward<Args>(p_args)...);
	} else {
		return convert<Result>(std::forward<Function>(p_function)(std::forward<Args>(p_args)...));
	}
}

template <class Result, class Owner, class Return, class... Params, class Instance, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_member(Return (Owner::*p_method)(Params...), Instance *p_self, Args &&...p_args) {
	return invoke_result<Result>([&]() -> decltype(auto) { return (p_self->*p_method)(convert<Params>(std::forward<Args>(p_args))...); });
}

template <class Result, class Owner, class Return, class... Params, class Instance, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_member(Return (Owner::*p_method)(Params...) const, Instance *p_self, Args &&...p_args) {
	return invoke_result<Result>([&]() -> decltype(auto) { return (p_self->*p_method)(convert<Params>(std::forward<Args>(p_args))...); });
}

template <class Result, class Return, class... Params, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_static(Return (*p_method)(Params...), Args &&...p_args) {
	return invoke_result<Result>([&]() -> decltype(auto) { return p_method(convert<Params>(std::forward<Args>(p_args))...); });
}

template <class Result, class... Args>
Result invoke_bind(const MethodBind *p_method, Object *p_self, const Args &...p_args) {
	ERR_FAIL_NULL_V(p_method, Result());
	std::array<Variant, sizeof...(Args)> arguments{ Variant(p_args)... };
	std::array<const Variant *, sizeof...(Args)> pointers{};
	for (size_t i = 0; i < arguments.size(); i++) {
		pointers[i] = &arguments[i];
	}
	Callable::CallError error;
	Variant result = p_method->call(p_self, pointers.data(), int(arguments.size()), error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), "Native game call failed: " + String(p_method->get_instance_class()) + "." + String(p_method->get_name()));
	if constexpr (!std::is_void_v<Result>) {
		return convert<Result>(result);
	}
}

} // namespace WGodotNative
