// wgodot-changes::file
#include "cinematic_shot.h"

#include "core/object/class_db.h"

void CinematicShot::set_length(double p_length) {
	ERR_FAIL_COND(!Math::is_finite(p_length) || p_length <= 0.0);
	length = p_length;
}

void CinematicShot::set_camera(Camera3D *p_camera) {
	camera_id = p_camera ? p_camera->get_instance_id() : ObjectID();
}

Camera3D *CinematicShot::get_camera() const {
	return Object::cast_to<Camera3D>(ObjectDB::get_instance(camera_id));
}

bool CinematicShot::prepare() {
	ERR_FAIL_NULL_V_MSG(get_camera(), false, vformat("Cinematic shot '%s' needs a camera.", get_name()));
	for (int i = 0; i < players.size(); i++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(players[i]);
		ERR_FAIL_NULL_V_MSG(player, false, vformat("Cinematic shot '%s' has an empty animation player.", get_name()));
		ERR_FAIL_COND_V_MSG(!player->has_animation(animation), false, vformat("Player '%s' has no animation '%s'.", player->get_name(), animation));
		player->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	}
	for (int i = 0; i < effects.size(); i++) {
		CinematicParticles *effect = Object::cast_to<CinematicParticles>(effects[i]);
		ERR_FAIL_NULL_V_MSG(effect, false, vformat("Cinematic shot '%s' has an empty particle interval.", get_name()));
		effect->prepare();
	}
	hide();
	emit_signal(SNAME("prepared"));
	return true;
}

void CinematicShot::activate(double p_seconds) {
	ERR_FAIL_NULL(get_camera());
	time_seconds = p_seconds;
	show();
	get_camera()->make_current();
	for (int i = 0; i < players.size(); i++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(players[i]);
		ERR_CONTINUE(!player);
		player->play(animation);
		// Seeking evaluates state without firing method/audio event tracks.
		player->seek(p_seconds, true, true);
	}
	emit_signal(SNAME("activated"), p_seconds);
	for (int i = 0; i < effects.size(); i++) {
		CinematicParticles *effect = Object::cast_to<CinematicParticles>(effects[i]);
		ERR_CONTINUE(!effect);
		effect->seek(p_seconds);
	}
}

void CinematicShot::advance(double p_delta) {
	for (int i = 0; i < players.size(); i++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(players[i]);
		ERR_CONTINUE(!player);
		player->advance(p_delta);
	}
	double previous = time_seconds;
	time_seconds += p_delta;
	emit_signal(SNAME("advanced"), previous, time_seconds);
	for (int i = 0; i < effects.size(); i++) {
		CinematicParticles *effect = Object::cast_to<CinematicParticles>(effects[i]);
		ERR_CONTINUE(!effect);
		effect->advance(previous, time_seconds);
	}
}

void CinematicShot::deactivate() {
	hide();
	for (int i = 0; i < players.size(); i++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(players[i]);
		if (player) {
			player->stop(true);
		}
	}
	emit_signal(SNAME("deactivated"));
}

void CinematicShot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_length", "length"), &CinematicShot::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &CinematicShot::get_length);
	ClassDB::bind_method(D_METHOD("set_animation", "animation"), &CinematicShot::set_animation);
	ClassDB::bind_method(D_METHOD("get_animation"), &CinematicShot::get_animation);
	ClassDB::bind_method(D_METHOD("set_players", "players"), &CinematicShot::set_players);
	ClassDB::bind_method(D_METHOD("get_players"), &CinematicShot::get_players);
	ClassDB::bind_method(D_METHOD("set_effects", "effects"), &CinematicShot::set_effects);
	ClassDB::bind_method(D_METHOD("get_effects"), &CinematicShot::get_effects);
	ClassDB::bind_method(D_METHOD("set_camera", "camera"), &CinematicShot::set_camera);
	ClassDB::bind_method(D_METHOD("get_camera"), &CinematicShot::get_camera);
	ClassDB::bind_method(D_METHOD("prepare"), &CinematicShot::prepare);
	ClassDB::bind_method(D_METHOD("activate", "seconds"), &CinematicShot::activate);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &CinematicShot::advance);
	ClassDB::bind_method(D_METHOD("deactivate"), &CinematicShot::deactivate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0.001,3600,0.001,or_greater,suffix:s"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "players", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:AnimationPlayer", Variant::OBJECT, PROPERTY_HINT_NODE_TYPE)), "set_players", "get_players");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"), "set_camera", "get_camera");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effects", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:CinematicParticles", Variant::OBJECT, PROPERTY_HINT_NODE_TYPE)), "set_effects", "get_effects");
	ADD_SIGNAL(MethodInfo("prepared"));
	ADD_SIGNAL(MethodInfo("activated", PropertyInfo(Variant::FLOAT, "seconds")));
	ADD_SIGNAL(MethodInfo("advanced", PropertyInfo(Variant::FLOAT, "previous"), PropertyInfo(Variant::FLOAT, "seconds")));
	ADD_SIGNAL(MethodInfo("deactivated"));
}
