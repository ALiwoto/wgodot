// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "servers/rendering/mesh_lod.h"

class MeshLODProfile : public Resource {
	GDCLASS(MeshLODProfile, Resource);
	MeshLODData data;
	Vector<int32_t> surface_override_sources;

protected:
	static void _bind_methods();

public:
	void set_surface_override_sources(const Vector<int32_t> &p_sources);
	Vector<int32_t> get_surface_override_sources() const { return surface_override_sources; }
	const Vector<int32_t> &get_override_sources() const { return surface_override_sources; }
	bool is_valid(int p_surface_count) const {
		return data.is_valid(p_surface_count) && (surface_override_sources.is_empty() || surface_override_sources.size() == p_surface_count);
	}
	void set_surface_levels(const Vector<int32_t> &p_levels);
	Vector<int32_t> get_surface_levels() const { return data.surface_levels; }
	void set_thresholds(const Vector<Vector2> &p_thresholds);
	Vector<Vector2> get_thresholds() const { return data.thresholds; }
	void set_center(const Vector3 &p_center);
	Vector3 get_center() const { return data.center; }
	void set_radius(float p_radius);
	float get_radius() const { return data.radius; }
	void set_minimum_level(int p_level);
	int get_minimum_level() const { return data.minimum_level; }
	const MeshLODData &get_data() const { return data; }
	Ref<MeshLODProfile> scaled(const Vector3 &p_scale) const;
};
