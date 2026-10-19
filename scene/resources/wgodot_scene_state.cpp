// wgodot-changes::file
#include "packed_scene.h"

void SceneState::compact_storage() {
	Vector<StringName> compact_names;
	HashMap<StringName, int> name_indices;
	auto name_index = [&](int p_index) {
		const StringName &name = names[p_index];
		if (const int *existing = name_indices.getptr(name)) {
			return *existing;
		}
		const int index = compact_names.size();
		compact_names.push_back(name);
		name_indices.insert(name, index);
		return index;
	};
	Vector<Variant> compact_values;
	Vector<int> value_indices;
	value_indices.resize(variants.size());
	value_indices.fill(-1);
	auto value_index = [&](int p_index) {
		int &index = value_indices.write[p_index];
		if (index < 0) {
			index = compact_values.size();
			compact_values.push_back(variants[p_index]);
		}
		return index;
	};
	// Preserve value identity, node order, path/instance flags and callbacks.
	// Equal mutable resources/containers are never merged by value.
	if (base_scene_idx >= 0) {
		base_scene_idx = value_index(base_scene_idx);
	}
	for (NodeData &node : nodes) {
		node.name = name_index(node.name);
		if (node.type != TYPE_INSTANTIATED) {
			node.type = name_index(node.type);
		}
		if (node.instance >= 0) {
			node.instance = (node.instance & ~FLAG_MASK) | value_index(node.instance & FLAG_MASK);
		}
		for (NodeData::Property &property : node.properties) {
			property.name = (property.name & ~FLAG_PROP_NAME_MASK) | name_index(property.name & FLAG_PROP_NAME_MASK);
			property.value = value_index(property.value);
		}
		for (int &group : node.groups) {
			group = name_index(group);
		}
	}
	for (ConnectionData &connection : connections) {
		connection.signal = name_index(connection.signal);
		connection.method = name_index(connection.method);
		for (int &bind : connection.binds) {
			bind = value_index(bind);
		}
	}
	names = std::move(compact_names);
	variants = std::move(compact_values);
}

void SceneState::set_native_node(int p_node, Node *(*p_constructor)()) {
	ERR_FAIL_INDEX(p_node, nodes.size());
	nodes.write[p_node].native_constructor = p_constructor;
}

void SceneState::set_native_property(int p_node, int p_property, const WGodotSceneProperty *p_accessor) {
	ERR_FAIL_INDEX(p_node, nodes.size());
	ERR_FAIL_INDEX(p_property, nodes[p_node].properties.size());
	nodes.write[p_node].properties.write[p_property].native = p_accessor;
}

void SceneState::set_native_connection(int p_connection, const WGodotSceneConnection *p_connection_data) {
	ERR_FAIL_INDEX(p_connection, connections.size());
	connections.write[p_connection].native = p_connection_data;
}

void PackedScene::_wgodot_native_copy(Resource *p_copy, const DuplicateParams &p_params) const {
	Resource::_wgodot_native_copy(p_copy, p_params);
	static_cast<PackedScene *>(p_copy)->state->copy_native_accessors_from(state);
}

void SceneState::copy_native_accessors_from(const Ref<SceneState> &p_source) {
	ERR_FAIL_COND(p_source.is_null());
	ERR_FAIL_COND(nodes.size() != p_source->nodes.size());
	ERR_FAIL_COND(connections.size() != p_source->connections.size());
	for (int i = 0; i < nodes.size(); i++) {
		NodeData &node = nodes.write[i];
		const NodeData &source = p_source->nodes[i];
		ERR_FAIL_COND(node.properties.size() != source.properties.size());
		node.native_constructor = source.native_constructor;
		for (int j = 0; j < node.properties.size(); j++) {
			node.properties.write[j].native = source.properties[j].native;
		}
	}
	for (int i = 0; i < connections.size(); i++) {
		connections.write[i].native = p_source->connections[i].native;
	}
}
