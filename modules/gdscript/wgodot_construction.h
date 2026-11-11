// wgodot-changes::file
#pragma once

#include "core/variant/variant_internal.h"
#include <type_traits>

namespace WGodotGDScriptConstruction {
template <typename T>
void packed_literal(Variant &r_result, const Variant *const *p_values, int p_count) {
	Vector<T> values;
	values.resize(p_count);
	T *output = values.ptrw();
	for (int i = 0; i < p_count; i++) {
		if constexpr (std::is_integral_v<T>) {
			output[i] = T(int64_t(*p_values[i]));
		} else if constexpr (std::is_floating_point_v<T>) {
			output[i] = T(double(*p_values[i]));
		} else {
			output[i] = T(*p_values[i]);
		}
	}
	VariantInternal::initialize_packed_storage(&r_result, values);
}

inline void packed_literal(Variant &r_result, Variant::Type p_type, const Variant *const *p_values, int p_count) {
	switch (p_type) {
#define WG_PACKED_LITERAL(m_variant, m_type) \
	case Variant::m_variant: \
		packed_literal<m_type>(r_result, p_values, p_count); \
		break;
		WG_PACKED_LITERAL(PACKED_BYTE_ARRAY, uint8_t)
		WG_PACKED_LITERAL(PACKED_INT32_ARRAY, int32_t)
		WG_PACKED_LITERAL(PACKED_INT64_ARRAY, int64_t)
		WG_PACKED_LITERAL(PACKED_FLOAT32_ARRAY, float)
		WG_PACKED_LITERAL(PACKED_FLOAT64_ARRAY, double)
		WG_PACKED_LITERAL(PACKED_STRING_ARRAY, String)
		WG_PACKED_LITERAL(PACKED_VECTOR2_ARRAY, Vector2)
		WG_PACKED_LITERAL(PACKED_VECTOR3_ARRAY, Vector3)
		WG_PACKED_LITERAL(PACKED_COLOR_ARRAY, Color)
		WG_PACKED_LITERAL(PACKED_VECTOR4_ARRAY, Vector4)
#undef WG_PACKED_LITERAL
		default:
			ERR_FAIL_MSG("Expected a packed-array literal type.");
	}
}
} // namespace WGodotGDScriptConstruction
