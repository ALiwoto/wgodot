// wgodot-changes::file
#include "wgodot_animation_values.h"
#include "scene/resources/animation.h"

WAnimationValues::WAnimationValues(const WAnimationValues &p_other) : keys(p_other.keys) {
	data = p_other.data ? p_other.data->clone() : nullptr;
}

WAnimationValues::WAnimationValues(WAnimationValues &&p_other) : data(p_other.data), keys(std::move(p_other.keys)) {
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
	keys.resize(p_size);
	if (data) {
		data->resize(p_size);
	}
}

void WAnimationValues::remove_at(uint32_t p_index) {
	keys.remove_at(p_index);
	if (data) {
		data->remove(p_index);
	}
}

void WAnimationValues::insert(uint32_t p_index, const WAnimationKey &p_key, const Variant &p_value) {
	ensure_type(p_value);
	keys.insert(p_index, p_key);
	data->insert(p_index);
	data->set(p_index, p_value);
}

void WAnimationValues::set(uint32_t p_index, const Variant &p_value) {
	ensure_type(p_value);
	data->set(p_index, p_value);
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
