// wgodot-changes::file
#pragma once

#include "particle_curves.h"
#include "core/variant/typed_array.h"
#include "scene/resources/gradient.h"
#include "scene/resources/mesh.h"
#include "particle_spawn.h"
#include "particle_appearance.h"

class ParticleMotionProfile3D : public Resource {
	GDCLASS(ParticleMotionProfile3D, Resource);

	Vector3 position;
	Ref<ParticleVectorCurve3D> position_over_life;
	Vector3 position_offset;
	Ref<ParticleFloatCurve> direct_velocity_scale;
	Vector3 velocity;
	Ref<ParticleBirthRule3D> initial_velocity;
	Ref<ParticleVectorCurve3D> source_movement;
	Ref<ParticleVectorCurve3D> size_scale;
	TypedArray<ParticleVectorCurve3D> size_over_life;
	int size_life_spawn_start = 0;
	bool inherit_parent_rotation = false;
	bool align_to_velocity = false;
	bool orbit = false;
	Vector3 orbit_offset;
	Ref<ParticleVectorCurve3D> orbit_offset_over_life;
	Vector3 orbit_offset_variation;
	bool random_orbit_offset = false;
	int orbit_offset_axis_lock = 1;
	Vector3 orbit_rotation;
	Ref<ParticleVectorCurve3D> orbit_rotation_over_emission;
	Vector3 orbit_rotation_variation;
	bool random_orbit_rotation = false;
	int orbit_rotation_axis_lock = 1;
	Ref<ParticleVectorCurve3D> orbit_rate;
	Ref<ParticleVectorCurve3D> orbit_rate_maximum;
	int orbit_rate_axis_lock = 1;
	bool random_orbit_rate = false;
	bool orbit_rate_on_spawn = false;
	bool orbit_rate_on_update = false;
	bool orbit_rate_emitter_clock = false;
	double orbit_rotation_threshold = 0.0;

protected:
	static void _bind_methods();

public:
	void set_position(const Vector3 & p_value) { position = p_value; }
	Vector3 get_position() const { return position; }
	void set_position_over_life(const Ref<ParticleVectorCurve3D> & p_value) { position_over_life = p_value; }
	Ref<ParticleVectorCurve3D> get_position_over_life() const { return position_over_life; }
	void set_position_offset(const Vector3 & p_value) { position_offset = p_value; }
	Vector3 get_position_offset() const { return position_offset; }
	void set_direct_velocity_scale(const Ref<ParticleFloatCurve> & p_value) { direct_velocity_scale = p_value; }
	Ref<ParticleFloatCurve> get_direct_velocity_scale() const { return direct_velocity_scale; }
	void set_velocity(const Vector3 & p_value) { velocity = p_value; }
	Vector3 get_velocity() const { return velocity; }
	void set_initial_velocity(const Ref<ParticleBirthRule3D> & p_value) { initial_velocity = p_value; }
	Ref<ParticleBirthRule3D> get_initial_velocity() const { return initial_velocity; }
	void set_source_movement(const Ref<ParticleVectorCurve3D> & p_value) { source_movement = p_value; }
	Ref<ParticleVectorCurve3D> get_source_movement() const { return source_movement; }
	void set_size_scale(const Ref<ParticleVectorCurve3D> & p_value) { size_scale = p_value; }
	Ref<ParticleVectorCurve3D> get_size_scale() const { return size_scale; }
	void set_size_over_life(const TypedArray<ParticleVectorCurve3D> & p_value) { size_over_life = p_value; }
	TypedArray<ParticleVectorCurve3D> get_size_over_life() const { return size_over_life; }
	void set_size_life_spawn_start(int p_value) { size_life_spawn_start = p_value; }
	int get_size_life_spawn_start() const { return size_life_spawn_start; }
	void set_inherit_parent_rotation(bool p_value) { inherit_parent_rotation = p_value; }
	bool get_inherit_parent_rotation() const { return inherit_parent_rotation; }
	void set_align_to_velocity(bool p_value) { align_to_velocity = p_value; }
	bool get_align_to_velocity() const { return align_to_velocity; }
	void set_orbit(bool p_value) { orbit = p_value; }
	bool get_orbit() const { return orbit; }
	void set_orbit_offset(const Vector3 & p_value) { orbit_offset = p_value; }
	Vector3 get_orbit_offset() const { return orbit_offset; }
	void set_orbit_offset_over_life(const Ref<ParticleVectorCurve3D> & p_value) { orbit_offset_over_life = p_value; }
	Ref<ParticleVectorCurve3D> get_orbit_offset_over_life() const { return orbit_offset_over_life; }
	void set_orbit_offset_variation(const Vector3 & p_value) { orbit_offset_variation = p_value; }
	Vector3 get_orbit_offset_variation() const { return orbit_offset_variation; }
	void set_random_orbit_offset(bool p_value) { random_orbit_offset = p_value; }
	bool get_random_orbit_offset() const { return random_orbit_offset; }
	void set_orbit_offset_axis_lock(int p_value) { orbit_offset_axis_lock = p_value; }
	int get_orbit_offset_axis_lock() const { return orbit_offset_axis_lock; }
	void set_orbit_rotation(const Vector3 & p_value) { orbit_rotation = p_value; }
	Vector3 get_orbit_rotation() const { return orbit_rotation; }
	void set_orbit_rotation_over_emission(const Ref<ParticleVectorCurve3D> & p_value) { orbit_rotation_over_emission = p_value; }
	Ref<ParticleVectorCurve3D> get_orbit_rotation_over_emission() const { return orbit_rotation_over_emission; }
	void set_orbit_rotation_variation(const Vector3 & p_value) { orbit_rotation_variation = p_value; }
	Vector3 get_orbit_rotation_variation() const { return orbit_rotation_variation; }
	void set_random_orbit_rotation(bool p_value) { random_orbit_rotation = p_value; }
	bool get_random_orbit_rotation() const { return random_orbit_rotation; }
	void set_orbit_rotation_axis_lock(int p_value) { orbit_rotation_axis_lock = p_value; }
	int get_orbit_rotation_axis_lock() const { return orbit_rotation_axis_lock; }
	void set_orbit_rate(const Ref<ParticleVectorCurve3D> & p_value) { orbit_rate = p_value; }
	Ref<ParticleVectorCurve3D> get_orbit_rate() const { return orbit_rate; }
	void set_orbit_rate_maximum(const Ref<ParticleVectorCurve3D> & p_value) { orbit_rate_maximum = p_value; }
	Ref<ParticleVectorCurve3D> get_orbit_rate_maximum() const { return orbit_rate_maximum; }
	void set_orbit_rate_axis_lock(int p_value) { orbit_rate_axis_lock = p_value; }
	int get_orbit_rate_axis_lock() const { return orbit_rate_axis_lock; }
	void set_random_orbit_rate(bool p_value) { random_orbit_rate = p_value; }
	bool get_random_orbit_rate() const { return random_orbit_rate; }
	void set_orbit_rate_on_spawn(bool p_value) { orbit_rate_on_spawn = p_value; }
	bool get_orbit_rate_on_spawn() const { return orbit_rate_on_spawn; }
	void set_orbit_rate_on_update(bool p_value) { orbit_rate_on_update = p_value; }
	bool get_orbit_rate_on_update() const { return orbit_rate_on_update; }
	void set_orbit_rate_emitter_clock(bool p_value) { orbit_rate_emitter_clock = p_value; }
	bool get_orbit_rate_emitter_clock() const { return orbit_rate_emitter_clock; }
	void set_orbit_rotation_threshold(double p_value) { orbit_rotation_threshold = p_value; }
	double get_orbit_rotation_threshold() const { return orbit_rotation_threshold; }
	double direct_velocity_at(double p_relative_age) const;
	Vector3 scale_size_over_life(Vector3 p_size, double p_relative_age, const Ref<RandomStream32> & p_random, bool p_spawning) const;
	Vector3 sample_orbit_offset(const Ref<RandomStream32> & p_random) const;
	Vector3 sample_orbit_rotation(const Ref<RandomStream32> & p_random, double p_emitter_seconds) const;
	Vector3 sample_orbit_rate(double p_time, const Ref<RandomStream32> & p_random) const;
	Vector3 rotate_orbit_offset(Vector3 p_offset, Vector3 p_turns) const;
};

class ParticleSpriteProfile3D : public ParticleMotionProfile3D {
	GDCLASS(ParticleSpriteProfile3D, ParticleMotionProfile3D);

	Vector3 size;
	Ref<ParticleVectorCurve3D> size_scale_maximum;
	int size_scale_axis_lock = 1;
	double rotation = 0.0;
	double angular_velocity = 0.0;
	Ref<ParticleFloatCurve> angular_velocity_over_life;
	bool square = false;
	TypedArray<Mesh> frames;
	Ref<ParticleFloatCurve> frame_over_life;
	Ref<ParticleFloatCurve> frame_over_life_maximum;
	bool fractional_frame = false;
	int random_frame_count = 0;
	bool change_random_frame = false;
	double frame_change_interval = 0.0;

protected:
	static void _bind_methods();

public:
	void set_size(const Vector3 & p_value) { size = p_value; }
	Vector3 get_size() const { return size; }
	void set_size_scale_maximum(const Ref<ParticleVectorCurve3D> & p_value) { size_scale_maximum = p_value; }
	Ref<ParticleVectorCurve3D> get_size_scale_maximum() const { return size_scale_maximum; }
	void set_size_scale_axis_lock(int p_value) { size_scale_axis_lock = p_value; }
	int get_size_scale_axis_lock() const { return size_scale_axis_lock; }
	void set_rotation(double p_value) { rotation = p_value; }
	double get_rotation() const { return rotation; }
	void set_angular_velocity(double p_value) { angular_velocity = p_value; }
	double get_angular_velocity() const { return angular_velocity; }
	void set_angular_velocity_over_life(const Ref<ParticleFloatCurve> & p_value) { angular_velocity_over_life = p_value; }
	Ref<ParticleFloatCurve> get_angular_velocity_over_life() const { return angular_velocity_over_life; }
	void set_square(bool p_value) { square = p_value; }
	bool get_square() const { return square; }
	void set_frames(const TypedArray<Mesh> & p_value) { frames = p_value; }
	TypedArray<Mesh> get_frames() const { return frames; }
	void set_frame_over_life(const Ref<ParticleFloatCurve> & p_value) { frame_over_life = p_value; }
	Ref<ParticleFloatCurve> get_frame_over_life() const { return frame_over_life; }
	void set_frame_over_life_maximum(const Ref<ParticleFloatCurve> & p_value) { frame_over_life_maximum = p_value; }
	Ref<ParticleFloatCurve> get_frame_over_life_maximum() const { return frame_over_life_maximum; }
	void set_fractional_frame(bool p_value) { fractional_frame = p_value; }
	bool get_fractional_frame() const { return fractional_frame; }
	void set_random_frame_count(int p_value) { random_frame_count = p_value; }
	int get_random_frame_count() const { return random_frame_count; }
	void set_change_random_frame(bool p_value) { change_random_frame = p_value; }
	bool get_change_random_frame() const { return change_random_frame; }
	void set_frame_change_interval(double p_value) { frame_change_interval = p_value; }
	double get_frame_change_interval() const { return frame_change_interval; }
	Vector2 size_at(double p_relative_age, Vector3 p_initial_size, const Ref<RandomStream32> & p_random, const Ref<RandomStream32> & p_life_random, bool p_spawning) const;
	double angular_velocity_at(double p_relative_age, double p_initial_rate, const Ref<RandomStream32> & p_random) const;
	double sample_frame(double p_relative_age, const Ref<RandomStream32> & p_random) const;
};

class ParticleMeshProfile3D : public ParticleMotionProfile3D {
	GDCLASS(ParticleMeshProfile3D, ParticleMotionProfile3D);

	Vector3 rotation;
	bool faces_camera = false;
	double spin_angle = 0.0;
	double spin_speed = 0.0;
	Ref<ParticleFloatCurve> spin_over_life;
	bool inherit_emitter_rotation = false;
	Vector3 angular_velocity;
	Vector3 size = Vector3(1, 1, 1);
	Ref<ParticleVectorCurve3D> rotation_rate_over_life;
	Vector<int32_t> flattened_axes;
	Ref<ParticleOrientation3D> pre_orientation;

protected:
	static void _bind_methods();

public:
	void set_rotation(const Vector3 & p_value) { rotation = p_value; }
	Vector3 get_rotation() const { return rotation; }
	void set_faces_camera(bool p_value) { faces_camera = p_value; }
	bool get_faces_camera() const { return faces_camera; }
	void set_spin_angle(double p_value) { spin_angle = p_value; }
	double get_spin_angle() const { return spin_angle; }
	void set_spin_speed(double p_value) { spin_speed = p_value; }
	double get_spin_speed() const { return spin_speed; }
	void set_spin_over_life(const Ref<ParticleFloatCurve> & p_value) { spin_over_life = p_value; }
	Ref<ParticleFloatCurve> get_spin_over_life() const { return spin_over_life; }
	void set_inherit_emitter_rotation(bool p_value) { inherit_emitter_rotation = p_value; }
	bool get_inherit_emitter_rotation() const { return inherit_emitter_rotation; }
	void set_angular_velocity(const Vector3 & p_value) { angular_velocity = p_value; }
	Vector3 get_angular_velocity() const { return angular_velocity; }
	void set_size(const Vector3 & p_value) { size = p_value; }
	Vector3 get_size() const { return size; }
	void set_rotation_rate_over_life(const Ref<ParticleVectorCurve3D> & p_value) { rotation_rate_over_life = p_value; }
	Ref<ParticleVectorCurve3D> get_rotation_rate_over_life() const { return rotation_rate_over_life; }
	void set_flattened_axes(const Vector<int32_t> & p_value) { flattened_axes = p_value; }
	Vector<int32_t> get_flattened_axes() const { return flattened_axes; }
	void set_pre_orientation(const Ref<ParticleOrientation3D> & p_value) { pre_orientation = p_value; }
	Ref<ParticleOrientation3D> get_pre_orientation() const { return pre_orientation; }
	Vector3 velocity_rotation(Vector3 p_direction_velocity) const;
	Vector3 scale_at(double p_relative_age, Vector3 p_initial_size, const Ref<RandomStream32> & p_random, const Ref<RandomStream32> & p_life_random, bool p_spawning) const;
	Vector3 angular_velocity_at(double p_relative_age, Vector3 p_initial_rate) const;
};
