// wgodot-changes::file
#include "packed_scene.h"

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
