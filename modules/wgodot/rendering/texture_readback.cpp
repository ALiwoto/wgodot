// wgodot-changes::file
#include "render_capture.h"

#ifdef TOOLS_ENABLED
#include "device_capture/recorder.h"
#include "gpu_profile.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/rendering_server.h"

namespace {
void drain_readbacks() {
	if (RD::get_singleton()) {
		RD::get_singleton()->flush_pending_readbacks();
	}
}

class TextureReadback : public RefCounted {
	GDSOFTCLASS(TextureReadback, RefCounted);

public:
	RendererRD::TextureStorage::Texture2DReadback descriptor;
	PackedByteArray bytes;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Error error = ERR_BUSY;

	void complete(Error p_error) {
		error = p_error;
		// notify_yield_over also handles completion before yield() is entered.
		WorkerThreadPool::get_singleton()->notify_yield_over(task);
	}

	void received(const PackedByteArray &p_bytes, Ref<TextureReadback> p_owner) {
		bytes = p_bytes;
		complete(bytes.is_empty() ? ERR_CANT_ACQUIRE_RESOURCE : OK);
	}

	static void begin(Ref<TextureReadback> p_request, RID p_texture) {
		WGodotGPUProfile::Scope scope("capture.queue_image_readback");
		WGPUCapture::Recorder::DiagnosticScope diagnostic_commands;
		RendererRD::TextureStorage *storage = RendererRD::TextureStorage::get_singleton();
		RD *device = RD::get_singleton();
		if (!storage || !device) {
			p_request->complete(ERR_UNAVAILABLE);
			return;
		}
		p_request->descriptor = storage->texture_2d_readback(p_texture);
		if (p_request->descriptor.cached.is_valid()) {
			p_request->complete(OK);
			return;
		}
		if (p_request->descriptor.texture.is_null()) {
			p_request->complete(ERR_INVALID_PARAMETER);
			return;
		}
		Error error = device->texture_get_data_async(p_request->descriptor.texture, 0,
				callable_mp(p_request.ptr(), &TextureReadback::received).bind(p_request));
		if (error != OK) {
			p_request->complete(error);
		}
	}
};
}

Ref<Image> WRenderCapture::read_texture_image(RID p_texture) {
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	const WorkerThreadPool::TaskID task = pool->get_caller_task_id();
	ERR_FAIL_COND_V_MSG(task == WorkerThreadPool::INVALID_TASK_ID || pool->get_caller_group_id() != WorkerThreadPool::INVALID_TASK_ID,
			Ref<Image>(), "Texture readback requires an ordinary worker task, not the main/render thread or a group task.");
	Ref<TextureReadback> request;
	request.instantiate();
	request->task = task;
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(TextureReadback::begin).bind(request, p_texture));
	{
		WGodotGPUProfile::Scope scope("capture.await_image_readback");
		// This is a collaborative wait: GPU readback copy jobs can run on this
		// worker too. A semaphore here could exhaust the pool and deadlock them.
		pool->yield();
	}
	ERR_FAIL_COND_V_MSG(request->error != OK, Ref<Image>(), "Texture readback failed: " + String(error_names[request->error]));
	if (request->descriptor.cached.is_valid()) {
		return request->descriptor.cached;
	}
	WGodotGPUProfile::Scope scope("capture.decode_image_readback");
	return request->descriptor.decode(request->bytes);
}

void WRenderCapture::finish_write_task(WorkerThreadPool::TaskID p_task) {
	WGodotGPUProfile::Scope scope("capture.teardown_writer");
	ERR_FAIL_COND(!Thread::is_main_thread());
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	while (!pool->is_task_completed(p_task)) {
		// _exit_tree cannot await another game frame. Explicitly retire queued
		// GPU copies here, or its writer could wait forever for a future draw.
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(drain_readbacks));
		RenderingServer::get_singleton()->sync();
		OS::get_singleton()->delay_usec(1000);
	}
	pool->wait_for_task_completion(p_task);
}
#endif
