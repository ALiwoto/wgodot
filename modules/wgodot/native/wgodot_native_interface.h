// wgodot-changes::file
#pragma once

#include "wgodot_native_callback.h"
#include "wgodot_native_calls.h"

#include "core/object/wgodot_interface.h"
#include "core/object/wgodot_native_interfaces.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace WGodotNative {

template <class Contract, class NativeBase, class NativeInterface>
class InterfaceValue {
	Variant value;
	NativeInterface *interface = nullptr;

public:
	using Interface = NativeInterface;
	InterfaceValue() = default;
	// Known implementors need only a C++ pointer conversion. Keep the original
	// value for reference ownership and the identity of freed non-reference objects.
	template <class T, class Pointer = decltype(object_pointer(std::declval<const T &>())),
			std::enable_if_t<std::is_convertible_v<Pointer, NativeBase *> && std::is_convertible_v<Pointer, NativeInterface *>, int> = 0>
	InterfaceValue(const T &p_value) : value(p_value), interface(static_cast<NativeInterface *>(object_pointer(p_value))) {}
	InterfaceValue(const Variant &p_value) {
		ERR_FAIL_COND_MSG(p_value.get_type() != Variant::NIL && p_value.get_type() != Variant::OBJECT, "Native game interface assignment requires an object or null.");
		if (Object *object = p_value.get_validated_object()) {
			interface = static_cast<NativeInterface *>(object->wgodot_get_native_interface(&NativeInterface::wgodot_interface_tag));
			ERR_FAIL_NULL_MSG(interface, "Incompatible native game interface assignment.");
		}
		value = p_value;
	}
	operator const Variant &() const { return value; }
	NativeBase *ptr() const { return static_cast<NativeBase *>(value.get_validated_object()); }
	NativeInterface *operator->() const { return ptr() ? interface : nullptr; }
	bool is_valid() const { return ptr() != nullptr; }
	bool operator==(const InterfaceValue &p_other) const { return value == p_other.value; }
	static bool accepts(const Variant &p_value) {
		if (p_value.get_type() == Variant::NIL) {
			return true;
		}
		if (p_value.get_type() != Variant::OBJECT) {
			return false;
		}
		Object *object = p_value.get_validated_object();
		return !object || object->wgodot_get_native_interface(&NativeInterface::wgodot_interface_tag) != nullptr;
	}
	static Contract cast(const Variant &p_value) {
		return accepts(p_value) ? Contract(p_value) : Contract();
	}
};

template <class Contract, class NativeBase, class NativeInterface>
NativeBase *object_pointer(const InterfaceValue<Contract, NativeBase, NativeInterface> &p_value) {
	return p_value.ptr();
}

template <class Callback, class Contract, class Method>
Callback interface_callable(const Contract &p_owner, Method p_method, uint64_t p_slot) {
	if (!p_owner.is_valid()) {
		return {};
	}
	const ObjectID id = p_owner.ptr()->get_instance_id();
	auto *interface = p_owner.operator->();
	return Callback::make([interface, p_method](auto &&...p_args) -> typename Callback::Result { return invoke_member<typename Callback::Result>(p_method, interface, p_args...); }, [id]() { return ObjectDB::get_instance(id) != nullptr; }, id, std::make_shared<MethodIdentity>(id, p_slot));
}

template <class Method>
struct InterfaceMethod;

template <class R, class C, class... Args>
struct InterfaceMethod<R (C::*)(Args...)> {
	using Result = R;
	template <size_t I>
	using Argument = std::tuple_element_t<I, std::tuple<Args...>>;
};

template <class R, class C, class... Args>
struct InterfaceMethod<R (C::*)(Args...) const> : InterfaceMethod<R (C::*)(Args...)> {};

template <class Contract>
struct InterfaceTypeInfo {
	static constexpr Variant::Type VARIANT_TYPE = Variant::OBJECT;
	static constexpr GodotTypeInfo::Metadata METADATA = GodotTypeInfo::METADATA_NONE;
	static PropertyInfo get_class_info() {
		return PropertyInfo(Variant::OBJECT, String(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT, Contract::get_class_static());
	}
};

template <class Contract>
struct InterfacePtrToArg {
	using EncodeT = Object *;
	static Contract convert(const void *p_pointer) { return Variant(*static_cast<Object *const *>(p_pointer)); }
	static void encode(const Contract &p_value, void *p_pointer) { *static_cast<Object **>(p_pointer) = p_value.ptr(); }
};

template <class Contract>
struct InterfaceVariantAccessor {
	static Contract get(const Variant *p_value) { return *p_value; }
	static void set(Variant *p_target, const Contract &p_value) { *p_target = static_cast<const Variant &>(p_value); }
};

} // namespace WGodotNative
