// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/method_ptrcall.h"
#include "core/variant/variant_caster.h"
#include "core/variant/variant_internal.h"

namespace WGodotNative {

template <class T>
class ObjectView;

// An Object-typed GDScript slot can hold a RefCounted object. It must retain that
// reference, and a freed non-reference object must keep its original ObjectID.
// A raw C++ pointer provides neither behavior.
template <class T>
class ObjectValue {
	friend class ObjectView<T>;
	Variant value;

public:
	ObjectValue() = default;
	ObjectValue(std::nullptr_t) {}
	ObjectValue(T *p_object) : value(p_object) {}
	ObjectValue(const Variant &p_value) {
		ERR_FAIL_COND_MSG(p_value.get_type() != Variant::NIL && p_value.get_type() != Variant::OBJECT, "Native game object assignment requires an object or null.");
		Object *object = p_value.get_validated_object();
		ERR_FAIL_COND_MSG(object && !Object::cast_to<T>(object), "Incompatible native game object assignment.");
		value = p_value;
	}
	operator const Variant &() const { return value; }
	T *ptr() const { return static_cast<T *>(value.get_validated_object()); }
	T *operator->() const { return ptr(); }
	bool operator==(const ObjectValue &p_other) const { return value == p_other.value; }
};

// Borrows a typed array slot, or the iterator's retained current element when
// the array is shared. Escaping values use owned() to retain their own handle.
template <class T>
class ObjectView {
	const Variant *slot;

public:
	using Owned = std::conditional_t<std::is_base_of_v<RefCounted, T>, Ref<T>, ObjectValue<T>>;
	explicit ObjectView(const Variant &p_slot) : slot(&p_slot) {}
	operator const Variant &() const { return *slot; }
	T *ptr() const { return static_cast<T *>(slot->get_validated_object()); }
	T *operator->() const { return ptr(); }
	Owned owned() const {
		if constexpr (std::is_base_of_v<RefCounted, T>) {
			return Ref<T>(ptr());
		} else {
			ObjectValue<T> result;
			result.value = *slot; // The typed return contract already establishes T.
			return result;
		}
	}
	operator Owned() const { return owned(); }
};

template <class T>
struct IsObjectValue : std::false_type {};

template <class T>
struct IsObjectValue<ObjectValue<T>> : std::true_type {};

} // namespace WGodotNative

template <class T>
struct GetTypeInfo<WGodotNative::ObjectValue<T>> : GetTypeInfo<T *> {};

template <class T>
struct PtrToArg<WGodotNative::ObjectValue<T>> {
	using EncodeT = Object *;
	static WGodotNative::ObjectValue<T> convert(const void *p_pointer) { return Object::cast_to<T>(*static_cast<Object *const *>(p_pointer)); }
	static void encode(const WGodotNative::ObjectValue<T> &p_value, void *p_pointer) { *static_cast<Object **>(p_pointer) = p_value.ptr(); }
};

template <class T>
struct VariantInternalAccessor<WGodotNative::ObjectValue<T>> {
	static WGodotNative::ObjectValue<T> get(const Variant *p_value) { return *p_value; }
	static void set(Variant *p_target, const WGodotNative::ObjectValue<T> &p_value) { *p_target = static_cast<const Variant &>(p_value); }
};

template <class T>
struct VariantObjectClassChecker<WGodotNative::ObjectValue<T>> {
	static bool check(const Variant &p_value) {
		Object *object = p_value.get_validated_object();
		return !object || Object::cast_to<T>(object);
	}
};
