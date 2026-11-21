// wgodot-changes::file
#include "wgodot_method_event.h"

#include "core/io/wgodot_named_types.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

bool WGodotMethodEvent::validate_parameterless(const StringName &p_target, const StringName &p_method, String &r_error) {
	if (p_target.is_empty() || !ClassDB::is_parent_class(WGodotNamedTypes::native_type(p_target), SNAME("Node"))) {
		r_error = "method event requires a declared Node target type";
		return false;
	}
	MethodInfo method;
	const Ref<Script> script = WGodotNamedTypes::script(p_target);
	if (script.is_valid() && script->has_method(p_method)) {
		method = script->get_method_info(p_method);
	} else if (!ClassDB::get_method_info(WGodotNamedTypes::native_type(p_target), p_method, &method)) {
		r_error = "unknown method event: " + String(p_target) + "." + String(p_method);
		return false;
	}
	if (!method.arguments.is_empty() || (method.flags & (METHOD_FLAG_VARARG | METHOD_FLAG_STATIC))) {
		r_error = "typed method events currently require a parameterless instance method";
		return false;
	}
	return true;
}

bool WGodotMethodEvent::matches(Object *p_object, const StringName &p_target) {
	if (p_target.is_empty()) {
		return true; // Legacy, non-strict animation events retain their behavior.
	}
	if (!ScriptServer::is_global_class(p_target)) {
		return p_object->is_class(p_target);
	}
	for (Ref<Script> script = p_object->get_script(); script.is_valid(); script = script->get_base_script()) {
		if (script->get_global_name() == p_target) {
			return true;
		}
	}
	return false;
}
