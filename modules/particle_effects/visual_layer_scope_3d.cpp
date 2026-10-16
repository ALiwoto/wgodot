// wgodot-changes::file
#include "visual_layer_scope_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "particle_instance_pool_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/reflection_probe.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

void VisualLayerScope3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin", "subject", "layers", "light_mask"), &VisualLayerScope3D::begin);
	ClassDB::bind_method(D_METHOD("end"), &VisualLayerScope3D::end);
}

void VisualLayerScope3D::include_visual(VisualInstance3D *p_visual, Node *p_owner, bool p_subject) {
	if (!p_subject) {
		if (!Object::cast_to<Light3D>(p_visual) && !Object::cast_to<ReflectionProbe>(p_visual)) {
			return;
		}
		if (!(p_visual->get_layer_mask() & light_mask)) {
			return;
		}
	}
	const uint32_t mask = p_visual->get_layer_mask();
	owners[p_owner].push_back({ p_visual, mask & layers });
	p_visual->set_layer_mask(mask | layers);
}

void VisualLayerScope3D::include_private(Node *p_node, Node *p_owner, bool p_subject) {
	VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	if (visual) {
		include_visual(visual, p_owner, p_subject);
	}
	for (int i = 0; i < p_node->get_child_count(); ++i) {
		include_private(p_node->get_child(i), p_owner, p_subject);
	}
}

void VisualLayerScope3D::include_node(Node *p_node, bool p_subject) {
	if (owners.has(p_node)) {
		return;
	}
	VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	if (visual) {
		include_visual(visual, p_node, p_subject);
	}
	ParticleInstancePool3D *pool = Object::cast_to<ParticleInstancePool3D>(p_node);
	if (pool) {
		const TypedArray<ParticleInstance3D> instances = pool->get_instances();
		for (int i = 0; i < instances.size(); ++i) {
			include_private(Object::cast_to<ParticleInstance3D>(instances[i]), pool, p_subject);
		}
	}
}

void VisualLayerScope3D::include_branch(Node *p_node, bool p_subject) {
	p_subject = p_subject || p_node == subject;
	include_node(p_node, p_subject);
	for (int i = 0; i < p_node->get_child_count(); ++i) {
		include_branch(p_node->get_child(i), p_subject);
	}
}

void VisualLayerScope3D::node_added(Node *p_node) {
	include_node(p_node, p_node == subject || subject->is_ancestor_of(p_node));
}

void VisualLayerScope3D::restore(Node *p_owner) {
	const Vector<VisualState> *states = owners.getptr(p_owner);
	if (!states) {
		return;
	}
	for (const VisualState &state : *states) {
		state.visual->set_layer_mask((state.visual->get_layer_mask() & ~layers) | state.previous_bits);
	}
	owners.erase(p_owner);
}

void VisualLayerScope3D::node_removed(Node *p_node) {
	if (p_node == subject) {
		end();
	} else {
		restore(p_node);
	}
}

void VisualLayerScope3D::begin(Node3D *p_subject, uint32_t p_layers, uint32_t p_light_mask) {
	ERR_FAIL_COND(!is_inside_tree());
	ERR_FAIL_NULL(p_subject);
	ERR_FAIL_COND(p_subject->get_tree() != get_tree());
	ERR_FAIL_COND(p_layers == 0 || (p_layers & ~((1u << 20) - 1)) != 0);
	end();
	subject = p_subject;
	layers = p_layers;
	light_mask = p_light_mask;
	active_tree = get_tree();
	include_branch(active_tree->get_root(), false);
	active_tree->connect(SNAME("node_added"), callable_mp(this, &VisualLayerScope3D::node_added));
	active_tree->connect(SNAME("node_removed"), callable_mp(this, &VisualLayerScope3D::node_removed));
}

void VisualLayerScope3D::end() {
	if (!active_tree) {
		return;
	}
	active_tree->disconnect(SNAME("node_added"), callable_mp(this, &VisualLayerScope3D::node_added));
	active_tree->disconnect(SNAME("node_removed"), callable_mp(this, &VisualLayerScope3D::node_removed));
	while (!owners.is_empty()) {
		restore(owners.begin()->key);
	}
	subject = nullptr;
	active_tree = nullptr;
}

void VisualLayerScope3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		end();
	}
}
