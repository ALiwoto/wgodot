// wgodot-changes::file
#include "bone_lag_modifier_3d.h"

#include "core/object/class_db.h"
#include "scene/3d/skeleton_3d.h"

void BoneLagSimulationState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_history", "previous_pose", "previous_target"), &BoneLagSimulationState::set_history);
}

Error BoneLagSimulationState::set_history(const Transform3D &p_previous_pose, const Vector3 &p_previous_target) {
	ERR_FAIL_COND_V(!p_previous_pose.is_finite() || !p_previous_target.is_finite(), ERR_INVALID_PARAMETER);
	previous_pose = p_previous_pose;
	previous_target = p_previous_target;
	initialized = true;
	return OK;
}

Ref<BoneLagSimulationState> BoneLagModifier3D::capture_state() const {
	Ref<BoneLagSimulationState> state;
	state.instantiate();
	state->initialized = initialized;
	state->previous_pose = Transform3D(Basis::from_euler(previous_rotation, interpolation_rotation_order), previous_position);
	state->previous_target = previous_target;
	return state;
}

Error BoneLagModifier3D::restore_state(const Ref<BoneLagSimulationState> &p_state, const Transform3D &p_frame_change) {
	ERR_FAIL_COND_V(p_state.is_null() || !p_frame_change.is_finite(), ERR_INVALID_PARAMETER);
	initialized = p_state->initialized;
	const Transform3D pose = p_frame_change * p_state->previous_pose;
	previous_position = pose.origin;
	previous_rotation = pose.basis.orthonormalized().get_euler(interpolation_rotation_order);
	previous_target = p_frame_change.xform(p_state->previous_target);
	return OK;
}

void BoneLagModifier3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bone_name", "name"), &BoneLagModifier3D::set_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_name"), &BoneLagModifier3D::get_bone_name);
	ClassDB::bind_method(D_METHOD("set_position_speed", "speed"), &BoneLagModifier3D::set_position_speed);
	ClassDB::bind_method(D_METHOD("get_position_speed"), &BoneLagModifier3D::get_position_speed);
	ClassDB::bind_method(D_METHOD("set_rotation_speed", "speed"), &BoneLagModifier3D::set_rotation_speed);
	ClassDB::bind_method(D_METHOD("get_rotation_speed"), &BoneLagModifier3D::get_rotation_speed);
	ClassDB::bind_method(D_METHOD("set_max_step", "seconds"), &BoneLagModifier3D::set_max_step);
	ClassDB::bind_method(D_METHOD("get_max_step"), &BoneLagModifier3D::get_max_step);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &BoneLagModifier3D::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &BoneLagModifier3D::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_interpolation_rotation_order", "order"), &BoneLagModifier3D::set_interpolation_rotation_order);
	ClassDB::bind_method(D_METHOD("get_interpolation_rotation_order"), &BoneLagModifier3D::get_interpolation_rotation_order);
	ClassDB::bind_method(D_METHOD("set_substepping_enabled", "enabled"), &BoneLagModifier3D::set_substepping_enabled);
	ClassDB::bind_method(D_METHOD("is_substepping_enabled"), &BoneLagModifier3D::is_substepping_enabled);
	ClassDB::bind_method(D_METHOD("set_reset_on_activation", "enabled"), &BoneLagModifier3D::set_reset_on_activation);
	ClassDB::bind_method(D_METHOD("is_reset_on_activation_enabled"), &BoneLagModifier3D::is_reset_on_activation_enabled);
	ClassDB::bind_method(D_METHOD("has_motion_history"), &BoneLagModifier3D::has_motion_history);
	ClassDB::bind_method(D_METHOD("capture_state"), &BoneLagModifier3D::capture_state);
	ClassDB::bind_method(D_METHOD("restore_state", "state", "frame_change"), &BoneLagModifier3D::restore_state, DEFVAL(Transform3D()));
	ClassDB::bind_method(D_METHOD("reset"), &BoneLagModifier3D::reset);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bone_name"), "set_bone_name", "get_bone_name");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "position_speed", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_position_speed", "get_position_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_speed", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_rotation_speed", "get_rotation_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "substepping_enabled"), "set_substepping_enabled", "is_substepping_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reset_on_activation"), "set_reset_on_activation", "is_reset_on_activation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_step", PROPERTY_HINT_RANGE, "0.001,1,0.001,suffix:s"), "set_max_step", "get_max_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0,100,0.001,or_greater,suffix:m"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "interpolation_rotation_order", PROPERTY_HINT_ENUM, "XYZ,XZY,YXZ,YZX,ZXY,ZYX"), "set_interpolation_rotation_order", "get_interpolation_rotation_order");
}

void BoneLagModifier3D::set_bone_name(const StringName &p_name) {
	bone_name = p_name;
	_validate_bone_names();
	reset();
}

void BoneLagModifier3D::set_position_speed(real_t p_speed) {
	ERR_FAIL_COND(p_speed < 0.0);
	position_speed = p_speed;
}

void BoneLagModifier3D::set_rotation_speed(real_t p_speed) {
	ERR_FAIL_COND(p_speed < 0.0);
	rotation_speed = p_speed;
}

void BoneLagModifier3D::set_max_step(real_t p_step) {
	ERR_FAIL_COND(p_step <= 0.0);
	max_step = p_step;
}

void BoneLagModifier3D::set_max_distance(real_t p_distance) {
	ERR_FAIL_COND(p_distance < 0.0);
	max_distance = p_distance;
}

void BoneLagModifier3D::set_interpolation_rotation_order(EulerOrder p_order) {
	ERR_FAIL_COND(p_order < EulerOrder::XYZ || p_order > EulerOrder::ZYX);
	interpolation_rotation_order = p_order;
	reset();
}

void BoneLagModifier3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	reset();
}

void BoneLagModifier3D::_validate_bone_names() {
	Skeleton3D *skeleton = get_skeleton();
	bone = skeleton ? skeleton->find_bone(bone_name) : -1;
}

void BoneLagModifier3D::_set_active(bool p_active) {
	if (p_active && reset_on_activation) {
		reset();
	}
}

void BoneLagModifier3D::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton || bone < 0) {
		return;
	}
	const Transform3D component = skeleton->get_global_transform();
	const Transform3D target = component * skeleton->get_bone_global_pose(bone);
	const Vector3 target_rotation = target.basis.orthonormalized().get_euler(interpolation_rotation_order);
	if (!initialized) {
		previous_position = target.origin;
		previous_target = target.origin;
		previous_rotation = target_rotation;
		initialized = true;
		return;
	}
	if (p_delta > 0.0) {
		real_t remaining = p_delta;
		Vector3 position_target = previous_target;
		Vector3 rotation_target = previous_rotation;
		Vector3 rotation_step;
		for (int axis = 0; axis < 3; axis++) {
			rotation_step[axis] = Math::wrapf(target_rotation[axis] - previous_rotation[axis], real_t(-Math::PI), real_t(Math::PI)) / p_delta;
		}
		const Vector3 position_step = (target.origin - previous_target) / p_delta;
		do {
			const real_t step = substepping_enabled ? MIN(remaining, max_step) : remaining;
			position_target += position_step * step;
			rotation_target += rotation_step * step;
			previous_position = position_speed > 0.0 ? previous_position.lerp(position_target, CLAMP(step * position_speed, 0.0, 1.0)) : target.origin;
			const Quaternion from = Basis::from_euler(previous_rotation, interpolation_rotation_order).get_rotation_quaternion();
			const Quaternion to = Basis::from_euler(rotation_target, interpolation_rotation_order).get_rotation_quaternion();
			previous_rotation = rotation_speed > 0.0 ? Basis(from.slerp(to, CLAMP(step * rotation_speed, 0.0, 1.0))).get_euler(interpolation_rotation_order) : target_rotation;
			remaining -= step;
		} while (remaining > 0.0001);
	}
	if (max_distance > 0.0) {
		previous_position = target.origin + (previous_position - target.origin).limit_length(max_distance);
	}
	previous_target = target.origin;
	Transform3D output(Basis::from_euler(previous_rotation, interpolation_rotation_order).scaled_local(target.basis.get_scale()), previous_position);
	skeleton->set_bone_global_pose(bone, component.affine_inverse() * output);
}
