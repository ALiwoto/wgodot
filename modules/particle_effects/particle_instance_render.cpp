// wgodot-changes::file
#include "particle_instance_3d.h"
#include "particle_instance_pool_3d.h"
#include "servers/rendering/rendering_server.h"

void ParticleInstance3D::collect_render_instances(Node *p_node) {
	VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	if (visual) {
		render_instances.push_back(visual);
	}
	for (int i = 0; i < p_node->get_child_count(); ++i) {
		collect_render_instances(p_node->get_child(i));
	}
}

void ParticleInstance3D::prepare_render_instances(ParticleInstancePool3D *p_pool) {
	instance_pool = p_pool;
	collect_render_instances(this);
}

Transform3D ParticleInstance3D::get_emitter_transform() const {
	if (instance_pool) {
		return instance_pool->get_global_transform();
	}
	// World-space particles become top-level, but retain their emitter parent.
	// get_parent_node_3d() deliberately hides that parent for top-level nodes.
	Node3D *emitter = Object::cast_to<Node3D>(get_parent());
	ERR_FAIL_NULL_V(emitter, Transform3D());
	return emitter->get_global_transform();
}

Transform3D ParticleInstance3D::get_render_transform(Node3D *p_node) const {
	if (!instance_pool) {
		return p_node->get_global_transform();
	}
	Transform3D result;
	const Node *current = p_node;
	while (current) {
		const Node3D *transform = Object::cast_to<Node3D>(current);
		if (transform) {
			result = transform->get_transform() * result;
			if (transform->is_set_as_top_level()) {
				return result;
			}
		}
		if (current == this) {
			return instance_pool->get_global_transform() * result;
		}
		current = current->get_parent();
	}
	ERR_FAIL_V_MSG(Transform3D(), "Render transform requires this particle or one of its children.");
}

Transform3D ParticleInstance3D::get_particle_transform() const {
	return get_render_transform(const_cast<ParticleInstance3D *>(this));
}

bool ParticleInstance3D::is_render_visible(Node3D *p_node) const {
	if (!instance_pool) {
		return p_node->is_visible_in_tree();
	}
	const Node *current = p_node;
	while (current) {
		const Node3D *transform = Object::cast_to<Node3D>(current);
		if (transform && !transform->is_visible()) {
			return false;
		}
		if (current == this) {
			return instance_pool->is_visible_in_tree();
		}
		current = current->get_parent();
	}
	return false;
}

void ParticleInstance3D::set_render_scenario(RID p_scenario) {
	for (VisualInstance3D *visual : render_instances) {
		RenderingServer::get_singleton()->instance_set_scenario(visual->get_instance(), p_scenario);
	}
}

void ParticleInstance3D::publish_render_instances() {
	if (!instance_pool) {
		return;
	}
	for (VisualInstance3D *visual : render_instances) {
		const bool visible = is_render_visible(visual);
		if (visible) {
			RenderingServer::get_singleton()->instance_set_transform(visual->get_instance(), get_render_transform(visual));
		}
		RenderingServer::get_singleton()->instance_set_visible(visual->get_instance(), visible);
	}
}

void ParticleInstance3D::set_render_shader_parameter(const StringName &p_name, const Variant &p_value) {
	for (VisualInstance3D *visual : render_instances) {
		GeometryInstance3D *render_geometry = Object::cast_to<GeometryInstance3D>(visual);
		if (render_geometry) {
			render_geometry->set_instance_shader_parameter(p_name, p_value);
		}
	}
}
