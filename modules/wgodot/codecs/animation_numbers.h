// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

#include <charconv>

// Shortest decimal that recovers the same IEEE value, including signed zero.
// Do not use String::num: animation data must not be rounded for display.
template <typename T>
String w_animation_number(T p_value) {
	char buffer[64];
	String text;
	for (std::chars_format format : { std::chars_format::fixed, std::chars_format::scientific }) {
		auto result = std::to_chars(buffer, buffer + sizeof(buffer), p_value, format);
		if (result.ec != std::errc()) {
			continue; // Very large/small doubles may not fit the fixed spelling.
		}
		String candidate = String::utf8(buffer, result.ptr - buffer);
		const int exponent = candidate.find("e");
		if (exponent >= 0) {
			int digits = exponent + 2; // Scientific to_chars always includes a sign.
			while (digits + 1 < candidate.length() && candidate[digits] == '0') {
				candidate = candidate.substr(0, digits) + candidate.substr(digits + 1);
			}
		} else if (candidate.find(".") < 0 && (p_value >= T(INT64_MAX) || p_value < T(INT64_MIN))) {
			candidate += ".0"; // Keep VariantParser out of the int64 overflow path.
		}
		if (text.is_empty() || candidate.length() < text.length()) {
			text = candidate;
		}
	}
	ERR_FAIL_COND_V(text.is_empty(), String());
	// VariantParser reads integer-looking tokens as int64, which loses -0.
	return text == "-0" ? String("-0.0") : text;
}
