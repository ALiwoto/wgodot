// wgodot-changes::file
#include "wgodot_scene_tree.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

void SceneTree::_call_group_as(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	r_error.error = Callable::CallError::CALL_OK;
	if (p_argcount < 2) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 2;
		return;
	}
	if (p_args[0]->get_type() != Variant::OBJECT) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::OBJECT;
		return;
	}
	if (p_args[1]->get_type() != Variant::STRING && p_args[1]->get_type() != Variant::STRING_NAME) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 1;
		r_error.expected = Variant::STRING_NAME;
		return;
	}

	Object *class_value = *p_args[0];
	Ref<Script> script = Object::cast_to<Script>(class_value);
	StringName native_class;
	if (script.is_valid()) {
		native_class = script->get_instance_base_type();
	}
#ifdef MODULE_GDSCRIPT_ENABLED
	else if (const auto *native = Object::cast_to<GDScriptNativeClass>(class_value)) {
		native_class = native->get_name();
	}
#endif
	if (!ClassDB::is_parent_class(native_class, SNAME("Node"))) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::OBJECT;
		ERR_FAIL_MSG("SceneTree.call_group_as() requires a Node-derived class value.");
	}
	const StringName method_name = *p_args[1];
	const bool script_method = script.is_valid() && script->has_method(method_name);
	const MethodBind *method = script_method ? nullptr : ClassDB::get_method(native_class, method_name);
	if ((!script_method && (!method || method->is_static())) || (script_method && script->has_static_method(method_name))) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		ERR_FAIL_MSG(vformat("SceneTree.call_group_as(): instance method '%s' does not exist on the supplied class.", method_name));
	}
	const auto matches = [&](Node *p_node) {
		if (script.is_null()) {
			return p_node->is_class(native_class);
		}
		ScriptInstance *instance = p_node->get_script_instance();
		return instance && (instance->get_script() == script || instance->get_script()->inherits_script(script));
	};
	WGodotSceneTree::call_group_as(this, matches, [&](Node *p_node) {
		Callable::CallError error;
		// Native methods use the declared class's binding, as an ordinary typed
		// GDScript call does. Script methods retain script virtual dispatch.
		if (script_method) {
			p_node->get_script_instance()->callp(method_name, p_args + 2, p_argcount - 2, error);
		} else {
			method->call(p_node, p_args + 2, p_argcount - 2, error);
		}
		ERR_FAIL_COND_MSG(error.error != Callable::CallError::CALL_OK,
				"SceneTree.call_group_as(): " + Variant::get_call_error_text(method_name, p_args + 2, p_argcount - 2, error));
	});
}
