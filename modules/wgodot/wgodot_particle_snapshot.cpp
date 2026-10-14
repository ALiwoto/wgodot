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
	RendererRD::ParticlesStorage *storage = RendererRD::ParticlesStorage::get_singleton();
	ERR_FAIL_NULL_MSG(storage, "GPU particle snapshots require a RenderingDevice renderer.");
	const RendererRD::ParticlesStorage::Particles *state = storage->particles_owner.get_or_null(particles);
	ERR_FAIL_NULL_MSG(state, "GPU particle snapshot requires a particle resource RID.");
	ERR_FAIL_COND_MSG(state->particle_buffer.is_null(), "GPU particles must render before capturing their state.");
	userdata_count = state->userdata_count;
	stride = sizeof(RendererRD::ParticlesStorage::ParticleData) + userdata_count * sizeof(float) * 4;
	data = RenderingDevice::get_singleton()->buffer_get_data(state->particle_buffer);
}
#endif
