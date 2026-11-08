// wgodot-changes::file
#pragma once

#include "native_payload.h"
#include "w_animation.h"

namespace WGodotNativePayload {
constexpr uint32_t ANIMATION_KEYS = 1;

template <>
struct Value<WAnimationNumber> {
	static WAnimationNumber read(Input &p_input) {
		const bool integer = Value<bool>::read(p_input);
		return integer ? WAnimationNumber(Value<int64_t>::read(p_input)) : WAnimationNumber(Value<double>::read(p_input));
	}
	static void write(FileAccess &p_file, const WAnimationNumber &p_value) {
		Value<bool>::write(p_file, p_value.is_integer);
		if (p_value.is_integer) {
			Value<int64_t>::write(p_file, p_value.integer);
		} else {
			Value<double>::write(p_file, p_value.real);
		}
	}
};

template <>
struct Value<WAnimation::BezierValue> {
	static WAnimation::BezierValue read(Input &p_input) {
		WAnimation::BezierValue result;
		result.value = Value<real_t>::read(p_input);
		result.in_handle = Value<Vector2>::read(p_input);
		result.out_handle = Value<Vector2>::read(p_input);
		[[maybe_unused]] const int32_t mode = Value<int32_t>::read(p_input);
#ifdef TOOLS_ENABLED
		result.handle_mode = Animation::HandleMode(mode);
#endif
		return result;
	}
	static void write(FileAccess &p_file, const WAnimation::BezierValue &p_value) {
		Value<real_t>::write(p_file, p_value.value);
		Value<Vector2>::write(p_file, p_value.in_handle);
		Value<Vector2>::write(p_file, p_value.out_handle);
#ifdef TOOLS_ENABLED
		Value<int32_t>::write(p_file, p_value.handle_mode);
#else
		Value<int32_t>::write(p_file, 0);
#endif
	}
};

template <class T>
struct Value<WAnimation::TypedValueKey<T>> {
	static WAnimation::TypedValueKey<T> read(Input &p_input) {
		WAnimation::TypedValueKey<T> result;
		result.transition = Value<real_t>::read(p_input);
		result.time = Value<double>::read(p_input);
		result.value = Value<T>::read(p_input);
		return result;
	}
	static void write(FileAccess &p_file, const WAnimation::TypedValueKey<T> &p_value) {
		Value<real_t>::write(p_file, p_value.transition);
		Value<double>::write(p_file, p_value.time);
		Value<T>::write(p_file, p_value.value);
	}
};

template <class T>
struct Value<LocalVector<WAnimation::TypedValueKey<T>>> : LocalValues<WAnimation::TypedValueKey<T>, sizeof(real_t) + sizeof(double)> {};
} // namespace WGodotNativePayload
