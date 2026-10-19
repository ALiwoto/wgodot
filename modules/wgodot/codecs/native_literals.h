// wgodot-changes::file
#pragma once

#include "core/math/color.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/math/vector4.h"

#include <charconv>
#include <cmath>
#include <type_traits>

namespace WGodotCppLiterals {

template <class T>
String number(T p_value) {
	if constexpr (std::is_integral_v<T>) {
		if constexpr (sizeof(T) == 8 && std::is_signed_v<T>) {
			if (p_value == INT64_MIN) {
				return "(-9223372036854775807LL - 1)";
			}
		}
		return itos(p_value);
	} else {
		const String type = std::is_same_v<T, float> ? "float" : "double";
		if (std::isnan(p_value)) {
			return "std::numeric_limits<" + type + ">::quiet_NaN()";
		}
		if (std::isinf(p_value)) {
			return String(p_value < 0 ? "-" : "") + "std::numeric_limits<" + type + ">::infinity()";
		}
		char buffer[64];
		const auto result = std::to_chars(buffer, buffer + sizeof(buffer), p_value);
		String text = String::utf8(buffer, result.ptr - buffer);
		if (!text.contains(".") && !text.contains("e") && !text.contains("E")) {
			text += ".0";
		}
		if constexpr (std::is_same_v<T, float>) {
			text += "f";
		}
		return text;
	}
}

template <class T>
String components(const T &p_value, int p_count) {
	String result = "{ ";
	for (int i = 0; i < p_count; i++) {
		if (i) {
			result += ", ";
		}
		result += number(p_value[i]);
	}
	return result + " }";
}

template <class T>
String packed_element(const T &p_value) {
	return number(p_value);
}

inline String packed_element(const Vector2 &p_value) {
	return components(p_value, 2);
}

inline String packed_element(const Vector3 &p_value) {
	return components(p_value, 3);
}

inline String packed_element(const Vector4 &p_value) {
	return components(p_value, 4);
}

inline String packed_element(const Color &p_value) {
	return components(p_value, 4);
}

} // namespace WGodotCppLiterals
