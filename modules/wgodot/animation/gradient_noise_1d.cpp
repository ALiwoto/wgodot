// wgodot-changes::file
#include "gradient_noise_1d.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

void GradientNoise1D::set_gradients(const Vector<float> &p_gradients) {
	gradients = p_gradients;
	emit_changed();
}

Vector<float> GradientNoise1D::get_gradients() const {
	return gradients;
}

void GradientNoise1D::set_amplitude(float p_amplitude) {
	amplitude = p_amplitude;
	emit_changed();
}

float GradientNoise1D::get_amplitude() const {
	return amplitude;
}

float GradientNoise1D::sample(float p_position) const {
	if (gradients.is_empty()) {
		return 0.0f;
	}
	const float cell = Math::floor(p_position);
	const float t = p_position - cell;
	const int64_t period = gradients.size();
	int64_t index = int64_t(cell) % period;
	if (index < 0) {
		index += period;
	}
	const float a = t * gradients[index];
	const float b = (t - 1.0f) * gradients[(index + 1) % period];
	const float weight = ((t * 6.0f - 15.0f) * t + 10.0f) * (t * t * t);
	return amplitude * (a + weight * (b - a));
}

void GradientNoise1D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_gradients", "gradients"), &GradientNoise1D::set_gradients);
	ClassDB::bind_method(D_METHOD("get_gradients"), &GradientNoise1D::get_gradients);
	ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &GradientNoise1D::set_amplitude);
	ClassDB::bind_method(D_METHOD("get_amplitude"), &GradientNoise1D::get_amplitude);
	ClassDB::bind_method(D_METHOD("sample", "position"), &GradientNoise1D::sample);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "gradients"), "set_gradients", "get_gradients");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude"), "set_amplitude", "get_amplitude");
}
