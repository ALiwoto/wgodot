// wgodot-changes::file
#include "particle_appearance.h"
#include "core/object/class_db.h"

void ParticleColorCurve::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ParticleColorCurve::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &ParticleColorCurve::get_value);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_value", "get_value");
	ClassDB::bind_method(D_METHOD("set_extreme", "value"), &ParticleColorCurve::set_extreme);
	ClassDB::bind_method(D_METHOD("get_extreme"), &ParticleColorCurve::get_extreme);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "extreme", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_extreme", "get_extreme");
	ClassDB::bind_method(D_METHOD("set_color_sampling", "value"), &ParticleColorCurve::set_color_sampling);
	ClassDB::bind_method(D_METHOD("get_color_sampling"), &ParticleColorCurve::get_color_sampling);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "color_sampling"), "set_color_sampling", "get_color_sampling");
	ClassDB::bind_method(D_METHOD("set_alpha_sampling", "value"), &ParticleColorCurve::set_alpha_sampling);
	ClassDB::bind_method(D_METHOD("get_alpha_sampling"), &ParticleColorCurve::get_alpha_sampling);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alpha_sampling"), "set_alpha_sampling", "get_alpha_sampling");
	ClassDB::bind_method(D_METHOD("set_axis_lock", "value"), &ParticleColorCurve::set_axis_lock);
	ClassDB::bind_method(D_METHOD("get_axis_lock"), &ParticleColorCurve::get_axis_lock);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "axis_lock"), "set_axis_lock", "get_axis_lock");
	ClassDB::bind_method(D_METHOD("set_color_order", "value"), &ParticleColorCurve::set_color_order);
	ClassDB::bind_method(D_METHOD("get_color_order"), &ParticleColorCurve::get_color_order);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "color_order"), "set_color_order", "get_color_order");
	ClassDB::bind_method(D_METHOD("set_channels", "value"), &ParticleColorCurve::set_channels);
	ClassDB::bind_method(D_METHOD("get_channels"), &ParticleColorCurve::get_channels);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "channels", PROPERTY_HINT_ARRAY_TYPE, "ParticleFloatCurve"), "set_channels", "get_channels");
	ClassDB::bind_method(D_METHOD("sample", "relative_age", "random", "task_ratio"), &ParticleColorCurve::sample, DEFVAL(0.0));
}

void ParticleOrientation3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_use_shared_random", "value"), &ParticleOrientation3D::set_use_shared_random);
	ClassDB::bind_method(D_METHOD("get_use_shared_random"), &ParticleOrientation3D::get_use_shared_random);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_shared_random"), "set_use_shared_random", "get_use_shared_random");
	ClassDB::bind_method(D_METHOD("get_random_stream"), &ParticleOrientation3D::get_random_stream);
	ClassDB::bind_method(D_METHOD("set_angles", "value"), &ParticleOrientation3D::set_angles);
	ClassDB::bind_method(D_METHOD("get_angles"), &ParticleOrientation3D::get_angles);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "angles"), "set_angles", "get_angles");
	ClassDB::bind_method(D_METHOD("set_variation", "value"), &ParticleOrientation3D::set_variation);
	ClassDB::bind_method(D_METHOD("get_variation"), &ParticleOrientation3D::get_variation);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "variation"), "set_variation", "get_variation");
	ClassDB::bind_method(D_METHOD("set_randomized", "value"), &ParticleOrientation3D::set_randomized);
	ClassDB::bind_method(D_METHOD("get_randomized"), &ParticleOrientation3D::get_randomized);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "randomized"), "set_randomized", "get_randomized");
	ClassDB::bind_method(D_METHOD("sample", "stream"), &ParticleOrientation3D::sample, DEFVAL(Ref<RandomStream32>()));
}

void ParticleMaterialChannels::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_velocity_sources", "value"), &ParticleMaterialChannels::set_velocity_sources);
	ClassDB::bind_method(D_METHOD("get_velocity_sources"), &ParticleMaterialChannels::get_velocity_sources);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4I, "velocity_sources"), "set_velocity_sources", "get_velocity_sources");
	ClassDB::bind_method(D_METHOD("set_velocity_scales", "value"), &ParticleMaterialChannels::set_velocity_scales);
	ClassDB::bind_method(D_METHOD("get_velocity_scales"), &ParticleMaterialChannels::get_velocity_scales);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "velocity_scales"), "set_velocity_scales", "get_velocity_scales");
	BIND_CONSTANT(VELOCITY_NONE);
	BIND_CONSTANT(VELOCITY_X);
	BIND_CONSTANT(VELOCITY_Y);
	BIND_CONSTANT(VELOCITY_Z);
	BIND_CONSTANT(VELOCITY_LENGTH);
	ClassDB::bind_method(D_METHOD("set_channels", "value"), &ParticleMaterialChannels::set_channels);
	ClassDB::bind_method(D_METHOD("get_channels"), &ParticleMaterialChannels::get_channels);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "channels", PROPERTY_HINT_ARRAY_TYPE, "ParticleFloatCurve"), "set_channels", "get_channels");
	ClassDB::bind_method(D_METHOD("set_emitter_clock_channels", "value"), &ParticleMaterialChannels::set_emitter_clock_channels);
	ClassDB::bind_method(D_METHOD("get_emitter_clock_channels"), &ParticleMaterialChannels::get_emitter_clock_channels);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "emitter_clock_channels"), "set_emitter_clock_channels", "get_emitter_clock_channels");
	ClassDB::bind_method(D_METHOD("set_spawn_channels", "value"), &ParticleMaterialChannels::set_spawn_channels);
	ClassDB::bind_method(D_METHOD("get_spawn_channels"), &ParticleMaterialChannels::get_spawn_channels);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "spawn_channels"), "set_spawn_channels", "get_spawn_channels");
	ClassDB::bind_method(D_METHOD("set_curve_spawn_channels", "value"), &ParticleMaterialChannels::set_curve_spawn_channels);
	ClassDB::bind_method(D_METHOD("get_curve_spawn_channels"), &ParticleMaterialChannels::get_curve_spawn_channels);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "curve_spawn_channels"), "set_curve_spawn_channels", "get_curve_spawn_channels");
	ClassDB::bind_method(D_METHOD("set_random_channels", "value"), &ParticleMaterialChannels::set_random_channels);
	ClassDB::bind_method(D_METHOD("get_random_channels"), &ParticleMaterialChannels::get_random_channels);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "random_channels"), "set_random_channels", "get_random_channels");
	ClassDB::bind_method(D_METHOD("set_random_spreads", "value"), &ParticleMaterialChannels::set_random_spreads);
	ClassDB::bind_method(D_METHOD("get_random_spreads"), &ParticleMaterialChannels::get_random_spreads);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "random_spreads"), "set_random_spreads", "get_random_spreads");
	ClassDB::bind_method(D_METHOD("spawn_values", "relative_age", "initial", "task_ratio", "emitter_seconds", "velocity"), &ParticleMaterialChannels::spawn_values, DEFVAL(0.0), DEFVAL(0.0), DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("sample", "relative_age", "initial", "task_ratio", "random", "spawned_now", "emitter_seconds", "velocity"), &ParticleMaterialChannels::sample, DEFVAL(Vector4()), DEFVAL(0.0), DEFVAL(Ref<RandomStream32>()), DEFVAL(false), DEFVAL(0.0), DEFVAL(Vector3()));
}
