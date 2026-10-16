// wgodot-changes::file
#pragma once

#include "particle_curves.h"
#include "core/variant/typed_array.h"
#include "scene/resources/gradient.h"
#include "scene/resources/mesh.h"

class ParticleSeed : public Resource {
	GDCLASS(ParticleSeed, Resource);

	int value = 0;
	bool reset_on_loop = true;
	Vector<int64_t> choices;
	bool random_selection = false;

protected:
	static void _bind_methods();

public:
	void set_value(int p_value) { value = p_value; }
	int get_value() const { return value; }
	void set_reset_on_loop(bool p_value) { reset_on_loop = p_value; }
	bool get_reset_on_loop() const { return reset_on_loop; }
	void set_choices(const Vector<int64_t> & p_value) { choices = p_value; }
	Vector<int64_t> get_choices() const { return choices; }
	void set_random_selection(bool p_value) { random_selection = p_value; }
	bool get_random_selection() const { return random_selection; }
	int64_t select(const Ref<RandomStream32> & p_random) const;
};

class ParticleVelocityRule3D : public Resource {
	GDCLASS(ParticleVelocityRule3D, Resource);

public:
	enum class Operation {
		Unknown,
		Acceleration,
		ConstantAcceleration,
		AccelerationOverLife,
		Scale,
		Drag,
	};

private:
	int operation = 0;
	int index = 0;
	Ref<ParticleVectorCurve3D> vector;
	Ref<ParticleFloatCurve> scalar;
	Vector3 constant;
	bool world_space = false;
	bool apply_owner_scale = false;

protected:
	static void _bind_methods();

public:
	void set_operation(int p_value) { operation = p_value; }
	int get_operation() const { return operation; }
	void set_index(int p_value) { index = p_value; }
	int get_index() const { return index; }
	void set_vector(const Ref<ParticleVectorCurve3D> & p_value) { vector = p_value; }
	Ref<ParticleVectorCurve3D> get_vector() const { return vector; }
	void set_scalar(const Ref<ParticleFloatCurve> & p_value) { scalar = p_value; }
	Ref<ParticleFloatCurve> get_scalar() const { return scalar; }
	void set_constant(const Vector3 & p_value) { constant = p_value; }
	Vector3 get_constant() const { return constant; }
	void set_world_space(bool p_value) { world_space = p_value; }
	bool get_world_space() const { return world_space; }
	void set_apply_owner_scale(bool p_value) { apply_owner_scale = p_value; }
	bool get_apply_owner_scale() const { return apply_owner_scale; }
};

class ParticleSpawnShape3D : public Resource {
	GDCLASS(ParticleSpawnShape3D, Resource);

public:
	enum class Kind {
		Unknown,
		Sphere,
		Cylinder,
	};

private:
	int kind = 0;
	Basis basis = Basis();
	Vector3 center;
	Ref<ParticleVectorCurve3D> center_curve;
	double radius = 0.0;
	Ref<ParticleFloatCurve> radius_curve;
	double half_height = 0.0;
	Ref<ParticleFloatCurve> half_height_curve;
	int height_axis = 1;
	Vector3i positive = Vector3i(1, 1, 1);
	Vector3i negative = Vector3i(1, 1, 1);
	bool surface_only = false;
	double surface_tolerance = 0.000001;
	bool velocity = false;
	bool radial_velocity = false;
	double velocity_scale = 0.0;
	double velocity_scale_variation = 0.0;
	bool random_velocity_scale = false;
	Ref<ParticleFloatCurve> velocity_scale_curve;
	Vector3i direction_order = Vector3i(0, 1, 2);
	Vector3 sample_direction(const Ref<RandomStream32> &p_random) const;

protected:
	static void _bind_methods();

public:
	void set_kind(int p_value) { kind = p_value; }
	int get_kind() const { return kind; }
	void set_basis(const Basis & p_value) { basis = p_value; }
	Basis get_basis() const { return basis; }
	void set_center(const Vector3 & p_value) { center = p_value; }
	Vector3 get_center() const { return center; }
	void set_center_curve(const Ref<ParticleVectorCurve3D> & p_value) { center_curve = p_value; }
	Ref<ParticleVectorCurve3D> get_center_curve() const { return center_curve; }
	void set_radius(double p_value) { radius = p_value; }
	double get_radius() const { return radius; }
	void set_radius_curve(const Ref<ParticleFloatCurve> & p_value) { radius_curve = p_value; }
	Ref<ParticleFloatCurve> get_radius_curve() const { return radius_curve; }
	void set_half_height(double p_value) { half_height = p_value; }
	double get_half_height() const { return half_height; }
	void set_half_height_curve(const Ref<ParticleFloatCurve> & p_value) { half_height_curve = p_value; }
	Ref<ParticleFloatCurve> get_half_height_curve() const { return half_height_curve; }
	void set_height_axis(int p_value) { height_axis = p_value; }
	int get_height_axis() const { return height_axis; }
	void set_positive(const Vector3i & p_value) { positive = p_value; }
	Vector3i get_positive() const { return positive; }
	void set_negative(const Vector3i & p_value) { negative = p_value; }
	Vector3i get_negative() const { return negative; }
	void set_surface_only(bool p_value) { surface_only = p_value; }
	bool get_surface_only() const { return surface_only; }
	void set_surface_tolerance(double p_value) { surface_tolerance = p_value; }
	double get_surface_tolerance() const { return surface_tolerance; }
	void set_velocity(bool p_value) { velocity = p_value; }
	bool get_velocity() const { return velocity; }
	void set_radial_velocity(bool p_value) { radial_velocity = p_value; }
	bool get_radial_velocity() const { return radial_velocity; }
	void set_velocity_scale(double p_value) { velocity_scale = p_value; }
	double get_velocity_scale() const { return velocity_scale; }
	void set_velocity_scale_variation(double p_value) { velocity_scale_variation = p_value; }
	double get_velocity_scale_variation() const { return velocity_scale_variation; }
	void set_random_velocity_scale(bool p_value) { random_velocity_scale = p_value; }
	bool get_random_velocity_scale() const { return random_velocity_scale; }
	void set_velocity_scale_curve(const Ref<ParticleFloatCurve> & p_value) { velocity_scale_curve = p_value; }
	Ref<ParticleFloatCurve> get_velocity_scale_curve() const { return velocity_scale_curve; }
	void set_direction_order(const Vector3i & p_value) { direction_order = p_value; }
	Vector3i get_direction_order() const { return direction_order; }
	Vector3 sample_center(const Ref<RandomStream32> & p_random, double p_emitter_seconds) const;
	Vector3 sample_offset(const Ref<RandomStream32> & p_random, double p_emitter_seconds = 0.0) const;
	Vector3 velocity_at(Vector3 p_offset, const Ref<RandomStream32> & p_random, double p_emitter_seconds = 0.0) const;
};

class ParticleBirthRule3D : public Resource {
	GDCLASS(ParticleBirthRule3D, Resource);

public:
	enum class Target {
		Unknown,
		Lifetime,
		Size,
		Rotation,
		SpriteFrame,
		MeshRotation,
		Velocity,
		Shape,
		MaterialParameter,
		Position,
		Alpha,
		Color,
		MeshAngularVelocity,
		AngularVelocity,
		EmitterPoint,
		Light,
		Acceleration,
		SourceParticle,
		Ground,
	};

private:
public:
	enum class TextureFlip {
		Unknown,
		None,
		Both,
		Horizontal,
		Vertical,
		RandomBoth,
		RandomHorizontal,
		RandomVertical,
		RandomIndependent,
	};

private:
	int target = 0;
	Vector3 value;
	Ref<ParticleVectorCurve3D> value_curve;
	Ref<ParticleVectorCurve3D> maximum_curve;
	Ref<ParticleFloatCurve> scalar_curve;
	Ref<ParticleFloatCurve> scalar_maximum_curve;
	Vector3 variation;
	int axis_lock = 1;
	Ref<ParticleSpawnShape3D> shape;
	int material_channel = 0;
	bool randomized = true;
	bool random_extreme = false;
	bool velocity_in_world_space = false;
	bool velocity_applies_owner_scale = false;
	Ref<ParticleSeed> random_seed;
	int texture_flip = int(TextureFlip::None);
	double source_velocity_scale = 0.0;
	double source_rotation_scale = 0.0;
	bool sequential_source = false;
	Ref<ParticleVelocityRule3D> acceleration;
	Vector3i color_order = Vector3i(0, 1, 2);

protected:
	static void _bind_methods();

public:
	void set_target(int p_value) { target = p_value; }
	int get_target() const { return target; }
	void set_value(const Vector3 & p_value) { value = p_value; }
	Vector3 get_value() const { return value; }
	void set_value_curve(const Ref<ParticleVectorCurve3D> & p_value) { value_curve = p_value; }
	Ref<ParticleVectorCurve3D> get_value_curve() const { return value_curve; }
	void set_maximum_curve(const Ref<ParticleVectorCurve3D> & p_value) { maximum_curve = p_value; }
	Ref<ParticleVectorCurve3D> get_maximum_curve() const { return maximum_curve; }
	void set_scalar_curve(const Ref<ParticleFloatCurve> & p_value) { scalar_curve = p_value; }
	Ref<ParticleFloatCurve> get_scalar_curve() const { return scalar_curve; }
	void set_scalar_maximum_curve(const Ref<ParticleFloatCurve> & p_value) { scalar_maximum_curve = p_value; }
	Ref<ParticleFloatCurve> get_scalar_maximum_curve() const { return scalar_maximum_curve; }
	void set_variation(const Vector3 & p_value) { variation = p_value; }
	Vector3 get_variation() const { return variation; }
	void set_axis_lock(int p_value) { axis_lock = p_value; }
	int get_axis_lock() const { return axis_lock; }
	void set_shape(const Ref<ParticleSpawnShape3D> & p_value) { shape = p_value; }
	Ref<ParticleSpawnShape3D> get_shape() const { return shape; }
	void set_material_channel(int p_value) { material_channel = p_value; }
	int get_material_channel() const { return material_channel; }
	void set_randomized(bool p_value) { randomized = p_value; }
	bool get_randomized() const { return randomized; }
	void set_random_extreme(bool p_value) { random_extreme = p_value; }
	bool get_random_extreme() const { return random_extreme; }
	void set_velocity_in_world_space(bool p_value) { velocity_in_world_space = p_value; }
	bool get_velocity_in_world_space() const { return velocity_in_world_space; }
	void set_velocity_applies_owner_scale(bool p_value) { velocity_applies_owner_scale = p_value; }
	bool get_velocity_applies_owner_scale() const { return velocity_applies_owner_scale; }
	void set_random_seed(const Ref<ParticleSeed> & p_value) { random_seed = p_value; }
	Ref<ParticleSeed> get_random_seed() const { return random_seed; }
	void set_texture_flip(int p_value) { texture_flip = p_value; }
	int get_texture_flip() const { return texture_flip; }
	void set_source_velocity_scale(double p_value) { source_velocity_scale = p_value; }
	double get_source_velocity_scale() const { return source_velocity_scale; }
	void set_source_rotation_scale(double p_value) { source_rotation_scale = p_value; }
	double get_source_rotation_scale() const { return source_rotation_scale; }
	void set_sequential_source(bool p_value) { sequential_source = p_value; }
	bool get_sequential_source() const { return sequential_source; }
	void set_acceleration(const Ref<ParticleVelocityRule3D> & p_value) { acceleration = p_value; }
	Ref<ParticleVelocityRule3D> get_acceleration() const { return acceleration; }
	void set_color_order(const Vector3i & p_value) { color_order = p_value; }
	Vector3i get_color_order() const { return color_order; }
	double sample_scalar(const Ref<RandomStream32> & p_random, double p_emitter_seconds = 0.0) const;
	Vector3 sample_vector(const Ref<RandomStream32> & p_random, double p_emitter_seconds = 0.0) const;
	Vector3 sample_texture_sign(const Ref<RandomStream32> & p_random) const;
	Vector3 sample_velocity(const Ref<RandomStream32> & p_random, const Basis & p_emitter_basis, bool p_local_space, double p_emitter_seconds = 0.0) const;
};
