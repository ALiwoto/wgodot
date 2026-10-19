// wgodot-changes::file
#include "particle_instance_3d.h"

#include <cmath>

void ParticleInstance3D::copy_source_state(ParticleInstance3D *p_source, bool p_spawning) {
	Vector3 location = p_source->state_position;
	Vector3 velocity = p_source->state_velocity;
	if (p_source->sprite_motion.is_valid() && !p_source->follows_emitter) {
		location = p_source->get_particle_transform().xform(location);
		velocity = p_source->get_particle_transform().basis.xform(velocity);
	}
	if (!follows_emitter) {
		if (p_spawning) {
			const Transform3D frame = get_particle_transform().orthonormalized();
			location = frame.affine_inverse().xform(location);
			velocity = frame.basis.inverse().xform(velocity);
		} else if (sprite_motion.is_valid()) {
			location = get_particle_transform().affine_inverse().xform(location);
			velocity = get_particle_transform().basis.inverse().xform(velocity);
		}
	}
	state_position = location;
	state_velocity = velocity;
	state_relative_age = p_source->state_relative_age;
}

bool ParticleInstance3D::project_ground(ParticleGroundProjector3D *p_ground, double p_task_ratio, bool p_spawning) {
	Transform3D frame = get_particle_transform();
	if (p_spawning && !follows_emitter) {
		frame.orthonormalize();
	} else if (motion.is_valid() && !follows_emitter) {
		frame = Transform3D();
	}
	if (!p_ground->project(frame.xform(state_position))) {
		return false;
	}
	state_position = frame.affine_inverse().xform(p_ground->projected_position(ground_random, state_relative_age, p_task_ratio));
	return true;
}

void ParticleInstance3D::set_frame(double p_frame) {
	uniform(bindings()->sprite_frame, p_frame);
	if (!sprite_motion->get_frames().is_empty()) {
		MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(geometry);
		mesh->set_mesh(sprite_motion->get_frames()[int(p_frame)]);
	}
}

void ParticleInstance3D::prepare_orbit(const Ref<ParticleMotionProfile3D> &p_trajectory, double p_emitter_seconds) {
	sprite_random->reset(use_fixed_seed ? random_seed : Math::rand());
	state_render_orbit_offset = Vector3();
	if (p_trajectory.is_null() || !p_trajectory->get_orbit()) {
		return;
	}
	state_orbit_offset = p_trajectory->sample_orbit_offset(sprite_random, p_emitter_seconds);
	state_orbit_rotation = p_trajectory->sample_orbit_rotation(sprite_random, p_emitter_seconds);
	state_base_orbit_rate = Vector3();
	if (p_trajectory->get_orbit_rate_on_spawn()) {
		state_base_orbit_rate = p_trajectory->sample_orbit_rate(p_trajectory->get_orbit_rate_emitter_clock() ? p_emitter_seconds : state_relative_age, sprite_random);
	}
}

Vector3 ParticleInstance3D::advance_orbit(const Ref<ParticleMotionProfile3D> &p_trajectory, double p_relative_age, double p_delta, bool p_spawned_now, double p_emitter_seconds) {
	if (!p_trajectory->get_orbit()) {
		return Vector3();
	}
	state_orbit_rate = state_base_orbit_rate;
	if (!p_spawned_now && p_trajectory->get_orbit_rate_on_update()) {
		state_orbit_rate += p_trajectory->sample_orbit_rate(p_trajectory->get_orbit_rate_emitter_clock() ? p_emitter_seconds : p_relative_age, sprite_random);
	}
	state_orbit_rotation += state_orbit_rate * p_delta;
	Vector3 orbit_offset = state_orbit_offset;
	if (!p_spawned_now && p_trajectory->get_orbit_offset_over_life().is_valid()) {
		orbit_offset += p_trajectory->get_orbit_offset_over_life()->sample(p_relative_age);
	}
	state_render_orbit_offset = p_trajectory->rotate_orbit_offset(orbit_offset, state_orbit_rotation);
	return state_render_orbit_offset;
}

void ParticleInstance3D::advance_sprite(double p_relative_age, double p_delta, bool p_spawned_now, double p_simulation_delta, const Ref<RandomStream32> &p_random, ParticleInstance3D *p_linked_particle, double p_emitter_seconds, double p_parameter_ratio) {
	const Vector3 position_before_motion = state_position;
	if (!p_spawned_now) {
		state_velocity = state_base_velocity;
		update_motion_velocity(p_relative_age, p_simulation_delta, p_emitter_seconds, p_parameter_ratio);
		if (p_linked_particle) {
			copy_source_state(p_linked_particle, false);
		}
	}
	const Vector3 spawn_displacement = p_spawned_now ? state_velocity * p_delta : Vector3();
	const double step = p_spawned_now && !advance_on_spawn ? 0.0 : p_simulation_delta;
	Vector3 displacement = state_velocity * step;
	if (sprite_motion->get_position_over_life().is_valid() && !p_linked_particle) {
		Vector3 next = sprite_motion->get_position_over_life()->sample(p_relative_age, location_random);
		next.y += state_emitter_ground_offset;
		if (!follows_emitter) {
			next = get_particle_transform().affine_inverse().xform(get_emitter_transform().xform(next) + sprite_motion->get_position_offset());
		} else {
			next += sprite_motion->get_position_offset();
		}
		displacement = next - state_position;
		if (!p_spawned_now) {
			state_velocity = p_delta > 0.0 ? displacement * sprite_motion->direct_velocity_at(p_relative_age) / p_delta : Vector3();
		}
		state_position = next;
		if (p_spawned_now) {
			state_position += spawn_displacement + state_velocity * step;
		}
	} else {
		state_position += spawn_displacement + displacement;
	}
	// Keep the simulation phase in binary32, like its shader input. Wrapping
	// each step avoids losing the visible phase after many fast revolutions.
	const float angular_velocity = sprite_motion->angular_velocity_at(p_relative_age, state_angular_speed, rotation_random);
	const float angle = float(state_angle) + angular_velocity * float(step);
	const float turns = std::trunc(angle / float(Math::TAU));
	const float whole_turns = turns * float(Math::TAU);
	state_angle = angle - (Math::abs(whole_turns) > Math::abs(angle) ? angle : whole_turns);
	if (sprite_motion->get_source_movement().is_valid()) {
		inherit_source_movement(sprite_motion->get_source_movement(), p_relative_age, p_simulation_delta);
	}
	const Vector3 previous_orbit = state_render_orbit_offset;
	const Vector3 orbit_position = advance_orbit(sprite_motion, p_relative_age, p_simulation_delta, p_spawned_now, p_emitter_seconds);
	displacement += orbit_position - previous_orbit;
	pose->set_position(state_position + orbit_position);
	if (light && light_parameters->get_high_quality() && light_parameters->get_sample_position_before_motion() && !p_spawned_now) {
		pose->set_position(position_before_motion);
	}
	if (!geometry) {
		return;
	}
	uniform(bindings()->sprite_size, sprite_motion->size_at(source_size_clock ? state_relative_age : p_relative_age, state_size, size_random, size_life_random, p_spawned_now, state_velocity));
	uniform(bindings()->angle, state_angle);
	if (sprite_motion->get_frame_over_life().is_valid() && (p_spawned_now || p_relative_age <= 1.0)) {
		set_frame(sprite_motion->sample_frame(p_relative_age, frame_random));
	} else if (sprite_motion->get_change_random_frame() && !p_spawned_now && p_relative_age <= 1.0) {
		if (sprite_motion->get_frame_change_interval() == 0.0 || state_last_frame_change == 0.0 || p_relative_age - state_last_frame_change > sprite_motion->get_frame_change_interval()) {
			const double frame_range = float(double(sprite_motion->get_random_frame_count()) - 0.00001);
			set_frame(int(p_random->next_float() * frame_range));
			state_last_frame_change = p_relative_age;
		}
	}
	if (sprite_motion->get_align_to_velocity()) {
		uniform(bindings()->velocity_direction, displacement);
	}
}

void ParticleInstance3D::inherit_source_movement(const Ref<ParticleVectorCurve3D> &p_curve, double p_relative_age, double p_delta) {
	const Transform3D emitter = get_emitter_transform();
	Vector3 movement = emitter.origin - state_previous_emitter_position;
	state_previous_emitter_position = emitter.origin;
	const Vector2 ages(p_relative_age, (p_delta + p_delta) * state_inverse_lifetime);
	if (ages.x > ages.y) {
		movement *= p_curve->sample(p_relative_age);
		state_position += follows_emitter || motion.is_valid() ? movement : get_particle_transform().basis.inverse().xform(movement);
	}
}

Vector3 ParticleInstance3D::acceleration_in_simulation(Vector3 p_value, bool p_world_space) {
	if (follows_emitter) {
		return p_world_space ? get_emitter_transform().basis.inverse().xform(p_value) : p_value;
	}
	return sprite_motion.is_valid() ? get_particle_transform().basis.inverse().xform(p_value) : p_value;
}

void ParticleInstance3D::add_acceleration(Vector3 p_value, double p_delta) {
	const Vector3 change = p_value * p_delta;
	state_velocity += change;
	state_base_velocity += change;
}

void ParticleInstance3D::apply_accelerations_on_spawn(double p_age) {
	using Operation = ParticleVelocityRule3D::Operation;
	for (int index = 0; index < velocity_modifiers.size(); ++index) {
		const Ref<ParticleVelocityRule3D> modifier = velocity_modifiers[index];
		switch (Operation(modifier->get_operation())) {
			case Operation::Acceleration:
				add_acceleration(acceleration_in_simulation(accelerations[modifier->get_index()], modifier->get_world_space()), p_age);
				break;
			case Operation::ConstantAcceleration:
				add_acceleration(acceleration_in_simulation(modifier->get_constant(), modifier->get_world_space()), p_age);
				break;
			default:
				break;
		}
	}
}

void ParticleInstance3D::update_motion_velocity(double p_relative_age, double p_delta, double p_emitter_seconds, double p_parameter_ratio) {
	using Operation = ParticleVelocityRule3D::Operation;
	for (int index = 0; index < velocity_modifiers.size(); ++index) {
		const Ref<ParticleVelocityRule3D> modifier = velocity_modifiers[index];
		switch (Operation(modifier->get_operation())) {
			case Operation::Acceleration:
				add_acceleration(acceleration_in_simulation(accelerations[modifier->get_index()], modifier->get_world_space()), p_delta);
				break;
			case Operation::ConstantAcceleration:
				add_acceleration(acceleration_in_simulation(modifier->get_constant(), modifier->get_world_space()), p_delta);
				break;
			case Operation::AccelerationOverLife:
				add_acceleration(acceleration_in_simulation(modifier->get_vector()->sample(p_relative_age), modifier->get_world_space()), p_delta);
				break;
			case Operation::Scale:
				update_velocity(modifier->sample_scale(p_relative_age, get_emitter_transform().basis, follows_emitter));
				break;
			case Operation::Drag: {
				const double coefficient = modifier->get_scalar()->sample(p_relative_age, 0.0, drag_random);
				const Vector3 change = (state_velocity * -coefficient) * p_delta;
				state_velocity += change;
				state_base_velocity += change;
			} break;
			case Operation::PointAttraction:
				apply_point_attraction(modifier, p_delta, p_emitter_seconds, p_parameter_ratio);
				break;
			default:
				break;
		}
	}
}

void ParticleInstance3D::apply_point_attraction(const Ref<ParticleVelocityRule3D> &p_rule, double p_delta, double p_emitter_seconds, double p_parameter_ratio) {
	Vector3 target = p_rule->get_vector()->sample(p_emitter_seconds);
	Vector3 position = state_position;
	Vector3 scale(1, 1, 1);
	const bool transform_target = !follows_emitter && !p_rule->get_world_space();
	const bool transform_particle = !follows_emitter && sprite_motion.is_valid();
	if (transform_target) {
		const Transform3D emitter = get_emitter_transform();
		target = emitter.xform(target);
		scale = emitter.basis.get_scale();
	}
	if (transform_particle) {
		position = get_particle_transform().xform(position);
	}
	const float scale_length = scale.length();
	const float range = float(p_rule->get_range()->sample(p_emitter_seconds, p_parameter_ratio)) * scale_length;
	Vector3 direction = target - position;
	const float squared_distance = direction.length_squared();
	const float distance = Math::sqrt(squared_distance);
	if (distance > range) {
		return;
	}
	float strength = 0.0f;
	if (!p_rule->get_strength_by_distance() || range != 0.0f) {
		const float time = p_rule->get_strength_by_distance() ? (range - distance) / range : float(p_emitter_seconds);
		strength = p_rule->get_scalar()->sample(time, p_parameter_ratio, attraction_random);
	}
	if (transform_target) {
		strength *= scale_length;
	}
	if (squared_distance > p_rule->get_normalization_threshold_squared()) {
		direction /= distance;
	}
	if (strength < 0.0f) {
		direction = -direction;
		strength = -strength;
	}
	const Vector3i positive = p_rule->get_positive();
	const Vector3i negative = p_rule->get_negative();
	for (int axis = 0; axis < 3; ++axis) {
		direction[axis] = CLAMP(direction[axis], -float(negative[axis]), float(positive[axis]));
	}
	Vector3 change = (direction * strength) * float(p_delta);
	if (transform_particle) {
		change = get_particle_transform().basis.inverse().xform(change);
	}
	state_velocity += change;
	if (p_rule->get_affect_base_velocity()) {
		state_base_velocity += change;
	}
}

void ParticleInstance3D::update_velocity(Vector3 p_multiplier) {
	if (follows_emitter || motion.is_valid()) {
		state_velocity *= p_multiplier;
	} else {
		const Basis basis = get_particle_transform().basis;
		state_velocity = basis.inverse().xform(basis.xform(state_velocity) * p_multiplier);
	}
}
