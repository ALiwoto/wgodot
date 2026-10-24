// wgodot-changes::file
#include "wgodot_particle_snapshot.h"

#ifdef TOOLS_ENABLED
#include "core/object/class_db.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

void WGPUParticleSnapshot::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("capture", "particles"), &WGPUParticleSnapshot::capture);
	ClassDB::bind_method(D_METHOD("get_data"), &WGPUParticleSnapshot::get_data);
	ClassDB::bind_method(D_METHOD("get_stride"), &WGPUParticleSnapshot::get_stride);
	ClassDB::bind_method(D_METHOD("get_userdata_count"), &WGPUParticleSnapshot::get_userdata_count);
}

Ref<WGPUParticleSnapshot> WGPUParticleSnapshot::capture(RID p_particles) {
	Ref<WGPUParticleSnapshot> snapshot;
	snapshot.instantiate();
	snapshot->particles = p_particles;
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(snapshot.ptr(), &WGPUParticleSnapshot::_capture_on_render_thread));
	snapshot->completed.wait();
	return snapshot;
}

void WGPUParticleSnapshot::_capture_on_render_thread() {
	_read_buffer();
	completed.post();
}

void WGPUParticleSnapshot::_read_buffer() {
	Layout layout;
	ERR_FAIL_COND(describe(particles, layout) != OK);
	ERR_FAIL_COND_MSG(layout.buffer.is_null(), "GPU particles must render before capturing their state.");
	userdata_count = layout.userdata_count;
	stride = layout.stride;
	data = RenderingDevice::get_singleton()->buffer_get_data(layout.buffer);
}

Error WGPUParticleSnapshot::describe(RID p_particles, Layout &r_layout) {
	RendererRD::ParticlesStorage *storage = RendererRD::ParticlesStorage::get_singleton();
	ERR_FAIL_NULL_V(storage, ERR_UNAVAILABLE);
	const RendererRD::ParticlesStorage::Particles *state = storage->particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(state, ERR_INVALID_PARAMETER);
	r_layout.buffer = state->particle_buffer;
	r_layout.userdata_count = state->userdata_count;
	r_layout.stride = sizeof(RendererRD::ParticlesStorage::ParticleData) + state->userdata_count * sizeof(float) * 4;
	r_layout.count = state->amount;
	if (state->trails_enabled && state->trail_bind_poses.size() > 1) {
		r_layout.count *= state->trail_bind_poses.size();
	}
	return OK;
}
#endif
