// wgodot-changes::file
#include "animation_mixer.h"

// Shader assignments establish the layout used by dependent typed parameter
// tracks. Resolve this dependency once per cache rebuild, not by asset ordering
// or by retrying parameter writes on a later frame.
void AnimationMixer::_blend_shader_tracks(double p_delta) {
	if (shader_track_caches.is_empty()) {
		return;
	}
	for (const AnimationInstance &ai : animation_instances) {
		const LocalVector<int> *indices = animation_shader_tracks.getptr(ai.animation);
		if (!indices) {
			continue;
		}
		const LocalVector<TrackCache *> &caches = animation_track_num_to_track_cache[ai.animation];
		const auto &tracks = ai.animation->get_tracks();
		Span<real_t> weights = ai.playback_info.track_weights ? *ai.playback_info.track_weights : Span<real_t>();
		for (int index : *indices) {
			if (!tracks[index]->enabled) {
				continue;
			}
			auto *track = static_cast<TrackCacheValue *>(caches[index]);
			ERR_CONTINUE(track->blend_idx < 0 || track->blend_idx >= track_count);
			real_t blend = ai.playback_info.weight;
			if (!weights.is_empty() && track->blend_idx < int(weights.size())) {
				blend *= weights[track->blend_idx];
			}
			if (!deterministic) {
				if (Math::is_zero_approx(track->total_weight)) {
					continue;
				}
				blend /= track->total_weight;
			}
			if (!Math::is_zero_approx(blend)) {
				_blend_value_track(ai, index, track, blend, p_delta);
			}
		}
	}
	for (TrackCacheValue *track : shader_track_caches) {
		_blend_apply_value(track);
	}
}

void AnimationMixer::_blend_value_track(const AnimationInstance &p_instance, int i, TrackCacheValue *t, real_t blend, double p_delta) {
	const Ref<Animation> &a = p_instance.animation;
	const PlaybackInfo &info = p_instance.playback_info;
	const double time = info.time;
	const double delta = info.delta;
	const double start = info.start;
	const double end = info.end;
	const bool seeked = info.seeked;
	const bool is_external_seeking = info.is_external_seeking;
	const auto looped_flag = info.looped_flag;
	const bool backward = std::signbit(delta);
	const bool seeked_backward = std::signbit(p_delta);
	const auto ttype = a->track_get_type(i);
	bool is_value = ttype == Animation::TYPE_VALUE;
	bool discrete = is_value && a->value_track_get_update_mode(i) == Animation::UPDATE_DISCRETE;
	bool force_continuous = callback_mode_discrete == ANIMATION_CALLBACK_MODE_DISCRETE_FORCE_CONTINUOUS;
	const auto post_process = value_post_process(a, i, t->post_process_object_id());
	if (!discrete || force_continuous) {
		t->use_continuous = true;
		WAnimationSample sample;
		double bezier = 0;
		if (!is_value) {
			bezier = a->bezier_track_interpolate(i, time);
		} else if (t->values->interpolatable()) {
			sample = a->value_track_sample(i, time, discrete && force_continuous ? backward : false);
		} else {
			sample.from = a->track_find_key(i, time, Animation::FIND_MODE_NEAREST, false, backward);
		}
		t->values->accumulate(**a, i, sample, bezier, blend, t->is_using_angle, post_process);
	} else if (seeked) {
		int key = a->track_find_key(i, time, is_external_seeking ? Animation::FIND_MODE_NEAREST : Animation::FIND_MODE_EXACT, false, seeked_backward);
		if (key < 0) {
			return;
		}
		t->use_discrete = true;
		if (Object *object = ObjectDB::get_instance(t->object_id)) {
			WAnimationValueMixer::write_key(object, t->target(), **a, i, key, post_process);
		}
	} else {
		LocalVector<int> indices;
		a->track_get_key_indices_in_range(i, time, delta, start, end, &indices, looped_flag);
		for (int key : indices) {
			t->use_discrete = true;
			if (Object *object = ObjectDB::get_instance(t->object_id)) {
				WAnimationValueMixer::write_key(object, t->target(), **a, i, key, post_process);
			}
		}
	}
}

void AnimationMixer::_blend_apply_value(TrackCacheValue *t) {
	const bool is_zero_amount = Math::is_zero_approx(t->total_weight);
	if (!deterministic && is_zero_amount) {
		return;
	}
	if (callback_mode_discrete == ANIMATION_CALLBACK_MODE_DISCRETE_FORCE_CONTINUOUS) {
		t->is_init = false; // Always update in Force Continuous.
	} else if (!t->use_continuous && (t->use_discrete || !deterministic)) {
		t->is_init = true; // If there is no continuous value and only discrete value is applied or just started, don't RESET.
	}

	if ((t->is_init && (is_zero_amount || !t->use_continuous)) ||
			(callback_mode_discrete != ANIMATION_CALLBACK_MODE_DISCRETE_FORCE_CONTINUOUS &&
					!is_zero_amount &&
					callback_mode_discrete == ANIMATION_CALLBACK_MODE_DISCRETE_DOMINANT &&
					t->use_discrete)) {
		return; // Don't overwrite the value set by UPDATE_DISCRETE.
	}

	if (callback_mode_discrete != ANIMATION_CALLBACK_MODE_DISCRETE_FORCE_CONTINUOUS) {
		t->is_init = !t->use_continuous; // If there is no Continuous in non-Force Continuous type, it means RESET.
	}

	if (Object *object = ObjectDB::get_instance(t->object_id)) {
		t->values->apply(object, t->target(), t->total_weight);
	}
}
