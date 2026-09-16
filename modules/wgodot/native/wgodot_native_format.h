// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

#include <array>

namespace WGodotNative {

inline String format_string(const String &p_format, const Span<Variant> &p_arguments) {
	bool error = false;
	const String result = p_format.sprintf(p_arguments, &error);
	ERR_FAIL_COND_V_MSG(error, String(), "Invalid native game string format: " + result);
	return result;
}

} // namespace WGodotNative
