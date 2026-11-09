// wgodot-changes::file
#include "gdscript.h"

#include "core/object/wgodot_interface_registry.h"

MethodInfo GDScript::wgodot_default_method_info(const StringName &p_method) const {
	const GDScriptDataType *provider = wgodot_default_methods.getptr(p_method);
	if (!provider) {
		return MethodInfo();
	}
	if (provider->script_type) {
		return provider->script_type->get_method_info(p_method);
	}
	const auto *contract = WGodotNativeInterfaces::get_descriptor(provider->native_type);
	return contract->methods[p_method];
}

Variant GDScript::wgodot_call_default(GDScriptInstance *p_instance, const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error, GDScript *p_class) const {
	const GDScriptDataType *provider = wgodot_default_methods.getptr(p_method);
	if (provider) {
		if (provider->script_type) {
			const auto *script = Object::cast_to<GDScript>(provider->script_type);
			const auto *method = script->member_functions.getptr(p_method);
			if (method) {
				return (*method)->call(p_instance, p_args, p_argcount, r_error);
			}
			return script->wgodot_call_default(p_instance, p_method, p_args, p_argcount, r_error, p_class);
		}
		const auto *contract = WGodotNativeInterfaces::get_descriptor(provider->native_type);
		if (contract->factory_methods.has(p_method)) {
			Object *receiver = p_class;
			if (!receiver) {
				receiver = p_instance ? p_instance->get_script().ptr() : const_cast<GDScript *>(this);
			}
			return contract->default_methods[p_method](receiver, p_args, p_argcount, r_error);
		}
		if (!p_instance) {
			r_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
			return Variant();
		}
		return contract->default_methods[p_method](p_instance->get_owner(), p_args, p_argcount, r_error);
	}
	r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}
