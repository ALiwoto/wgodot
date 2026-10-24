// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"
#include "core/os/semaphore.h"

class WGPUParticleSnapshot : public RefCounted {
	GDCLASS(WGPUParticleSnapshot, RefCounted);

	RID particles;
	PackedByteArray data;
	int stride = 0;
	int userdata_count = 0;
	Semaphore completed;

	void _read_buffer();
	void _capture_on_render_thread();

protected:
	static void _bind_methods();

public:
	struct Layout {
		RID buffer;
		int stride = 0;
		int userdata_count = 0;
		int count = 0;
	};
	// Render-thread only. A missing allocation is a valid not-yet-simulated
	// emitter; an invalid RID/backend is an error, not an empty particle set.
	static Error describe(RID p_particles, Layout &r_layout);
	static Ref<WGPUParticleSnapshot> capture(RID p_particles);
	PackedByteArray get_data() const { return data; }
	int get_stride() const { return stride; }
	int get_userdata_count() const { return userdata_count; }
};
#endif
