// wgodot-changes::file
#pragma once

#include "particle_curves.h"
#include "core/variant/typed_array.h"
#include "scene/resources/gradient.h"
#include "scene/resources/mesh.h"

class ParticleColorCurve : public Resource {
	GDCLASS(ParticleColorCurve, Resource);

public:
	enum class Sampling {
		Fixed,
		Uniform,
		Extreme,
	};

private:
	Ref<Gradient> value;
	Ref<Gradient> extreme;
	int color_sampling = 0;
	int alpha_sampling = 0;
	int axis_lock = 1;
	Vector3i color_order = Vector3i(0, 1, 2);

protected:
	static void _bind_methods();

public:
	void set_value(const Ref<Gradient> & p_value) { value = p_value; }
	Ref<Gradient> get_value() const { return value; }
	void set_extreme(const Ref<Gradient> & p_value) { extreme = p_value; }
	Ref<Gradient> get_extreme() const { return extreme; }
	void set_color_sampling(int p_value) { color_sampling = p_value; }
	int get_color_sampling() const { return color_sampling; }
	void set_alpha_sampling(int p_value) { alpha_sampling = p_value; }
	int get_alpha_sampling() const { return alpha_sampling; }
	void set_axis_lock(int p_value) { axis_lock = p_value; }
	int get_axis_lock() const { return axis_lock; }
	void set_color_order(const Vector3i & p_value) { color_order = p_value; }
	Vector3i get_color_order() const { return color_order; }
	Color sample(double p_relative_age, const Ref<RandomStream32> & p_random) const;
};

class ParticleOrientation3D : public Resource {
	GDCLASS(ParticleOrientation3D, Resource);

	Vector3 angles;
	Vector3 variation;
	bool randomized = false;
	Ref<RandomStream32> shared_random;
	bool use_shared_random = false;

protected:
	static void _bind_methods();

public:
	void set_angles(const Vector3 & p_value) { angles = p_value; }
	Vector3 get_angles() const { return angles; }
	void set_variation(const Vector3 & p_value) { variation = p_value; }
	Vector3 get_variation() const { return variation; }
	void set_randomized(bool p_value) { randomized = p_value; }
	bool get_randomized() const { return randomized; }
	void set_use_shared_random(bool p_value) { use_shared_random = p_value; }
	bool get_use_shared_random() const { return use_shared_random; }
	Ref<RandomStream32> get_random_stream();
	Vector3 sample(const Ref<RandomStream32> & p_stream = Ref<RandomStream32>());
};

class ParticleMaterialChannels : public Resource {
	GDCLASS(ParticleMaterialChannels, Resource);

	TypedArray<ParticleFloatCurve> channels;
	Vector<int32_t> emitter_clock_channels;
	Vector<int32_t> spawn_channels;
	Vector<int32_t> curve_spawn_channels;
	Vector<int32_t> random_channels;
	Vector4 random_spreads;

protected:
	static void _bind_methods();

public:
	void set_channels(const TypedArray<ParticleFloatCurve> & p_value) { channels = p_value; }
	TypedArray<ParticleFloatCurve> get_channels() const { return channels; }
	void set_emitter_clock_channels(const Vector<int32_t> & p_value) { emitter_clock_channels = p_value; }
	Vector<int32_t> get_emitter_clock_channels() const { return emitter_clock_channels; }
	void set_spawn_channels(const Vector<int32_t> & p_value) { spawn_channels = p_value; }
	Vector<int32_t> get_spawn_channels() const { return spawn_channels; }
	void set_curve_spawn_channels(const Vector<int32_t> & p_value) { curve_spawn_channels = p_value; }
	Vector<int32_t> get_curve_spawn_channels() const { return curve_spawn_channels; }
	void set_random_channels(const Vector<int32_t> & p_value) { random_channels = p_value; }
	Vector<int32_t> get_random_channels() const { return random_channels; }
	void set_random_spreads(const Vector4 & p_value) { random_spreads = p_value; }
	Vector4 get_random_spreads() const { return random_spreads; }
	Vector4 spawn_values(double p_relative_age, Vector4 p_initial, double p_task_ratio = 0.0, double p_emitter_seconds = 0.0) const;
	Vector4 sample(double p_relative_age, Vector4 p_initial = Vector4(), double p_task_ratio = 0.0, const Ref<RandomStream32> & p_random = Ref<RandomStream32>(), bool p_spawned_now = false, double p_emitter_seconds = 0.0) const;
};
