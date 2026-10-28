// wgodot-changes::file
#pragma once

#include "particle_instance_pool_3d.h"

// Runs an ordered batch without per-particle script dispatch. The owner retains
// the pool (or authored nodes); this object borrows them for its playback.
class ParticleSimulation3D : public RefCounted {
	GDCLASS(ParticleSimulation3D, RefCounted);

	ParticleInstancePool3D *pool = nullptr;
	TypedArray<ParticleInstance3D> authored;
	TypedArray<ParticleInstance3D> active;
	int spawned_count = 0;
	bool collect_simulation_bounds = false;
	bool simulation_bounds_valid = false;
	AABB simulation_bounds;
	bool legacy_spawning = false;
	bool follow_sources = false;
	TypedArray<ParticleBirthRule3D> initial_values;
	ParticleGroundProjector3D *ground = nullptr;
	Ref<ParticleSpawnContext3D> context;
	Ref<RandomStream32> random;
	TypedArray<RandomStream32> initial_random;
	double seconds = 0.0;
	double delta = 0.0;
	double parameter_ratio = 0.0;
	double emitter_seconds = 0.0;
	Vector3 birth_offset;
#ifdef TOOLS_ENABLED
	bool debug_prepare = false;
	bool debug_sample = false;
#endif

	ParticleInstance3D *linked_particle(int p_index) const;
	bool sample(ParticleInstance3D *p_particle, double p_age, Vector3 p_offset, int p_index);
	bool spawn(double p_age, double p_interpolation);
	bool uses_legacy_spawning() const;
	void remove_particle(int p_index);

protected:
	static void _bind_methods();

public:
	void reset(ParticleInstancePool3D *p_pool, const TypedArray<ParticleInstance3D> &p_authored, bool p_legacy_spawning, const TypedArray<ParticleBirthRule3D> &p_initial_values, const TypedArray<RandomStream32> &p_initial_random, ParticleGroundProjector3D *p_ground, const Ref<ParticleSpawnContext3D> &p_context, bool p_follow_sources);
	bool advance(double p_seconds, double p_delta, const Ref<RandomStream32> &p_random, double p_parameter_ratio, double p_emitter_seconds, Vector3 p_birth_offset);
	void spawn_continuous(int p_count, double p_age, double p_interval);
	void spawn_burst(int p_count);
	void flush_spawn_parameters();
	void clear();
	bool has_particles() const { return !active.is_empty(); }
	// Source contexts share the live list. Only this simulation changes membership;
	// clear it before the owning pool or authored nodes are destroyed.
	void bind_source(const Ref<ParticleSpawnContext3D> &p_context, int p_channel) const;
	int get_active_particle_count() const { return active.size(); }
	ParticleInstance3D *get_active_particle(int p_index) const;
	// Opt-in aggregation in the existing update/spawn loop; reads never rescan
	// particles or observe a later attachment transform.
	void set_collect_simulation_bounds(bool p_enabled) { collect_simulation_bounds = p_enabled; }
	AABB get_simulation_aabb() const { return simulation_bounds; }
	bool has_active_particle(ParticleInstance3D *p_particle) const { return active.has(p_particle); }
	int get_spawned_count() const { return spawned_count; }
	double remaining_seconds() const;
};
