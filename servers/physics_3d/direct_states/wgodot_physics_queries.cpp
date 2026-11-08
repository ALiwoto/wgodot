// wgodot-changes::file
#include "physics_direct_space_state_3d.h"

Ref<PhysicsRayQueryResult3D> PhysicsDirectSpaceState3D::intersect_ray_typed(RequiredParam<PhysicsRayQueryParameters3D> p_ray_query) {
	EXTRACT_PARAM_OR_FAIL_V(ray_query, p_ray_query, Ref<PhysicsRayQueryResult3D>());
	PS3DT::RayResult hit;
	if (!intersect_ray(ray_query->get_parameters(), hit)) {
		return Ref<PhysicsRayQueryResult3D>();
	}
	Ref<PhysicsRayQueryResult3D> result;
	result.instantiate();
	result->result = hit;
	return result;
}

bool PhysicsDirectSpaceState3D::is_ray_obstructed(const Vector3 &p_from, const Vector3 &p_to, uint32_t p_collision_mask) {
	PS3DT::RayParameters parameters;
	parameters.from = p_from;
	parameters.to = p_to;
	parameters.collision_mask = p_collision_mask;
	parameters.hit_from_inside = true;
	PS3DT::RayResult result;
	return intersect_ray(parameters, result);
}
