// wgodot-changes::file
/**************************************************************************/
/*  wgodot_project_info.cpp                                               */
/**************************************************************************/

#include "wgodot_project_info.h"

#include "core/config/project_settings.h"
#include "core/core_string_names.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/script_language.h"
#include "scene/resources/packed_scene.h"

namespace WGodotProjectInfo {

namespace {

bool find_root_script_path(const Ref<PackedScene> &p_scene, int p_depth, String &r_script_path) {
	if (p_scene.is_null() || p_depth > 32) {
		return false;
	}
	const Ref<SceneState> state = p_scene->get_state();
	if (state.is_null() || state->get_node_count() == 0) {
		return false;
	}

	const int property_count = state->get_node_property_count(0);
	for (int property_index = 0; property_index < property_count; property_index++) {
		if (state->get_node_property_name(0, property_index) != CoreStringName(script)) {
			continue;
		}
		const Ref<Script> script = state->get_node_property_value(0, property_index);
		r_script_path = script.is_valid() ? script->get_path() : String();
		return true;
	}

	return find_root_script_path(state->get_node_instance(0), p_depth + 1, r_script_path);
}

} // namespace

void add_status_fields(Dictionary &r_response) {
	const String configured_scene = GLOBAL_GET("application/run/main_scene");
	const String main_scene = configured_scene.is_empty() ? String() : ResourceUID::ensure_path(configured_scene);
	String attached_script;
	if (!main_scene.is_empty()) {
		const Ref<PackedScene> scene = ResourceLoader::load(main_scene, "PackedScene");
		find_root_script_path(scene, 0, attached_script);
	}
	r_response["main_scene"] = main_scene;
	r_response["main_scene_script"] = attached_script;
}

} // namespace WGodotProjectInfo
