// wgodot-changes::file
#pragma once

#include "scene/3d/skeleton_modifier_3d.h"

class BoneLagSimulationState : public RefCounted {
	GDCLASS(BoneLagSimulationState, RefCounted);
	friend class BoneLagModifier3D;

	bool initialized = false;
	Transform3D previous_pose;
	Vector3 previous_target;

protected:
	static void _bind_methods();

public:
	Error set_history(const Transform3D &p_previous_pose, const Vector3 &p_previous_target);
};

// World-space trailing motion for a bone and its descendants. Rates and
// substeps are independent of the animation's playback rate.
class BoneLagModifier3D : public SkeletonModifier3D {
	GDCLASS(BoneLagModifier3D, SkeletonModifier3D);

	StringName bone_name;
	int bone = -1;
	real_t position_speed = 10.0;
	real_t rotation_speed = 10.0;
	real_t max_step = 1.0 / 60.0;
	real_t max_distance = 0.0;
	EulerOrder interpolation_rotation_order = EulerOrder::YXZ;
	bool substepping_enabled = true;
	bool reset_on_activation = true;
	bool initialized = false;
	Vector3 previous_position;
	Vector3 previous_target;
	Vector3 previous_rotation;

protected:
	static void _bind_methods();
	void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;
	void _validate_bone_names() override;
	void _set_active(bool p_active) override;
	void _process_modification(double p_delta) override;

public:
	void set_bone_name(const StringName &p_name);
	StringName get_bone_name() const { return bone_name; }
	void set_position_speed(real_t p_speed);
	real_t get_position_speed() const { return position_speed; }
	void set_rotation_speed(real_t p_speed);
	real_t get_rotation_speed() const { return rotation_speed; }
	void set_max_step(real_t p_step);
	real_t get_max_step() const { return max_step; }
	void set_max_distance(real_t p_distance);
	real_t get_max_distance() const { return max_distance; }
	void set_interpolation_rotation_order(EulerOrder p_order);
	EulerOrder get_interpolation_rotation_order() const { return interpolation_rotation_order; }
	void set_substepping_enabled(bool p_enabled) { substepping_enabled = p_enabled; }
	bool is_substepping_enabled() const { return substepping_enabled; }
	void set_reset_on_activation(bool p_enabled) { reset_on_activation = p_enabled; }
	bool is_reset_on_activation_enabled() const { return reset_on_activation; }
	bool has_motion_history() const { return initialized; }
	Ref<BoneLagSimulationState> capture_state() const;
	Error restore_state(const Ref<BoneLagSimulationState> &p_state, const Transform3D &p_frame_change = Transform3D());
	void reset() { initialized = false; }
	bool has_process() const override { return true; }
};
