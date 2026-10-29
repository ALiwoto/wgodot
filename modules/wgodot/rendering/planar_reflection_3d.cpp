// wgodot-changes::file
#ifndef _3D_DISABLED
#include "planar_reflection_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "servers/rendering/rendering_server.h"

void PlanarReflection3D::_update_view() {
	Viewport *parent_view = get_viewport();
	Camera3D *source = parent_view->get_camera_3d();
	bool active = is_visible_in_tree() && source && (source->get_cull_mask() & visibility_layers);
	if (!active) {
		reflection_view->set_update_mode(SubViewport::UPDATE_DISABLED);
		return;
	}
	const Transform3D transform = get_global_transform();
	if (Math::is_zero_approx(transform.basis.determinant())) {
		reflection_view->set_update_mode(SubViewport::UPDATE_DISABLED);
		return;
	}
	const Vector3 normal = transform.basis.inverse().transposed().xform(Vector3(0, 1, 0)).normalized();
	const Plane plane(normal, normal.dot(transform.origin));
	if (active) {
		// Back-facing and off-screen planes do not submit a second scene render.
		active = plane.distance_to(source->get_camera_transform().origin) > 0.0;
		const AABB bounds = transform.xform(AABB(Vector3(-size.x * 0.5, 0, -size.y * 0.5), Vector3(size.x, 0, size.y)));
		const Vector<Plane> frustum = source->get_frustum();
		for (const Plane &face : frustum) {
			if (face.distance_to(bounds.get_support(-face.normal)) > 0.0) {
				active = false;
				break;
			}
		}
	}
	reflection_view->set_update_mode(active ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED);
	if (!active) {
		return;
	}
	const Size2 viewport_size = parent_view->get_visible_rect().size;
	reflection_view->set_size(Size2i(MAX(1, int(Math::round(viewport_size.x * resolution_scale))), MAX(1, int(Math::round(viewport_size.y * resolution_scale)))));
	reflection_view->set_world_3d(get_world_3d());
	if (reflection_view->get_mesh_lod_threshold() != parent_view->get_mesh_lod_threshold()) {
		reflection_view->set_mesh_lod_threshold(parent_view->get_mesh_lod_threshold());
	}
	if (reflection_view->get_positional_shadow_atlas_size() != parent_view->get_positional_shadow_atlas_size()) {
		reflection_view->set_positional_shadow_atlas_size(parent_view->get_positional_shadow_atlas_size());
	}
	if (reflection_view->get_positional_shadow_atlas_16_bits() != parent_view->get_positional_shadow_atlas_16_bits()) {
		reflection_view->set_positional_shadow_atlas_16_bits(parent_view->get_positional_shadow_atlas_16_bits());
	}
	for (int quadrant = 0; quadrant < 4; quadrant++) {
		const Viewport::PositionalShadowAtlasQuadrantSubdiv subdivision = parent_view->get_positional_shadow_atlas_quadrant_subdiv(quadrant);
		if (reflection_view->get_positional_shadow_atlas_quadrant_subdiv(quadrant) != subdivision) {
			reflection_view->set_positional_shadow_atlas_quadrant_subdiv(quadrant, subdivision);
		}
	}
	reflection_view->set_anisotropic_filtering_level(parent_view->get_anisotropic_filtering_level());
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->viewport_attach_camera(reflection_view->get_viewport_rid(), reflection_camera);
	rs->camera_set_cull_mask(reflection_camera, cull_mask);
	rs->camera_set_planar_reflection(reflection_camera, source->get_camera(), plane, render_particles, prefilter_roughness, roughness_distance, mesh_lod_scale);
}

void PlanarReflection3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (RenderingServer::get_singleton()->get_current_rendering_method() != "forward_plus") {
			return;
		}
		RenderingServer::get_singleton()->connect(SNAME("frame_pre_draw"), callable_mp(this, &PlanarReflection3D::_update_view));
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		if (RenderingServer::get_singleton()->is_connected(SNAME("frame_pre_draw"), callable_mp(this, &PlanarReflection3D::_update_view))) {
			RenderingServer::get_singleton()->disconnect(SNAME("frame_pre_draw"), callable_mp(this, &PlanarReflection3D::_update_view));
		}
		reflection_view->set_update_mode(SubViewport::UPDATE_DISABLED);
	}
}

void PlanarReflection3D::set_size(const Vector2 &p_size) {
	ERR_FAIL_COND(!p_size.is_finite() || p_size.x <= 0.0 || p_size.y <= 0.0);
	size = p_size;
}

void PlanarReflection3D::set_resolution_scale(float p_scale) {
	ERR_FAIL_COND(!Math::is_finite(p_scale) || p_scale <= 0.0f || p_scale > 1.0f);
	resolution_scale = p_scale;
}

void PlanarReflection3D::set_prefilter_roughness(float p_roughness) {
	ERR_FAIL_COND(!Math::is_finite(p_roughness) || p_roughness < 0.0f || p_roughness > 1.0f);
	prefilter_roughness = p_roughness;
}

void PlanarReflection3D::set_roughness_distance(float p_distance) {
	ERR_FAIL_COND(!Math::is_finite(p_distance) || p_distance <= 0.0f);
	roughness_distance = p_distance;
}

void PlanarReflection3D::set_mesh_lod_scale(float p_scale) {
	ERR_FAIL_COND(!Math::is_finite(p_scale) || p_scale < 0.0f);
	mesh_lod_scale = p_scale;
}

Ref<ViewportTexture> PlanarReflection3D::get_texture() const {
	return reflection_view->get_texture();
}

void PlanarReflection3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &PlanarReflection3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &PlanarReflection3D::get_size);
	ClassDB::bind_method(D_METHOD("set_resolution_scale", "scale"), &PlanarReflection3D::set_resolution_scale);
	ClassDB::bind_method(D_METHOD("get_resolution_scale"), &PlanarReflection3D::get_resolution_scale);
	ClassDB::bind_method(D_METHOD("set_prefilter_roughness", "roughness"), &PlanarReflection3D::set_prefilter_roughness);
	ClassDB::bind_method(D_METHOD("get_prefilter_roughness"), &PlanarReflection3D::get_prefilter_roughness);
	ClassDB::bind_method(D_METHOD("set_roughness_distance", "distance"), &PlanarReflection3D::set_roughness_distance);
	ClassDB::bind_method(D_METHOD("get_roughness_distance"), &PlanarReflection3D::get_roughness_distance);
	ClassDB::bind_method(D_METHOD("set_mesh_lod_scale", "scale"), &PlanarReflection3D::set_mesh_lod_scale);
	ClassDB::bind_method(D_METHOD("get_mesh_lod_scale"), &PlanarReflection3D::get_mesh_lod_scale);
	ClassDB::bind_method(D_METHOD("set_cull_mask", "mask"), &PlanarReflection3D::set_cull_mask);
	ClassDB::bind_method(D_METHOD("get_cull_mask"), &PlanarReflection3D::get_cull_mask);
	ClassDB::bind_method(D_METHOD("set_visibility_layers", "layers"), &PlanarReflection3D::set_visibility_layers);
	ClassDB::bind_method(D_METHOD("get_visibility_layers"), &PlanarReflection3D::get_visibility_layers);
	ClassDB::bind_method(D_METHOD("set_render_particles", "enabled"), &PlanarReflection3D::set_render_particles);
	ClassDB::bind_method(D_METHOD("is_rendering_particles"), &PlanarReflection3D::is_rendering_particles);
	ClassDB::bind_method(D_METHOD("get_texture"), &PlanarReflection3D::get_texture);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "resolution_scale", PROPERTY_HINT_RANGE, "0.01,1,0.01"), "set_resolution_scale", "get_resolution_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "prefilter_roughness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_prefilter_roughness", "get_prefilter_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness_distance", PROPERTY_HINT_RANGE, "0.001,1000,0.001,or_greater,suffix:m"), "set_roughness_distance", "get_roughness_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mesh_lod_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_mesh_lod_scale", "get_mesh_lod_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_cull_mask", "get_cull_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_visibility_layers", "get_visibility_layers");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "render_particles"), "set_render_particles", "is_rendering_particles");
}

PackedStringArray PlanarReflection3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (RenderingServer::get_singleton()->get_current_rendering_method() != "forward_plus") {
		warnings.push_back(RTR("PlanarReflection3D requires the Forward+ renderer."));
	}
	return warnings;
}

PlanarReflection3D::PlanarReflection3D() {
	reflection_view = memnew(SubViewport);
	reflection_view->set_name("ReflectionView");
	reflection_view->set_use_hdr_2d(true);
	reflection_view->set_transparent_background(true);
	reflection_view->set_update_mode(SubViewport::UPDATE_DISABLED);
	reflection_view->set_disable_input(true);
	add_child(reflection_view, false, INTERNAL_MODE_FRONT);
	reflection_camera = RenderingServer::get_singleton()->camera_create();
	RenderingServer::get_singleton()->viewport_attach_camera(reflection_view->get_viewport_rid(), reflection_camera);
}

PlanarReflection3D::~PlanarReflection3D() {
	RenderingServer::get_singleton()->free_rid(reflection_camera);
}

#endif // _3D_DISABLED
