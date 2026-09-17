// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

#include "scene/animation/tween.h"
#include "scene/resources/animation.h"

namespace WGodotNative {

// The accessors live in the tweener's allocation. No Callable, property names,
// or separately allocated callback state are needed. PropertyTweener still
// owns scheduling, interpolation, ObjectID validation and RefCounted retention.
template <class Target, class Getter, class Setter>
class NativePropertyTweener final : public PropertyTweener {
	using Value = std::invoke_result_t<Getter, Target *>;
	Getter getter;
	Setter setter;

	Variant wgodot_read_property(Object *p_target) const override {
		return getter(static_cast<Target *>(p_target));
	}

	void wgodot_write_property(Object *p_target, const Variant &p_value) const override {
		setter(static_cast<Target *>(p_target), convert<Value>(p_value));
	}

public:
	NativePropertyTweener(Target *p_target, Getter p_getter, Setter p_setter, const Variant &p_to, double p_duration) :
			PropertyTweener(p_target, p_getter(p_target), p_to, p_duration), getter(p_getter), setter(p_setter) {}
};

template <class Target, class Getter, class Setter>
Ref<PropertyTweener> tween_property(Tween *p_tween, Target *p_target, Getter p_getter, Setter p_setter, Variant p_to, double p_duration) {
	ERR_FAIL_NULL_V(p_tween, Ref<PropertyTweener>());
	ERR_FAIL_NULL_V(p_target, Ref<PropertyTweener>());
	if (!p_tween->wgodot_can_append()) {
		return Ref<PropertyTweener>();
	}
	// Match tween_property's validation read and its constructor's second read.
	// In particular, from_current() retains the constructor sample; ordinary
	// tweens sample again at start or after their delay, in PropertyTweener.
	if (!Animation::validate_type_match(p_getter(p_target), p_to)) {
		return Ref<PropertyTweener>();
	}
	using NativeTweener = NativePropertyTweener<Target, Getter, Setter>;
	Ref<PropertyTweener> tweener = memnew(NativeTweener(p_target, p_getter, p_setter, p_to, p_duration));
	p_tween->append(tweener);
	return tweener;
}

} // namespace WGodotNative
