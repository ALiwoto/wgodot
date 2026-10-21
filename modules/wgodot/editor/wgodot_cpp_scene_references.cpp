// wgodot-changes::file
#include "wgodot_cpp_scene_references.h"

NodePath WGodotCppSceneReferences::key(const Vector<StringName> &p_names) {
	return p_names.is_empty() ? NodePath(".") : NodePath(p_names, false);
}

void WGodotCppSceneReferences::prepare(const Ref<SceneState> &p_state) {
	indices.clear();
	paths.resize(p_state->get_node_count());
	for (int i = 0; i < paths.size(); i++) {
		Vector<StringName> names;
		const NodePath path = p_state->get_node_path(i);
		for (int j = 0; j < path.get_name_count(); j++) {
			if (path.get_name(j) != SNAME(".")) {
				names.push_back(path.get_name(j));
			}
		}
		paths.write[i] = names;
		indices.insert(key(names), i);
	}
}

int WGodotCppSceneReferences::find(int p_from, const NodePath &p_path) const {
	if (p_path.is_empty()) {
		return NULL_NODE;
	}
	if (p_path.is_absolute()) {
		return RUNTIME_PATH;
	}
	Vector<StringName> names = paths[p_from];
	int index = p_from;
	for (int i = 0; i < p_path.get_name_count(); i++) {
		const StringName name = p_path.get_name(i);
		if (name == SNAME(".")) {
			continue;
		}
		if (name == SNAME("..")) {
			if (names.is_empty()) {
				return RUNTIME_PATH;
			}
			names.resize(names.size() - 1);
		} else if (name.is_node_unique_name()) {
			return RUNTIME_PATH; // Unique-name lookup depends on the instance owner.
		} else {
			names.push_back(name);
		}
		const int *found = indices.getptr(key(names));
		if (!found) {
			// Includes children supplied only by a base/instanced scene. Their local
			// indices are not positions in this SceneState's instantiation table.
			return RUNTIME_PATH;
		}
		index = *found;
	}
	return index;
}
