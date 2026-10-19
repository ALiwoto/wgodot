// wgodot-changes::file
#include "particle_motion.h"

namespace {

void orbit_sin_cos(float p_turns, float &r_sin, float &r_cos) {
	// Authored turns pass through degrees in binary32. Reduce the accumulated
	// angle before trigonometry so many revolutions retain the same phase.
	const float degrees = p_turns * 360.0f;
	const float radians = degrees * float(Math::PI / 180.0);
	const float revolutions = std::nearbyint(radians * float(1.0 / Math::TAU));
	const float reduced = radians - revolutions * float(Math::TAU);
	r_sin = Math::sin(reduced);
	r_cos = Math::cos(reduced);
}

} // namespace

double ParticleMotionProfile3D::direct_velocity_at(double p_relative_age) const {
	return direct_velocity_scale.is_valid() ? direct_velocity_scale->sample(p_relative_age) : 1.0;
}

Vector3 ParticleMotionProfile3D::scale_size_over_life(Vector3 p_size, Vector3 p_initial_size, double p_relative_age, const Ref<RandomStream32> &p_random, bool p_spawning, Vector3 p_velocity) const {
	for (int index = 0; index <= size_over_life.size(); ++index) {
		if (size_by_speed && !p_spawning && index == size_speed_life_start) {
			const real_t speed = p_velocity.length();
			p_size = p_initial_size.abs();
			p_size.x *= MIN(MAX(speed * size_speed_scale.x, real_t(1)), size_speed_maximum.x);
			p_size.z *= MIN(MAX(speed * size_speed_scale.y, real_t(1)), size_speed_maximum.y);
		}
		if (index == size_over_life.size()) {
			break;
		}
		const Ref<ParticleVectorCurve3D> curve = size_over_life[index];
		const Vector3 multiplier = curve->sample(p_relative_age, p_random);
		if (!p_spawning || index >= size_life_spawn_start) {
			p_size *= multiplier;
		}
	}
	return p_size;
}

Vector3 ParticleMotionProfile3D::sample_orbit_offset(const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	if (orbit_offset_over_emission.is_valid()) {
		return orbit_offset_over_emission->sample(p_emitter_seconds, p_random);
	}
	if (!random_orbit_offset) {
		return orbit_offset;
	}
	return orbit_offset + orbit_offset_variation * p_random->next_weight(orbit_offset_axis_lock);
}

Vector3 ParticleMotionProfile3D::sample_orbit_rotation(const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	if (orbit_rotation_over_emission.is_valid()) {
		return orbit_rotation_over_emission->sample(p_emitter_seconds, p_random);
	}
	if (!random_orbit_rotation) {
		return orbit_rotation;
	}
	return orbit_rotation + orbit_rotation_variation * p_random->next_weight(orbit_rotation_axis_lock);
}

Vector3 ParticleMotionProfile3D::sample_orbit_rate(double p_time, const Ref<RandomStream32> &p_random) const {
	const Vector3 minimum = orbit_rate->sample(p_time, p_random);
	if (!random_orbit_rate) {
		return minimum;
	}
	return minimum + (orbit_rate_maximum->sample(p_time) - minimum) * p_random->next_weight(orbit_rate_axis_lock);
}

Vector3 ParticleMotionProfile3D::rotate_orbit_offset(Vector3 p_offset, Vector3 p_turns) const {
	const Vector3 magnitude = p_turns.abs();
	if (magnitude[magnitude.max_axis_index()] <= orbit_rotation_threshold) {
		return p_offset;
	}
	float sx, cx, sy, cy, sz, cz;
	orbit_sin_cos(p_turns.x, sx, cx);
	orbit_sin_cos(-p_turns.y, sy, cy);
	orbit_sin_cos(p_turns.z, sz, cz);
	const Basis rotation(
			cy * cz, sy * sx - cy * sz * cx, cy * sz * sx + sy * cx,
			sz, cz * cx, -cz * sx,
			-sy * cz, sy * sz * cx + cy * sx, cy * cx - sy * sz * sx);
	return rotation.xform(p_offset);
}

Vector2 ParticleSpriteProfile3D::size_at(double p_relative_age, Vector3 p_initial_size, const Ref<RandomStream32> &p_random, const Ref<RandomStream32> &p_life_random, bool p_spawning, Vector3 p_velocity) const {
	Vector3 scale = p_initial_size;
	if (get_size_scale().is_valid()) {
		Vector3 multiplier = get_size_scale()->sample(p_relative_age, p_random);
		if (size_scale_maximum.is_valid()) {
			multiplier += (size_scale_maximum->sample(p_relative_age) - multiplier) * p_random->next_weight(size_scale_axis_lock);
		}
		scale = scale.abs() * multiplier;
	}
	scale = scale_size_over_life(scale, p_initial_size, p_relative_age, p_life_random, p_spawning, p_velocity);
	return Vector2(scale.x, square ? scale.x : scale.z);
}

double ParticleSpriteProfile3D::angular_velocity_at(double p_relative_age, double p_initial_rate, const Ref<RandomStream32> &p_random) const {
	return p_initial_rate * angular_velocity_over_life->sample(p_relative_age, 0.0, p_random);
}

double ParticleSpriteProfile3D::sample_frame(double p_relative_age, const Ref<RandomStream32> &p_random) const {
	double frame = frame_over_life->sample(p_relative_age);
	if (frame_over_life_maximum.is_valid()) {
		frame = Math::lerp(frame, frame_over_life_maximum->sample(p_relative_age), p_random->next_float());
	}
	return fractional_frame ? frame : double(int(frame));
}

Vector3 ParticleMeshProfile3D::velocity_rotation(Vector3 p_direction_velocity) const {
	const Vector3 direction = p_direction_velocity.normalized();
	const double weight = 1.0 + direction.x;
	const Quaternion orientation = weight < 0.000001 ? Quaternion(0.0, 1.0, 0.0, 0.0) : Quaternion(0.0, -direction.z, direction.y, weight).normalized();
	return Basis(orientation).get_euler(EulerOrder::YZX);
}

Vector3 ParticleMeshProfile3D::scale_at(double p_relative_age, Vector3 p_initial_size, const Ref<RandomStream32> &p_random, const Ref<RandomStream32> &p_life_random, bool p_spawning, Vector3 p_velocity) const {
	Vector3 scale = p_initial_size;
	if (get_size_scale().is_valid()) {
		scale = scale.abs() * get_size_scale()->sample(p_relative_age, p_random);
	}
	scale = scale_size_over_life(scale, p_initial_size, p_relative_age, p_life_random, p_spawning, p_velocity);
	for (int axis : flattened_axes) {
		scale[axis] = 1.0;
	}
	return scale;
}

Vector3 ParticleMeshProfile3D::angular_velocity_at(double p_relative_age, Vector3 p_initial_rate) const {
	return rotation_rate_over_life.is_valid() ? p_initial_rate * rotation_rate_over_life->sample(p_relative_age) : p_initial_rate;
}
