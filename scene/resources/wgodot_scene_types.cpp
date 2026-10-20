// wgodot-changes::file
#include "wgodot_scene_types.h"

namespace {
String path_text(const NodePath &p_path) {
	String result;
	for (int i = 0; i < p_path.get_name_count(); i++) {
		if (i) {
			result += "/";
		}
		result += p_path.get_name(i);
	}
	return result;
}
}

WGodotSceneTypes::Type WGodotSceneTypes::resolve(const Ref<SceneState> &p_state, const NodePath &p_path) {
	const NodePath path = p_path.simplified();
	Type result;
	int closest = -1;
	String prefix;
	const String text = path_text(path);
	for (int i = 0; i < p_state->get_node_count(); i++) {
		const NodePath candidate = p_state->get_node_path(i).simplified();
		if (candidate == path) {
			result.native = p_state->get_node_type(i);
			for (int j = 0; j < p_state->get_node_property_count(i); j++) {
				if (p_state->get_node_property_name(i, j) == SNAME("script")) {
					result.script = p_state->get_node_property_value(i, j);
					break;
				}
			}
			if (!result.native.is_empty()) {
				return result;
			}
		}
		if (p_state->get_node_instance(i).is_valid()) {
			const String candidate_text = path_text(candidate);
			if ((candidate_text == "." || candidate_text == text || text.begins_with(candidate_text + "/")) &&
					(closest < 0 || candidate_text.length() > prefix.length())) {
				closest = i;
				prefix = candidate_text;
			}
		}
	}
	Type inherited;
	if (closest >= 0) {
		const String relative = prefix == "." ? text : prefix == text ? "." : text.substr(prefix.length() + 1);
		inherited = resolve(p_state->get_node_instance(closest)->get_state(), NodePath(relative));
	} else if (p_state->get_base_scene_state().is_valid()) {
		inherited = resolve(p_state->get_base_scene_state(), path);
	}
	result.native = inherited.native;
	if (result.script.is_null()) {
		result.script = inherited.script;
	}
	return result;
}

String WGodotSceneTypes::validate(const Ref<SceneState> &p_state) {
	for (int i = 0; i < p_state->get_node_count(); i++) {
		for (int j = 0; j < p_state->get_node_property_count(i); j++) {
			if (p_state->get_node_property_name(i, j) != SNAME("script")) {
				continue;
			}
			const Ref<Script> script = p_state->get_node_property_value(i, j);
			if (script.is_null()) {
				return "Script removal is not a scene type. Declare the intended class_name on the node's type attribute.";
			}
			const Type type = resolve(p_state, p_state->get_node_path(i));
			if (script->get_global_name().is_empty() || type.native != script->get_instance_base_type()) {
				return "Node '" + path_text(p_state->get_node_path(i)) + "' requires a class_name extending its exact native type '" + String(type.native) + "'.";
			}
		}
	}
	return String();
}

void WGodotSceneTypes::InheritedTypes::add(const String &p_path, const Ref<SceneState> &p_state) {
	instances.insert(p_path, p_state);
}

WGodotSceneTypes::Type WGodotSceneTypes::InheritedTypes::resolve(const String &p_path) const {
	if (instances.is_empty()) {
		return Type();
	}
	String ancestor = p_path;
	while (true) {
		if (const Ref<SceneState> *state = instances.getptr(ancestor)) {
			const String relative = ancestor == p_path ? String(".") : ancestor == "." ? p_path : p_path.substr(ancestor.length() + 1);
			return WGodotSceneTypes::resolve(*state, NodePath(relative));
		}
		if (ancestor == ".") {
			return Type();
		}
		const int slash = ancestor.rfind("/");
		ancestor = slash >= 0 ? ancestor.substr(0, slash) : String(".");
	}
}
