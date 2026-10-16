// wgodot-changes::file
#pragma once

#include "particle_curves.h"
#include "core/variant/typed_array.h"
#include "scene/resources/gradient.h"
#include "scene/resources/mesh.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/physics/ray_cast_3d.h"

class ParticleLightProfile3D : public Resource {
	GDCLASS(ParticleLightProfile3D, Resource);

	Ref<Gradient> color;
	Ref<ParticleFloatCurve> brightness;
	double radius_scale = 0.0;
	double spawn_fraction = 1.0;
	bool high_quality = false;
	bool sample_position_before_motion = false;
	bool inverse_squared = true;
	double inverse_squared_scale = 1.0;
	double normalization_units = 1.0;

protected:
	static void _bind_methods();

public:
	void set_color(const Ref<Gradient> & p_value) { color = p_value; }
	Ref<Gradient> get_color() const { return color; }
	void set_brightness(const Ref<ParticleFloatCurve> & p_value) { brightness = p_value; }
	Ref<ParticleFloatCurve> get_brightness() const { return brightness; }
	void set_radius_scale(double p_value) { radius_scale = p_value; }
	double get_radius_scale() const { return radius_scale; }
	void set_spawn_fraction(double p_value) { spawn_fraction = p_value; }
	double get_spawn_fraction() const { return spawn_fraction; }
	void set_high_quality(bool p_value) { high_quality = p_value; }
	bool get_high_quality() const { return high_quality; }
	void set_sample_position_before_motion(bool p_value) { sample_position_before_motion = p_value; }
	bool get_sample_position_before_motion() const { return sample_position_before_motion; }
	void set_inverse_squared(bool p_value) { inverse_squared = p_value; }
	bool get_inverse_squared() const { return inverse_squared; }
	void set_inverse_squared_scale(double p_value) { inverse_squared_scale = p_value; }
	double get_inverse_squared_scale() const { return inverse_squared_scale; }
	void set_normalization_units(double p_value) { normalization_units = p_value; }
	double get_normalization_units() const { return normalization_units; }
	void apply(OmniLight3D * p_light, double p_relative_age, Color p_tint, Vector2 p_size, Vector3 p_emitter_scale) const;
};

class ParticleGroundProjector3D : public RayCast3D {
	GDCLASS(ParticleGroundProjector3D, RayCast3D);

	double check_height = 5.12;
	double world_offset = 0.0;
	double offset_variation = 0.0;
	bool random_offset = false;
	bool targets_emitter = false;
	bool update_on_tick = false;
	Vector3 emitter_origin;
	Vector3 emitter_rotation;

protected:
	static void _bind_methods();

public:
	void set_check_height(double p_value) { check_height = p_value; }
	double get_check_height() const { return check_height; }
	void set_world_offset(double p_value) { world_offset = p_value; }
	double get_world_offset() const { return world_offset; }
	void set_offset_variation(double p_value) { offset_variation = p_value; }
	double get_offset_variation() const { return offset_variation; }
	void set_random_offset(bool p_value) { random_offset = p_value; }
	bool get_random_offset() const { return random_offset; }
	void set_targets_emitter(bool p_value) { targets_emitter = p_value; }
	bool get_targets_emitter() const { return targets_emitter; }
	void set_update_on_tick(bool p_value) { update_on_tick = p_value; }
	bool get_update_on_tick() const { return update_on_tick; }
	void set_emitter_origin(const Vector3 & p_value) { emitter_origin = p_value; }
	Vector3 get_emitter_origin() const { return emitter_origin; }
	void set_emitter_rotation(const Vector3 & p_value) { emitter_rotation = p_value; }
	Vector3 get_emitter_rotation() const { return emitter_rotation; }
	bool project(Vector3 p_point);
	Vector3 projected_position(const Ref<RandomStream32> & p_random);
	bool project_emitter(Transform3D p_frame);
	double emitter_offset(Transform3D p_frame);
};
