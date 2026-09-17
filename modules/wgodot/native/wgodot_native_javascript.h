// wgodot-changes::file
#pragma once

#include "wgodot_native_values.h"

#include "platform/web/api/javascript_bridge_singleton.h"

namespace WGodotNative {

template <class... Args>
Variant javascript_call(JavaScriptObject *p_object, const StringName &p_method, const Args &...p_args) {
	Arguments<sizeof...(Args)> arguments(p_args...);
	Callable::CallError error;
	// Virtual dispatch reaches JavaScriptObjectImpl, which owns the JS handle.
	Variant result = p_object->callp(p_method, arguments.data(), arguments.size(), error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Variant(), "JavaScriptObject.call: " + Variant::get_call_error_text(p_method, arguments.data(), arguments.size(), error));
	return result;
}

} // namespace WGodotNative
