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
		bool operator<(const Reset &p_other) const { return channel < p_other.channel; }
	};
	HashMap<uint64_t, HashMap<String, Vector<Reset>>> inputs;
	HashMap<ObjectID, String> bindings;
	Error apply_sample(ParticleInstance3D *p_particle, const HashMap<String, Vector<Reset>> &p_sample) const;

protected:
	static void _bind_methods();

public:
	Error configure(const Ref<ConfigFile> &p_recording);
	void bind_particle(ParticleInstance3D *p_particle, const String &p_identity);
	Error apply(ParticleInstance3D *p_particle, uint32_t p_frame, uint32_t p_step) const;
	Error apply_active(const Ref<ParticleSimulation3D> &p_simulation, uint32_t p_frame, uint32_t p_step) const;
};
#endif
