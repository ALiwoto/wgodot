// wgodot-changes::file
#include "particle_environment.h"

void ParticleLightProfile3D::apply(OmniLight3D *p_light, double p_relative_age, Color p_tint, Vector2 p_size, Vector3 p_emitter_scale) const {
	const Color scaled = p_tint * color->get_color_at_offset(p_relative_age);
	double intensity = brightness->sample(p_relative_age) * p_tint.a;
	if (inverse_squared) {
		intensity *= inverse_squared_scale;
	}
	const Vector3 rgb = Vector3(scaled.r, scaled.g, scaled.b).max(Vector3()) * intensity;
	const double energy = rgb.length();
	Vector3 normalized = energy > 0.0 ? rgb / energy : Vector3();
	Color light_color(normalized.x, normalized.y, normalized.z);
	p_size *= Vector2(p_emitter_scale.x, p_emitter_scale.z);
	double radius = (Math::abs(p_size.x) + Math::abs(p_size.y)) * 0.5;
	if (high_quality) {
		const double units = inverse_squared ? normalization_units : 1.0;
		if (energy * units <= 0.0001) {
			normalized = rgb * units;
		}
		normalized = (normalized * 255.0).floor() / 255.0;
		light_color = Color(normalized.x, normalized.y, normalized.z);
		radius = Math::abs(p_size.x);
	} else {
		light_color = light_color.linear_to_srgb();
	}
	p_light->set_color(light_color);
	p_light->set_param(Light3D::PARAM_ENERGY, energy);
	p_light->set_param(Light3D::PARAM_RANGE, MAX(0.01, radius * radius_scale));
}

bool ParticleGroundProjector3D::project(Vector3 p_point) {
	set_global_position(p_point + Vector3(0, check_height * 0.5, 0));
	force_raycast_update();
	return is_colliding();
}

Vector3 ParticleGroundProjector3D::projected_position(const Ref<RandomStream32> &p_random) {
	double offset = world_offset;
	if (random_offset) {
		offset += offset_variation * p_random->next_float();
	}
	return get_collision_point() + Vector3(0, offset, 0);
}

bool ParticleGroundProjector3D::project_emitter(Transform3D p_frame) {
	return project(p_frame.orthonormalized().xform(emitter_origin));
}

double ParticleGroundProjector3D::emitter_offset(Transform3D p_frame) {
	const Transform3D component = p_frame.orthonormalized();
	const Vector3 point = component.xform(emitter_origin);
	const Basis emitter_basis = component.basis * Basis::from_euler(emitter_rotation, EulerOrder::YZX);
	return emitter_basis.inverse().xform(get_collision_point() - point).y;
}
