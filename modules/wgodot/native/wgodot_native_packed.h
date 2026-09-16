// wgodot-changes::file
#pragma once

#include "core/variant/method_ptrcall.h"
#include "core/variant/type_info.h"
#include "core/variant/variant_internal.h"

namespace WGodotNative {

// Packed arrays share the Variant's packed-array reference in GDScript. A plain
// Vector<T> would detach on mutation and silently change assignment semantics.
template <class ArrayType>
class Packed {
	Variant value;

public:
	using Native = ArrayType;
	Packed() : value(ArrayType()) {}
	Packed(const ArrayType &p_value) : value(p_value) {}
	Packed(const Variant &p_value) {
		if (p_value.get_type() == GetTypeInfo<ArrayType>::VARIANT_TYPE) {
			value = p_value;
		} else {
			const Variant *argument = &p_value;
			Callable::CallError error;
			Variant::construct(GetTypeInfo<ArrayType>::VARIANT_TYPE, value, &argument, 1, error);
			ERR_FAIL_COND_MSG(error.error != Callable::CallError::CALL_OK, "Invalid native game packed-array conversion.");
		}
	}
	operator const Variant &() const { return value; }
	const ArrayType &native() const { return VariantInternalAccessor<ArrayType>::get(&value); }
};

template <class T>
struct IsPacked : std::false_type {};

template <class T>
struct IsPacked<Packed<T>> : std::true_type {};

} // namespace WGodotNative

template <class T>
struct GetTypeInfo<WGodotNative::Packed<T>> : GetTypeInfo<T> {};

template <class T>
struct PtrToArg<WGodotNative::Packed<T>> {
	using EncodeT = T;
	static WGodotNative::Packed<T> convert(const void *p_pointer) { return *static_cast<const T *>(p_pointer); }
	static void encode(const WGodotNative::Packed<T> &p_value, void *p_pointer) { *static_cast<T *>(p_pointer) = p_value.native(); }
};

template <class T>
struct VariantInternalAccessor<WGodotNative::Packed<T>> {
	static WGodotNative::Packed<T> get(const Variant *p_value) { return *p_value; }
	static void set(Variant *p_target, const WGodotNative::Packed<T> &p_value) { *p_target = static_cast<const Variant &>(p_value); }
};
