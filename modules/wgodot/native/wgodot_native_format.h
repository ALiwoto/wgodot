// wgodot-changes::file
#pragma once

#include "wgodot_native_warray.h"

#include "core/variant/variant.h"

#include <array>

namespace WGodotNative {

inline String format_string(const String &p_format, const Span<Variant> &p_arguments) {
	bool error = false;
	const String result = p_format.sprintf(p_arguments, &error);
	ERR_FAIL_COND_V_MSG(error, String(), "Invalid native game string format: " + result);
	return result;
}

template <class T>
String format_string(const String &p_format, const WArray<T> &p_arguments) {
	Vector<Variant> values;
	values.resize(p_arguments.size());
	for (int64_t i = 0; i < p_arguments.size(); i++) {
		values.set(i, Variant(p_arguments.native()[i]));
	}
	return format_string(p_format, values.span());
}

} // namespace WGodotNative
