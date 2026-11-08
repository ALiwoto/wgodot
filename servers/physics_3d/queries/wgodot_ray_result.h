// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"
#include "servers/physics_3d/physics_server_3d_types.h"

// A typed query snapshot. ObjectID is resolved on access, so retaining a result
// never leaves a dangling collider pointer after the body is freed.
class PhysicsRayQueryResult3D : public RefCounted {
	GDCLASS(PhysicsRayQueryResult3D, RefCounted);
	friend class PhysicsDirectSpaceState3D;
	PS3DT::RayResult result;

protected:
	static void _bind_methods();

public:
	Vector3 get_position() const { return result.position; }
	Vector3 get_normal() const { return result.normal; }
	RID get_rid() const { return result.rid; }
	Object *get_collider() const { return ObjectDB::get_instance(result.collider_id); }
	uint64_t get_collider_id() const { return result.collider_id; }
	int get_shape() const { return result.shape; }
	int get_face_index() const { return result.face_index; }
};
