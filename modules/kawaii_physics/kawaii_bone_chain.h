// wgodot-changes::file
#pragma once

#include "kawaii_bone_collision.h"

#include "core/io/resource.h"
#include "core/variant/typed_array.h"

class KawaiiBoneChain : public Resource {
	GDCLASS(KawaiiBoneChain, Resource);

public:
	enum PlanarConstraint {
		PLANAR_CONSTRAINT_NONE,
		PLANAR_CONSTRAINT_X,
		PLANAR_CONSTRAINT_Y,
		PLANAR_CONSTRAINT_Z,
	};

private:
	StringName root_bone;
	PackedStringArray excluded_bones;
	real_t damping = 0.1;
	real_t world_damping_translation = 0.8;
	real_t world_damping_rotation = 0.8;
	real_t stiffness = 0.05;
	real_t radius = 0.03;
	real_t limit_angle = 0.0;
	int target_framerate = 60;
	real_t teleport_distance = 3.0;
	real_t teleport_rotation = 10.0;
	Vector3 gravity;
	bool wind_enabled = true;
	real_t wind_scale = 1.0;
	real_t dummy_length = 0.0;
	Vector3 forward_axis = Vector3(1, 0, 0);
	PlanarConstraint planar_constraint = PLANAR_CONSTRAINT_NONE;
	TypedArray<KawaiiBoneCollision> collisions;
	void _collision_changed();

protected:
	static void _bind_methods();

public:
	void set_root_bone(const StringName &p_value);
	StringName get_root_bone() const;
	void set_excluded_bones(const PackedStringArray &p_value);
	PackedStringArray get_excluded_bones() const;
	void set_damping(real_t p_value);
	real_t get_damping() const;
	void set_world_damping_translation(real_t p_value);
	real_t get_world_damping_translation() const;
	void set_world_damping_rotation(real_t p_value);
	real_t get_world_damping_rotation() const;
	void set_stiffness(real_t p_value);
	real_t get_stiffness() const;
	void set_radius(real_t p_value);
	real_t get_radius() const;
	void set_limit_angle(real_t p_value);
	real_t get_limit_angle() const;
	void set_target_framerate(int p_value);
	int get_target_framerate() const;
	void set_teleport_distance(real_t p_value);
	real_t get_teleport_distance() const;
	void set_teleport_rotation(real_t p_value);
	real_t get_teleport_rotation() const;
	void set_gravity(const Vector3 &p_value);
	Vector3 get_gravity() const;
	void set_wind_enabled(bool p_value);
	bool get_wind_enabled() const;
	void set_wind_scale(real_t p_value);
	real_t get_wind_scale() const;
	void set_dummy_length(real_t p_value);
	real_t get_dummy_length() const;
	void set_forward_axis(const Vector3 &p_value);
	Vector3 get_forward_axis() const;
	void set_planar_constraint(PlanarConstraint p_value);
	PlanarConstraint get_planar_constraint() const;
	void set_collisions(const TypedArray<KawaiiBoneCollision> &p_value);
	TypedArray<KawaiiBoneCollision> get_collisions() const;
};

VARIANT_ENUM_CAST(KawaiiBoneChain::PlanarConstraint);
