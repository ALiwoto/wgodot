// wgodot-changes::file
#include "packed_scene.h"

#include "core/variant/variant_internal.h"

namespace {

struct SceneValueComparator {
	template <typename T, int Components, typename Scalar = real_t>
	static bool compare_numeric(const Variant &p_left, const Variant &p_right) {
		// These math types contain only scalar components, with no padding.
		// Ordinary Variant equality merges signed zeros and different NaN payloads.
		static_assert(sizeof(T) == Components * sizeof(Scalar));
		const T &left = VariantInternalAccessor<T>::get(&p_left);
		const T &right = VariantInternalAccessor<T>::get(&p_right);
		return memcmp(&left, &right, sizeof(T)) == 0;
	}

	static bool compare(const Variant &p_left, const Variant &p_right) {
		if (p_left.get_type() != p_right.get_type()) {
			return false;
		}
		switch (p_left.get_type()) {
			case Variant::FLOAT:
				return compare_numeric<double, 1, double>(p_left, p_right);
			case Variant::VECTOR2:
				return compare_numeric<Vector2, 2>(p_left, p_right);
			case Variant::RECT2:
				return compare_numeric<Rect2, 4>(p_left, p_right);
			case Variant::VECTOR3:
				return compare_numeric<Vector3, 3>(p_left, p_right);
			case Variant::TRANSFORM2D:
				return compare_numeric<Transform2D, 6>(p_left, p_right);
			case Variant::VECTOR4:
				return compare_numeric<Vector4, 4>(p_left, p_right);
			case Variant::PLANE:
				return compare_numeric<Plane, 4>(p_left, p_right);
			case Variant::QUATERNION:
				return compare_numeric<Quaternion, 4>(p_left, p_right);
			case Variant::AABB:
				return compare_numeric<AABB, 6>(p_left, p_right);
			case Variant::BASIS:
				return compare_numeric<Basis, 9>(p_left, p_right);
			case Variant::TRANSFORM3D:
				return compare_numeric<Transform3D, 12>(p_left, p_right);
			case Variant::PROJECTION:
				return compare_numeric<Projection, 16>(p_left, p_right);
			case Variant::COLOR:
				return compare_numeric<Color, 4, float>(p_left, p_right);
			default:
				return p_left.hash_compare(p_right);
		}
	}
};

struct ScenePathKey {
	NodePath path;
	PackedInt32Array recovery_ids;

	bool operator==(const ScenePathKey &p_other) const {
		return path == p_other.path && recovery_ids == p_other.recovery_ids;
	}

	uint32_t hash() const {
		uint32_t result = path.hash();
		for (int32_t id : recovery_ids) {
			result = hash_murmur3_one_32(id, result);
		}
		return hash_fmix32(result);
	}
};

} // namespace

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
	HashMap<Variant, int, HashMapHasherDefault, SceneValueComparator> immutable_values;
	Vector<int> value_indices;
	value_indices.resize(variants.size());
	value_indices.fill(-1);
	auto value_index = [&](int p_index) {
		int &index = value_indices.write[p_index];
		if (index < 0) {
			const Variant &value = variants[p_index];
			// NIL through NODE_PATH are value types. Handles, objects, callables,
			// signals and all containers retain their original slot identity.
			if (value.get_type() <= Variant::NODE_PATH) {
				if (const int *existing = immutable_values.getptr(value)) {
					index = *existing;
					return index;
				}
				immutable_values.insert(value, compact_values.size());
			}
			index = compact_values.size();
			compact_values.push_back(value);
		}
		return index;
	};
	Vector<NodePath> compact_paths;
	Vector<PackedInt32Array> compact_id_paths;
	HashMap<ScenePathKey, int> path_indices;
	auto node_reference = [&](int p_reference) {
		if (p_reference < 0 || p_reference == NO_PARENT_SAVED || !(p_reference & FLAG_ID_IS_PATH)) {
			return p_reference;
		}
		const int old_index = p_reference & FLAG_MASK;
		const ScenePathKey key = { node_paths[old_index], old_index < id_paths.size() ? id_paths[old_index] : PackedInt32Array() };
		int index;
		if (const int *existing = path_indices.getptr(key)) {
			index = *existing;
		} else {
			index = compact_paths.size();
			compact_paths.push_back(key.path);
			compact_id_paths.push_back(key.recovery_ids);
			path_indices.insert(key, index);
		}
		return (p_reference & ~FLAG_MASK) | index;
	};
	// Preserve value identity, node order, path/instance flags and callbacks.
	// Equal mutable resources/containers are never merged by value.
	bool needs_node_ids = base_scene_idx >= 0;
	if (base_scene_idx >= 0 && native_construction.is_null()) {
		base_scene_idx = value_index(base_scene_idx);
	}
	for (NodeData &node : nodes) {
		// Existing instance nodes can acquire IDs from their base scene during
		// instantiation. An explicit zero on an instance root can also reset its ID.
		needs_node_ids |= node.type == TYPE_INSTANTIATED || node.instance >= 0;
		node.parent = node_reference(node.parent);
		node.owner = node_reference(node.owner);
		node.name = name_index(node.name);
		if (node.type != TYPE_INSTANTIATED) {
			node.type = name_index(node.type);
		}
		if (node.instance >= 0 && native_construction.is_null()) {
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
		connection.from = node_reference(connection.from);
		connection.to = node_reference(connection.to);
		connection.signal = name_index(connection.signal);
		connection.method = name_index(connection.method);
		for (int &bind : connection.binds) {
			bind = value_index(bind);
		}
	}
	for (int32_t id : ids) {
		needs_node_ids |= id != Node::UNIQUE_SCENE_ID_UNASSIGNED;
	}
	// Missing trailing recovery paths have the same meaning as empty paths.
	// Retain every nonempty recovery path and its index, including internal gaps.
	int recovery_path_count = compact_id_paths.size();
	while (recovery_path_count > 0 && compact_id_paths[recovery_path_count - 1].is_empty()) {
		recovery_path_count--;
	}
	compact_id_paths.resize(recovery_path_count);
	if (!needs_node_ids && compact_id_paths.is_empty()) {
		ids.clear();
	}
	names = std::move(compact_names);
	variants = std::move(compact_values);
	node_paths = std::move(compact_paths);
	id_paths = std::move(compact_id_paths);
}

void SceneState::set_native_node(int p_node, Node *(*p_constructor)()) {
	ERR_FAIL_INDEX(p_node, nodes.size());
	nodes.write[p_node].native_constructor = p_constructor;
}

void SceneState::set_native_construction(const Ref<WGodotSceneConstruction> &p_construction) {
	native_construction = p_construction;
}

void SceneState::duplicate_native_storage(const Resource::NativeCopyContext &p_context) {
	native_construction = native_construction->duplicate(p_context);
	for (NativeInstance &instance : native_instances) {
		instance.scene = p_context.resource(instance.scene);
	}
}

int SceneState::add_native_instance(const Ref<PackedScene> &p_scene, const String &p_placeholder) {
	native_instances.push_back({ p_scene, p_placeholder });
	return native_instances.size() - 1;
}

void SceneState::set_native_base_scene(int p_index) {
	ERR_FAIL_INDEX(p_index, native_instances.size());
	base_scene_idx = p_index;
}

Ref<PackedScene> SceneState::instance_scene(int p_index) const {
	if (native_construction.is_valid()) {
		return native_instances[p_index].scene;
	}
	return variants[p_index];
}

String SceneState::instance_placeholder(int p_index) const {
	if (native_construction.is_valid()) {
		return native_instances[p_index].placeholder;
	}
	return variants[p_index];
}

void PackedScene::_wgodot_native_copy(Resource *p_copy, const DuplicateParams &p_params) const {
	Resource::_wgodot_native_copy(p_copy, p_params);
	if (state->has_native_construction()) {
		const Ref<SceneState> copy = static_cast<PackedScene *>(p_copy)->state;
		copy->copy_from(state);
		copy->duplicate_native_storage(NativeCopyContext(*this, p_params));
	}
}

bool PackedScene::_wgodot_native_storage(const StringName &p_property) const {
	return state->has_native_construction() && p_property == SNAME("_bundled");
}
