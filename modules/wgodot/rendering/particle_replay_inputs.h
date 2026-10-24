// wgodot-changes::file
#pragma once
#include "modules/modules_enabled.gen.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_PARTICLE_EFFECTS_ENABLED)
#include "core/io/config_file.h"
#include "modules/particle_effects/particle_instance_3d.h"
#include "modules/particle_effects/particle_simulation_3d.h"

// Decode the recorded random inputs once, before playback. Applying a sample
// uses typed stream access, without per-particle dictionaries or property calls.
class WParticleReplayInputs : public RefCounted {
	GDCLASS(WParticleReplayInputs, RefCounted);
	struct Reset {
		uint32_t channel = 0;
		int64_t state = 0;
		bool applied = false;
		bool operator<(const Reset &p_other) const { return channel < p_other.channel; }
	};
	struct StreamState {
		Ref<RandomStream32> stream;
		int64_t state = 0;
	};
	HashMap<uint64_t, HashMap<String, Vector<Reset>>> inputs;
	HashMap<ObjectID, String> bindings;
	HashMap<ObjectID, StreamState> original_streams;
	Error apply_stream(Reset &p_reset, const Ref<RandomStream32> &p_stream);
	Error apply_sample(ParticleInstance3D *p_particle, HashMap<String, Vector<Reset>> &p_sample);

protected:
	static void _bind_methods();

public:
	Error configure(const Ref<ConfigFile> &p_recording);
	void bind_particle(ParticleInstance3D *p_particle, const String &p_identity);
	Error apply(ParticleInstance3D *p_particle, uint32_t p_frame, uint32_t p_step);
	Error apply_active(const Ref<ParticleSimulation3D> &p_simulation, uint32_t p_frame, uint32_t p_step);
	Error apply_emitter(const String &p_identity, uint32_t p_frame, uint32_t p_step, const Ref<RandomStream32> &p_burst, const Ref<RandomStream32> &p_rate);
	PackedStringArray get_unapplied_inputs() const;
	void restore_streams();
};
#endif
