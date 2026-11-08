// wgodot-changes::file
#include "godot_physics_server_3d.h"
#include "godot_shape_3d.h"

#include "core/object/ref_counted.h"

// This unpublished shape has no RID or owners. Its only mutable state belongs
// to the loading job. Once returned, its arrays are immutable and can be shared
// with the live shape using Godot's existing copy-on-write storage.
class GodotPreparedConcaveShape3D : public RefCounted {
	GDCLASS(GodotPreparedConcaveShape3D, RefCounted);

protected:
	static void _bind_methods() {}

public:
	GodotConcavePolygonShape3D shape;
};

Variant GodotPhysicsServer3D::shape_prepare_data(PS3DE::ShapeType p_type, const Variant &p_data) const {
	if (p_type != PS3DE::SHAPE_CONCAVE_POLYGON || p_data.get_type() != Variant::DICTIONARY) {
		return p_data;
	}
	Ref<GodotPreparedConcaveShape3D> prepared;
	prepared.instantiate();
	prepared->shape.set_data(p_data);
	return prepared;
}

bool GodotConcavePolygonShape3D::_set_prepared_data(const Variant &p_data) {
	if (p_data.get_type() != Variant::OBJECT) {
		return false;
	}
	Ref<GodotPreparedConcaveShape3D> prepared = p_data;
	if (prepared.is_null()) {
		return false;
	}
	const GodotConcavePolygonShape3D &source = prepared->shape;
	faces = source.faces;
	vertices = source.vertices;
	bvh = source.bvh;
	backface_collision = source.backface_collision;
	configure(source.get_aabb());
	return true;
}
