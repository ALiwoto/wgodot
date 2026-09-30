// wgodot-changes::file
#pragma once

#include "core/variant/type_info.h"
#include "core/variant/variant.h"

namespace WGodotNative {

// Retain the engine's shared packed allocation directly. Only ABI conversions
// create a Variant; native handles and element operations never contain one.
template <class T>
struct PackedStorage {
	using Data = Variant::PackedArrayRef<T>;
	static Data *create() { return Data::create(); }
	static Data *create(const Vector<T> &p_values) { return Data::create(p_values); }
	static void retain(Data *p_data) { p_data->reference(); }
	static void release(Data *p_data) { Variant::PackedArrayRefBase::destroy(p_data); }
	static Vector<T> &elements(Data *p_data) { return p_data->array; }
	static bool read_only(const Data *) { return false; }
	static void make_read_only(Data *) {} // GDScript packed arrays have no read-only flag.

	static Data *from_variant(const Variant &p_value) {
		if (p_value.type != GetTypeInfo<Vector<T>>::VARIANT_TYPE) {
			// Dynamic engine boundaries can supply a convertible Array or packed
			// kind. Keep that conversion here, outside native container operations.
			Variant converted;
			const Variant *argument = &p_value;
			Callable::CallError error;
			Variant::construct(GetTypeInfo<Vector<T>>::VARIANT_TYPE, converted, &argument, 1, error);
			ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, create(), "Invalid native packed-array boundary type.");
			return from_variant(converted);
		}
		auto *data = static_cast<Data *>(p_value._data.packed_array);
		retain(data);
		return data;
	}
	static Variant to_variant(Data *p_data) {
		Variant result;
		result.type = GetTypeInfo<Vector<T>>::VARIANT_TYPE;
		retain(p_data);
		result._data.packed_array = p_data;
		return result;
	}
};

template <class T, Variant::Type Kind>
struct ArrayStorage : PackedStorage<T> {
	static_assert(Kind >= Variant::PACKED_BYTE_ARRAY && Kind <= Variant::PACKED_VECTOR4_ARRAY);
	static_assert(Kind == GetTypeInfo<Vector<T>>::VARIANT_TYPE);
};

template <class T>
struct ArrayStorage<T, Variant::ARRAY> {
	struct Data {
		SafeRefCount references;
		Vector<T> values;
		bool read_only = false;
		Data() { references.init(); }
		explicit Data(std::initializer_list<T> p_values) : values(p_values) { references.init(); }
	};
	static Data *create() { return memnew(Data); }
	static Data *create(std::initializer_list<T> p_values) { return memnew(Data(p_values)); }
	static void retain(Data *p_data) { p_data->references.ref(); }
	static void release(Data *p_data) {
		if (p_data->references.unref()) {
			memdelete(p_data);
		}
	}
	static Vector<T> &elements(Data *p_data) { return p_data->values; }
	static bool read_only(const Data *p_data) { return p_data->read_only; }
	static void make_read_only(Data *p_data) { p_data->read_only = true; }
};

} // namespace WGodotNative
