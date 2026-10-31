// wgodot-changes::file
#include "particle_instance_3d.h"
#include "particle_instance_pool_3d.h"
#include "servers/rendering/rendering_server.h"

void ParticleInstance3D::collect_render_instances(Node *p_node) {
	VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	if (visual) {
		RenderInstance instance;
		instance.visual = visual;
		RenderingServer::get_singleton()->instance_geometry_set_flag(visual->get_instance(), RSE::INSTANCE_FLAG_PARTICLE_EFFECT, true);
		instance.geometry = Object::cast_to<GeometryInstance3D>(visual);
		if (instance.geometry) {
			instance.authored_aabb = instance.geometry->get_custom_aabb();
		}
		render_instances.push_back(instance);
	}
	for (int i = 0; i < p_node->get_child_count(); ++i) {
		collect_render_instances(p_node->get_child(i));
	}
}

void ParticleInstance3D::prepare_render_instances(ParticleInstancePool3D *p_pool) {
	instance_pool = p_pool;
	collect_render_instances(this);
	render_instances_prepared = true;
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

AABB ParticleInstance3D::get_simulation_aabb() const {
	// Simulation-space bounds deliberately exclude camera-facing rotations and
	// camera offsets. Capture while advancing the simulation, before attachments
	// can move again for rendering. These are distinct from render culling bounds.
	const Transform3D emitter = get_emitter_transform();
	const Vector3 center = follows_emitter || sprite_motion.is_valid() ? get_particle_transform().xform(state_position) : state_position;
	const Vector3 scaled_size = (state_bounds_size * emitter.basis.get_scale()).abs();
	Vector3 extent;
	if (sprite_motion.is_valid()) {
		const Vector2 pivot = sprite_motion->get_pivot_offset();
		const Vector3 pivot_extent = (state_bounds_size * Vector3(pivot.x - 0.5, 0, pivot.y - 0.5)).abs();
		const Vector3 base_extent = sprite_motion->get_orbit() ? state_orbit_offset.abs() : scaled_size;
		const real_t radius = base_extent[base_extent.max_axis_index()] + pivot_extent[pivot_extent.max_axis_index()];
		extent = Vector3(radius, radius, radius);
	} else {
		const MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(geometry);
		if (mesh && mesh->get_mesh().is_valid()) {
			extent = mesh->get_mesh()->get_aabb().size * 0.5 * scaled_size;
		}
	}
	return AABB(center - extent, extent * 2);
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
	for (const RenderInstance &instance : render_instances) {
		RenderingServer::get_singleton()->instance_set_scenario(instance.visual->get_instance(), p_scenario);
	}
}

void ParticleInstance3D::set_sorting_priority(int32_t p_priority) {
	for (const RenderInstance &instance : render_instances) {
		if (instance.geometry) {
			instance.geometry->set_sorting_priority(p_priority);
		}
	}
}

void ParticleInstance3D::update_render_bounds(RenderInstance &p_instance, const Transform3D &p_transform) {
	GeometryInstance3D *render_geometry = p_instance.geometry;
	if (!render_geometry) {
		return;
	}
	const bool fixed = visibility_aabb != AABB();
	const bool sprite = render_geometry == geometry && sprite_motion.is_valid();
	const bool facing_mesh = render_geometry == geometry && motion.is_valid() && motion->get_faces_camera();
	real_t camera_distance = render_geometry == geometry ? Math::abs(state_camera_offset) : 0.0;
	if (render_geometry == geometry && motion.is_valid() && follows_emitter && camera_distance != 0.0) {
		// Mesh particles apply camera offsets in emitter space before facing.
		// Bound its world displacement even under nonuniform scale or shear.
		const Basis emitter = get_emitter_transform().basis;
		real_t squared_scale = 0.0;
		for (int axis = 0; axis < 3; ++axis) {
			squared_scale += emitter.get_column(axis).length_squared();
		}
		camera_distance *= Math::sqrt(squared_scale);
	}
	if (!fixed && !sprite && !facing_mesh && camera_distance == 0.0) {
		if (p_instance.bounds_overridden) {
			render_geometry->set_custom_aabb(p_instance.authored_aabb);
			p_instance.bounds_overridden = false;
		}
		return;
	}
	if (!p_instance.bounds_overridden) {
		p_instance.authored_aabb = render_geometry->get_custom_aabb();
	}
	// The renderer itself rejects singular instance transforms. They have no
	// inverse; the next non-singular pose will publish its bounds normally.
	if (p_transform.basis.determinant() == 0.0) {
		return;
	}
	const Transform3D inverse = p_transform.affine_inverse();
	AABB bounds;
	if (fixed) {
		bounds = (inverse * get_emitter_transform()).xform(visibility_aabb);
	} else {
		const AABB shape = p_instance.authored_aabb != AABB() ? p_instance.authored_aabb : render_geometry->get_aabb();
		if (sprite || facing_mesh) {
			Vector3 corner = shape.get_center().abs() + shape.size * 0.5;
			real_t radius;
			if (sprite) {
				const Vector2 pivot = sprite_motion->get_pivot_offset();
				const Vector3 center = shape.get_center() + Vector3(pivot.x, -pivot.y, 0.0);
				corner = center.abs() + shape.size * 0.5;
				const Vector3 scale = p_transform.basis.get_scale_abs();
				// Sprite profiles use X/Z dimensions; square alignment uses X
				// for both axes. A sphere covers all camera/velocity rotations.
				corner.x *= Math::abs(state_render_size.x) * scale.x;
				corner.y *= Math::abs(state_render_size.y) * (sprite_motion->get_square() ? scale.x : scale.z);
				corner.z *= scale.z;
				radius = corner.length();
			} else {
				// Match the shader's signed particle scale. Camera-facing mesh
				// size excludes the emitter basis, including scales below one.
				// The norm also bounds every orthonormal mesh pre-rotation.
				radius = corner.length() * stretch->get_scale().length();
			}
			radius += camera_distance;
			const Vector3 extent(radius, radius, radius);
			// Cancel translation analytically: world movement must not cause
			// rounding jitter and redundant renderer updates to a local box.
			bounds = Transform3D(inverse.basis, Vector3()).xform(AABB(-extent, extent * 2.0));
		} else {
			bounds = inverse.xform(p_transform.xform(shape).grow(camera_distance));
		}
	}
	render_geometry->set_custom_aabb(bounds);
	p_instance.bounds_overridden = true;
}

void ParticleInstance3D::publish_render_instances() {
	// Non-pooled particles stay in the tree, but shader-expanded geometry
	// needs the same culling bounds as prepared instances.
	if (!render_instances_prepared) {
		collect_render_instances(this);
		render_instances_prepared = true;
	}
	const bool emitter_culling = motion.is_valid() && motion->get_emitter_culling();
	GeometryInstance3D::CullTransform cull_transform = GeometryInstance3D::CULL_TRANSFORM_AUTOMATIC;
	if (emitter_culling) {
		// Batched particle winding belongs to the emitter; individual signed
		// scales may turn a mesh inside out without reversing its raster state.
		cull_transform = get_emitter_transform().basis.determinant() < 0 ? GeometryInstance3D::CULL_TRANSFORM_NEGATIVE : GeometryInstance3D::CULL_TRANSFORM_POSITIVE;
	}
	for (int i = 0; i < render_instances.size(); ++i) {
		RenderInstance &instance = render_instances.write[i];
		VisualInstance3D *visual = instance.visual;
		const bool visible = is_render_visible(visual);
		if (visible) {
			if (instance.geometry) {
				if (emitter_culling) {
					if (!instance.culling_overridden) {
						instance.authored_cull_transform = instance.geometry->get_cull_transform();
						instance.culling_overridden = true;
					}
					instance.geometry->set_cull_transform(cull_transform);
				} else if (instance.culling_overridden) {
					instance.geometry->set_cull_transform(instance.authored_cull_transform);
					instance.culling_overridden = false;
				}
			}
			const Transform3D transform = get_render_transform(visual);
			update_render_bounds(instance, transform);
			if (instance_pool) {
				RenderingServer::get_singleton()->instance_set_transform(visual->get_instance(), transform);
			}
		}
		if (instance_pool) {
			RenderingServer::get_singleton()->instance_set_visible(visual->get_instance(), visible);
		}
	}
}

void ParticleInstance3D::set_render_shader_parameter(const StringName &p_name, const Variant &p_value) {
	for (const RenderInstance &instance : render_instances) {
		if (instance.geometry) {
			instance.geometry->set_instance_shader_parameter(p_name, p_value);
		}
	}
}
