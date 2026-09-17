// wgodot-changes::file
#pragma once

#include "wgodot_native_object.h"
#include "wgodot_native_packed.h"
#include "wgodot_native_warray.h"

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
		// Backend interfaces register a concrete engine factory.
		T *instance = Object::cast_to<T>(ClassDB::instantiate(T::get_class_static()));
		if constexpr (std::is_base_of_v<RefCounted, T>) {
			return Ref<T>(instance);
		} else {
			return ObjectValue<T>(instance);
		}
	} else {
		// memnew can itself invoke a factory returning Ref<T>.
		auto instance = memnew(T);
		if constexpr (std::is_base_of_v<RefCounted, T>) {
			return Ref<T>(instance);
		} else {
			return ObjectValue<T>(instance);
		}
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

template <class T>
T *object_pointer(const ObjectView<T> &p_value) {
	return p_value.ptr();
}

inline Object *object_pointer(const Variant &p_value) {
	return p_value.get_validated_object();
}

template <class T>
bool valid_instance(T *p_instance) {
	ERR_FAIL_NULL_V(p_instance, false);
	return true;
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

// Borrow existing arguments, but own temporaries: returning an rvalue reference
// through a function does not extend its lifetime in an auto&& argument slot.
template <class To, class From>
decltype(auto) convert(From &&p_value) {
	using Target = std::decay_t<To>;
	using Source = std::decay_t<From>;
	if constexpr (std::is_same_v<Target, Source>) {
		if constexpr (std::is_lvalue_reference_v<From &&>) {
			return (p_value);
		} else {
			return Target(std::forward<From>(p_value));
		}
	} else if constexpr (IsWCallable<Target>::value && IsWCallable<Source>::value) {
		return Target::adapt(p_value);
	} else if constexpr (IsWArray<Target>::value || IsWArray<Source>::value) {
		static_assert(std::is_same_v<Target, Source>, "WArray requires an explicit container boundary handler; implicit Array/Variant conversion is forbidden.");
	} else if constexpr (std::is_convertible_v<From &&, Target>) {
		return Target(std::forward<From>(p_value));
	} else if constexpr (IsPacked<Source>::value) {
		if constexpr (std::is_same_v<Target, typename Source::Native>) {
			if constexpr (std::is_lvalue_reference_v<From &&>) {
				return p_value.native();
			} else {
				return Target(p_value.native());
			}
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

// Used only by exporter handlers for APIs that return an independent snapshot.
// This is not an implicit conversion: aliases of p_source are not preserved.
template <class Result>
Result copy_array(const Array &p_source) {
	static_assert(IsWArray<Result>::value);
	Result result;
	result.resize(p_source.size());
	for (int64_t i = 0; i < p_source.size(); i++) {
		result.set(i, convert<typename Result::Element>(p_source[i]));
	}
	return result;
}

template <class Result, class T>
Result copy_vector(const Vector<T> &p_source) {
	static_assert(IsWArray<Result>::value);
	Result result;
	result.resize(p_source.size());
	for (int64_t i = 0; i < p_source.size(); i++) {
		result.set(i, convert<typename Result::Element>(p_source[i]));
	}
	return result;
}

template <class Result, class Owner, class Return, class... Params, class Instance, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_member(Return (Owner::*p_method)(Params...), Instance *p_self, Args &&...p_args) {
	ERR_FAIL_NULL_V(p_self, Result());
	return invoke_result<Result>([&]() -> decltype(auto) { return (p_self->*p_method)(convert<Params>(std::forward<Args>(p_args))...); });
}

template <class Result, class Owner, class Return, class... Params, class Instance, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_member(Return (Owner::*p_method)(Params...) const, Instance *p_self, Args &&...p_args) {
	ERR_FAIL_NULL_V(p_self, Result());
	return invoke_result<Result>([&]() -> decltype(auto) { return (p_self->*p_method)(convert<Params>(std::forward<Args>(p_args))...); });
}

template <class Result, class Return, class... Params, class... Args,
		std::enable_if_t<sizeof...(Params) == sizeof...(Args), int> = 0>
Result invoke_static(Return (*p_method)(Params...), Args &&...p_args) {
	return invoke_result<Result>([&]() -> decltype(auto) { return p_method(convert<Params>(std::forward<Args>(p_args))...); });
}

} // namespace WGodotNative
