// wgodot-changes::file
#pragma once

#include "wgodot_native_interface.h"

#include "scene/animation/tween.h"
#include "scene/resources/animation.h"

namespace WGodotNative {

// The accessors live in the tweener's allocation. No Callable, property names,
// or separately allocated callback state are needed. PropertyTweener still
// owns scheduling, interpolation, ObjectID validation and RefCounted retention.
template <class Target, class Getter, class Setter>
class NativePropertyTweener final : public PropertyTweener {
	using Value = std::invoke_result_t<Getter, const Target &>;
	// An interface handle carries both the Object and its adjusted interface
	// pointer. PropertyTweener tracks the Object; accessors use the typed handle.
	Target property_target;
	Getter getter;
	Setter setter;

	Variant wgodot_read_property(Object *) const override {
		return getter(property_target);
	}

	void wgodot_write_property(Object *, const Variant &p_value) const override {
		setter(property_target, convert<Value>(p_value));
	}

public:
	NativePropertyTweener(const Target &p_target, Getter p_getter, Setter p_setter, const Variant &p_to, double p_duration) :
			PropertyTweener(object_pointer(p_target), p_getter(p_target), p_to, p_duration), property_target(p_target), getter(p_getter), setter(p_setter) {}
};

template <class Target, class Getter, class Setter>
Ref<PropertyTweener> tween_property(Tween *p_tween, Target p_target, Getter p_getter, Setter p_setter, Variant p_to, double p_duration) {
	ERR_FAIL_NULL_V(p_tween, Ref<PropertyTweener>());
	ERR_FAIL_NULL_V(object_pointer(p_target), Ref<PropertyTweener>());
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
