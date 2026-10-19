// wgodot-changes::file
#pragma once

#include "random_stream_32.h"
#include "scene/resources/curve.h"

class ParticleFloatCurve : public Resource {
	GDCLASS(ParticleFloatCurve, Resource);

	double value = 1.0;
	Ref<Curve> curve;
	bool task_clock = false;
	Vector<float> samples;
	double sample_start = 0.0;
	double samples_per_second = 0.0;
	Ref<ParticleFloatCurve> maximum;
	bool remap = false;
	double input_scale = 1.0;
	bool absolute = false;
	Vector2 input_range = Vector2(-INFINITY, INFINITY);
	double input_offset = 0.0;
	double output_scale = 1.0;
	double output_offset = 0.0;

	double sample_value(double p_time, double p_task_ratio) const;
	double remap_value(double p_value) const;

protected:
	static void _bind_methods();

public:
	void set_value(double p_value) { value = p_value; }
	double get_value() const { return value; }
	void set_curve(const Ref<Curve> &p_value) { curve = p_value; }
	Ref<Curve> get_curve() const { return curve; }
	void set_task_clock(bool p_value) { task_clock = p_value; }
	bool get_task_clock() const { return task_clock; }
	void set_samples(const Vector<float> &p_value) { samples = p_value; }
	Vector<float> get_samples() const { return samples; }
	void set_sample_start(double p_value) { sample_start = p_value; }
	double get_sample_start() const { return sample_start; }
	void set_samples_per_second(double p_value) { samples_per_second = p_value; }
	double get_samples_per_second() const { return samples_per_second; }
	void set_maximum(const Ref<ParticleFloatCurve> &p_value) { maximum = p_value; }
	Ref<ParticleFloatCurve> get_maximum() const { return maximum; }
	void set_remap(bool p_value) { remap = p_value; }
	bool get_remap() const { return remap; }
	void set_input_scale(double p_value) { input_scale = p_value; }
	double get_input_scale() const { return input_scale; }
	void set_absolute(bool p_value) { absolute = p_value; }
	bool get_absolute() const { return absolute; }
	void set_input_range(Vector2 p_value) { input_range = p_value; }
	Vector2 get_input_range() const { return input_range; }
	void set_input_offset(double p_value) { input_offset = p_value; }
	double get_input_offset() const { return input_offset; }
	void set_output_scale(double p_value) { output_scale = p_value; }
	double get_output_scale() const { return output_scale; }
	void set_output_offset(double p_value) { output_offset = p_value; }
	double get_output_offset() const { return output_offset; }
	double sample(double p_time, double p_task_ratio = 0.0, const Ref<RandomStream32> &p_random = Ref<RandomStream32>()) const;
	Vector2 get_bounds() const;
};

class ParticleVectorCurve3D : public Resource {
	GDCLASS(ParticleVectorCurve3D, Resource);

	Vector3 value = Vector3(1, 1, 1);
	Ref<Curve> x;
	Ref<Curve> y;
	Ref<Curve> z;
	Ref<ParticleVectorCurve3D> maximum;
	int axis_lock = 1;
	bool random_extreme = false;
	Vector<Vector3> samples;
	double sample_start = 0.0;
	double samples_per_second = 0.0;

	Vector3 sample_value(double p_time) const;

protected:
	static void _bind_methods();

public:
	void set_value(const Vector3 &p_value) { value = p_value; }
	Vector3 get_value() const { return value; }
	void set_x(const Ref<Curve> &p_value) { x = p_value; }
	Ref<Curve> get_x() const { return x; }
	void set_y(const Ref<Curve> &p_value) { y = p_value; }
	Ref<Curve> get_y() const { return y; }
	void set_z(const Ref<Curve> &p_value) { z = p_value; }
	Ref<Curve> get_z() const { return z; }
	void set_maximum(const Ref<ParticleVectorCurve3D> &p_value) { maximum = p_value; }
	Ref<ParticleVectorCurve3D> get_maximum() const { return maximum; }
	void set_axis_lock(int p_value) { axis_lock = p_value; }
	int get_axis_lock() const { return axis_lock; }
	void set_random_extreme(bool p_value) { random_extreme = p_value; }
	bool get_random_extreme() const { return random_extreme; }
	void set_samples(const Vector<Vector3> &p_value) { samples = p_value; }
	Vector<Vector3> get_samples() const { return samples; }
	void set_sample_start(double p_value) { sample_start = p_value; }
	double get_sample_start() const { return sample_start; }
	void set_samples_per_second(double p_value) { samples_per_second = p_value; }
	double get_samples_per_second() const { return samples_per_second; }
	Vector3 sample(double p_time, const Ref<RandomStream32> &p_random = Ref<RandomStream32>()) const;
};

