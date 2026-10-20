// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_resource_identity.h"

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
	if (!project.get_target().includes(path)) {
		diagnostics.push_back(project.get_target().dependency_error(path));
		return 0;
	}
	unit_resource_paths.insert(path);
	const auto &catalog = prepared_resource_ids ? *prepared_resource_ids : resource_ids;
	if (const int64_t *id = catalog.getptr(path)) {
		return *id;
	}
	if (prepared_resource_ids) {
		diagnostics.push_back("Resource dependency was not prepared before parallel generation: " + path);
		return 0;
	}
	const int64_t id = WGodotResourceIdentity::from_path(path);
	const String *owner = resource_id_owners.getptr(id);
	if (id == 0 || (owner && *owner != path)) {
		diagnostics.push_back("Native resource identity collision: " + path + " and " + (owner ? *owner : String("reserved null identity")));
		return 0;
	}
	resource_id_owners.insert(id, path);
	resource_ids.insert(path, id);
	return id;
}

WGodotCppEmitter::Value WGodotCppEmitter::resource_path_constant(const Variant &p_value) {
	Value result("int64_t(" + itos(resource_id(p_value)) + ")", "int64_t");
	result.effects = false;
	result.invariant = true;
	return result;
}
