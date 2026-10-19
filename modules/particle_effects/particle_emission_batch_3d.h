// wgodot-changes::file
#pragma once

#include "particle_spawn.h"
#include "particle_environment.h"

// Samples a batch for programmable GPU particles without per-particle script
// dispatch. The process shader receives raw birth data in the emission basis:
// column 0 = (relative age, inverse lifetime, angular velocity in radians/second),
// column 1 = signed size, column 2.x = rotation in radians.
// Origin/velocity keep the configured simulation units; COLOR.rgb contains
// independently sampled custom attributes. The shader owns appearance encoding.
class ParticleEmissionBatch3D : public RefCounted {
	GDCLASS(ParticleEmissionBatch3D, RefCounted);

	Vector<Ref<ParticleBirthRule3D>> rules;
	Transform3D birth_transform;
	bool local_space = true;
	double world_unit_scale = 1.0;
	Vector3 position_offset;
	Vector3 position_variation;
	Vector3 attribute_minimum;
	Vector3 attribute_maximum;
	int inverse_lifetime_mantissa_bits = 23;

	RID particles;
	Transform3D emitter_transform;
	double emitter_seconds = 0.0;
	Ref<RandomStream32> random;
	Vector<Ref<RandomStream32>> rule_streams;
	Ref<RandomStream32> attribute_random;
	Vector3 movement_offset;
	Vector3 emitter_velocity;
	ParticleGroundProjector3D *ground = nullptr;
	Ref<RandomStream32> ground_random;

protected:
	static void _bind_methods();

public:
	void set_ground(ParticleGroundProjector3D *p_value) { ground = p_value; }
	ParticleGroundProjector3D *get_ground() const { return ground; }
	Error configure(const TypedArray<ParticleBirthRule3D> &p_rules, const Transform3D &p_birth_transform, bool p_local_space, double p_world_unit_scale, const Vector3 &p_position_offset, const Vector3 &p_position_variation, const Vector3 &p_attribute_minimum, const Vector3 &p_attribute_maximum, int p_inverse_lifetime_mantissa_bits = 23);
	void set_rule_streams(const TypedArray<RandomStream32> &p_rule_streams);
	void begin_frame(RID p_particles, const Transform3D &p_emitter_transform, double p_emitter_seconds, const Ref<RandomStream32> &p_random, const Ref<RandomStream32> &p_attribute_random, const Vector3 &p_movement_offset, const Vector3 &p_emitter_velocity = Vector3());
	// Returns the longest remaining lifetime, accounting for each birth's age
	// and the requested inverse-lifetime mantissa precision of the GPU storage.
	double emit(int p_count, double p_age = 0.0, double p_interval = 0.0, int64_t p_start_index = 0);
};
