// wgodot-changes::file

#include "gdscript.h"

#include "core/object/class_db.h"
#include "gdscript_function.h"
#include "gdscript_analyzer.h"
#include "gdscript_cache.h"

void GDScript::_wgodot_strip_function_export_constants(GDScriptFunction *p_function) {
	if (p_function == nullptr) {
		return;
	}

	// The compiler only records local constants that are allowed to be stripped.
	// Constants marked @no_mangle are never added to this set.
	for (const StringName &constant_name : p_function->wgodot_declared_local_constants) {
		p_function->constant_map.erase(constant_name);
	}
	p_function->wgodot_declared_local_constants.clear();
	for (GDScriptFunction *lambda : p_function->lambdas) {
		_wgodot_strip_function_export_constants(lambda);
	}
}

void GDScript::_wgodot_strip_export_constants() {
	for (const StringName &constant_name : wgodot_declared_constants) {
		constants.erase(constant_name);
	}
	wgodot_declared_constants.clear();

	for (const KeyValue<StringName, GDScriptFunction *> &function : member_functions) {
		_wgodot_strip_function_export_constants(function.value);
	}
	_wgodot_strip_function_export_constants(initializer);
	_wgodot_strip_function_export_constants(implicit_initializer);
	_wgodot_strip_function_export_constants(implicit_ready);
	_wgodot_strip_function_export_constants(static_initializer);

	for (const KeyValue<StringName, Ref<GDScript>> &subclass : subclasses) {
		subclass.value->_wgodot_strip_export_constants();
	}
}

bool GDScript::wgodot_is_interface_type() const {
	return wgodot_is_interface;
}

String GDScript::wgodot_get_interface_id() const {
	return wgodot_interface_key;
}

const String &GDScript::wgodot_get_interface_key() const {
	return wgodot_interface_key;
}

bool GDScript::wgodot_implements_interface(const String &p_interface_key) const {
	if ((wgodot_is_interface && wgodot_interface_key == p_interface_key) || wgodot_implemented_interfaces.has(p_interface_key)) {
		return true;
	}

	return (base.is_valid() && base->wgodot_implements_interface(p_interface_key)) || ClassDB::wgodot_class_implements_interface(get_instance_base_type(), p_interface_key);
}

bool GDScript::wgodot_object_implements_interface(Object *p_object, const GDScript *p_interface_script) {
	return p_object != nullptr && p_interface_script != nullptr && p_interface_script->wgodot_is_interface_type() && p_interface_script->wgodot_is_instance_compatible(p_object);
}

void GDScript::wgodot_get_interface_ids(HashSet<String> &r_interfaces) const {
	if (wgodot_is_interface) {
		r_interfaces.insert(wgodot_interface_key);
	}
	for (const String &id : wgodot_implemented_interfaces) {
		r_interfaces.insert(id);
	}
	if (base.is_valid()) {
		base->wgodot_get_interface_ids(r_interfaces);
	}
}

Error GDScript::wgodot_validate_native_implementation(const StringName &p_class, String &r_error) const {
	Error error = OK;
	Ref<GDScriptParserRef> parser_ref = GDScriptCache::get_parser(path, GDScriptParserRef::INTERFACE_SOLVED, error);
	if (error != OK || parser_ref.is_null()) {
		r_error = vformat("Could not resolve interface '%s'.", path);
		return error != OK ? error : ERR_PARSE_ERROR;
	}
	return parser_ref->get_analyzer()->wgodot_validate_native_interface(fully_qualified_name, p_class, r_error);
}
