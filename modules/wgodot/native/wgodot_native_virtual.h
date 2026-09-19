// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

namespace WGodotNative {

template <class Target, class Source>
decltype(auto) virtual_argument(const Source &p_value) {
	return convert<Target>(p_value);
}

template <class Target, class T>
Target virtual_argument(const RequiredParam<T> &p_value) {
	EXTRACT_PARAM_OR_FAIL_V(value, p_value, Target());
	if constexpr (std::is_const_v<T>) {
		// Script object types do not carry the engine's const qualification.
		return convert<Target>(const_cast<std::remove_const_t<T> *>(object_pointer(value)));
	} else {
		return convert<Target>(value);
	}
}

} // namespace WGodotNative
