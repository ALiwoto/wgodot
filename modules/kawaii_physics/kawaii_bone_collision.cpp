// wgodot-changes::file
#include "kawaii_bone_collision.h"

#include "core/object/class_db.h"

void KawaiiBoneCollision::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shape", "value"), &KawaiiBoneCollision::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &KawaiiBoneCollision::get_shape);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Sphere,Capsule,Plane"), "set_shape", "get_shape");

	ClassDB::bind_method(D_METHOD("set_bone_name", "value"), &KawaiiBoneCollision::set_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_name"), &KawaiiBoneCollision::get_bone_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bone_name"), "set_bone_name", "get_bone_name");

	ClassDB::bind_method(D_METHOD("set_offset", "value"), &KawaiiBoneCollision::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &KawaiiBoneCollision::get_offset);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "offset"), "set_offset", "get_offset");

	ClassDB::bind_method(D_METHOD("set_radius", "value"), &KawaiiBoneCollision::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &KawaiiBoneCollision::get_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,10,0.001,or_greater,suffix:m"), "set_radius", "get_radius");

	ClassDB::bind_method(D_METHOD("set_length", "value"), &KawaiiBoneCollision::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &KawaiiBoneCollision::get_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0,10,0.001,or_greater,suffix:m"), "set_length", "get_length");

	ClassDB::bind_method(D_METHOD("set_inside", "value"), &KawaiiBoneCollision::set_inside);
	ClassDB::bind_method(D_METHOD("get_inside"), &KawaiiBoneCollision::get_inside);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inside"), "set_inside", "get_inside");

	BIND_ENUM_CONSTANT(SHAPE_SPHERE);
	BIND_ENUM_CONSTANT(SHAPE_CAPSULE);
	BIND_ENUM_CONSTANT(SHAPE_PLANE);
}

void KawaiiBoneCollision::set_shape(Shape p_value) {
	ERR_FAIL_COND(p_value < SHAPE_SPHERE || p_value > SHAPE_PLANE);
	shape = p_value;
	emit_changed();
}

KawaiiBoneCollision::Shape KawaiiBoneCollision::get_shape() const {
	return shape;
}

void KawaiiBoneCollision::set_bone_name(const StringName &p_value) {
	bone_name = p_value;
	emit_changed();
}

StringName KawaiiBoneCollision::get_bone_name() const {
	return bone_name;
}

void KawaiiBoneCollision::set_offset(const Transform3D &p_value) {
	offset = p_value;
	emit_changed();
}

Transform3D KawaiiBoneCollision::get_offset() const {
	return offset;
}

void KawaiiBoneCollision::set_radius(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0);
	radius = p_value;
	emit_changed();
}

real_t KawaiiBoneCollision::get_radius() const {
	return radius;
}

void KawaiiBoneCollision::set_length(real_t p_value) {
	ERR_FAIL_COND(p_value < 0.0);
	length = p_value;
	emit_changed();
}

real_t KawaiiBoneCollision::get_length() const {
	return length;
}

void KawaiiBoneCollision::set_inside(bool p_value) {
	inside = p_value;
	emit_changed();
}

bool KawaiiBoneCollision::get_inside() const {
	return inside;
}
