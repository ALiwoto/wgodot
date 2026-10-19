// wgodot-changes::file
#include "w_animation.h"

namespace {
template <typename T>
Error validate_keys(const T *p_keys, uint32_t p_count) {
	for (uint32_t i = 0; i < p_count; i++) {
		ERR_FAIL_COND_V(!Math::is_finite(p_keys[i].time) || (i && p_keys[i].time < p_keys[i - 1].time), ERR_INVALID_DATA);
	}
	return OK;
}

template <typename T>
Error copy_keys(LocalVector<T> &r_keys, const T *p_keys, uint32_t p_count) {
	RETURN_IF_ERROR(validate_keys(p_keys, p_count));
	r_keys.resize(p_count);
	for (uint32_t i = 0; i < p_count; i++) {
		r_keys[i] = p_keys[i];
	}
	return OK;
}
} // namespace

#ifdef TOOLS_ENABLED
bool WAnimation::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name != SNAME("_codec_constants")) {
		return false;
	}
	PackedFloat64Array values = p_value;
	codec_constants.resize(values.size());
	for (int i = 0; i < values.size(); i++) {
		codec_constants.write[i] = { values[i], Math::is_finite(values[i]) };
	}
	return true;
}

bool WAnimation::_get(const StringName &p_name, Variant &r_value) const {
	if (p_name != SNAME("_codec_constants")) {
		return false;
	}
	PackedFloat64Array values;
	values.resize(codec_constants.size());
	for (int i = 0; i < values.size(); i++) {
		values.write[i] = codec_constants[i].defined ? codec_constants[i].value : Math::NaN;
	}
	r_value = values;
	return true;
}

void WAnimation::_get_property_list(List<PropertyInfo> *p_list) const {
	// Retain save IDs through disposable binary caches and editor duplication.
	// The text/native codecs omit this field; it is not animation key storage.
	if (!codec_constants.is_empty()) {
		p_list->push_back(PropertyInfo(Variant::PACKED_FLOAT64_ARRAY, "_codec_constants", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL));
	}
}
#endif

int WAnimation::append_track(const TrackSettings &p_settings) {
	int index = add_track(p_settings.type);
	track_set_path(index, p_settings.path);
	track_set_interpolation_type(index, p_settings.interpolation);
	track_set_enabled(index, p_settings.enabled);
	track_set_imported(index, p_settings.imported);
	track_set_interpolation_loop_wrap(index, p_settings.loop_wrap);
	if (p_settings.type == TYPE_VALUE) {
		value_track_set_update_mode(index, p_settings.update_mode);
	} else if (p_settings.type == TYPE_AUDIO) {
		audio_track_set_use_blend(index, p_settings.audio_blend);
	}
	return index;
}

WAnimation::TrackSettings WAnimation::track_settings(int p_track) const {
	TrackSettings settings;
	settings.type = track_get_type(p_track);
	settings.path = track_get_path(p_track);
	settings.interpolation = track_get_interpolation_type(p_track);
	settings.enabled = track_is_enabled(p_track);
	settings.imported = track_is_imported(p_track);
	settings.loop_wrap = track_get_interpolation_loop_wrap(p_track);
	if (settings.type == TYPE_VALUE) {
		settings.update_mode = value_track_get_update_mode(p_track);
	} else if (settings.type == TYPE_AUDIO) {
		settings.audio_blend = audio_track_is_use_blend(p_track);
	}
	return settings;
}

void WAnimation::clear_tracks() {
	while (get_track_count()) {
		remove_track(get_track_count() - 1);
	}
}

void WAnimation::replace_tracks(WAnimation &p_source) {
	clear_tracks();
	tracks = std::move(p_source.tracks);
#ifdef TOOLS_ENABLED
	codec_constants = std::move(p_source.codec_constants);
#endif
	_check_capture_included();
	emit_changed();
}

Error WAnimation::set_keyframes(int p_track, const VectorKey *p_keys, uint32_t p_count) {
	ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(p_track), tracks.size(), ERR_INVALID_PARAMETER);
	LocalVector<VectorKey> *keys = nullptr;
	if (tracks[p_track]->type == TYPE_POSITION_3D) {
		keys = &static_cast<PositionTrack *>(tracks[p_track])->positions;
	} else if (tracks[p_track]->type == TYPE_SCALE_3D) {
		keys = &static_cast<ScaleTrack *>(tracks[p_track])->scales;
	}
	ERR_FAIL_NULL_V(keys, ERR_INVALID_PARAMETER);
	Error result = copy_keys(*keys, p_keys, p_count);
	emit_changed();
	return result;
}

Error WAnimation::set_keyframes(int p_track, LocalVector<VectorKey> &&p_keys) {
	ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(p_track), tracks.size(), ERR_INVALID_PARAMETER);
	RETURN_IF_ERROR(validate_keys(p_keys.ptr(), p_keys.size()));
	if (tracks[p_track]->type == TYPE_POSITION_3D) {
		static_cast<PositionTrack *>(tracks[p_track])->positions = std::move(p_keys);
	} else if (tracks[p_track]->type == TYPE_SCALE_3D) {
		static_cast<ScaleTrack *>(tracks[p_track])->scales = std::move(p_keys);
	} else {
		return ERR_INVALID_PARAMETER;
	}
	emit_changed();
	return OK;
}

const LocalVector<WAnimation::VectorKey> &WAnimation::vector_keys(int p_track) const {
	if (track_get_type(p_track) == TYPE_POSITION_3D) {
		return static_cast<PositionTrack *>(tracks[p_track])->positions;
	}
	DEV_ASSERT(track_get_type(p_track) == TYPE_SCALE_3D);
	return static_cast<ScaleTrack *>(tracks[p_track])->scales;
}

#define W_ANIMATION_KEYS(m_key, m_method, m_type, m_track, m_values) \
	Error WAnimation::set_keyframes(int p_track, const m_key *p_keys, uint32_t p_count) { \
		ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(p_track), tracks.size(), ERR_INVALID_PARAMETER); \
		ERR_FAIL_COND_V(tracks[p_track]->type != m_type, ERR_INVALID_PARAMETER); \
		Error result = copy_keys(static_cast<m_track *>(tracks[p_track])->m_values, p_keys, p_count); \
		emit_changed(); \
		return result; \
	} \
	Error WAnimation::set_keyframes(int p_track, LocalVector<m_key> &&p_keys) { \
		ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(p_track), tracks.size(), ERR_INVALID_PARAMETER); \
		ERR_FAIL_COND_V(tracks[p_track]->type != m_type, ERR_INVALID_PARAMETER); \
		RETURN_IF_ERROR(validate_keys(p_keys.ptr(), p_keys.size())); \
		static_cast<m_track *>(tracks[p_track])->m_values = std::move(p_keys); \
		emit_changed(); \
		return OK; \
	} \
	const LocalVector<WAnimation::m_key> &WAnimation::m_method(int p_track) const { \
		DEV_ASSERT(track_get_type(p_track) == m_type); \
		return static_cast<m_track *>(tracks[p_track])->m_values; \
	}

W_ANIMATION_KEYS(RotationKey, rotation_keys, TYPE_ROTATION_3D, RotationTrack, rotations)
W_ANIMATION_KEYS(ScalarKey, scalar_keys, TYPE_BLEND_SHAPE, BlendShapeTrack, blend_shapes)
W_ANIMATION_KEYS(ValueKey, value_keys, TYPE_VALUE, ValueTrack, values)
W_ANIMATION_KEYS(MethodKey, method_keys, TYPE_METHOD, MethodTrack, methods)
W_ANIMATION_KEYS(BezierKey, bezier_keys, TYPE_BEZIER, BezierTrack, values)
W_ANIMATION_KEYS(AudioKey, audio_keys, TYPE_AUDIO, AudioTrack, values)
W_ANIMATION_KEYS(AnimationKey, animation_keys, TYPE_ANIMATION, AnimationTrack, values)
#undef W_ANIMATION_KEYS
