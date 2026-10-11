// wgodot-changes::file
#include "kawaii_bone_chain.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void KawaiiBoneChain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_root_bone", "value"), &KawaiiBoneChain::set_root_bone);
	ClassDB::bind_method(D_METHOD("get_root_bone"), &KawaiiBoneChain::get_root_bone);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "root_bone"), "set_root_bone", "get_root_bone");

	ClassDB::bind_method(D_METHOD("set_excluded_bones", "value"), &KawaiiBoneChain::set_excluded_bones);
	ClassDB::bind_method(D_METHOD("get_excluded_bones"), &KawaiiBoneChain::get_excluded_bones);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "excluded_bones"), "set_excluded_bones", "get_excluded_bones");

	ClassDB::bind_method(D_METHOD("set_damping", "value"), &KawaiiBoneChain::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &KawaiiBoneChain::get_damping);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_damping", "get_damping");

	ClassDB::bind_method(D_METHOD("set_world_damping_translation", "value"), &KawaiiBoneChain::set_world_damping_translation);
	ClassDB::bind_method(D_METHOD("get_world_damping_translation"), &KawaiiBoneChain::get_world_damping_translation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "world_damping_translation", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_world_damping_translation", "get_world_damping_translation");

	ClassDB::bind_method(D_METHOD("set_world_damping_rotation", "value"), &KawaiiBoneChain::set_world_damping_rotation);
	ClassDB::bind_method(D_METHOD("get_world_damping_rotation"), &KawaiiBoneChain::get_world_damping_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "world_damping_rotation", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_world_damping_rotation", "get_world_damping_rotation");

	ClassDB::bind_method(D_METHOD("set_stiffness", "value"), &KawaiiBoneChain::set_stiffness);
	ClassDB::bind_method(D_METHOD("get_stiffness"), &KawaiiBoneChain::get_stiffness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stiffness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_stiffness", "get_stiffness");

	ClassDB::bind_method(D_METHOD("set_radius", "value"), &KawaiiBoneChain::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &KawaiiBoneChain::get_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_radius", "get_radius");

	ClassDB::bind_method(D_METHOD("set_limit_angle", "value"), &KawaiiBoneChain::set_limit_angle);
	ClassDB::bind_method(D_METHOD("get_limit_angle"), &KawaiiBoneChain::get_limit_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "limit_angle", PROPERTY_HINT_RANGE, "0,180,0.1,suffix:deg"), "set_limit_angle", "get_limit_angle");

	ClassDB::bind_method(D_METHOD("set_target_framerate", "value"), &KawaiiBoneChain::set_target_framerate);
	ClassDB::bind_method(D_METHOD("get_target_framerate"), &KawaiiBoneChain::get_target_framerate);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "target_framerate", PROPERTY_HINT_RANGE, "1,240,1,or_greater"), "set_target_framerate", "get_target_framerate");

	ClassDB::bind_method(D_METHOD("set_teleport_distance", "value"), &KawaiiBoneChain::set_teleport_distance);
	ClassDB::bind_method(D_METHOD("get_teleport_distance"), &KawaiiBoneChain::get_teleport_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "teleport_distance", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_teleport_distance", "get_teleport_distance");

	ClassDB::bind_method(D_METHOD("set_teleport_rotation", "value"), &KawaiiBoneChain::set_teleport_rotation);
	ClassDB::bind_method(D_METHOD("get_teleport_rotation"), &KawaiiBoneChain::get_teleport_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "teleport_rotation", PROPERTY_HINT_RANGE, "-1,360,0.1,suffix:deg"), "set_teleport_rotation", "get_teleport_rotation");

	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &KawaiiBoneChain::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &KawaiiBoneChain::get_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "gravity"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("set_wind_enabled", "value"), &KawaiiBoneChain::set_wind_enabled);
	ClassDB::bind_method(D_METHOD("get_wind_enabled"), &KawaiiBoneChain::get_wind_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wind_enabled"), "set_wind_enabled", "get_wind_enabled");

	ClassDB::bind_method(D_METHOD("set_wind_scale", "value"), &KawaiiBoneChain::set_wind_scale);
	ClassDB::bind_method(D_METHOD("get_wind_scale"), &KawaiiBoneChain::get_wind_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_scale"), "set_wind_scale", "get_wind_scale");

	ClassDB::bind_method(D_METHOD("set_dummy_length", "value"), &KawaiiBoneChain::set_dummy_length);
	ClassDB::bind_method(D_METHOD("get_dummy_length"), &KawaiiBoneChain::get_dummy_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dummy_length", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:m"), "set_dummy_length", "get_dummy_length");

	ClassDB::bind_method(D_METHOD("set_forward_axis", "value"), &KawaiiBoneChain::set_forward_axis);
	ClassDB::bind_method(D_METHOD("get_forward_axis"), &KawaiiBoneChain::get_forward_axis);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "forward_axis"), "set_forward_axis", "get_forward_axis");

	ClassDB::bind_method(D_METHOD("set_planar_constraint", "value"), &KawaiiBoneChain::set_planar_constraint);
	ClassDB::bind_method(D_METHOD("get_planar_constraint"), &KawaiiBoneChain::get_planar_constraint);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "planar_constraint", PROPERTY_HINT_ENUM, "None,X,Y,Z"), "set_planar_constraint", "get_planar_constraint");

	ClassDB::bind_method(D_METHOD("set_collisions", "value"), &KawaiiBoneChain::set_collisions);
	ClassDB::bind_method(D_METHOD("get_collisions"), &KawaiiBoneChain::get_collisions);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "collisions", PROPERTY_HINT_ARRAY_TYPE, "KawaiiBoneCollision"), "set_collisions", "get_collisions");

	BIND_ENUM_CONSTANT(PLANAR_CONSTRAINT_NONE);
	BIND_ENUM_CONSTANT(PLANAR_CONSTRAINT_X);
	BIND_ENUM_CONSTANT(PLANAR_CONSTRAINT_Y);
	BIND_ENUM_CONSTANT(PLANAR_CONSTRAINT_Z);
}

void KawaiiBoneChain::set_root_bone(const StringName &p_value) {
	root_bone = p_value;
	emit_changed();
}

StringName KawaiiBoneChain::get_root_bone() const {
	return root_bone;
}

void KawaiiBoneChain::set_excluded_bones(const PackedStringArray &p_value) {
	excluded_bones = p_value;
	emit_changed();
}

PackedStringArray KawaiiBoneChain::get_excluded_bones() const {
	return excluded_bones;
}

void KawaiiBoneChain::set_damping(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0 || p_value > 1.0);
	damping = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_damping() const {
	return damping;
}

void KawaiiBoneChain::set_world_damping_translation(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0 || p_value > 1.0);
	world_damping_translation = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_world_damping_translation() const {
	return world_damping_translation;
}

void KawaiiBoneChain::set_world_damping_rotation(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0 || p_value > 1.0);
	world_damping_rotation = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_world_damping_rotation() const {
	return world_damping_rotation;
}

void KawaiiBoneChain::set_stiffness(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0 || p_value > 1.0);
	stiffness = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_stiffness() const {
	return stiffness;
}

void KawaiiBoneChain::set_radius(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0);
	radius = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_radius() const {
	return radius;
}

void KawaiiBoneChain::set_limit_angle(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0 || p_value > 180.0);
	limit_angle = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_limit_angle() const {
	return limit_angle;
}

void KawaiiBoneChain::set_target_framerate(int p_value) {
	ERR_FAIL_COND(p_value <= 0);
	target_framerate = p_value;
	emit_changed();
}

int KawaiiBoneChain::get_target_framerate() const {
	return target_framerate;
}

void KawaiiBoneChain::set_teleport_distance(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0);
	teleport_distance = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_teleport_distance() const {
	return teleport_distance;
}

void KawaiiBoneChain::set_teleport_rotation(real_t p_value) {
	ERR_FAIL_COND(p_value < -1.0);
	teleport_rotation = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_teleport_rotation() const {
	return teleport_rotation;
}

void KawaiiBoneChain::set_gravity(const Vector3 &p_value) {
	gravity = p_value;
	emit_changed();
}

Vector3 KawaiiBoneChain::get_gravity() const {
	return gravity;
}

void KawaiiBoneChain::set_wind_enabled(bool p_value) {
	wind_enabled = p_value;
	emit_changed();
}

bool KawaiiBoneChain::get_wind_enabled() const {
	return wind_enabled;
}

void KawaiiBoneChain::set_wind_scale(real_t p_value) {
	wind_scale = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_wind_scale() const {
	return wind_scale;
}

void KawaiiBoneChain::set_dummy_length(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0);
	dummy_length = p_value;
	emit_changed();
}

real_t KawaiiBoneChain::get_dummy_length() const {
	return dummy_length;
}

void KawaiiBoneChain::set_forward_axis(const Vector3 &p_value) {
	ERR_FAIL_COND(!p_value.is_normalized());
	forward_axis = p_value;
	emit_changed();
}

Vector3 KawaiiBoneChain::get_forward_axis() const {
	return forward_axis;
}

void KawaiiBoneChain::set_planar_constraint(PlanarConstraint p_value) {
	ERR_FAIL_COND(p_value < PLANAR_CONSTRAINT_NONE || p_value > PLANAR_CONSTRAINT_Z);
	planar_constraint = p_value;
	emit_changed();
}

KawaiiBoneChain::PlanarConstraint KawaiiBoneChain::get_planar_constraint() const {
	return planar_constraint;
}

void KawaiiBoneChain::_collision_changed() {
	emit_changed();
}

void KawaiiBoneChain::set_collisions(const TypedArray<KawaiiBoneCollision> &p_value) {
	const Callable changed = callable_mp(this, &KawaiiBoneChain::_collision_changed);
	for (int i = 0; i < collisions.size(); i++) {
		Ref<KawaiiBoneCollision> collision = collisions[i];
		if (collision.is_valid() && collision->is_connected(SNAME("changed"), changed)) {
			collision->disconnect_changed(changed);
		}
	}
	collisions = p_value.duplicate();
	for (int i = 0; i < collisions.size(); i++) {
		Ref<KawaiiBoneCollision> collision = collisions[i];
		if (collision.is_valid() && !collision->is_connected(SNAME("changed"), changed)) {
			collision->connect_changed(changed);
		}
	}
	emit_changed();
}

TypedArray<KawaiiBoneCollision> KawaiiBoneChain::get_collisions() const {
	return collisions.duplicate();
}
