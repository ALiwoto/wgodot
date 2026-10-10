// wgodot-changes::file
#include "wgodot_export_scene.h"

#include "wgodot_cpp_project.h"
#include "wgodot_native_resource_policy.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"

namespace {
String join_node_path(const String &p_base, const NodePath &p_path) {
	const String path = p_base.path_join(String(p_path.get_concatenated_names())).simplify_path();
	return path.is_empty() ? String(".") : path;
}

struct SceneNode {
	StringName type;
	String owner;
	HashMap<StringName, Variant> properties;
};

class SceneFilter {
	const WGodotCppProject *project;
	const WGodotExportTarget &target;
	Vector<String> &diagnostics;
	String source;
	HashMap<String, SceneNode> nodes;
	HashSet<const SceneState *> visiting;
	Vector<String> excluded;
	HashSet<String> excluded_paths;

	bool editor_member(const SceneNode &p_node, const StringName &p_name) const {
		if (String(p_name).begins_with("metadata/_edit_")) {
			return true;
		}
		const Variant *value = p_node.properties.getptr(SNAME("script"));
		const Ref<Script> script = value ? *value : Variant();
		GDScriptParser *parser = project && script.is_valid() ? project->find_parser(script->get_path()) : nullptr;
		return parser && project->is_editor_member(parser->get_tree(), p_name);
	}

	void error(const String &p_message) {
		diagnostics.push_back(source + ": " + p_message);
	}

	void gather(const Ref<SceneState> &p_state, const String &p_prefix) {
		if (visiting.has(p_state.ptr())) {
			error("Cyclic scene inheritance/instancing.");
			return;
		}
		visiting.insert(p_state.ptr());
		if (p_state->get_base_scene_state().is_valid()) {
			gather(p_state->get_base_scene_state(), p_prefix);
		}
		for (int i = 0; i < p_state->get_node_count(); i++) {
			const String path = join_node_path(p_prefix, p_state->get_node_path(i));
			const Ref<PackedScene> instance = p_state->get_node_instance(i);
			if (instance.is_valid()) {
				gather(instance->get_state(), path);
			}
			SceneNode &node = nodes[path];
			const NodePath owner = p_state->get_node_owner_path(i);
			if (!owner.is_empty()) {
				node.owner = join_node_path(p_prefix, owner);
			}
			if (!p_state->get_node_type(i).is_empty()) {
				node.type = p_state->get_node_type(i);
			}
			for (int j = 0; j < p_state->get_node_property_count(i); j++) {
				const StringName name = p_state->get_node_property_name(i, j);
				const Variant value = p_state->get_node_property_value(i, j);
				if (name == WGodotExportTarget::NODE_PROPERTY) {
					if (value.get_type() != Variant::STRING || !WGodotExportTarget::valid_owner(value)) {
						error(path + ": wgodot_target must be shared, client, server or editor.");
					} else if (p_state->get_node_type(i).is_empty() && instance.is_null()) {
						error(path + ": declare wgodot_target in the scene that creates this node, not an inherited override.");
					}
				}
				node.properties.insert(name, value);
			}
		}
		visiting.erase(p_state.ptr());
	}

	bool removed(const String &p_path) const {
		if (excluded_paths.is_empty()) {
			return false;
		}
		if (excluded_paths.has(".")) {
			return true;
		}
		String path = p_path;
		while (!path.is_empty()) {
			if (excluded_paths.has(path)) {
				return true;
			}
			const int separator = path.rfind("/");
			if (separator < 0) {
				break;
			}
			path = path.substr(0, separator);
		}
		return false;
	}

	String resolve(const String &p_base, const NodePath &p_path) const {
		if (p_path.is_absolute() || p_path.is_empty()) {
			return String(); // Runtime/global paths have no authored local target.
		}
		String current = p_base;
		for (int i = 0; i < p_path.get_name_count(); i++) {
			const String name = p_path.get_name(i);
			if (!name.begins_with("%")) {
				current = join_node_path(current, NodePath(name));
				continue;
			}
			const SceneNode *base = nodes.getptr(current);
			String resolved;
			// Godot first checks unique nodes owned by the current node, then
			// those owned by its scene owner. Instance scopes remain separate.
			for (int scope = 0; scope < 2 && resolved.is_empty(); scope++) {
				const String owner = scope == 0 ? current : base ? base->owner
																 : String();
				if (owner.is_empty()) {
					continue;
				}
				for (const KeyValue<String, SceneNode> &node : nodes) {
					const Variant *unique = node.value.properties.getptr(SNAME("unique_name_in_owner"));
					if (node.value.owner == owner && unique && bool(*unique) && node.key.get_file() == name.substr(1)) {
						resolved = node.key;
						break;
					}
				}
			}
			if (resolved.is_empty()) {
				return String();
			}
			current = resolved;
		}
		return current;
	}

	void check_path(const String &p_base, const NodePath &p_path, const String &p_context) {
		const String resolved = resolve(p_base, p_path);
		if (!resolved.is_empty() && removed(resolved)) {
			error(p_context + " references excluded node " + resolved + " for " + target.get_name() + ".");
		}
	}

	void check_value(const Variant &p_value, const String &p_base, const String &p_context, HashSet<const Resource *> &r_seen) {
		if (p_value.get_type() == Variant::NODE_PATH) {
			check_path(p_base, p_value, p_context);
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array array = p_value;
			for (const Variant &value : array) {
				check_value(value, p_base, p_context, r_seen);
			}
		} else if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary dictionary = p_value;
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				check_value(entry.key, p_base, p_context, r_seen);
				check_value(entry.value, p_base, p_context, r_seen);
			}
		} else if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (resource.is_null() || r_seen.has(resource.ptr())) {
				return;
			}
			r_seen.insert(resource.ptr());
			if (!target.includes(resource->get_path())) {
				error(p_context + ": " + target.dependency_error(resource->get_path()));
				return;
			}
			if (Object::cast_to<PackedScene>(resource.ptr()) || Object::cast_to<Script>(resource.ptr())) {
				return; // Independent scene/script analysis owns these contents.
			}
			List<PropertyInfo> properties;
			WGodotNativeResourcePolicy::get_reference_properties(resource, properties);
			const Ref<Script> script = resource->get_script();
			GDScriptParser *parser = project && script.is_valid() ? project->find_parser(script->get_path()) : nullptr;
			for (const PropertyInfo &property : properties) {
				if (parser && project->is_editor_member(parser->get_tree(), property.name)) {
					continue;
				}
				if (property.usage & PROPERTY_USAGE_STORAGE) {
					check_value(resource->get(property.name), p_base, p_context + "/" + String(property.name), r_seen);
				}
			}
		}
	}

	int node_path(const Ref<SceneState> &p_output, const NodePath &p_path, const PackedInt32Array &p_ids) {
		return p_path.is_empty() ? -1 : p_output->add_node_path(p_path, p_ids);
	}

public:
	SceneFilter(const WGodotCppProject *p_project, const WGodotExportTarget &p_target, Vector<String> &r_diagnostics, const String &p_source) : project(p_project), target(p_target), diagnostics(r_diagnostics), source(p_source) {}

	Ref<SceneState> run(const Ref<SceneState> &p_state) {
		gather(p_state, ".");
		for (const KeyValue<String, SceneNode> &node : nodes) {
			const Variant *owner = node.value.properties.getptr(WGodotExportTarget::NODE_PROPERTY);
			if (owner && owner->get_type() == Variant::STRING && WGodotExportTarget::valid_owner(*owner) && !target.includes_owner(*owner)) {
				excluded.push_back(node.key);
			}
		}
		for (const String &path : excluded) {
			excluded_paths.insert(path);
		}
		if (removed(".")) {
			error("Scene root is excluded. Assign whole-scene ownership in wgodot_targets.cfg.");
		}
		for (const KeyValue<String, SceneNode> &node : nodes) {
			if (removed(node.key)) {
				continue;
			}
			for (const KeyValue<StringName, Variant> &property : node.value.properties) {
				if (editor_member(node.value, property.key)) {
					continue;
				}
				HashSet<const Resource *> seen;
				String base = node.key;
				// Animation track paths are relative to the mixer's root_node, not
				// the AnimationLibrary or the node storing the resource property.
				if (property.key == SNAME("libraries") && ClassDB::is_parent_class(node.value.type, SNAME("AnimationMixer"))) {
					const Variant *root = node.value.properties.getptr(SNAME("root_node"));
					const NodePath root_path = root ? *root : ClassDB::class_get_default_property_value(node.value.type, SNAME("root_node"));
					base = resolve(base, root_path);
				}
				check_value(property.value, base, node.key + ":" + String(property.key), seen);
			}
		}
		Ref<SceneState> output;
		output.instantiate();
		output->set_path(p_state->get_path());
		const Dictionary bundled = p_state->get_bundled_scene();
		if (bundled.has("base_scene")) {
			const Array variants = bundled["variants"];
			const Variant base = variants[int(bundled["base_scene"])];
			const Ref<PackedScene> base_scene = base;
			if (base_scene.is_valid() && !target.includes(base_scene->get_path())) {
				error(target.dependency_error(base_scene->get_path()));
			}
			output->set_base_scene(output->add_value(base));
		}
		for (int i = 0; i < p_state->get_node_count(); i++) {
			const String path = join_node_path(".", p_state->get_node_path(i));
			if (removed(path)) {
				continue;
			}
			const NodePath parent = p_state->get_node_path(i, true);
			const NodePath owner = p_state->get_node_owner_path(i);
			check_path(".", owner, path + " owner");
			int instance = -1;
			if (p_state->is_node_instance_placeholder(i)) {
				const String placeholder = p_state->get_node_instance_placeholder(i);
				if (!target.includes(placeholder)) {
					error(target.dependency_error(placeholder));
				}
				instance = output->add_value(placeholder) | SceneState::FLAG_INSTANCE_IS_PLACEHOLDER;
			} else if (p_state->get_node_instance(i).is_valid()) {
				const Ref<PackedScene> scene = p_state->get_node_instance(i);
				if (!target.includes(scene->get_path())) {
					error(target.dependency_error(scene->get_path()));
				}
				instance = output->add_value(scene);
			}
			const StringName type = p_state->get_node_type(i);
			const int index = output->add_node(node_path(output, parent, p_state->get_node_parent_id_path(i)), node_path(output, owner, p_state->get_node_owner_id_path(i)), type.is_empty() ? SceneState::TYPE_INSTANTIATED : output->add_name(type), output->add_name(p_state->get_node_name(i)), instance, p_state->get_node_index(i), p_state->get_node_unique_id(i));
			const Vector<String> deferred = p_state->get_node_deferred_nodepath_properties(i);
			for (int j = 0; j < p_state->get_node_property_count(i); j++) {
				const StringName name = p_state->get_node_property_name(i, j);
				if (name != WGodotExportTarget::NODE_PROPERTY && !editor_member(nodes[path], name)) {
					output->add_node_property(index, output->add_name(name), output->add_value(p_state->get_node_property_value(i, j)), deferred.has(name));
				}
			}
			for (const StringName &group : p_state->get_node_groups(i)) {
				output->add_node_group(index, output->add_name(group));
			}
		}
		for (int i = 0; i < p_state->get_connection_count(); i++) {
			const NodePath from = p_state->get_connection_source(i);
			const NodePath to = p_state->get_connection_target(i);
			const bool removed_from = removed(join_node_path(".", from));
			const bool removed_to = removed(join_node_path(".", to));
			if (removed_from || removed_to) {
				if (removed_from != removed_to) {
					error("Signal connection crosses an excluded subtree: " + String(from) + " -> " + String(to));
				}
				continue;
			}
			if (editor_member(nodes[join_node_path(".", from)], p_state->get_connection_signal(i)) || editor_member(nodes[join_node_path(".", to)], p_state->get_connection_method(i))) {
				error("Runtime signal connection references an @editor_only member: " + String(from) + " -> " + String(to));
				continue;
			}
			Vector<int> binds;
			HashSet<const Resource *> seen;
			for (const Variant &bind : p_state->get_connection_binds(i)) {
				check_value(bind, String(from), "signal bind", seen);
				binds.push_back(output->add_value(bind));
			}
			output->add_connection(node_path(output, from, p_state->get_connection_source_id_path(i)), node_path(output, to, p_state->get_connection_target_id_path(i)), output->add_name(p_state->get_connection_signal(i)), output->add_name(p_state->get_connection_method(i)), p_state->get_connection_flags(i), p_state->get_connection_unbinds(i), binds);
		}
		// Editable-instance paths are editor metadata; runtime factories don't need them.
		return output;
	}
};
} // namespace

Ref<SceneState> WGodotExportScene::filter(const Ref<PackedScene> &p_scene) {
	if (const Ref<SceneState> *state = states.getptr(p_scene.ptr())) {
		return *state;
	}
	SceneFilter filter(&project, project.get_target(), diagnostics, p_scene->get_path());
	const Ref<SceneState> state = filter.run(p_scene->get_state());
	states.insert(p_scene.ptr(), state);
	return state;
}

Ref<SceneState> WGodotExportScene::filter_external(const Ref<PackedScene> &p_scene, const WGodotExportTarget &p_target, Vector<String> &r_diagnostics) {
	SceneFilter filter(nullptr, p_target, r_diagnostics, p_scene->get_path());
	return filter.run(p_scene->get_state());
}
