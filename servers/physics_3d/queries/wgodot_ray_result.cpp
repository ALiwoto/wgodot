// wgodot-changes::file
#include "wgodot_ray_result.h"
#include "core/object/class_db.h"

void PhysicsRayQueryResult3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_position"), &PhysicsRayQueryResult3D::get_position);
	ClassDB::bind_method(D_METHOD("get_normal"), &PhysicsRayQueryResult3D::get_normal);
	ClassDB::bind_method(D_METHOD("get_rid"), &PhysicsRayQueryResult3D::get_rid);
	ClassDB::bind_method(D_METHOD("get_collider"), &PhysicsRayQueryResult3D::get_collider);
	ClassDB::bind_method(D_METHOD("get_collider_id"), &PhysicsRayQueryResult3D::get_collider_id);
	ClassDB::bind_method(D_METHOD("get_shape"), &PhysicsRayQueryResult3D::get_shape);
	ClassDB::bind_method(D_METHOD("get_face_index"), &PhysicsRayQueryResult3D::get_face_index);
}
