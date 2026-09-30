// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "core/config/project_settings.h"
#include "core/io/resource_uid.h"

int64_t WGodotCppEmitter::resource_id(const String &p_path) {
	if (p_path.is_empty()) {
		return 0;
	}
	String path = ResourceUID::ensure_path(p_path);
	if (path.is_relative_path()) {
		path = "res://" + path;
	}
	path = ProjectSettings::get_singleton()->localize_path(path).simplify_path();
	if (!path.begins_with("res://")) {
		diagnostics.push_back("WResPath must identify a packaged project resource: " + p_path);
		return 0;
	}
	if (const int64_t *id = resource_ids.getptr(path)) {
		return *id;
	}
	const int64_t id = resource_ids.size() + 1;
	resource_ids.insert(path, id);
	return id;
}

WGodotCppEmitter::Value WGodotCppEmitter::resource_path_constant(const Variant &p_value) {
	Value result("int64_t(" + itos(resource_id(p_value)) + ")", "int64_t");
	result.effects = false;
	result.invariant = true;
	return result;
}
