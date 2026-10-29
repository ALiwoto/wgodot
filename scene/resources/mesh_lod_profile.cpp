// wgodot-changes::file
#include "mesh_lod_profile.h"

#include "core/object/class_db.h"

void MeshLODProfile::set_surface_levels(const Vector<int32_t> &p_levels) {
	data.surface_levels = p_levels;
	emit_changed();
}

void MeshLODProfile::set_thresholds(const Vector<Vector2> &p_thresholds) {
	data.thresholds = p_thresholds;
	emit_changed();
}

void MeshLODProfile::set_center(const Vector3 &p_center) {
	ERR_FAIL_COND(!p_center.is_finite());
	data.center = p_center;
	emit_changed();
}

void MeshLODProfile::set_radius(float p_radius) {
	ERR_FAIL_COND(!Math::is_finite(p_radius) || p_radius < 0.0f);
	data.radius = p_radius;
	emit_changed();
}

void MeshLODProfile::set_minimum_level(int p_level) {
	ERR_FAIL_COND(p_level < 0);
	data.minimum_level = p_level;
	emit_changed();
}

void MeshLODProfile::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_surface_levels", "levels"), &MeshLODProfile::set_surface_levels);
	ClassDB::bind_method(D_METHOD("get_surface_levels"), &MeshLODProfile::get_surface_levels);
	ClassDB::bind_method(D_METHOD("set_thresholds", "thresholds"), &MeshLODProfile::set_thresholds);
	ClassDB::bind_method(D_METHOD("get_thresholds"), &MeshLODProfile::get_thresholds);
	ClassDB::bind_method(D_METHOD("set_center", "center"), &MeshLODProfile::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &MeshLODProfile::get_center);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &MeshLODProfile::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &MeshLODProfile::get_radius);
	ClassDB::bind_method(D_METHOD("set_minimum_level", "level"), &MeshLODProfile::set_minimum_level);
	ClassDB::bind_method(D_METHOD("get_minimum_level"), &MeshLODProfile::get_minimum_level);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "surface_levels"), "set_surface_levels", "get_surface_levels");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "thresholds"), "set_thresholds", "get_thresholds");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,10000,0.001,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "minimum_level", PROPERTY_HINT_RANGE, "0,16,1,or_greater"), "set_minimum_level", "get_minimum_level");
}
