// wgodot-changes::file
#include "particle_replay_inputs.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_PARTICLE_EFFECTS_ENABLED)
#include "core/object/class_db.h"

namespace {
struct Channel {
	const char *prefix;
	Ref<RandomStream32> (ParticleInstance3D::*stream)() const;
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
};

uint64_t sample_key(uint32_t p_frame, uint32_t p_step) {
	return (uint64_t(p_frame) << 32) | p_step;
}
}

void WParticleReplayInputs::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "recording"), &WParticleReplayInputs::configure);
	ClassDB::bind_method(D_METHOD("bind_particle", "particle", "identity"), &WParticleReplayInputs::bind_particle);
	ClassDB::bind_method(D_METHOD("apply", "particle", "frame", "step"), &WParticleReplayInputs::apply);
	ClassDB::bind_method(D_METHOD("apply_active", "simulation", "frame", "step"), &WParticleReplayInputs::apply_active);
}

Error WParticleReplayInputs::configure(const Ref<ConfigFile> &p_recording) {
	ERR_FAIL_COND_V(p_recording.is_null(), ERR_INVALID_PARAMETER);
	inputs.clear();
	bindings.clear();
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
		for (uint32_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
			if (prefix != channels[i].prefix) {
				continue;
			}
			ERR_FAIL_COND_V_MSG(frame < 0 || frame > UINT32_MAX || step < 0 || step > UINT32_MAX, ERR_FILE_CORRUPT, "Invalid particle input clock: " + section);
			auto &sample = inputs[sample_key(uint32_t(frame), uint32_t(step))];
			for (const String &identity : p_recording->get_section_keys(section)) {
				const Variant value = p_recording->get_value(section, identity);
				ERR_FAIL_COND_V_MSG(value.get_type() != Variant::INT, ERR_FILE_CORRUPT, "Particle input is not an integer: " + section + "/" + identity);
				sample[identity].push_back({ i, int64_t(value) });
			}
			break;
		}
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

Error WParticleReplayInputs::apply_sample(ParticleInstance3D *p_particle, const HashMap<String, Vector<Reset>> &p_sample) const {
	const String *identity = bindings.getptr(p_particle->get_instance_id());
	if (!identity) {
		return OK; // Not part of a recorded activation yet.
	}
	const Vector<Reset> *resets = p_sample.getptr(*identity);
	if (!resets) {
		return OK;
	}
	for (const Reset &reset : *resets) {
		Ref<RandomStream32> stream = (p_particle->*channels[reset.channel].stream)();
		ERR_FAIL_COND_V_MSG(stream.is_null(), ERR_UNCONFIGURED, "Recorded particle input targets an uninitialized random stream: " + *identity);
		stream->reset(reset.state);
	}
	return OK;
}

Error WParticleReplayInputs::apply(ParticleInstance3D *p_particle, uint32_t p_frame, uint32_t p_step) const {
	ERR_FAIL_NULL_V(p_particle, ERR_INVALID_PARAMETER);
	const auto *sample = inputs.getptr(sample_key(p_frame, p_step));
	return sample ? apply_sample(p_particle, *sample) : OK;
}

Error WParticleReplayInputs::apply_active(const Ref<ParticleSimulation3D> &p_simulation, uint32_t p_frame, uint32_t p_step) const {
	ERR_FAIL_COND_V(p_simulation.is_null(), ERR_INVALID_PARAMETER);
	const auto *sample = inputs.getptr(sample_key(p_frame, p_step));
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
#endif
