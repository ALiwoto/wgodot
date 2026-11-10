// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

namespace WGodotSerialization {
// Dispatch once per collection; keep packed elements in their original storage.
template <class Action>
bool visit_array(Variant &r_value, const Action &p_action) {
#define SERIALIZATION_ARRAY_CASE(KIND, TYPE) \
	case Variant::KIND: { \
		TYPE array = r_value; \
		if (!p_action(array)) { \
			return false; \
		} \
		r_value = array; \
		return true; \
	}
	switch (r_value.get_type()) {
		SERIALIZATION_ARRAY_CASE(ARRAY, Array)
		SERIALIZATION_ARRAY_CASE(PACKED_BYTE_ARRAY, PackedByteArray)
		SERIALIZATION_ARRAY_CASE(PACKED_INT32_ARRAY, PackedInt32Array)
		SERIALIZATION_ARRAY_CASE(PACKED_INT64_ARRAY, PackedInt64Array)
		SERIALIZATION_ARRAY_CASE(PACKED_FLOAT32_ARRAY, PackedFloat32Array)
		SERIALIZATION_ARRAY_CASE(PACKED_FLOAT64_ARRAY, PackedFloat64Array)
		SERIALIZATION_ARRAY_CASE(PACKED_STRING_ARRAY, PackedStringArray)
		SERIALIZATION_ARRAY_CASE(PACKED_VECTOR3_ARRAY, PackedVector3Array)
		default:
			return false;
	}
#undef SERIALIZATION_ARRAY_CASE
}

inline bool fits_array(const Variant &p_value, Variant::Type p_type) {
	if (p_type == Variant::PACKED_BYTE_ARRAY) {
		const int64_t value = p_value;
		return value >= 0 && value <= UINT8_MAX;
	}
	if (p_type == Variant::PACKED_INT32_ARRAY) {
		const int64_t value = p_value;
		return value >= INT32_MIN && value <= INT32_MAX;
	}
	return true;
}
} // namespace WGodotSerialization
