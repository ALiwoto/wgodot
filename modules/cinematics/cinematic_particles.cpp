// wgodot-changes::file
#include "cinematic_particles.h"

#include "core/object/class_db.h"

void CinematicParticles::prepare() {
	for (int i = 0; i < emitters.size(); i++) {
		GPUParticles3D *emitter = Object::cast_to<GPUParticles3D>(emitters[i]);
		ERR_CONTINUE(!emitter);
		emitter->set_speed_scale(0.0);
		emitter->set_emitting(false);
	}
	hide();
}

void CinematicParticles::seek(double p_seconds) {
	set_visible(p_seconds >= start_time);
	for (int i = 0; i < emitters.size(); i++) {
		GPUParticles3D *emitter = Object::cast_to<GPUParticles3D>(emitters[i]);
		ERR_CONTINUE(!emitter);
		emitter->restart(true);
		emitter->set_emitting(false);
		if (is_visible()) {
			emitter->request_particles_process(MAX(0.0, MIN(p_seconds, end_time) - start_time), MAX(0.0, p_seconds - end_time));
		}
	}
}

void CinematicParticles::advance(double p_previous, double p_seconds) {
	if (p_seconds <= start_time) {
		return;
	}
	show();
	double active = MAX(0.0, MIN(p_seconds, end_time) - MAX(p_previous, start_time));
	double trailing = MAX(0.0, p_seconds - MAX(p_previous, end_time));
	for (int i = 0; i < emitters.size(); i++) {
		GPUParticles3D *emitter = Object::cast_to<GPUParticles3D>(emitters[i]);
		ERR_CONTINUE(!emitter);
		emitter->request_particles_process(active, trailing);
	}
}

void CinematicParticles::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_emitters", "emitters"), &CinematicParticles::set_emitters);
	ClassDB::bind_method(D_METHOD("get_emitters"), &CinematicParticles::get_emitters);
	ClassDB::bind_method(D_METHOD("set_start_time", "time"), &CinematicParticles::set_start_time);
	ClassDB::bind_method(D_METHOD("get_start_time"), &CinematicParticles::get_start_time);
	ClassDB::bind_method(D_METHOD("set_end_time", "time"), &CinematicParticles::set_end_time);
	ClassDB::bind_method(D_METHOD("get_end_time"), &CinematicParticles::get_end_time);
	ClassDB::bind_method(D_METHOD("prepare"), &CinematicParticles::prepare);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &CinematicParticles::seek);
	ClassDB::bind_method(D_METHOD("advance", "previous", "seconds"), &CinematicParticles::advance);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "emitters", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:GPUParticles3D", Variant::OBJECT, PROPERTY_HINT_NODE_TYPE)), "set_emitters", "get_emitters");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_time", PROPERTY_HINT_RANGE, "0,3600,0.001,or_greater,suffix:s"), "set_start_time", "get_start_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "end_time", PROPERTY_HINT_RANGE, "0,3600,0.001,or_greater,suffix:s"), "set_end_time", "get_end_time");
}
