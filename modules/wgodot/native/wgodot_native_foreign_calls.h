// wgodot-changes::file
#pragma once

#include "wgodot_native_values.h"

namespace WGodotNative {

template <class... Args>
Variant foreign_call(Object *p_object, const StringName &p_method, const Args &...p_args) {
	Arguments<sizeof...(Args)> arguments(p_args...);
	Callable::CallError error;
	// The foreign object owns its Java/JavaScript handle and dispatches callp.
	Variant result = p_object->callp(p_method, arguments.data(), arguments.size(), error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Variant(), "Foreign object call: " + Variant::get_call_error_text(p_method, arguments.data(), arguments.size(), error));
	return result;
}

} // namespace WGodotNative
