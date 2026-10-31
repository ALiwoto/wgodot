// wgodot-changes::file
#include "particle_spawn.h"

#include <cmath>

Vector3 ParticleVelocityRule3D::sample_scale(double p_relative_age, Basis p_emitter_basis, bool p_local_space) const {
	Vector3 multiplier = vector->sample(p_relative_age);
	const Basis rotation = p_emitter_basis.orthonormalized();
	if (world_space && p_local_space) {
		multiplier = rotation.inverse().xform(multiplier);
	} else if (!world_space && !p_local_space) {
		multiplier = rotation.xform(multiplier);
	}
	if (apply_owner_scale) {
		multiplier *= p_emitter_basis.get_scale();
	}
	return multiplier;
}

int64_t ParticleSeed::select(const Ref<RandomStream32> &p_random) const {
	if (!random_selection) {
		return value;
	}
	const int index = int(p_random->next_float() * (choices.size() - 0.00001));
	return choices[index];
}

Vector3 ParticleSpawnShape3D::sample_center(const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	return center_curve.is_valid() ? center_curve->sample(p_emitter_seconds, p_random) : center;
}

ParticleSpawnShape3D::Sample ParticleSpawnShape3D::sample(const Ref<RandomStream32> &p_random, double p_emitter_seconds, int64_t p_index) const {
	const Vector3 origin = sample_center(p_random, p_emitter_seconds);
	if (kind != int(Kind::TriangularPrism)) {
		const Vector3 offset = sample_offset(p_random, p_emitter_seconds, p_index);
		return { origin + offset, offset };
	}
	const float height = triangle_height.is_valid() ? triangle_height->sample(p_emitter_seconds, 0.0, p_random) : 0.0;
	const float angle = triangle_angle_degrees.is_valid() ? triangle_angle_degrees->sample(p_emitter_seconds, 0.0, p_random) : 0.0;
	const float thickness = triangle_thickness.is_valid() ? triangle_thickness->sample(p_emitter_seconds, 0.0, p_random) : 0.0;
	const float half_width = std::tan(angle * float(Math::PI / 360.0)) * height;
	float a = p_random->next_float();
	float b = p_random->next_float();
	float c = p_random->next_float();
	const float depth = p_random->next_float();
	// Normalized independent weights deliberately preserve the authored
	// sampler's density; these are not uniform-area barycentric coordinates.
	const float sum = MAX((a + b) + c, 0.0001f);
	a /= sum;
	b /= sum;
	c /= sum;
	const float start_x = origin[direction_order.x];
	const float start_y = origin[direction_order.y];
	Vector3 position;
	position[direction_order.x] = (start_x - height * 0.5f) * (c + b) + (height * 0.5f + start_x) * a;
	position[direction_order.y] = (a * start_y + (start_y + half_width) * b) + (start_y - half_width) * c;
	position[direction_order.z] = depth * thickness - thickness * 0.5f;
	return { position, position - origin };
}

Vector3 ParticleSpawnShape3D::sample_direction(const Ref<RandomStream32> &p_random) const {
	Vector3 direction;
	for (int i = 0; i < 3; ++i) {
		direction[direction_order[i]] = p_random->next_float();
	}
	for (int axis = 0; axis < 3; ++axis) {
		if (positive[axis] && negative[axis]) {
			direction[axis] = direction[axis] * 2.0 - 1.0;
		} else if (negative[axis]) {
			direction[axis] = -direction[axis];
		} else if (!positive[axis]) {
			direction[axis] = 0.0;
		}
	}
	return direction;
}

Vector3 ParticleSpawnShape3D::sample_offset(const Ref<RandomStream32> &p_random, double p_emitter_seconds, int64_t p_index) const {
	Vector3 direction;
	if (kind == int(Kind::Sphere)) {
		direction = sample_direction(p_random);
	}
	const double sampled_radius = radius_curve.is_valid() ? radius_curve->sample(p_emitter_seconds, 0.0, p_random) : radius;
	if (kind == int(Kind::Circle)) {
		const double start = arc_start_degrees.is_valid() ? arc_start_degrees->sample(p_emitter_seconds, 0.0, p_random) : 0.0;
		const double arc = arc_size_degrees.is_valid() ? arc_size_degrees->sample(p_emitter_seconds, 0.0, p_random) : 360.0;
		const int64_t count = point_count.is_valid() ? MAX(int64_t(Math::floor(point_count->sample(p_emitter_seconds, 0.0, p_random))), int64_t(2)) : 2;
		const double clamped_arc = CLAMP(arc, -360.0, 360.0);
		const int64_t divisions = Math::abs(clamped_arc) == 360.0 ? count : count - 1;
		const double angle = Math::deg_to_rad(CLAMP(start, -360.0, 360.0) + clamped_arc * double(p_index % count) / divisions);
		Vector3 offset;
		offset[direction_order.x] = Math::cos(angle) * sampled_radius;
		offset[direction_order.y] = Math::sin(angle) * sampled_radius;
		return offset;
	}
	const double sampled_height = half_height_curve.is_valid() ? half_height_curve->sample(p_emitter_seconds, 0.0, p_random) : half_height;
	if (kind == int(Kind::Sphere)) {
		const Vector3 normal = direction.normalized();
		const Vector3 bounds = normal.abs() * sampled_radius;
		return ((surface_only ? normal : direction) * sampled_radius).clamp(-bounds, bounds);
	}
	direction = sample_direction(p_random);
	Vector3 radial = direction;
	radial[height_axis] = 0.0;
	for (int attempt = 0; attempt < 50; ++attempt) {
		if (radial.length_squared() <= 1.0) {
			break;
		}
		direction = sample_direction(p_random);
		radial = direction;
		radial[height_axis] = 0.0;
	}
	const Vector3 normal = radial.normalized();
	Vector3 offset = direction * sampled_radius;
	offset[height_axis] = direction[height_axis] * sampled_height;
	if (surface_only && Math::abs(Math::abs(offset[height_axis]) - sampled_height) > surface_tolerance) {
		const real_t height = offset[height_axis];
		offset = normal * sampled_radius;
		offset[height_axis] = height;
	}
	Vector3 bounds = normal.abs() * sampled_radius;
	bounds[height_axis] = sampled_height;
	return offset.clamp(-bounds, bounds);
}

Vector3 ParticleSpawnShape3D::velocity_at(Vector3 p_offset, const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	if (radial_velocity && kind == int(Kind::Cylinder)) {
		p_offset[height_axis] = 0.0;
	}
	double scale = velocity_scale;
	if (velocity_scale_curve.is_valid()) {
		scale = velocity_scale_curve->sample(p_emitter_seconds, 0.0, p_random);
	} else if (random_velocity_scale) {
		scale += velocity_scale_variation * p_random->next_float();
	}
	return p_offset * scale;
}

double ParticleBirthRule3D::sample_scalar(const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	const double sampled = scalar_curve.is_valid() ? scalar_curve->sample(p_emitter_seconds) : value.x;
	if (!randomized) {
		return sampled;
	}
	const double spread = scalar_maximum_curve.is_valid() ? double(float(scalar_maximum_curve->sample(p_emitter_seconds) - sampled)) : variation.x;
	if (random_extreme) {
		return float(sampled + spread * double(p_random->next_float() > 0.5));
	}
	return float(sampled + double(float(spread * p_random->next_float())));
}

Vector3 ParticleBirthRule3D::sample_vector(const Ref<RandomStream32> &p_random, double p_emitter_seconds) const {
	const Vector3 sampled = value_curve.is_valid() ? value_curve->sample(p_emitter_seconds) : value;
	if (!randomized) {
		return sampled;
	}
	const Vector3 spread = maximum_curve.is_valid() ? maximum_curve->sample(p_emitter_seconds) - sampled : variation;
	if (random_extreme) {
		return sampled + spread * double(p_random->next_float() > 0.5);
	}
	return sampled + spread * p_random->next_weight(axis_lock);
}

Vector3 ParticleBirthRule3D::sample_texture_sign(const Ref<RandomStream32> &p_random) const {
	Vector3 result(1, 1, 1);
	switch (TextureFlip(texture_flip)) {
		case TextureFlip::Both:
			result = Vector3(-1, -1, -1);
			break;
		case TextureFlip::Horizontal:
			result.x = -1.0;
			break;
		case TextureFlip::Vertical:
			result.z = -1.0;
			break;
		case TextureFlip::RandomBoth:
			if (p_random->next_float() > 0.5) {
				result = Vector3(-1, -1, -1);
			}
			break;
		case TextureFlip::RandomHorizontal:
			if (p_random->next_float() > 0.5) {
				result.x = -1.0;
			}
			break;
		case TextureFlip::RandomVertical:
			if (p_random->next_float() > 0.5) {
				result.z = -1.0;
			}
			break;
		case TextureFlip::RandomIndependent:
			if (p_random->next_float() > 0.5) {
				result.x = -1.0;
			}
			if (p_random->next_float() > 0.5) {
				result.z = -1.0;
			}
			break;
		default:
			break;
	}
	return result;
}

Vector3 ParticleBirthRule3D::sample_velocity(const Ref<RandomStream32> &p_random, const Basis &p_emitter_basis, bool p_local_space, double p_emitter_seconds) const {
	Vector3 velocity = sample_vector(p_random, p_emitter_seconds);
	if (!velocity_in_world_space) {
		velocity = velocity_basis.xform(velocity);
	}
	const Basis rotation = p_emitter_basis.orthonormalized();
	if (p_local_space && velocity_in_world_space) {
		velocity = rotation.inverse().xform(velocity);
	} else if (!p_local_space && !velocity_in_world_space) {
		velocity = rotation.xform(velocity);
	}
	if (velocity_applies_owner_scale) {
		velocity *= p_emitter_basis.get_scale();
	}
	return p_local_space ? velocity : rotation.inverse().xform(velocity);
}

Vector3 ParticleBirthRule3D::sample_radial_velocity(const Ref<RandomStream32> &p_random, const Basis &p_emitter_basis, bool p_local_space, const Vector3 &p_position, double p_emitter_seconds) const {
	if (radial_velocity.is_null()) {
		return Vector3();
	}
	// Sample even at the origin or with a zero range: authored random curves
	// still advance their stream before subsequent birth rules.
	const real_t speed = radial_velocity->sample(p_emitter_seconds, 0.0, p_random);
	if (speed == 0.0) {
		return Vector3();
	}
	const Basis rotation = p_emitter_basis.orthonormalized();
	const Vector3 offset = p_position - velocity_origin;
	Vector3 velocity = (p_local_space ? offset : rotation.xform(offset)).normalized() * speed;
	if (velocity_applies_owner_scale) {
		velocity *= p_emitter_basis.get_scale();
	}
	return p_local_space ? velocity : rotation.inverse().xform(velocity);
}

Vector3 ParticleBirthRule3D::sample_parent_velocity(const Ref<RandomStream32> &p_random, const Basis &p_emitter_basis, bool p_local_space, const Vector3 &p_world_velocity, double p_emitter_seconds) const {
	Vector3 velocity = p_local_space ? p_emitter_basis.inverse().xform(p_world_velocity) : p_world_velocity;
	velocity *= sample_vector(p_random, p_emitter_seconds);
	// Detached particles retain their birth rotation as their simulation basis.
	return p_local_space ? velocity : p_emitter_basis.orthonormalized().inverse().xform(velocity);
}
