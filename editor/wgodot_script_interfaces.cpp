// wgodot-changes::file
#include "wgodot_script_interfaces.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/wgodot_stdlib.h"
#endif

namespace WGodotEditorInterfaces {
static Ref<Script> resolve(const StringName &p_type, bool p_interface_only = false) {
	if (ClassDB::class_exists(p_type)) {
		return Ref<Script>();
	}
	String path;
#ifdef MODULE_GDSCRIPT_ENABLED
	if (WGodotGDScriptStdLib::has_global_interface(p_type)) {
		path = WGodotGDScriptStdLib::get_global_interface_path(p_type);
	} else
#endif
	if (ScriptServer::is_global_class(p_type)) {
		if (p_interface_only && !ScriptServer::is_global_class_abstract(p_type)) {
			return Ref<Script>();
		}
		path = ScriptServer::get_global_class_path(p_type);
	} else if (String(p_type).begins_with("res://") || String(p_type).begins_with("wgodot://")) {
		path = p_type;
	}
	return path.is_empty() ? Ref<Script>() : Ref<Script>(ResourceLoader::load(path));
}

bool accepts_object(const StringName &p_interface, const Object *p_object) {
	Ref<Script> contract = resolve(p_interface, true);
	return contract.is_valid() && contract->wgodot_is_interface_type() && contract->wgodot_is_instance_compatible(const_cast<Object *>(p_object));
}

bool accepts_type(const StringName &p_interface, const StringName &p_type) {
	Ref<Script> contract = resolve(p_interface, true);
	if (contract.is_null() || !contract->wgodot_is_interface_type()) {
		return false;
	}
	if (ClassDB::class_exists(p_type)) {
		return ClassDB::wgodot_class_implements_interface(p_type, contract->wgodot_get_interface_id());
	}
	return contract->wgodot_is_type_compatible(resolve(p_type));
}
}
