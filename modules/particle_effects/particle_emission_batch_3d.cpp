// wgodot-changes::file
#include "particle_emission_batch_3d.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

void ParticleEmissionBatch3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_ground", "ground"), &ParticleEmissionBatch3D::set_ground);
	ClassDB::bind_method(D_METHOD("get_ground"), &ParticleEmissionBatch3D::get_ground);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "ground", PROPERTY_HINT_NODE_TYPE, "ParticleGroundProjector3D"), "set_ground", "get_ground");
	ClassDB::bind_method(D_METHOD("configure", "rules", "birth_transform", "local_space", "world_unit_scale", "position_offset", "position_variation", "attribute_minimum", "attribute_maximum", "inverse_lifetime_mantissa_bits"), &ParticleEmissionBatch3D::configure, DEFVAL(23));
	ClassDB::bind_method(D_METHOD("set_rule_streams", "streams"), &ParticleEmissionBatch3D::set_rule_streams);
	ClassDB::bind_method(D_METHOD("begin_frame", "particles", "emitter_transform", "emitter_seconds", "random", "attribute_random", "movement_offset", "emitter_velocity"), &ParticleEmissionBatch3D::begin_frame, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("emit", "count", "age", "interval", "start_index"), &ParticleEmissionBatch3D::emit, DEFVAL(0.0), DEFVAL(0.0), DEFVAL(0));
}

Error ParticleEmissionBatch3D::configure(const TypedArray<ParticleBirthRule3D> &p_rules, const Transform3D &p_birth_transform, bool p_local_space, double p_world_unit_scale, const Vector3 &p_position_offset, const Vector3 &p_position_variation, const Vector3 &p_attribute_minimum, const Vector3 &p_attribute_maximum, int p_inverse_lifetime_mantissa_bits) {
	ERR_FAIL_COND_V(p_inverse_lifetime_mantissa_bits < 0 || p_inverse_lifetime_mantissa_bits > 23, ERR_INVALID_PARAMETER);
	rules.clear();
	for (int index = 0; index < p_rules.size(); index++) {
		const Ref<ParticleBirthRule3D> rule = p_rules[index];
		ERR_FAIL_COND_V(rule.is_null(), ERR_INVALID_PARAMETER);
		using Target = ParticleBirthRule3D::Target;
		switch (Target(rule->get_target())) {
			case Target::Lifetime:
			case Target::Size:
			case Target::Rotation:
			case Target::AngularVelocity:
			case Target::Position:
			case Target::WorldOffset:
			case Target::Ground:
			case Target::Velocity:
			case Target::ParentVelocity:
			case Target::Shape:
				break;
			default:
				ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Unsupported GPU particle birth target: " + itos(rule->get_target()));
		}
		rules.push_back(rule);
	}
	birth_transform = p_birth_transform;
	ground_random.instantiate();
	ground_random->reset(Math::rand());
	local_space = p_local_space;
	world_unit_scale = p_world_unit_scale;
	position_offset = p_position_offset;
	position_variation = p_position_variation;
	attribute_minimum = p_attribute_minimum;
	attribute_maximum = p_attribute_maximum;
	inverse_lifetime_mantissa_bits = p_inverse_lifetime_mantissa_bits;
	return OK;
}

void ParticleEmissionBatch3D::set_rule_streams(const TypedArray<RandomStream32> &p_rule_streams) {
	ERR_FAIL_COND(p_rule_streams.size() != rules.size());
	rule_streams.resize(rules.size());
	for (int index = 0; index < rules.size(); index++) {
		rule_streams.write[index] = p_rule_streams[index];
		ERR_FAIL_COND(rules[index]->get_random_seed().is_valid() && rule_streams[index].is_null());
	}
}

void ParticleEmissionBatch3D::begin_frame(RID p_particles, const Transform3D &p_emitter_transform, double p_emitter_seconds, const Ref<RandomStream32> &p_random, const Ref<RandomStream32> &p_attribute_random, const Vector3 &p_movement_offset, const Vector3 &p_emitter_velocity) {
	ERR_FAIL_COND(rule_streams.size() != rules.size());
	ERR_FAIL_COND(p_random.is_null() || p_attribute_random.is_null());
	particles = p_particles;
	emitter_transform = p_emitter_transform;
	emitter_seconds = p_emitter_seconds;
	random = p_random;
	attribute_random = p_attribute_random;
	movement_offset = p_movement_offset;
	emitter_velocity = p_emitter_velocity * world_unit_scale;
	for (int index = 0; index < rules.size(); index++) {
		if (rules[index]->get_random_seed().is_null()) {
			rule_streams.write[index] = random;
		}
	}
}

double ParticleEmissionBatch3D::emit(int p_count, double p_age, double p_interval, int64_t p_start_index) {
	ERR_FAIL_COND_V(particles.is_null() || random.is_null() || attribute_random.is_null(), 0.0);
	ERR_FAIL_COND_V(p_count < 0, 0.0);
	double remaining = 0.0;
	const Basis world_rotation = emitter_transform.basis.orthonormalized();
	for (int particle = 0; particle < p_count; particle++) {
		Vector3 position = birth_transform.origin;
		Vector3 velocity;
		Vector3 size;
		float lifetime = 0.0f;
		double angle = 0.0;
		double angular_velocity = 0.0;
		bool ground_failed = false;
		for (int index = 0; index < rules.size(); index++) {
			const Ref<ParticleBirthRule3D> &rule = rules[index];
			const Ref<RandomStream32> &stream = rule_streams[index];
			using Target = ParticleBirthRule3D::Target;
			switch (Target(rule->get_target())) {
				case Target::Lifetime:
					lifetime = float(double(lifetime) + rule->sample_scalar(stream, emitter_seconds));
					break;
				case Target::Size:
					size = rule->sample_vector(stream, emitter_seconds);
					size *= rule->sample_texture_sign(stream);
					break;
				case Target::Rotation:
					angle += rule->sample_scalar(stream, emitter_seconds);
					break;
				case Target::AngularVelocity:
					angular_velocity += rule->sample_scalar(stream, emitter_seconds);
					break;
				case Target::Position:
					position += birth_transform.basis.xform(rule->sample_vector(stream, emitter_seconds));
					break;
				case Target::WorldOffset: {
					const Basis basis = local_space ? emitter_transform.basis : world_rotation;
					position += basis.inverse().xform(rule->sample_vector(stream, emitter_seconds));
				} break;
				case Target::Ground: {
					ERR_FAIL_NULL_V(ground, remaining);
					const Transform3D frame = local_space ? emitter_transform : emitter_transform.orthonormalized();
					if (ground->project(frame.xform(position / world_unit_scale))) {
						position = frame.affine_inverse().xform(ground->projected_position(ground_random)) * world_unit_scale;
					} else {
						ground_failed = true;
					}
				} break;
				case Target::Velocity:
					velocity += birth_transform.basis.xform(rule->sample_velocity(stream, emitter_transform.basis, local_space, emitter_seconds));
					break;
				case Target::ParentVelocity:
					velocity += rule->sample_parent_velocity(stream, emitter_transform.basis, local_space, emitter_velocity, emitter_seconds);
					break;
				case Target::Shape: {
					const Ref<ParticleSpawnShape3D> shape = rule->get_shape();
					const ParticleSpawnShape3D::Sample spawn = shape->sample(stream, emitter_seconds, p_start_index + particle);
					position += birth_transform.basis.xform(spawn.position);
					if (shape->get_velocity()) {
						velocity += birth_transform.basis.xform(shape->velocity_at(spawn.velocity_offset, stream, emitter_seconds));
					}
				} break;
				default:
					break;
			}
		}
		if (!local_space) {
			position = world_rotation.xform(position) + emitter_transform.origin * world_unit_scale;
			velocity = world_rotation.xform(velocity);
			position += movement_offset * (1.0 - double(particle) / p_count);
		}
		position += position_offset + position_variation * attribute_random->next_float();
		Color attributes;
		for (int axis = 0; axis < 3; axis++) {
			attributes[axis] = Math::lerp(double(attribute_minimum[axis]), double(attribute_maximum[axis]), attribute_random->next_float());
		}
		ERR_FAIL_COND_V_MSG(lifetime <= 0.0f, remaining, "GPU particle birth requires a positive lifetime.");
		if (ground_failed) {
			p_age = float(p_age - p_interval);
			continue;
		}
		const float inverse_lifetime = float(1.0 / double(lifetime));
		uint32_t lifetime_bits;
		memcpy(&lifetime_bits, &inverse_lifetime, sizeof(lifetime_bits));
		lifetime_bits &= ~((1U << (23 - inverse_lifetime_mantissa_bits)) - 1U);
		float stored_inverse_lifetime;
		memcpy(&stored_inverse_lifetime, &lifetime_bits, sizeof(stored_inverse_lifetime));
		remaining = MAX(remaining, 1.0 / double(stored_inverse_lifetime) - p_age);
		position += velocity * p_age;
		Basis emission_basis;
		emission_basis.set_column(0, Vector3(float(p_age * inverse_lifetime), inverse_lifetime, angular_velocity));
		emission_basis.set_column(1, size);
		emission_basis.set_column(2, Vector3(angle, 0.0, 0.0));
		RenderingServer::get_singleton()->particles_emit(particles, Transform3D(emission_basis, position), velocity, attributes, Color(), RSE::PARTICLES_EMIT_FLAG_POSITION | RSE::PARTICLES_EMIT_FLAG_ROTATION_SCALE | RSE::PARTICLES_EMIT_FLAG_VELOCITY | RSE::PARTICLES_EMIT_FLAG_COLOR | RSE::PARTICLES_EMIT_FLAG_CUSTOM);
		p_age = float(p_age - p_interval);
	}
	return remaining;
}
