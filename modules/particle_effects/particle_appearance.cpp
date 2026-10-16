// wgodot-changes::file
#include "particle_appearance.h"

Color ParticleColorCurve::sample(double p_relative_age, const Ref<RandomStream32> &p_random) const {
	Color result = value->get_color_at_offset(p_relative_age);
	if (extreme.is_null()) {
		return result;
	}
	const Color maximum = extreme->get_color_at_offset(p_relative_age);
	if (color_sampling == int(Sampling::Extreme)) {
		if (p_random->next_float() > 0.5) {
			result.r = maximum.r;
			result.g = maximum.g;
			result.b = maximum.b;
		}
	} else if (color_sampling == int(Sampling::Uniform)) {
		const Vector3 weight = p_random->next_weight(axis_lock);
		result.r = Math::lerp(double(result.r), double(maximum.r), double(weight[color_order.x]));
		result.g = Math::lerp(double(result.g), double(maximum.g), double(weight[color_order.y]));
		result.b = Math::lerp(double(result.b), double(maximum.b), double(weight[color_order.z]));
	}
	if (alpha_sampling == int(Sampling::Uniform)) {
		result.a = Math::lerp(double(result.a), double(maximum.a), p_random->next_float());
	} else if (alpha_sampling == int(Sampling::Extreme) && p_random->next_float() > 0.5) {
		result.a = maximum.a;
	}
	return result;
}

Ref<RandomStream32> ParticleOrientation3D::get_random_stream() {
	if (shared_random.is_null()) {
		shared_random.instantiate();
	}
	return shared_random;
}

Vector3 ParticleOrientation3D::sample(const Ref<RandomStream32> &p_stream) {
	if (!randomized) {
		return angles;
	}
	Ref<RandomStream32> stream = p_stream;
	if (stream.is_null()) {
		stream = get_random_stream();
	}
	return angles + variation * stream->next_vector();
}

Vector4 ParticleMaterialChannels::spawn_values(double p_relative_age, Vector4 p_initial, double p_task_ratio, double p_emitter_seconds) const {
	for (int channel : curve_spawn_channels) {
		const Ref<ParticleFloatCurve> curve = channels[channel];
		p_initial[channel] = curve->sample(emitter_clock_channels.has(channel) ? p_emitter_seconds : p_relative_age, p_task_ratio);
	}
	return p_initial;
}

Vector4 ParticleMaterialChannels::sample(double p_relative_age, Vector4 p_initial, double p_task_ratio, const Ref<RandomStream32> &p_random, bool p_spawned_now, double p_emitter_seconds) const {
	Vector4 result;
	for (int channel = 0; channel < 4; ++channel) {
		const Ref<ParticleFloatCurve> curve = channels[channel];
		result[channel] = curve->sample(emitter_clock_channels.has(channel) ? p_emitter_seconds : p_relative_age, p_task_ratio);
	}
	for (int channel : spawn_channels) {
		result[channel] = p_initial[channel];
	}
	for (int channel : random_channels) {
		result[channel] = p_spawned_now ? p_initial[channel] : result[channel] + random_spreads[channel] * p_random->next_float();
	}
	return result;
}
