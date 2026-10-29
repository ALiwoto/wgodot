// wgodot-changes::file
#pragma once

#include "scene/3d/node_3d.h"
#include "scene/main/viewport.h"

// One scene-linear, single-view reflection. Its SubViewport is ordered before
// the owning viewport, so consumers sample this frame without a CPU fence.
class PlanarReflection3D : public Node3D {
	GDCLASS(PlanarReflection3D, Node3D);

	SubViewport *reflection_view = nullptr;
	RID reflection_camera;
	Vector2 size = Vector2(40, 40);
	float resolution_scale = 0.5f;
	float prefilter_roughness = 0.0f;
	float roughness_distance = 100.0f;
	float mesh_lod_scale = 1.0f;
	uint32_t cull_mask = 0xFFFFF;
	uint32_t visibility_layers = 1;
	bool render_particles = true;

	void _update_view();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	void set_resolution_scale(float p_scale);
	float get_resolution_scale() const { return resolution_scale; }
	void set_prefilter_roughness(float p_roughness);
	float get_prefilter_roughness() const { return prefilter_roughness; }
	void set_roughness_distance(float p_distance);
	float get_roughness_distance() const { return roughness_distance; }
	void set_mesh_lod_scale(float p_scale);
	float get_mesh_lod_scale() const { return mesh_lod_scale; }
	void set_cull_mask(uint32_t p_mask) { cull_mask = p_mask; }
	uint32_t get_cull_mask() const { return cull_mask; }
	void set_visibility_layers(uint32_t p_layers) { visibility_layers = p_layers; }
	uint32_t get_visibility_layers() const { return visibility_layers; }
	void set_render_particles(bool p_enabled) { render_particles = p_enabled; }
	bool is_rendering_particles() const { return render_particles; }
	Ref<ViewportTexture> get_texture() const;

	PackedStringArray get_configuration_warnings() const override;
	PlanarReflection3D();
	~PlanarReflection3D();
};
