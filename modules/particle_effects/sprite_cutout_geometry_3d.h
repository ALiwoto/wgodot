// wgodot-changes::file
#pragma once

#include "scene/resources/mesh.h"

// One authored atlas: consecutive convex polygons in texture coordinates.
class SpriteCutoutGeometry3D : public Resource {
	GDCLASS(SpriteCutoutGeometry3D, Resource);

	int vertices_per_frame = 4;
	Vector<Vector2> points;

protected:
	static void _bind_methods();

public:
	void set_vertices_per_frame(int p_count);
	int get_vertices_per_frame() const { return vertices_per_frame; }
	void set_points(const Vector<Vector2> &p_points);
	Vector<Vector2> get_points() const { return points; }
	const Vector<Vector2> &get_point_data() const { return points; }
};

// Ordinary Mesh references work in particle profiles and mesh animation tracks.
// Only the source geometry and frame index are serialized, never GPU buffers.
class SpriteCutoutMesh3D : public ArrayMesh {
	GDCLASS(SpriteCutoutMesh3D, ArrayMesh);

	Ref<SpriteCutoutGeometry3D> geometry;
	int frame = -1;
	void _rebuild();

protected:
	static void _bind_methods();
	bool _is_generated() const override { return true; }
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_geometry(const Ref<SpriteCutoutGeometry3D> &p_geometry);
	Ref<SpriteCutoutGeometry3D> get_geometry() const { return geometry; }
	void set_frame(int p_frame);
	int get_frame() const { return frame; }
	~SpriteCutoutMesh3D();
};
