// wgodot-changes::file
#pragma once

#include "wgodot_animation_property.h"
#include "scene/resources/animation.h"

// Present only when an actual script/extension overrides the generic hook.
// Ordinary editor playback and native playback never cross this boundary.
struct WAnimationValuePostProcess {
	void *context = nullptr;
	const Animation *animation = nullptr;
	int track = 0;
	ObjectID object;
	int sub_index = -1;
	Variant (*invoke)(void *, const Animation *, int, const Variant &, ObjectID, int) = nullptr;

	template <typename T> bool apply(T &r_value) const {
		if (!invoke) {
			return true;
		}
		Variant value = invoke(context, animation, track, r_value, object, sub_index);
		if (value.get_type() == Variant::NIL) {
			r_value = T();
			return false;
		}
		r_value = value;
		return true;
	}
};

struct WAnimationValueTarget {
	const WGodotAnimationProperty *property = nullptr;
	Vector<StringName> path;
	Ref<RefCounted> context;
	Object *receiver(Object *p_object) const { return context.is_valid() ? context.ptr() : p_object; }

	template <typename T> T read(Object *p_object) const {
		p_object = receiver(p_object);
		if (!property) {
			return p_object->get_indexed(path);
		}
		if (property->kind == WAnimationValueType<T>::kind) {
			return static_cast<const WGodotAnimationPropertyTyped<T> *>(property)->read(p_object);
		}
		if constexpr (std::is_arithmetic_v<T>) {
			return T(property->read_number(p_object));
		} else {
			ERR_FAIL_V_MSG(T(), "Animation property and track types do not match.");
		}
	}

	template <typename T> void write(Object *p_object, const T &p_value) const {
		p_object = receiver(p_object);
		if (!property) {
			p_object->set_indexed(path, p_value);
			return;
		}
		if (property->kind == WAnimationValueType<T>::kind) {
			static_cast<const WGodotAnimationPropertyTyped<T> *>(property)->write(p_object, p_value);
			return;
		}
		if constexpr (std::is_arithmetic_v<T>) {
			property->write_number(p_object, double(p_value));
		} else {
			ERR_FAIL_MSG("Animation property and track types do not match.");
		}
	}
};

class WAnimationValueMixer {
public:
	virtual ~WAnimationValueMixer() = default;
	static WAnimationValueMixer *create(const Variant &p_initial, bool p_generic = false);
	static WAnimationValueMixer *create(const Animation &p_animation, int p_track, bool p_zero);
	virtual WAnimationValueMixer *clone() const = 0;
	virtual Variant initial_value() const = 0;
	virtual bool generic() const { return false; }
	virtual bool is_string() const = 0;
	virtual bool interpolatable() const = 0;
	virtual void begin() = 0;
	virtual void accumulate(const Animation &p_animation, int p_track, const WAnimationSample &p_sample, double p_bezier_value, float p_weight, bool p_angle, const WAnimationValuePostProcess &p_post_process) = 0;
	virtual void apply(Object *p_object, const WAnimationValueTarget &p_target, real_t p_weight) = 0;
	virtual void backup(Object *p_object, const WAnimationValueTarget &p_target) = 0;
	virtual void capture(Object *p_object, const WAnimationValueTarget &p_target, Animation &r_animation, int p_track) const = 0;
	static void write_key(Object *p_object, const WAnimationValueTarget &p_target, const Animation &p_animation, int p_track, int p_key, const WAnimationValuePostProcess &p_post_process);
};
