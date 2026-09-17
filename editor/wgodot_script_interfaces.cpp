// wgodot-changes::file
#include "wgodot_script_interfaces.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/wgodot_interface_registry.h"
#include "core/object/wgodot_native_interfaces.h"

namespace WGodotEditorInterfaces {
static Ref<Script> resolve_script(const StringName &p_type, bool p_interface_only = false) {
	if (ClassDB::class_exists(p_type) || WGodotNativeInterfaces::get_descriptor(p_type)) {
		return Ref<Script>();
	}
	String path;
	if (ScriptServer::is_global_class(p_type)) {
		if (p_interface_only && !ScriptServer::is_global_class_abstract(p_type)) {
			return Ref<Script>();
		}
		path = ScriptServer::get_global_class_path(p_type);
	} else if (String(p_type).begins_with("res://")) {
		path = p_type;
	}
	return path.is_empty() ? Ref<Script>() : Ref<Script>(ResourceLoader::load(path));
}

bool accepts_object(const StringName &p_interface, const Object *p_object) {
	if (WGodotNativeInterfaces::get_descriptor(p_interface)) {
		return !p_object || WGodotNativeInterfaces::is_instance(const_cast<Object *>(p_object), p_interface);
	}
	Ref<Script> contract = resolve_script(p_interface, true);
	return contract.is_valid() && contract->wgodot_is_interface_type() && contract->wgodot_is_instance_compatible(const_cast<Object *>(p_object));
}

bool accepts_type(const StringName &p_interface, const StringName &p_type) {
	if (WGodotNativeInterfaces::get_descriptor(p_interface)) {
		if (WGodotNativeInterfaces::can_reference(p_type, p_interface)) {
			return true;
		}
		const Ref<Script> script = resolve_script(p_type);
		return script.is_valid() && script->wgodot_implements_interface(p_interface);
	}
	Ref<Script> contract = resolve_script(p_interface, true);
	if (contract.is_null() || !contract->wgodot_is_interface_type()) {
		return false;
	}
	if (ClassDB::class_exists(p_type)) {
		return ClassDB::wgodot_class_implements_interface(p_type, contract->wgodot_get_interface_id());
	}
	return contract->wgodot_is_type_compatible(resolve_script(p_type));
}
} //namespace WGodotEditorInterfaces
