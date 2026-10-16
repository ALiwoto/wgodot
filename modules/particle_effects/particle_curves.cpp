// wgodot-changes::file
#include "core/object/class_db.h"
#include "particle_curves.h"

// Match the authored float32 sampling grid without a mutable scratch array.
// Curves can be shared by independent simulation tasks after preparation.
static double round_float(double p_value) {
	return double(float(p_value));
}

double ParticleFloatCurve::sample_value(double p_time, double p_task_ratio) const {
	if (samples.size() > 1) {
		const double position = CLAMP(round_float(round_float(p_time - sample_start) * samples_per_second), 0.0, double(samples.size() - 1));
		const int first = int(position);
		const int second = MIN(first + 1, samples.size() - 1);
		return round_float(double(samples[first]) + round_float(round_float(double(samples[second]) - double(samples[first])) * (position - first)));
	}
	return curve.is_valid() ? double(curve->sample(task_clock ? p_task_ratio : p_time)) : value;
}

double ParticleFloatCurve::sample(double p_time, double p_task_ratio, const Ref<RandomStream32> &p_random) const {
	double result = sample_value(p_time, p_task_ratio);
	if (maximum.is_valid()) {
		ERR_FAIL_COND_V(p_random.is_null(), result);
		result += (maximum->sample(p_time, p_task_ratio) - result) * p_random->next_float();
	}
	return result;
}

Vector2 ParticleFloatCurve::get_bounds() const {
	double low = value;
	double high = value;
	if (samples.size() > 1) {
		low = high = samples[0];
		for (const float sample : samples) {
			low = MIN(low, double(sample));
			high = MAX(high, double(sample));
		}
	} else if (curve.is_valid()) {
		low = high = curve->sample(curve->get_min_domain());
		for (int i = 0; i < curve->get_point_count(); ++i) {
			const Curve::Point point = curve->get_point(i);
			low = MIN(low, double(point.position.y));
			high = MAX(high, double(point.position.y));
			if (i + 1 >= curve->get_point_count() || point.right_mode == Curve::TANGENT_CONSTANT) {
				continue;
			}
			const Curve::Point next = curve->get_point(i + 1);
			const double third = (double(next.position.x) - point.position.x) / 3.0;
			// A cubic is contained in the convex hull of its Bezier controls.
			// This conservative bound includes tangent overshoot without sampling.
			const double right = point.position.y + third * point.right_tangent;
			const double left = next.position.y - third * next.left_tangent;
			low = MIN(low, MIN(right, left));
			high = MAX(high, MAX(right, left));
		}
	}
	if (maximum.is_valid()) {
		const Vector2 other = maximum->get_bounds();
		low = MIN(low, double(other.x));
		high = MAX(high, double(other.y));
	}
	return Vector2(low, high);
}

Vector3 ParticleVectorCurve3D::sample_value(double p_time) const {
	if (samples.size() > 1) {
		const double position = CLAMP(round_float(round_float(p_time - sample_start) * samples_per_second), 0.0, double(samples.size() - 1));
		const int first = int(position);
		const int second = MIN(first + 1, samples.size() - 1);
		return samples[first].lerp(samples[second], position - first);
	}
	return Vector3(x.is_valid() ? x->sample(p_time) : value.x,
			y.is_valid() ? y->sample(p_time) : value.y,
			z.is_valid() ? z->sample(p_time) : value.z);
}

Vector3 ParticleVectorCurve3D::sample(double p_time, const Ref<RandomStream32> &p_random) const {
	Vector3 result = sample_value(p_time);
	if (maximum.is_valid()) {
		ERR_FAIL_COND_V(p_random.is_null(), result);
		const Vector3 spread = maximum->sample(p_time) - result;
		if (random_extreme) {
			result += spread * real_t(p_random->next_float() > 0.5);
		} else {
			result += spread * p_random->next_weight(axis_lock);
		}
	}
	return result;
}

void ParticleFloatCurve::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bounds"), &ParticleFloatCurve::get_bounds);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ParticleFloatCurve::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &ParticleFloatCurve::get_value);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
	ClassDB::bind_method(D_METHOD("set_curve", "value"), &ParticleFloatCurve::set_curve);
	ClassDB::bind_method(D_METHOD("get_curve"), &ParticleFloatCurve::get_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_curve", "get_curve");
	ClassDB::bind_method(D_METHOD("set_task_clock", "value"), &ParticleFloatCurve::set_task_clock);
	ClassDB::bind_method(D_METHOD("get_task_clock"), &ParticleFloatCurve::get_task_clock);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "task_clock"), "set_task_clock", "get_task_clock");
	ClassDB::bind_method(D_METHOD("set_samples", "value"), &ParticleFloatCurve::set_samples);
	ClassDB::bind_method(D_METHOD("get_samples"), &ParticleFloatCurve::get_samples);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "samples"), "set_samples", "get_samples");
	ClassDB::bind_method(D_METHOD("set_sample_start", "value"), &ParticleFloatCurve::set_sample_start);
	ClassDB::bind_method(D_METHOD("get_sample_start"), &ParticleFloatCurve::get_sample_start);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sample_start"), "set_sample_start", "get_sample_start");
	ClassDB::bind_method(D_METHOD("set_samples_per_second", "value"), &ParticleFloatCurve::set_samples_per_second);
	ClassDB::bind_method(D_METHOD("get_samples_per_second"), &ParticleFloatCurve::get_samples_per_second);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "samples_per_second"), "set_samples_per_second", "get_samples_per_second");
	ClassDB::bind_method(D_METHOD("set_maximum", "value"), &ParticleFloatCurve::set_maximum);
	ClassDB::bind_method(D_METHOD("get_maximum"), &ParticleFloatCurve::get_maximum);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "maximum", PROPERTY_HINT_RESOURCE_TYPE, "ParticleFloatCurve"), "set_maximum", "get_maximum");
	ClassDB::bind_method(D_METHOD("sample", "time", "task_ratio", "random"), &ParticleFloatCurve::sample, DEFVAL(0.0), DEFVAL(Ref<RandomStream32>()));
}

void ParticleVectorCurve3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ParticleVectorCurve3D::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &ParticleVectorCurve3D::get_value);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "value"), "set_value", "get_value");
	ClassDB::bind_method(D_METHOD("set_x", "value"), &ParticleVectorCurve3D::set_x);
	ClassDB::bind_method(D_METHOD("get_x"), &ParticleVectorCurve3D::get_x);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "x", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_x", "get_x");
	ClassDB::bind_method(D_METHOD("set_y", "value"), &ParticleVectorCurve3D::set_y);
	ClassDB::bind_method(D_METHOD("get_y"), &ParticleVectorCurve3D::get_y);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "y", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_y", "get_y");
	ClassDB::bind_method(D_METHOD("set_z", "value"), &ParticleVectorCurve3D::set_z);
	ClassDB::bind_method(D_METHOD("get_z"), &ParticleVectorCurve3D::get_z);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "z", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_z", "get_z");
	ClassDB::bind_method(D_METHOD("set_maximum", "value"), &ParticleVectorCurve3D::set_maximum);
	ClassDB::bind_method(D_METHOD("get_maximum"), &ParticleVectorCurve3D::get_maximum);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "maximum", PROPERTY_HINT_RESOURCE_TYPE, "ParticleVectorCurve3D"), "set_maximum", "get_maximum");
	ClassDB::bind_method(D_METHOD("set_axis_lock", "value"), &ParticleVectorCurve3D::set_axis_lock);
	ClassDB::bind_method(D_METHOD("get_axis_lock"), &ParticleVectorCurve3D::get_axis_lock);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "axis_lock"), "set_axis_lock", "get_axis_lock");
	ClassDB::bind_method(D_METHOD("set_random_extreme", "value"), &ParticleVectorCurve3D::set_random_extreme);
	ClassDB::bind_method(D_METHOD("get_random_extreme"), &ParticleVectorCurve3D::get_random_extreme);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_extreme"), "set_random_extreme", "get_random_extreme");
	ClassDB::bind_method(D_METHOD("set_samples", "value"), &ParticleVectorCurve3D::set_samples);
	ClassDB::bind_method(D_METHOD("get_samples"), &ParticleVectorCurve3D::get_samples);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "samples"), "set_samples", "get_samples");
	ClassDB::bind_method(D_METHOD("set_sample_start", "value"), &ParticleVectorCurve3D::set_sample_start);
	ClassDB::bind_method(D_METHOD("get_sample_start"), &ParticleVectorCurve3D::get_sample_start);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sample_start"), "set_sample_start", "get_sample_start");
	ClassDB::bind_method(D_METHOD("set_samples_per_second", "value"), &ParticleVectorCurve3D::set_samples_per_second);
	ClassDB::bind_method(D_METHOD("get_samples_per_second"), &ParticleVectorCurve3D::get_samples_per_second);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "samples_per_second"), "set_samples_per_second", "get_samples_per_second");
	ClassDB::bind_method(D_METHOD("sample", "time", "random"), &ParticleVectorCurve3D::sample, DEFVAL(Ref<RandomStream32>()));
}

