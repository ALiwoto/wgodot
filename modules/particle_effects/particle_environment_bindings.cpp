// wgodot-changes::file
#include "particle_environment.h"
#include "core/object/class_db.h"

void ParticleLightProfile3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sample_position_before_motion", "value"), &ParticleLightProfile3D::set_sample_position_before_motion);
	ClassDB::bind_method(D_METHOD("get_sample_position_before_motion"), &ParticleLightProfile3D::get_sample_position_before_motion);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sample_position_before_motion"), "set_sample_position_before_motion", "get_sample_position_before_motion");
	ClassDB::bind_method(D_METHOD("set_color", "value"), &ParticleLightProfile3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &ParticleLightProfile3D::get_color);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_color", "get_color");
	ClassDB::bind_method(D_METHOD("set_brightness", "value"), &ParticleLightProfile3D::set_brightness);
	ClassDB::bind_method(D_METHOD("get_brightness"), &ParticleLightProfile3D::get_brightness);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "brightness", PROPERTY_HINT_RESOURCE_TYPE, "ParticleFloatCurve"), "set_brightness", "get_brightness");
	ClassDB::bind_method(D_METHOD("set_radius_scale", "value"), &ParticleLightProfile3D::set_radius_scale);
	ClassDB::bind_method(D_METHOD("get_radius_scale"), &ParticleLightProfile3D::get_radius_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius_scale"), "set_radius_scale", "get_radius_scale");
	ClassDB::bind_method(D_METHOD("set_spawn_fraction", "value"), &ParticleLightProfile3D::set_spawn_fraction);
	ClassDB::bind_method(D_METHOD("get_spawn_fraction"), &ParticleLightProfile3D::get_spawn_fraction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spawn_fraction"), "set_spawn_fraction", "get_spawn_fraction");
	ClassDB::bind_method(D_METHOD("set_high_quality", "value"), &ParticleLightProfile3D::set_high_quality);
	ClassDB::bind_method(D_METHOD("get_high_quality"), &ParticleLightProfile3D::get_high_quality);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "high_quality"), "set_high_quality", "get_high_quality");
	ClassDB::bind_method(D_METHOD("set_inverse_squared", "value"), &ParticleLightProfile3D::set_inverse_squared);
	ClassDB::bind_method(D_METHOD("get_inverse_squared"), &ParticleLightProfile3D::get_inverse_squared);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inverse_squared"), "set_inverse_squared", "get_inverse_squared");
	ClassDB::bind_method(D_METHOD("set_inverse_squared_scale", "value"), &ParticleLightProfile3D::set_inverse_squared_scale);
	ClassDB::bind_method(D_METHOD("get_inverse_squared_scale"), &ParticleLightProfile3D::get_inverse_squared_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inverse_squared_scale"), "set_inverse_squared_scale", "get_inverse_squared_scale");
	ClassDB::bind_method(D_METHOD("set_normalization_units", "value"), &ParticleLightProfile3D::set_normalization_units);
	ClassDB::bind_method(D_METHOD("get_normalization_units"), &ParticleLightProfile3D::get_normalization_units);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normalization_units"), "set_normalization_units", "get_normalization_units");
	ClassDB::bind_method(D_METHOD("apply", "light", "relative_age", "tint", "size", "emitter_scale"), &ParticleLightProfile3D::apply);
}

void ParticleGroundProjector3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_check_height", "value"), &ParticleGroundProjector3D::set_check_height);
	ClassDB::bind_method(D_METHOD("get_check_height"), &ParticleGroundProjector3D::get_check_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "check_height"), "set_check_height", "get_check_height");
	ClassDB::bind_method(D_METHOD("set_world_offset", "value"), &ParticleGroundProjector3D::set_world_offset);
	ClassDB::bind_method(D_METHOD("get_world_offset"), &ParticleGroundProjector3D::get_world_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "world_offset"), "set_world_offset", "get_world_offset");
	ClassDB::bind_method(D_METHOD("set_offset_variation", "value"), &ParticleGroundProjector3D::set_offset_variation);
	ClassDB::bind_method(D_METHOD("get_offset_variation"), &ParticleGroundProjector3D::get_offset_variation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_variation"), "set_offset_variation", "get_offset_variation");
	ClassDB::bind_method(D_METHOD("set_random_offset", "value"), &ParticleGroundProjector3D::set_random_offset);
	ClassDB::bind_method(D_METHOD("get_random_offset"), &ParticleGroundProjector3D::get_random_offset);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_offset"), "set_random_offset", "get_random_offset");
	ClassDB::bind_method(D_METHOD("set_targets_emitter", "value"), &ParticleGroundProjector3D::set_targets_emitter);
	ClassDB::bind_method(D_METHOD("get_targets_emitter"), &ParticleGroundProjector3D::get_targets_emitter);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "targets_emitter"), "set_targets_emitter", "get_targets_emitter");
	ClassDB::bind_method(D_METHOD("set_update_on_tick", "value"), &ParticleGroundProjector3D::set_update_on_tick);
	ClassDB::bind_method(D_METHOD("get_update_on_tick"), &ParticleGroundProjector3D::get_update_on_tick);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "update_on_tick"), "set_update_on_tick", "get_update_on_tick");
	ClassDB::bind_method(D_METHOD("set_emitter_origin", "value"), &ParticleGroundProjector3D::set_emitter_origin);
	ClassDB::bind_method(D_METHOD("get_emitter_origin"), &ParticleGroundProjector3D::get_emitter_origin);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "emitter_origin"), "set_emitter_origin", "get_emitter_origin");
	ClassDB::bind_method(D_METHOD("set_emitter_rotation", "value"), &ParticleGroundProjector3D::set_emitter_rotation);
	ClassDB::bind_method(D_METHOD("get_emitter_rotation"), &ParticleGroundProjector3D::get_emitter_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "emitter_rotation"), "set_emitter_rotation", "get_emitter_rotation");
	ClassDB::bind_method(D_METHOD("project", "point"), &ParticleGroundProjector3D::project);
	ClassDB::bind_method(D_METHOD("projected_position", "random"), &ParticleGroundProjector3D::projected_position);
	ClassDB::bind_method(D_METHOD("project_emitter", "frame"), &ParticleGroundProjector3D::project_emitter);
	ClassDB::bind_method(D_METHOD("emitter_offset", "frame"), &ParticleGroundProjector3D::emitter_offset);
}
