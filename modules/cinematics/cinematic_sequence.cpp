// wgodot-changes::file
#include "cinematic_sequence.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

void CinematicSequence::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint() && autoplay) {
		play();
	} else if (p_what == NOTIFICATION_INTERNAL_PROCESS && !Engine::get_singleton()->is_editor_hint()) {
		advance_playback(get_process_delta_time());
	}
}

void CinematicSequence::set_shots(const TypedArray<CinematicShot> &p_shots) {
	pause();
	shots = p_shots;
	prepared = false;
	shot_index = -1;
}

void CinematicSequence::set_presentation(AnimationPlayer *p_player) {
	presentation_id = p_player ? p_player->get_instance_id() : ObjectID();
}

AnimationPlayer *CinematicSequence::get_presentation() const {
	return Object::cast_to<AnimationPlayer>(ObjectDB::get_instance(presentation_id));
}

CinematicShot *CinematicSequence::get_shot(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, shots.size(), nullptr);
	return Object::cast_to<CinematicShot>(shots[p_index]);
}

bool CinematicSequence::prepare() {
	pause();
	prepared = false;
	length = 0.0;
	time_seconds = 0.0;
	shot_index = -1;
	shot_start = 0.0;
	ERR_FAIL_COND_V_MSG(shots.is_empty(), false, vformat("Cinematic sequence '%s' needs at least one shot.", get_name()));
	for (int i = 0; i < shots.size(); i++) {
		CinematicShot *shot = get_shot(i);
		ERR_FAIL_NULL_V_MSG(shot, false, vformat("Cinematic sequence '%s' has an empty shot at index %d.", get_name(), i));
		if (!shot->prepare()) {
			return false;
		}
		length += shot->get_length();
	}
	if (AnimationPlayer *presentation = get_presentation()) {
		ERR_FAIL_COND_V_MSG(!presentation->has_animation(animation), false, vformat("Presentation player has no animation '%s'.", animation));
		presentation->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
		presentation->set_assigned_animation(animation);
	}
	prepared = true;
	return true;
}

double CinematicSequence::get_shot_start(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, shots.size(), 0.0);
	double start = 0.0;
	for (int i = 0; i < p_index; i++) {
		CinematicShot *shot = get_shot(i);
		ERR_FAIL_NULL_V(shot, 0.0);
		start += shot->get_length();
	}
	return start;
}

void CinematicSequence::_select_shot() {
	if (shot_index >= 0) {
		if (CinematicShot *previous = get_shot(shot_index)) {
			previous->deactivate();
		}
	}
	shot_start = 0.0;
	shot_index = 0;
	while (shot_index < shots.size() - 1) {
		CinematicShot *shot = get_shot(shot_index);
		ERR_FAIL_NULL(shot);
		double end = shot_start + shot->get_length();
		if (time_seconds < end) {
			break;
		}
		shot_start = end;
		shot_index++;
	}
	CinematicShot *shot = get_shot(shot_index);
	ERR_FAIL_NULL(shot);
	shot->activate(time_seconds - shot_start);
}

void CinematicSequence::advance_playback(double p_delta) {
	if (!playing) {
		return;
	}
	ERR_FAIL_COND(!Math::is_finite(p_delta) || p_delta < 0.0);
	double elapsed = MIN(p_delta, length - time_seconds);
	time_seconds += elapsed;
	CinematicShot *shot = get_shot(shot_index);
	if (!shot) {
		pause();
		ERR_FAIL_MSG("The active cinematic shot was freed during playback.");
	}
	if (time_seconds >= shot_start + shot->get_length()) {
		_select_shot();
	} else {
		shot->advance(elapsed);
	}
	if (AnimationPlayer *presentation = get_presentation()) {
		presentation->advance(elapsed);
	}
	if (time_seconds >= length) {
		pause();
		emit_signal(SNAME("finished"));
	}
}

void CinematicSequence::pause() {
	playing = false;
	set_process_internal(false);
	if (AnimationPlayer *presentation = get_presentation()) {
		presentation->pause();
	}
}

void CinematicSequence::show_frame(double p_seconds) {
	ERR_FAIL_COND(!Math::is_finite(p_seconds));
	if (!prepared && !prepare()) {
		return;
	}
	pause();
	time_seconds = CLAMP(p_seconds, 0.0, length);
	_select_shot();
	if (AnimationPlayer *presentation = get_presentation()) {
		presentation->seek(time_seconds, true, true);
	}
}

void CinematicSequence::play() {
	if (!prepared && !prepare()) {
		return;
	}
	if (time_seconds >= length) {
		time_seconds = 0.0;
	}
	_select_shot();
	if (AnimationPlayer *presentation = get_presentation()) {
		presentation->play(animation);
		presentation->seek(time_seconds, true);
	}
	playing = true;
	set_process_internal(!Engine::get_singleton()->is_editor_hint());
}

void CinematicSequence::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shots", "shots"), &CinematicSequence::set_shots);
	ClassDB::bind_method(D_METHOD("get_shots"), &CinematicSequence::get_shots);
	ClassDB::bind_method(D_METHOD("set_presentation", "player"), &CinematicSequence::set_presentation);
	ClassDB::bind_method(D_METHOD("get_presentation"), &CinematicSequence::get_presentation);
	ClassDB::bind_method(D_METHOD("set_animation", "animation"), &CinematicSequence::set_animation);
	ClassDB::bind_method(D_METHOD("get_animation"), &CinematicSequence::get_animation);
	ClassDB::bind_method(D_METHOD("set_autoplay", "autoplay"), &CinematicSequence::set_autoplay);
	ClassDB::bind_method(D_METHOD("is_autoplay"), &CinematicSequence::is_autoplay);
	ClassDB::bind_method(D_METHOD("get_length"), &CinematicSequence::get_length);
	ClassDB::bind_method(D_METHOD("get_time_seconds"), &CinematicSequence::get_time_seconds);
	ClassDB::bind_method(D_METHOD("get_shot_index"), &CinematicSequence::get_shot_index);
	ClassDB::bind_method(D_METHOD("get_shot_count"), &CinematicSequence::get_shot_count);
	ClassDB::bind_method(D_METHOD("get_shot", "index"), &CinematicSequence::get_shot);
	ClassDB::bind_method(D_METHOD("get_shot_start", "index"), &CinematicSequence::get_shot_start);
	ClassDB::bind_method(D_METHOD("is_playing"), &CinematicSequence::is_playing);
	ClassDB::bind_method(D_METHOD("prepare"), &CinematicSequence::prepare);
	ClassDB::bind_method(D_METHOD("advance_playback", "delta"), &CinematicSequence::advance_playback);
	ClassDB::bind_method(D_METHOD("show_frame", "seconds"), &CinematicSequence::show_frame);
	ClassDB::bind_method(D_METHOD("play"), &CinematicSequence::play);
	ClassDB::bind_method(D_METHOD("pause"), &CinematicSequence::pause);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "shots", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:CinematicShot", Variant::OBJECT, PROPERTY_HINT_NODE_TYPE)), "set_shots", "get_shots");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "presentation", PROPERTY_HINT_NODE_TYPE, "AnimationPlayer"), "set_presentation", "get_presentation");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_seconds", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_time_seconds");
	ADD_SIGNAL(MethodInfo("finished"));
}
