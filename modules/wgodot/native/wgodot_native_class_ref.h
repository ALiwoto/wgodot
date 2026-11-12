// wgodot-changes::file
#pragma once

#include "wgodot_native_object.h"

#include "core/object/wgodot_class_references.h"
#include "core/variant/type_info.h"

namespace WGodotNative {

template <class T>
class ClassRef {
	Ref<WNativeClassReference> reference;

public:
	ClassRef() = default;
	ClassRef(std::nullptr_t) {}
	explicit ClassRef(const Variant &p_value) {
		ERR_FAIL_COND_MSG(!WGodotClassReferences::accepts(p_value, T::get_class_static()), "Incompatible native class reference.");
		reference = p_value;
	}
	template <class U>
	ClassRef(const ClassRef<U> &p_value) : ClassRef(Variant(p_value)) {}

	static ClassRef from_name(const StringName &p_name) {
		return ClassRef(WGodotClassReferences::resolve(p_name));
	}
	static ClassRef try_from(const Variant &p_value) {
		return WGodotClassReferences::accepts(p_value, T::get_class_static()) ? ClassRef(p_value) : ClassRef();
	}
	auto instantiate() const {
		using Instance = std::conditional_t<std::is_base_of_v<RefCounted, T>, Ref<T>, ObjectValue<T>>;
		ERR_FAIL_COND_V_MSG(reference.is_null(), Instance(), "Cannot construct a null class reference.");
		return Instance(reference->instantiate());
	}
	operator Variant() const { return reference; }
	WNativeClassReference *ptr() const { return reference.ptr(); }
	explicit operator bool() const { return reference.is_valid(); }
	bool operator==(const ClassRef &p_other) const { return reference == p_other.reference; }
	uint32_t hash() const { return Variant(reference).hash(); }
};

template <class T>
WNativeClassReference *object_pointer(const ClassRef<T> &p_value) {
	return p_value.ptr();
}

} // namespace WGodotNative

template <class T>
struct GetTypeInfo<WGodotNative::ClassRef<T>> {
	static constexpr Variant::Type VARIANT_TYPE = Variant::OBJECT;
	static constexpr GodotTypeInfo::Metadata METADATA = GodotTypeInfo::METADATA_NONE;
	static PropertyInfo get_class_info() {
		PropertyInfo info(Variant::OBJECT, String());
		info.class_name = WGodotClassReferences::constraint(T::get_class_static());
		return info;
	}
};

template <class T>
struct GetTypeInfo<const WGodotNative::ClassRef<T> &> : GetTypeInfo<WGodotNative::ClassRef<T>> {};

template <class T>
struct VariantCaster<WGodotNative::ClassRef<T>> {
	static WGodotNative::ClassRef<T> cast(const Variant &p_value) { return WGodotNative::ClassRef<T>(p_value); }
};

template <class T>
struct VariantObjectClassChecker<WGodotNative::ClassRef<T>> {
	static bool check(const Variant &p_value) { return WGodotClassReferences::accepts(p_value, T::get_class_static()); }
};

template <class T>
struct PtrToArg<WGodotNative::ClassRef<T>> {
	using EncodeT = Object *;
	static WGodotNative::ClassRef<T> convert(const void *p_pointer) { return WGodotNative::ClassRef<T>(Variant(*static_cast<Object *const *>(p_pointer))); }
	static void encode(const WGodotNative::ClassRef<T> &p_value, void *p_pointer) { *static_cast<Object **>(p_pointer) = Variant(p_value).get_validated_object(); }
};
