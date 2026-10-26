// wgodot-changes::file
#include "particle_replay_inputs.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_PARTICLE_EFFECTS_ENABLED)
#include "core/object/class_db.h"

namespace {
struct Channel {
	enum Target { PARTICLE, BURST_SCALE, EMISSION_RATE, BIRTH };
	const char *prefix;
	Ref<RandomStream32> (ParticleInstance3D::*stream)() const;
	Target target = PARTICLE;
};
const Channel channels[] = {
	{ "particle_size", &ParticleInstance3D::get_size_random },
	{ "particle_size_life", &ParticleInstance3D::get_size_life_random },
	{ "particle_location", &ParticleInstance3D::get_location_random },
	{ "particle_drag", &ParticleInstance3D::get_drag_random },
	{ "particle_rotation", &ParticleInstance3D::get_rotation_random },
	{ "particle_frame", &ParticleInstance3D::get_frame_random },
	{ "particle_ground", &ParticleInstance3D::get_ground_random },
	{ "particle_acceleration", &ParticleInstance3D::get_acceleration_random },
	{ "particle_camera", &ParticleInstance3D::get_camera_random },
	{ "particle_orientation", &ParticleInstance3D::get_orientation_random },
	{ "particle_color", &ParticleInstance3D::get_color_random },
	{ "particle_color_life", &ParticleInstance3D::get_color_life_random },
	{ "particle_noise_update", &ParticleInstance3D::get_sprite_random },
	{ "emitter_burst_scale", nullptr, Channel::BURST_SCALE },
	{ "emitter_rate", nullptr, Channel::EMISSION_RATE },
	{ "particle_birth", nullptr, Channel::BIRTH },
};

uint64_t sample_key(uint32_t p_frame, uint32_t p_step) {
	return (uint64_t(p_frame) << 32) | p_step;
}
}

void WParticleReplayInputs::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "recording"), &WParticleReplayInputs::configure);
	ClassDB::bind_method(D_METHOD("replace_inputs", "recording"), &WParticleReplayInputs::replace_inputs);
	ClassDB::bind_method(D_METHOD("apply_birth", "particle", "frame", "step", "random"), &WParticleReplayInputs::apply_birth);
	ClassDB::bind_method(D_METHOD("bind_particle", "particle", "identity"), &WParticleReplayInputs::bind_particle);
	ClassDB::bind_method(D_METHOD("apply", "particle", "frame", "step"), &WParticleReplayInputs::apply);
	ClassDB::bind_method(D_METHOD("apply_active", "simulation", "frame", "step"), &WParticleReplayInputs::apply_active);
	ClassDB::bind_method(D_METHOD("apply_emitter", "identity", "frame", "step", "burst", "rate"), &WParticleReplayInputs::apply_emitter);
	ClassDB::bind_method(D_METHOD("get_unapplied_inputs"), &WParticleReplayInputs::get_unapplied_inputs);
	ClassDB::bind_method(D_METHOD("restore_streams"), &WParticleReplayInputs::restore_streams);
}

Error WParticleReplayInputs::configure(const Ref<ConfigFile> &p_recording) {
	ERR_FAIL_COND_V(p_recording.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(!original_streams.is_empty(), ERR_ALREADY_IN_USE, "Restore the previous particle replay before configuring another recording.");
	inputs.clear();
	bindings.clear();
	return decode_inputs(p_recording);
}

Error WParticleReplayInputs::replace_inputs(const Ref<ConfigFile> &p_recording) {
	ERR_FAIL_COND_V(p_recording.is_null(), ERR_INVALID_PARAMETER);
	// Live inspection retains only the incoming frame. Keep stream ownership
	// and active particle bindings until the session restores them.
	inputs.clear();
	return decode_inputs(p_recording);
}

Error WParticleReplayInputs::decode_inputs(const Ref<ConfigFile> &p_recording) {
	for (const String &section : p_recording->get_sections()) {
		// Channel names can contain underscores. Split numeric suffixes from
		// the end, so size_life is never mistaken for the size channel.
		int separator = section.rfind("_");
		if (separator < 0 || !section.substr(separator + 1).is_valid_int()) {
			continue;
		}
		int64_t frame = section.substr(separator + 1).to_int();
		int64_t step = 0;
		String prefix = section.substr(0, separator);
		separator = prefix.rfind("_");
		if (separator >= 0 && prefix.substr(separator + 1).is_valid_int()) {
			step = frame;
			frame = prefix.substr(separator + 1).to_int();
			prefix = prefix.substr(0, separator);
		}
		bool known_channel = false;
		for (uint32_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
			if (prefix != channels[i].prefix) {
				continue;
			}
			known_channel = true;
			ERR_FAIL_COND_V_MSG(frame < 0 || frame > UINT32_MAX || step < 0 || step > UINT32_MAX, ERR_FILE_CORRUPT, "Invalid particle input clock: " + section);
			auto &sample = inputs[sample_key(uint32_t(frame), uint32_t(step))];
			for (const String &identity : p_recording->get_section_keys(section)) {
				const Variant value = p_recording->get_value(section, identity);
				ERR_FAIL_COND_V_MSG(value.get_type() != Variant::INT, ERR_FILE_CORRUPT, "Particle input is not an integer: " + section + "/" + identity);
				sample[identity].push_back({ i, int64_t(value) });
			}
			break;
		}
		ERR_FAIL_COND_V_MSG(!known_channel && (prefix.begins_with("particle_") || prefix.begins_with("emitter_")), ERR_FILE_CORRUPT, "Unknown particle replay channel: " + section);
	}
	for (auto &sample : inputs) {
		for (auto &particle : sample.value) {
			particle.value.sort();
		}
	}
	return OK;
}

void WParticleReplayInputs::bind_particle(ParticleInstance3D *p_particle, const String &p_identity) {
	ERR_FAIL_NULL(p_particle);
	// A pool reuses its object, but each birth has a different source identity.
	// The sampler binds on every birth and activation; no node is retained here.
	bindings.insert(p_particle->get_instance_id(), p_identity);
}

Error WParticleReplayInputs::apply_stream(Reset &p_reset, const Ref<RandomStream32> &p_stream) {
	ERR_FAIL_COND_V_MSG(p_stream.is_null(), ERR_UNCONFIGURED, "Recorded input targets an uninitialized random stream.");
	const ObjectID stream_id = p_stream->get_instance_id();
	if (!original_streams.has(stream_id)) {
		original_streams.insert(stream_id, { p_stream, p_stream->get_state() });
	}
	p_stream->debug_restore_state(p_reset.state);
	p_reset.applied = true;
	return OK;
}

Error WParticleReplayInputs::apply_sample(ParticleInstance3D *p_particle, HashMap<String, Vector<Reset>> &p_sample) {
	const String *identity = bindings.getptr(p_particle->get_instance_id());
	if (!identity) {
		return OK; // Not part of a recorded activation yet.
	}
	Vector<Reset> *resets = p_sample.getptr(*identity);
	if (!resets) {
		return OK;
	}
	Reset *values = resets->ptrw();
	for (int i = 0; i < resets->size(); i++) {
		Reset &reset = values[i];
		if (channels[reset.channel].target == Channel::BIRTH) {
			continue;
		}
		ERR_FAIL_COND_V_MSG(channels[reset.channel].target != Channel::PARTICLE, ERR_FILE_CORRUPT, "Emitter replay input targets a particle: " + *identity);
		Ref<RandomStream32> stream = (p_particle->*channels[reset.channel].stream)();
		Error error = apply_stream(reset, stream);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error WParticleReplayInputs::apply(ParticleInstance3D *p_particle, uint32_t p_frame, uint32_t p_step) {
	ERR_FAIL_NULL_V(p_particle, ERR_INVALID_PARAMETER);
	auto *sample = inputs.getptr(sample_key(p_frame, p_step));
	return sample ? apply_sample(p_particle, *sample) : OK;
}

Error WParticleReplayInputs::apply_birth(ParticleInstance3D *p_particle, uint32_t p_frame, uint32_t p_step, const Ref<RandomStream32> &p_random) {
	ERR_FAIL_NULL_V(p_particle, ERR_INVALID_PARAMETER);
	const String *identity = bindings.getptr(p_particle->get_instance_id());
	auto *sample = inputs.getptr(sample_key(p_frame, p_step));
	if (!identity || !sample) {
		return OK;
	}
	Vector<Reset> *resets = sample->getptr(*identity);
	if (resets) {
		for (Reset &reset : *resets) {
			if (channels[reset.channel].target == Channel::BIRTH) {
				return apply_stream(reset, p_random);
			}
		}
	}
	return OK;
}

Error WParticleReplayInputs::apply_active(const Ref<ParticleSimulation3D> &p_simulation, uint32_t p_frame, uint32_t p_step) {
	ERR_FAIL_COND_V(p_simulation.is_null(), ERR_INVALID_PARAMETER);
	auto *sample = inputs.getptr(sample_key(p_frame, p_step));
	if (!sample) {
		return OK;
	}
	for (int i = 0; i < p_simulation->get_active_particle_count(); i++) {
		Error error = apply_sample(p_simulation->get_active_particle(i), *sample);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error WParticleReplayInputs::apply_emitter(const String &p_identity, uint32_t p_frame, uint32_t p_step, const Ref<RandomStream32> &p_burst, const Ref<RandomStream32> &p_rate) {
	auto *sample = inputs.getptr(sample_key(p_frame, p_step));
	Vector<Reset> *resets = sample ? sample->getptr(p_identity) : nullptr;
	if (!resets) {
		return OK;
	}
	Reset *values = resets->ptrw();
	for (int i = 0; i < resets->size(); i++) {
		Reset &reset = values[i];
		const Channel::Target target = channels[reset.channel].target;
		ERR_FAIL_COND_V_MSG(target == Channel::PARTICLE, ERR_FILE_CORRUPT, "Particle replay input targets an emitter: " + p_identity);
		Error error = apply_stream(reset, target == Channel::BURST_SCALE ? p_burst : p_rate);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

PackedStringArray WParticleReplayInputs::get_unapplied_inputs() const {
	PackedStringArray missing;
	for (const auto &sample : inputs) {
		for (const auto &particle : sample.value) {
			for (const Reset &reset : particle.value) {
				if (!reset.applied) {
					missing.push_back(String(channels[reset.channel].prefix) + "_" + itos(sample.key >> 32) + "_" + itos(uint32_t(sample.key)) + "/" + particle.key);
				}
			}
		}
	}
	missing.sort();
	return missing;
}

void WParticleReplayInputs::restore_streams() {
	for (const auto &entry : original_streams) {
		entry.value.stream->debug_restore_state(entry.value.state);
	}
	original_streams.clear();
}
#endif
