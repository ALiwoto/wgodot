// wgodot-changes::file
#include "physics_direct_space_state_3d.h"

bool PhysicsDirectSpaceState3D::is_ray_obstructed(const Vector3 &p_from, const Vector3 &p_to, uint32_t p_collision_mask) {
	PS3DT::RayParameters parameters;
	parameters.from = p_from;
	parameters.to = p_to;
	parameters.collision_mask = p_collision_mask;
	parameters.hit_from_inside = true;
	PS3DT::RayResult result;
	return intersect_ray(parameters, result);
}
