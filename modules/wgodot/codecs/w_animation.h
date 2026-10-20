// wgodot-changes::file
#pragma once

#include "scene/resources/animation.h"

// Uses Animation's own key storage and interpolation; only authoring/storage
// and bulk native construction differ. No parallel copy of the pose data.
class WAnimation : public Animation {
	GDCLASS(WAnimation, Animation);

protected:
	static void _bind_methods() {}
#ifdef TOOLS_ENABLED
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_value) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
#endif

public:
	// Editor save hints only. Playback keys contain copied values, never IDs
	// or pointers into this table. Gaps preserve IDs after unrelated edits.
	struct CodecConstant {
		double value = 0;
		bool defined = false;
	};

#ifdef TOOLS_ENABLED
private:
	mutable Vector<CodecConstant> codec_constants;

public:
	void set_codec_constants(const Vector<CodecConstant> &p_constants) const { codec_constants = p_constants; }
	const Vector<CodecConstant> &get_codec_constants() const { return codec_constants; }
#endif

	using VectorKey = Animation::TKey<Vector3>;
	using RotationKey = Animation::TKey<Quaternion>;
	using ScalarKey = Animation::TKey<float>;
	template <typename T> using TypedValueKey = Animation::TKey<T>;
	// Generic keys exist only at the text/editor API boundary.
	using ValueKey = Animation::TKey<Variant>;
	using MethodKey = Animation::MethodKey;
	using BezierValue = Animation::BezierKey;
	using BezierKey = Animation::TKey<BezierValue>;
	using AudioValue = Animation::AudioKey;
	using AudioKey = Animation::TKey<AudioValue>;
	using AnimationKey = Animation::TKey<StringName>;

	struct TrackSettings {
		TrackType type = TYPE_VALUE;
		NodePath path;
		InterpolationType interpolation = INTERPOLATION_LINEAR;
		bool enabled = true;
		bool imported = false;
		bool loop_wrap = true;
		UpdateMode update_mode = UPDATE_CONTINUOUS;
		bool audio_blend = true;
	};

	int append_track(const TrackSettings &p_settings);
	TrackSettings track_settings(int p_track) const;
	void clear_tracks();
	void replace_tracks(WAnimation &p_source);

#define W_ANIMATION_KEY_API(m_key, m_name) \
	Error set_keyframes(int p_track, const m_key *p_keys, uint32_t p_count); \
	Error set_keyframes(int p_track, LocalVector<m_key> &&p_keys); \
	const LocalVector<m_key> &m_name(int p_track) const;
	W_ANIMATION_KEY_API(VectorKey, vector_keys)
	W_ANIMATION_KEY_API(RotationKey, rotation_keys)
	W_ANIMATION_KEY_API(ScalarKey, scalar_keys)
	W_ANIMATION_KEY_API(MethodKey, method_keys)
	W_ANIMATION_KEY_API(BezierKey, bezier_keys)
	W_ANIMATION_KEY_API(AudioKey, audio_keys)
	W_ANIMATION_KEY_API(AnimationKey, animation_keys)
#undef W_ANIMATION_KEY_API
	Error set_keyframes(int p_track, const ValueKey *p_keys, uint32_t p_count);
	Error set_keyframes(int p_track, LocalVector<ValueKey> &&p_keys);
	LocalVector<ValueKey> value_keys(int p_track) const;

	template <typename T> Error set_value_keyframes(int p_track, const TypedValueKey<T> *p_keys, uint32_t p_count) {
		ERR_FAIL_COND_V(track_get_type(p_track) != TYPE_VALUE, ERR_INVALID_PARAMETER);
		LocalVector<WAnimationKey> times;
		LocalVector<T> values;
		times.resize(p_count);
		values.resize(p_count);
		for (uint32_t i = 0; i < p_count; i++) {
			ERR_FAIL_COND_V(!Math::is_finite(p_keys[i].time) || (i && p_keys[i].time < p_keys[i - 1].time), ERR_INVALID_DATA);
			times[i] = { p_keys[i].transition, p_keys[i].time };
			values[i] = p_keys[i].value;
		}
		value_track_storage(p_track).assign(std::move(times), std::move(values));
		emit_changed();
		return OK;
	}
};
