// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

#include "core/object/wgodot_native_interfaces.h"

namespace WGodotNative {

template <class Contract, class NativeBase>
class InterfaceValue {
	Variant value;

public:
	InterfaceValue() = default;
	InterfaceValue(const Variant &p_value) {
		ERR_FAIL_COND_MSG(!accepts(p_value), "Incompatible native game interface assignment.");
		value = p_value;
	}
	operator const Variant &() const { return value; }
	NativeBase *ptr() const { return static_cast<NativeBase *>(value.get_validated_object()); }
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
		return !object || WGodotNativeInterfaces::accepts(object->get_class_name(), Contract::get_class_static());
	}
	static Contract cast(const Variant &p_value) {
		return accepts(p_value) ? Contract(p_value) : Contract();
	}
};

template <class Contract, class NativeBase>
NativeBase *object_pointer(const InterfaceValue<Contract, NativeBase> &p_value) {
	return p_value.ptr();
}

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
