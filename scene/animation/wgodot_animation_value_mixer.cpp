// wgodot-changes::file
#include "wgodot_animation_value_mixer.h"
#include "scene/resources/wgodot_animation_math.h"

namespace {
using namespace WAnimationMath;

template <typename T>
bool sample_blend(const WAnimationValues &p_values, const WAnimationSample &p_sample, Blend<T> &r_value) {
	if (p_values.kind() == WAnimationValueType<T>::kind) {
		r_value = sample_for_blending(p_values.typed<T>(), p_sample);
		return true;
	}
	if constexpr (std::is_arithmetic_v<T>) {
		switch (p_values.kind()) {
			case WAnimationValueKind::Float: r_value = sample(p_values.typed<double>(), p_sample); return true;
			case WAnimationValueKind::Int: r_value = sample(p_values.typed<int64_t>(), p_sample); return true;
			case WAnimationValueKind::Bool: r_value = sample(p_values.typed<bool>(), p_sample); return true;
			case WAnimationValueKind::Number: r_value = sample(p_values.typed<WAnimationNumber>(), p_sample).as_double(); return true;
			default: break;
		}
	}
	ERR_FAIL_V_MSG(false, "Blended animation tracks have incompatible value types.");
}

template <typename T> class TypedMixer final : public WAnimationValueMixer {
	T initial;
	Blend<T> value;
	double element_size = 0;

public:
	explicit TypedMixer(const T &p_initial) : initial(p_initial) { begin(); }
	WAnimationValueMixer *clone() const override { return memnew(TypedMixer<T>(*this)); }
	Variant initial_value() const override { return initial; }
	bool is_string() const override { return WAnimationMath::is_string<T>; }
	bool interpolatable() const override {
		constexpr Variant::Type type = WAnimationValueType<T>::variant;
		return (type >= Variant::BOOL && type <= Variant::STRING_NAME) || type >= Variant::PACKED_INT32_ARRAY;
	}
	void begin() override {
		value = to_blend(initial);
		if constexpr (WAnimationMath::is_string<T>) {
			element_size = String(initial).length();
		} else {
			element_size = 0;
		}
	}
	void accumulate(const Animation &p_animation, int p_track, const WAnimationSample &p_sample, double p_bezier_value, float p_weight, bool p_angle, const WAnimationValuePostProcess &p_post_process) override {
		Blend<T> sampled;
		if (p_animation.track_get_type(p_track) == Animation::TYPE_BEZIER) {
			if constexpr (std::is_arithmetic_v<T>) {
				sampled = p_bezier_value;
			} else {
				ERR_FAIL_MSG("A Bezier track must animate a numeric property.");
			}
		} else {
			if (p_sample.from < 0 || !sample_blend<T>(p_animation.value_track_storage(p_track), p_sample, sampled)) {
				return;
			}
		}
		if (p_post_process.invoke) {
			// The public hook receives the sampled key type, not the blend storage.
			// Ordinary playback has no hook and never constructs this Variant.
			Variant input = p_animation.track_get_type(p_track) == Animation::TYPE_BEZIER ? Variant(p_bezier_value) : p_animation.value_track_storage(p_track).sample_variant(p_sample);
			Variant result = p_post_process.invoke(p_post_process.context, p_post_process.animation, p_post_process.track, input, p_post_process.object, p_post_process.sub_index);
			if (result.get_type() == Variant::NIL) {
				return;
			}
			if constexpr (std::is_arithmetic_v<T>) {
				sampled = double(result);
			} else {
				sampled = to_blend(T(result));
			}
		}
		if (!interpolatable()) {
			value = sampled;
			return;
		}
		if constexpr (std::is_arithmetic_v<T>) {
			if (p_angle) {
				value = Animation::interpolate_via_rest(value, sampled, p_weight, to_blend(initial));
				return;
			}
		}
		if constexpr (WAnimationMath::is_string<T>) {
			element_size += (sampled.size() - double(String(initial).length())) * p_weight;
		} else if constexpr (ArrayTraits<T>::enabled) {
			element_size = MAX(element_size, double(sampled.size()));
		}
		value = blend(value, subtract(sampled, to_blend(initial)), p_weight);
	}
	void apply(Object *p_object, const WAnimationValueTarget &p_target, real_t p_weight) override {
		if constexpr (ArrayTraits<Blend<T>>::enabled) {
			int count = int(Math::round(Math::abs(element_size)));
			real_t weight = Math::abs(p_weight);
			if (count < value.size()) {
				if (weight >= 1.0) {
					value.resize(count);
				} else if constexpr (WAnimationMath::is_string<T>) {
					value.resize(int(Math::round(Math::lerp(double(String(initial).length()), double(count), double(float(weight))))));
				}
			}
		}
		p_target.write(p_object, from_blend<T>(value));
	}
	void backup(Object *p_object, const WAnimationValueTarget &p_target) override {
		value = to_blend(p_target.read<T>(p_object));
		if constexpr (ArrayTraits<Blend<T>>::enabled) {
			element_size = value.size();
		}
	}
	void capture(Object *p_object, const WAnimationValueTarget &p_target, Animation &r_animation, int p_track) const override {
		LocalVector<WAnimationKey> keys;
		keys.push_back({ 1.0, 0.0 });
		LocalVector<T> values;
		values.push_back(p_target.read<T>(p_object));
		r_animation.value_track_storage(p_track).assign(std::move(keys), std::move(values));
	}
};

// Official Animation permits heterogeneous dictionaries/arrays and arbitrary
// objects. Keep their editor/VM semantics at this explicit generic boundary;
// native WAnimation export rejects these instead of silently boxing playback.
class GenericMixer final : public WAnimationValueMixer {
	Variant initial;
	Variant value;
	double element_size = 0;
public:
	explicit GenericMixer(const Variant &p_initial) : initial(p_initial) { begin(); }
	WAnimationValueMixer *clone() const override { return memnew(GenericMixer(*this)); }
	Variant initial_value() const override { return initial; }
	bool generic() const override { return true; }
	bool is_string() const override { return initial.is_string(); }
	bool interpolatable() const override { return Animation::is_variant_interpolatable(initial); }
	void begin() override {
		value = Animation::cast_to_blendwise(initial);
		element_size = initial.is_string() ? String(initial).length() : 0;
	}
	void accumulate(const Animation &p_animation, int p_track, const WAnimationSample &p_sample, double p_bezier_value, float p_weight, bool p_angle, const WAnimationValuePostProcess &p_post_process) override {
		Variant sampled = p_animation.track_get_type(p_track) == Animation::TYPE_BEZIER ? Variant(p_bezier_value) : p_animation.value_track_storage(p_track).sample_variant(p_sample);
		if (!p_post_process.apply(sampled) || sampled.get_type() == Variant::NIL) {
			return;
		}
		if (!interpolatable()) {
			value = sampled;
			return;
		}
		if (p_angle) {
			value = Animation::interpolate_via_rest(double(value), double(sampled), p_weight, double(initial));
			return;
		}
		sampled = Animation::cast_to_blendwise(sampled);
		if (initial.is_string()) {
			element_size += (Array(sampled).size() - double(String(initial).length())) * p_weight;
		} else if (initial.is_array()) {
			element_size = MAX(element_size, double(Array(sampled).size()));
		}
		value = Animation::blend_variant(value, Animation::subtract_variant(sampled, Animation::cast_to_blendwise(initial)), p_weight);
	}
	void apply(Object *p_object, const WAnimationValueTarget &p_target, real_t p_weight) override {
		if (value.is_array()) {
			int count = int(Math::round(Math::abs(element_size)));
			real_t weight = Math::abs(p_weight);
			if (count < Array(value).size()) {
				if (weight >= 1.0) {
					Array(value).resize(count);
				} else if (initial.is_string()) {
					Array(value).resize(Animation::interpolate_variant(String(initial).length(), count, weight));
				}
			}
		}
		Variant result = Animation::cast_from_blendwise(value, initial.get_type());
		if (p_target.property) {
			p_target.property->write_variant(p_target.receiver(p_object), result);
		} else {
			p_object->set_indexed(p_target.path, result);
		}
	}
	void backup(Object *p_object, const WAnimationValueTarget &p_target) override {
		value = Animation::cast_to_blendwise(p_target.property ? p_target.property->read_variant(p_target.receiver(p_object)) : p_object->get_indexed(p_target.path));
		if (value.is_array()) {
			element_size = Array(value).size();
		}
	}
	void capture(Object *p_object, const WAnimationValueTarget &p_target, Animation &r_animation, int p_track) const override {
		r_animation.track_insert_key(p_track, 0, p_target.property ? p_target.property->read_variant(p_target.receiver(p_object)) : p_object->get_indexed(p_target.path));
	}
};
} // namespace

WAnimationValueMixer *WAnimationValueMixer::create(const Variant &p_initial, bool p_generic) {
	if (p_generic) {
		return memnew(GenericMixer(p_initial));
	}
	if (p_initial.get_type() == Variant::NIL || p_initial.get_type() == Variant::OBJECT) {
		Object *object = p_initial;
		if (!object || Object::cast_to<Resource>(object)) {
			return memnew(TypedMixer<Ref<Resource>>(Ref<Resource>(Object::cast_to<Resource>(object))));
		}
		return memnew(GenericMixer(p_initial));
	}
	switch (p_initial.get_type()) {
#define W_CREATE_MIXER(m_name, m_type, m_variant) case Variant::m_variant: return memnew(TypedMixer<m_type>(p_initial));
		W_ANIMATION_VALUE_TYPES(W_CREATE_MIXER)
#undef W_CREATE_MIXER
		default: return memnew(GenericMixer(p_initial));
	}
}

WAnimationValueMixer *WAnimationValueMixer::create(const Animation &p_animation, int p_track, bool p_zero) {
	if (p_animation.track_get_type(p_track) == Animation::TYPE_BEZIER) {
		return memnew(TypedMixer<double>(p_zero ? 0.0 : double(p_animation.bezier_track_get_key_value(p_track, 0))));
	}
	return p_animation.value_track_storage(p_track).visit([&](const auto &values) -> WAnimationValueMixer * {
		using T = std::decay_t<decltype(values[0])>;
		if constexpr (std::is_same_v<T, Variant>) {
			Variant initial = values[0];
			if (p_zero) {
				initial.zero();
			}
			return memnew(GenericMixer(initial));
		} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
			return values[0].is_integer ? static_cast<WAnimationValueMixer *>(memnew(TypedMixer<int64_t>(p_zero ? 0 : values[0].integer))) : memnew(TypedMixer<double>(p_zero ? 0.0 : values[0].real));
		} else {
			return memnew(TypedMixer<T>(p_zero ? T() : values[0]));
		}
	});
}

void WAnimationValueMixer::write_key(Object *p_object, const WAnimationValueTarget &p_target, const Animation &p_animation, int p_track, int p_key, const WAnimationValuePostProcess &p_post_process) {
	if (p_post_process.invoke) {
		Variant value = p_animation.track_get_key_value(p_track, p_key);
		p_post_process.apply(value);
		if (p_target.property) {
			p_target.property->write_variant(p_target.receiver(p_object), value);
		} else {
			p_object->set_indexed(p_target.path, value);
		}
		return;
	}
	p_animation.value_track_storage(p_track).visit([&](const auto &values) {
		using T = std::decay_t<decltype(values[0])>;
		T value = values[p_key];
		if constexpr (std::is_same_v<T, Variant>) {
			p_post_process.apply(value);
			if (p_target.property) {
				p_target.property->write_variant(p_target.receiver(p_object), value);
			} else {
				p_object->set_indexed(p_target.path, value);
			}
		} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
			if (value.is_integer) {
				int64_t number = value.integer;
				p_post_process.apply(number);
				p_target.write(p_object, number);
			} else {
				double number = value.real;
				p_post_process.apply(number);
				p_target.write(p_object, number);
			}
		} else {
			p_post_process.apply(value);
			p_target.write(p_object, value);
		}
	});
}
