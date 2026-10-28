// wgodot-changes::file
#include "particle_simulation_3d.h"
#include "core/object/class_db.h"

void ParticleSimulation3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("flush_spawn_parameters"), &ParticleSimulation3D::flush_spawn_parameters);
	ClassDB::bind_method(D_METHOD("reset", "pool", "authored", "legacy_spawning", "initial_values", "initial_random", "ground", "context", "follow_sources"), &ParticleSimulation3D::reset);
	ClassDB::bind_method(D_METHOD("advance", "seconds", "delta", "random", "parameter_ratio", "emitter_seconds", "birth_offset"), &ParticleSimulation3D::advance);
	ClassDB::bind_method(D_METHOD("spawn_continuous", "count", "age", "interval"), &ParticleSimulation3D::spawn_continuous);
	ClassDB::bind_method(D_METHOD("spawn_burst", "count"), &ParticleSimulation3D::spawn_burst);
	ClassDB::bind_method(D_METHOD("clear"), &ParticleSimulation3D::clear);
	ClassDB::bind_method(D_METHOD("has_particles"), &ParticleSimulation3D::has_particles);
	ClassDB::bind_method(D_METHOD("bind_source", "context", "channel"), &ParticleSimulation3D::bind_source);
	ClassDB::bind_method(D_METHOD("get_active_particle_count"), &ParticleSimulation3D::get_active_particle_count);
	ClassDB::bind_method(D_METHOD("get_active_particle", "index"), &ParticleSimulation3D::get_active_particle);
	ClassDB::bind_method(D_METHOD("get_simulation_aabb"), &ParticleSimulation3D::get_simulation_aabb);
	ClassDB::bind_method(D_METHOD("set_collect_simulation_bounds", "enabled"), &ParticleSimulation3D::set_collect_simulation_bounds);
	ClassDB::bind_method(D_METHOD("has_active_particle", "particle"), &ParticleSimulation3D::has_active_particle);
	ClassDB::bind_method(D_METHOD("get_spawned_count"), &ParticleSimulation3D::get_spawned_count);
	ClassDB::bind_method(D_METHOD("remaining_seconds"), &ParticleSimulation3D::remaining_seconds);
#ifdef TOOLS_ENABLED
	// Replay diagnostics can supply otherwise unseeded inputs and observe RNG
	// consumption. Exported simulations contain neither hooks nor callbacks.
	ADD_SIGNAL(MethodInfo("debug_particle_prepared", PropertyInfo(Variant::OBJECT, "particle", PROPERTY_HINT_RESOURCE_TYPE, "ParticleInstance3D")));
	ADD_SIGNAL(MethodInfo("debug_particle_sampled", PropertyInfo(Variant::INT, "random_before"), PropertyInfo(Variant::INT, "random_after")));
#endif
}

void ParticleSimulation3D::reset(ParticleInstancePool3D *p_pool, const TypedArray<ParticleInstance3D> &p_authored, bool p_legacy_spawning, const TypedArray<ParticleBirthRule3D> &p_initial_values, const TypedArray<RandomStream32> &p_initial_random, ParticleGroundProjector3D *p_ground, const Ref<ParticleSpawnContext3D> &p_context, bool p_follow_sources) {
	ERR_FAIL_COND(p_initial_random.size() != p_initial_values.size());
	clear();
	pool = p_pool;
	authored = p_authored;
	legacy_spawning = p_legacy_spawning;
	initial_values = p_initial_values;
	initial_random = p_initial_random;
	ground = p_ground;
	context = p_context;
	follow_sources = p_follow_sources;
	spawned_count = 0;
	for (int i = 0; i < authored.size(); ++i) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(authored[i]);
		particle->reset_playback();
	}
}

ParticleInstance3D *ParticleSimulation3D::linked_particle(int p_index) const {
	if (!follow_sources) {
		return nullptr;
	}
	const TypedArray<ParticleInstance3D> sources = context->get_sources();
	return p_index < sources.size() ? Object::cast_to<ParticleInstance3D>(sources[p_index]) : nullptr;
}

bool ParticleSimulation3D::sample(ParticleInstance3D *p_particle, double p_age, Vector3 p_offset, int p_index) {
	const bool alive = p_particle->sample_playback(seconds, delta, random, initial_values, p_age, ground, p_offset, parameter_ratio, emitter_seconds, initial_random, context, linked_particle(p_index));
	if (collect_simulation_bounds && alive) {
		const AABB bounds = p_particle->get_simulation_aabb();
		simulation_bounds = simulation_bounds_valid ? simulation_bounds.merge(bounds) : bounds;
		simulation_bounds_valid = true;
	}
	return alive;
}

bool ParticleSimulation3D::advance(double p_seconds, double p_delta, const Ref<RandomStream32> &p_random, double p_parameter_ratio, double p_emitter_seconds, Vector3 p_birth_offset) {
	seconds = p_seconds;
	delta = p_delta;
	random = p_random;
	parameter_ratio = p_parameter_ratio;
	emitter_seconds = p_emitter_seconds;
	birth_offset = p_birth_offset;
	simulation_bounds = AABB();
	simulation_bounds_valid = false;
#ifdef TOOLS_ENABLED
	debug_prepare = has_connections(SNAME("debug_particle_prepared"));
	debug_sample = has_connections(SNAME("debug_particle_sampled"));
#endif
	for (int i = active.size() - 1; i >= 0; --i) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(active[i]);
		if (!sample(particle, -1.0, Vector3(), i)) {
			remove_particle(i);
		}
	}
	return has_particles();
}

void ParticleSimulation3D::bind_source(const Ref<ParticleSpawnContext3D> &p_context, int p_channel) const {
	ERR_FAIL_COND(p_context.is_null());
	p_context->set_source_channel(p_channel, active);
}

ParticleInstance3D *ParticleSimulation3D::get_active_particle(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, active.size(), nullptr);
	return Object::cast_to<ParticleInstance3D>(active[p_index]);
}

void ParticleSimulation3D::flush_spawn_parameters() {
	if (!context->get_surface_ray() || !context->has_surface_plane_changed()) {
		return;
	}
	for (int i = 0; i < active.size(); ++i) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(active[i]);
		particle->publish_surface_plane(context->get_surface_plane());
	}
	context->clear_surface_plane_changed();
}

bool ParticleSimulation3D::uses_legacy_spawning() const {
	if (pool) {
		return legacy_spawning;
	}
	ERR_FAIL_INDEX_V(spawned_count, authored.size(), false);
	const ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(authored[spawned_count]);
	return particle->get_advance_on_spawn();
}

bool ParticleSimulation3D::spawn(double p_age, double p_interpolation) {
	ParticleInstance3D *particle;
	if (pool) {
		particle = pool->acquire();
		ERR_FAIL_NULL_V(particle, false);
		particle->reset_playback();
	} else {
		ERR_FAIL_INDEX_V(spawned_count, authored.size(), false);
		particle = Object::cast_to<ParticleInstance3D>(authored[spawned_count]);
	}
	particle->set_spawn_index(spawned_count++);
#ifdef TOOLS_ENABLED
	if (debug_prepare) {
		emit_signal(SNAME("debug_particle_prepared"), particle);
	}
	const int64_t random_before = random->get_state();
#endif
	particle->set_birth(seconds - delta);
	const bool alive = sample(particle, p_age, birth_offset * p_interpolation, active.size());
	if (alive) {
		active.push_back(particle);
	} else if (pool) {
		pool->release(particle);
	}
#ifdef TOOLS_ENABLED
	if (debug_sample) {
		emit_signal(SNAME("debug_particle_sampled"), random_before, random->get_state());
	}
#endif
	return alive;
}

void ParticleSimulation3D::spawn_continuous(int p_count, double p_age, double p_interval) {
	double interpolation = 1.0;
	for (int i = 0; i < p_count; ++i) {
		const bool legacy = uses_legacy_spawning();
		if (legacy) {
			interpolation -= 1.0 / p_count;
		}
		if (spawn(p_age, interpolation) && !legacy) {
			p_age = float(p_age - p_interval);
			interpolation -= 1.0 / p_count;
		}
	}
}

void ParticleSimulation3D::spawn_burst(int p_count) {
	for (int i = 0; i < p_count; ++i) {
		const bool legacy = uses_legacy_spawning();
		const double age = legacy ? float(double(float(1.0 / p_count)) * delta) : 0.0;
		const double interpolation = legacy ? 1.0 - double(i + 1) / p_count : 1.0;
		spawn(age, interpolation);
	}
}

void ParticleSimulation3D::remove_particle(int p_index) {
	if (pool) {
		pool->release(Object::cast_to<ParticleInstance3D>(active[p_index]));
	}
	active[p_index] = active[active.size() - 1];
	active.resize(active.size() - 1);
}

void ParticleSimulation3D::clear() {
	for (int i = 0; i < active.size(); ++i) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(active[i]);
		if (pool) {
			pool->release(particle);
		} else {
			particle->hide();
		}
	}
	active.clear();
	simulation_bounds = AABB();
	simulation_bounds_valid = false;
}

double ParticleSimulation3D::remaining_seconds() const {
	double remaining = 0.0;
	for (int i = 0; i < active.size(); ++i) {
		const ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(active[i]);
		remaining = MAX(remaining, particle->remaining_seconds());
	}
	return remaining;
}
