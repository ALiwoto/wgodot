// wgodot-changes::file
#include "wgodot_scene_declarations.h"

#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/resource_importer.h"
#include "core/object/script_language.h"

namespace {
Error collect(const Ref<SceneState> &p_state, const String &p_prefix, Dictionary &r_nodes, HashSet<const SceneState *> &r_visiting) {
	ERR_FAIL_COND_V(r_visiting.has(p_state.ptr()), ERR_CYCLIC_LINK);
	r_visiting.insert(p_state.ptr());
	if (p_state->get_base_scene_state().is_valid()) {
		RETURN_IF_ERROR(collect(p_state->get_base_scene_state(), p_prefix, r_nodes, r_visiting));
	}
	for (int i = 0; i < p_state->get_node_count(); i++) {
		const String relative = String(p_state->get_node_path(i));
		const String path = relative == "." ? p_prefix : p_prefix == "." ? relative : p_prefix.path_join(relative);
		const Ref<PackedScene> instance = p_state->get_node_instance(i);
		if (instance.is_valid()) {
			RETURN_IF_ERROR(collect(instance->get_state(), path, r_nodes, r_visiting));
		}
		StringName type = p_state->get_node_type(i);
		for (int property = 0; property < p_state->get_node_property_count(i); property++) {
			if (p_state->get_node_property_name(i, property) == SNAME("script")) {
				const Ref<Script> script = p_state->get_node_property_resource(i, property);
				if (script.is_valid() && script->get_global_name() != StringName()) {
					type = script->get_global_name();
				}
			}
		}
		if (type != StringName()) {
			r_nodes[path] = type;
		}
	}
	r_visiting.erase(p_state.ptr());
	return OK;
}
}

String WGodotSceneDeclarations::path_for_source(const String &p_source) {
	return ResourceFormatImporter::get_singleton()->get_import_base_path(p_source) + ".nodes";
}

Error WGodotSceneDeclarations::save(const Ref<SceneState> &p_state, const String &p_path) {
	Dictionary nodes;
	HashSet<const SceneState *> visiting;
	RETURN_IF_ERROR(collect(p_state, ".", nodes, visiting));
	Error error = OK;
	const String temporary = p_path + ".tmp";
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &error);
	if (file.is_null()) {
		return error;
	}
	file->store_var(nodes, false);
	file->flush();
	error = file->get_error();
	file.unref();
	return error == OK ? DirAccess::rename_absolute(temporary, p_path) : error;
}

Error WGodotSceneDeclarations::read(const String &p_path, HashMap<String, StringName> &r_nodes) {
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
	if (file.is_null()) {
		return error;
	}
	const Variant value = file->get_var(false);
	if (value.get_type() != Variant::DICTIONARY) {
		return ERR_FILE_CORRUPT;
	}
	const Dictionary nodes = value;
	for (const auto &node : nodes) {
		if (node.key.get_type() != Variant::STRING || node.value.get_type() != Variant::STRING_NAME) {
			return ERR_FILE_CORRUPT;
		}
		// SceneState spells child paths as ./Child; text scenes use Child.
		const String path = node.key;
		r_nodes.insert(path == "." ? path : path.simplify_path(), node.value);
	}
	return r_nodes.has(".") ? OK : ERR_FILE_CORRUPT;
}
