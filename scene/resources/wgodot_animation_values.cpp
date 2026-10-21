// wgodot-changes::file
#include "wgodot_animation_values.h"
#include "scene/resources/animation.h"

WAnimationValues::WAnimationValues(const WAnimationValues &p_other) : timing_curves(p_other.timing_curves), timing_curve_subdivisions(p_other.timing_curve_subdivisions), timing_samples(p_other.timing_samples), keys(p_other.keys) {
	data = p_other.data ? p_other.data->clone() : nullptr;
}

WAnimationValues::WAnimationValues(WAnimationValues &&p_other) : data(p_other.data), timing_curves(std::move(p_other.timing_curves)), timing_curve_subdivisions(p_other.timing_curve_subdivisions), timing_samples(std::move(p_other.timing_samples)), keys(std::move(p_other.keys)) {
	p_other.data = nullptr;
}

WAnimationValues &WAnimationValues::operator=(const WAnimationValues &p_other) {
	if (this != &p_other) {
		WAnimationValues copy(p_other);
		*this = std::move(copy);
	}
	return *this;
}

WAnimationValues &WAnimationValues::operator=(WAnimationValues &&p_other) {
	if (this != &p_other) {
		if (data) {
			memdelete(data);
		}
		data = p_other.data;
		keys = std::move(p_other.keys);
		timing_curves = std::move(p_other.timing_curves);
		timing_curve_subdivisions = p_other.timing_curve_subdivisions;
		timing_samples = std::move(p_other.timing_samples);
		p_other.data = nullptr;
	}
	return *this;
}

WAnimationValues::~WAnimationValues() {
	if (data) {
		memdelete(data);
	}
}

void WAnimationValues::change_type(Data *p_data) {
	p_data->resize(keys.size());
	if (data) {
		for (uint32_t i = 0; i < keys.size(); i++) {
			p_data->set(i, data->get(i));
		}
		memdelete(data);
	}
	data = p_data;
}

void WAnimationValues::ensure_type(const Variant &p_value) {
	const Variant::Type type = p_value.get_type();
	if (data) {
		if ((kind() == WAnimationValueKind::Int && type == Variant::FLOAT) || (kind() == WAnimationValueKind::Float && type == Variant::INT)) {
			change_type(memnew(TypedData<WAnimationNumber>));
			return;
		}
		if (kind() == WAnimationValueKind::Generic || (kind() == WAnimationValueKind::Number && (type == Variant::INT || type == Variant::FLOAT))) {
			return;
		}
		bool matches = false;
		switch (kind()) {
#define W_ANIMATION_MATCH(m_name, m_type, m_variant) \
			case WAnimationValueKind::m_name: matches = type == Variant::m_variant; break;
			W_ANIMATION_VALUE_TYPES(W_ANIMATION_MATCH)
#undef W_ANIMATION_MATCH
			default: break;
		}
		if (kind() == WAnimationValueKind::Resource) {
			Object *object = p_value;
			matches = (type == Variant::NIL || type == Variant::OBJECT) && (!object || Object::cast_to<Resource>(object));
		}
		if (!matches) {
			change_type(memnew(TypedData<Variant>));
		}
		return;
	}
	if (type == Variant::OBJECT || type == Variant::NIL) {
		Object *object = p_value;
		if (!object || Object::cast_to<Resource>(object)) {
			change_type(memnew(TypedData<Ref<Resource>>));
			return;
		}
		change_type(memnew(TypedData<Variant>));
		return;
	}
	switch (type) {
#define W_ANIMATION_CREATE(m_name, m_type, m_variant) \
		case Variant::m_variant: change_type(memnew(TypedData<m_type>)); return;
		W_ANIMATION_VALUE_TYPES(W_ANIMATION_CREATE)
#undef W_ANIMATION_CREATE
		default: change_type(memnew(TypedData<Variant>)); return;
	}
}

void WAnimationValues::resize(uint32_t p_size) {
	if (!timing_curves.is_empty()) {
		int previous = timing_curves.size();
		timing_curves.resize(p_size);
		for (int i = previous; i < int(p_size); i++) {
			timing_curves.write[i] = Vector4(0, 0, 1, 1);
		}
		rebuild_timing_samples();
	}
	keys.resize(p_size);
	if (data) {
		data->resize(p_size);
	}
}

void WAnimationValues::remove_at(uint32_t p_index) {
	if (!timing_curves.is_empty()) {
		timing_curves.remove_at(p_index);
		rebuild_timing_samples();
	}
	keys.remove_at(p_index);
	if (data) {
		data->remove(p_index);
	}
}

void WAnimationValues::insert(uint32_t p_index, const WAnimationKey &p_key, const Variant &p_value) {
	ensure_type(p_value);
	keys.insert(p_index, p_key);
	if (!timing_curves.is_empty()) {
		timing_curves.insert(p_index, Vector4(0, 0, 1, 1));
		rebuild_timing_samples();
	}
	data->insert(p_index);
	data->set(p_index, p_value);
}

void WAnimationValues::set(uint32_t p_index, const Variant &p_value) {
	ensure_type(p_value);
	data->set(p_index, p_value);
}

void WAnimationValues::rebuild_timing_samples() {
	if (timing_curve_subdivisions < 2 || timing_curves.is_empty()) {
		timing_samples.clear();
		return;
	}
	const int interior = timing_curve_subdivisions - 1;
	ERR_FAIL_COND(timing_samples.resize(int64_t(timing_curves.size()) * interior) != OK);
	Vector2 *samples = timing_samples.ptrw();
	for (int64_t key = 0; key < timing_curves.size(); key++) {
		const Vector4 &curve = timing_curves[key];
		for (int point = 1; point < timing_curve_subdivisions; point++) {
			const double t = double(point) / timing_curve_subdivisions;
			const double u = 1 - t;
			samples[key * interior + point - 1] = Vector2(
					3 * u * u * t * curve.x + 3 * u * t * t * curve.z + t * t * t,
					3 * u * u * t * curve.y + 3 * u * t * t * curve.w + t * t * t);
		}
	}
}

Error WAnimationValues::set_timing_curves(const PackedVector4Array &p_curves, int p_subdivisions) {
	ERR_FAIL_COND_V(p_subdivisions < 0, ERR_INVALID_DATA);
	ERR_FAIL_COND_V(!p_curves.is_empty() && p_curves.size() != int(keys.size()), ERR_INVALID_DATA);
	for (const Vector4 &curve : p_curves) {
		ERR_FAIL_COND_V(!curve.is_finite(), ERR_INVALID_DATA);
		if (curve == Vector4(-1, 0, 0, 0)) {
			continue;
		}
		ERR_FAIL_COND_V(curve.x < 0 || curve.x > 1 || curve.z < 0 || curve.z > 1, ERR_INVALID_DATA);
	}
	timing_curves = p_curves;
	timing_curve_subdivisions = p_subdivisions;
	rebuild_timing_samples();
	return OK;
}

WAnimationSample WAnimationValues::ease_sample(WAnimationSample s, bool p_backward) const {
	if (timing_curves.is_empty() || s.from < 0 || s.from == s.next || s.weight <= 0 || s.weight >= 1) {
		return s;
	}
	const int key = p_backward ? s.next : s.from;
	const Vector4 &curve = timing_curves[key];
	if (curve == Vector4(-1, 0, 0, 0)) {
		s.weight = p_backward ? 1 : 0;
		return s;
	}
	if ((curve.x == curve.y && curve.z == curve.w) || timing_curve_subdivisions == 1) {
		return s;
	}
	const double x = p_backward ? 1.0 - s.weight : s.weight;
	if (timing_curve_subdivisions > 1) {
		// Authored sampled easing uses the source curve's polyline, not an
		// analytic Bezier approximation. Points are prepared once on load/edit.
		const int interior = timing_curve_subdivisions - 1;
		const Vector2 *samples = timing_samples.ptr() + int64_t(key) * interior;
		int low = 0;
		int high = interior;
		while (low < high) {
			const int middle = low + (high - low) / 2;
			if (samples[middle].x < x) {
				low = middle + 1;
			} else {
				high = middle;
			}
		}
		const Vector2 left = low == 0 ? Vector2() : samples[low - 1];
		const Vector2 right = low == interior ? Vector2(1, 1) : samples[low];
		const double y = Math::lerp(double(left.y), double(right.y), (x - left.x) / (right.x - left.x));
		s.weight = p_backward ? 1 - y : y;
		return s;
	}
	double low = 0;
	double high = 1;
	double t = x;
	// Monotonic time controls: bounded inversion, including flat end tangents.
	for (int i = 0; i < 20; i++) {
		double u = 1 - t;
		double at = 3 * u * u * t * curve.x + 3 * u * t * t * curve.z + t * t * t;
		if (at < x) {
			low = t;
		} else {
			high = t;
		}
		t = (low + high) * 0.5;
	}
	double u = 1 - t;
	double y = 3 * u * u * t * curve.y + 3 * u * t * t * curve.w + t * t * t;
	s.weight = p_backward ? 1 - y : y;
	return s;
}

Variant WAnimationValues::sample_variant(const WAnimationSample &s) const {
	if (s.from < 0) {
		return Variant();
	}
	const Variant a = get(s.from);
	if (s.interpolation == Animation::INTERPOLATION_NEAREST) {
		return a;
	}
	const Variant b = get(s.next);
	if (s.interpolation == Animation::INTERPOLATION_CUBIC || s.interpolation == Animation::INTERPOLATION_CUBIC_ANGLE) {
		const Variant pa = get(s.pre);
		const Variant pb = get(s.post);
		if (s.interpolation == Animation::INTERPOLATION_CUBIC_ANGLE && a.is_num() && b.is_num() && pa.is_num() && pb.is_num() && (a.get_type() == Variant::FLOAT || b.get_type() == Variant::FLOAT || pa.get_type() == Variant::FLOAT || pb.get_type() == Variant::FLOAT)) {
			return Math::fposmod(float(Math::cubic_interpolate_angle_in_time(real_t(a), real_t(b), real_t(pa), real_t(pb), s.weight, s.next_time, s.pre_time, s.post_time)), float(Math::TAU));
		}
		return Animation::cubic_interpolate_in_time_variant(pa, a, b, pb, s.weight, s.pre_time, s.next_time, s.post_time);
	}
	if (s.interpolation == Animation::INTERPOLATION_LINEAR_ANGLE && a.is_num() && b.is_num() && (a.get_type() == Variant::FLOAT || b.get_type() == Variant::FLOAT)) {
		return Math::fposmod(float(Math::lerp_angle(real_t(a), real_t(b), s.weight)), float(Math::TAU));
	}
	return Animation::interpolate_variant(a, b, s.weight);
}
