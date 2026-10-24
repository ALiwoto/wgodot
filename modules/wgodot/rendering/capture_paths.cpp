// wgodot-changes::file
#include "capture_paths.h"
#ifdef TOOLS_ENABLED
#include "core/object/class_db.h"

void WCapturePaths::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin", "root"), &WCapturePaths::begin);
	ClassDB::bind_method(D_METHOD("finish"), &WCapturePaths::finish);
	ClassDB::bind_method(D_METHOD("get_path", "root", "node"), &WCapturePaths::get_path);
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	GDVIRTUAL_BIND(_particle_pool_name, "pool");
#endif
}

void WCapturePaths::begin(Node *p_root) {
	ERR_FAIL_NULL(p_root);
	finish();
	cache_root = p_root->get_instance_id();
}

void WCapturePaths::finish() {
	cache_root = ObjectID();
	paths.clear();
	pool_names.clear();
}

String WCapturePaths::get_path(Node *p_root, Node *p_node) {
	ERR_FAIL_NULL_V(p_root, String());
	ERR_FAIL_NULL_V(p_node, String());
	if (cache_root != p_root->get_instance_id()) {
		return build(p_root, p_node);
	}
	const ObjectID id = p_node->get_instance_id();
	if (const String *existing = paths.getptr(id)) {
		return *existing;
	}
	String path = build(p_root, p_node);
	paths.insert(id, path);
	return path;
}

String WCapturePaths::build(Node *p_root, Node *p_node) {
	Vector<String> parts;
	String prefix;
	Node *current = p_node;
	while (current && current != p_root) {
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
		if (Object::cast_to<ParticleInstancePool3D>(current)) {
			current = current->get_parent();
			continue;
		}
#endif
		if (cache_root == p_root->get_instance_id()) {
			if (const String *existing = paths.getptr(current->get_instance_id())) {
				prefix = *existing;
				break;
			}
		}
		if (current->is_inside_tree()) {
			// Cache ancestry within this snapshot, not just the requested leaf.
			// get_path_to() allocates a visited set and walks both ancestries on
			// every call; siblings here can share the already resolved prefix.
			Vector<Node *> ancestors;
			Node *ancestor = current;
			const bool caching = cache_root == p_root->get_instance_id();
			while (ancestor && ancestor != p_root) {
				bool cacheable = caching;
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
				// Direct pool requests omit the storage container. An in-tree
				// descendant still has its ordinary Godot path through it.
				cacheable = cacheable && !Object::cast_to<ParticleInstancePool3D>(ancestor);
#endif
				if (cacheable) {
					if (const String *existing = paths.getptr(ancestor->get_instance_id())) {
						prefix = *existing;
						break;
					}
				}
				ancestors.push_back(ancestor);
				ancestor = ancestor->get_parent();
			}
			if (ancestor == p_root) {
				prefix = ".";
			} else if (!ancestor) {
				prefix = "world:";
			}
			for (int i = ancestors.size() - 1; i >= 0; i--) {
				Node *part_node = ancestors[i];
				const String name = part_node->get_name();
				prefix = prefix == "." ? name : prefix + (prefix == "world:" ? "" : "/") + name;
				bool cacheable = caching;
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
				cacheable = cacheable && !Object::cast_to<ParticleInstancePool3D>(part_node);
#endif
				if (cacheable) {
					paths.insert(part_node->get_instance_id(), prefix);
				}
			}
			break;
		}
		String part = current->get_name();
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(current);
		if (particle && particle->get_instance_pool()) {
			ParticleInstancePool3D *pool = particle->get_instance_pool();
			String name;
			const String *cached = cache_root.is_valid() ? pool_names.getptr(pool->get_instance_id()) : nullptr;
			if (cached) {
				name = *cached;
			} else {
				GDVIRTUAL_CALL(_particle_pool_name, pool, name);
				if (cache_root.is_valid()) {
					pool_names.insert(pool->get_instance_id(), name);
				}
			}
			if (!name.is_empty()) {
				part = name + "Particle" + itos(particle->get_spawn_index());
			}
			parts.push_back(part);
			current = pool->get_parent();
			continue;
		}
#endif
		parts.push_back(part);
		current = current->get_parent();
	}
	parts.reverse();
	String suffix = String("/").join(parts);
	if (!prefix.is_empty()) {
		if (parts.is_empty()) {
			return prefix;
		}
		return prefix == "." ? suffix : prefix + "/" + suffix;
	}
	if (current == p_root) {
		return parts.is_empty() ? String(".") : suffix;
	}
	return "world:" + (parts.is_empty() ? String(".") : suffix);
}
#endif
