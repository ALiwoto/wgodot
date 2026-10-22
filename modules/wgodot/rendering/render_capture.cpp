// wgodot-changes::file
#include "render_capture.h"

#ifdef TOOLS_ENABLED
#include "core/io/compression.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_viewport.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/rendering_server.h"

HashMap<RID, Ref<WRenderCapture>> WRenderCapture::captures;

void WRenderCapture::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("begin", "viewport", "directory", "passes"), &WRenderCapture::begin, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_sample", "frame", "seconds", "render_time", "time_step", "detailed_inputs"), &WRenderCapture::set_sample, DEFVAL(-1), DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("end"), &WRenderCapture::end);
	ClassDB::bind_method(D_METHOD("is_completed"), &WRenderCapture::is_completed);
	ClassDB::bind_method(D_METHOD("get_error"), &WRenderCapture::get_error);
}

Ref<WRenderCapture> WRenderCapture::begin(RID p_viewport, const String &p_directory, bool p_passes) {
	Ref<WRenderCapture> result;
	result.instantiate();
	result->viewport = p_viewport;
	result->directory = p_directory;
	result->passes = p_passes;
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&WRenderCapture::_begin_capture).bind(result));
	return result;
}

void WRenderCapture::_begin_capture(Ref<WRenderCapture> p_capture) {
	p_capture->_begin();
}

void WRenderCapture::_begin() {
	target = RSG::viewport->viewport_get_render_target(viewport);
	if (target.is_null() || captures.has(target) || !RenderingDevice::get_singleton()) {
		failure.set(ERR_INVALID_PARAMETER);
		completed.set();
		return;
	}
	Error error = DirAccess::make_dir_recursive_absolute(directory);
	if (error != OK) {
		failure.set(error);
		completed.set();
		return;
	}
	captures.insert(target, Ref<WRenderCapture>(this));
	Ref<ConfigFile> manifest;
	manifest.instantiate();
	manifest->set_value("capture", "state", "running");
	manifest->set_value("capture", "scope", "complete 3D color before UI, HDR color and depth; optional pass checkpoints");
	error = manifest->save(directory.path_join("capture.ini"));
	if (error != OK) {
		failure.set(error);
	}
}

void WRenderCapture::set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs) {
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_set_sample).bind(p_frame, p_seconds, p_render_time, p_time_step, p_detailed_inputs));
}

void WRenderCapture::_set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs) {
	sample_frame = p_frame;
	sample_seconds = p_seconds;
	sample_render_time = p_render_time;
	sample_time_step = p_time_step;
	detailed_inputs = p_detailed_inputs;
}

WRenderCapture *WRenderCapture::_find(const RenderDataRD *p_data) {
	if (captures.is_empty() || p_data->render_buffers.is_null()) {
		return nullptr;
	}
	Ref<WRenderCapture> *capture = captures.getptr(p_data->render_buffers->get_render_target());
	return capture ? capture->ptr() : nullptr;
}

void WRenderCapture::_read_texture(const String &p_name, RID p_texture) {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (p_texture.is_null()) {
		failure.set(ERR_UNAVAILABLE);
		return;
	}
	RD::TextureFormat format = rd->texture_get_format(p_texture);
	if (format.array_layers != 1) {
		failure.set(ERR_UNAVAILABLE);
		return;
	}
	if (format.samples != RD::TEXTURE_SAMPLES_1) {
		// Never resolve into the renderer's live color buffer. Doing so after
		// a compositor pass would overwrite the very effect being measured.
		RD::TextureFormat destination;
		destination.width = format.width;
		destination.height = format.height;
		destination.format = format.format;
		destination.usage_bits = RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		RID resolved = rd->texture_create(destination, RD::TextureView());
		if (resolved.is_null()) {
			failure.set(ERR_CANT_CREATE);
			return;
		}
		Error error = rd->texture_resolve_multisample(p_texture, resolved);
		if (error == OK) {
			_read_texture(p_name, resolved);
		} else {
			failure.set(error);
		}
		rd->free_rid(resolved);
		return;
	}
	if (RD::format_has_depth(format.format)) {
		// Copy the sampled depth aspect into R32F with the existing texelFetch
		// kernel. Combined depth/stencil CPU layouts vary across backends.
		RD::TextureFormat destination;
		destination.width = format.width;
		destination.height = format.height;
		destination.format = RD::DATA_FORMAT_R32_SFLOAT;
		destination.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
		RID depth = rd->texture_create(destination, RD::TextureView());
		if (depth.is_null()) {
			failure.set(ERR_CANT_CREATE);
			return;
		}
		RendererRD::CopyEffects::get_singleton()->copy_depth_to_rect(p_texture, depth, Rect2i(0, 0, format.width, format.height));
		_read_texture(p_name, depth);
		rd->free_rid(depth);
		return;
	}
	Plane plane;
	plane.name = p_name;
	plane.width = format.width;
	plane.height = format.height;
	plane.format = format.format;
	plane.bytes = rd->texture_get_data(p_texture, 0);
	if (plane.bytes.is_empty()) {
		failure.set(ERR_CANT_ACQUIRE_RESOURCE);
	}
	planes.push_back(plane);
}

bool WRenderCapture::prepare_frame(const RenderDataRD *p_data) {
	WRenderCapture *capture = _find(p_data);
	if (capture && capture->sample_frame >= 0 && capture->sample_render_time >= 0) {
		// Replay source shader time explicitly. Disk/GPU readback stalls must
		// not advance TIME independently of the captured animation sample.
		p_data->scene_data->time = capture->sample_render_time;
		p_data->scene_data->time_step = capture->sample_time_step;
		return true;
	}
	return false;
}

void WRenderCapture::stage(const RenderDataRD *p_data, const String &p_name, bool p_multisample) {
	WRenderCapture *capture = _find(p_data);
	if (!capture || !capture->passes || capture->sample_frame < 0) {
		return;
	}
	Ref<RenderSceneBuffersRD> rb = p_data->render_buffers;
	for (uint32_t view = 0; view < rb->get_view_count(); view++) {
		RID color = p_multisample && rb->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED ? rb->get_color_msaa(view) : rb->get_internal_texture(view);
		capture->_read_texture(p_name + "_" + itos(view), color);
	}
}

void WRenderCapture::texture(const RenderDataRD *p_data, const String &p_name, RID p_texture) {
	WRenderCapture *capture = _find(p_data);
	if (capture && capture->passes && capture->sample_frame >= 0) {
		capture->_read_texture(p_name, p_texture);
	}
}

void WRenderCapture::_write_plane(uint32_t p_index) {
	Plane &plane = planes.write[p_index];
	PackedByteArray compressed;
	compressed.resize(Compression::get_max_compressed_buffer_size(plane.bytes.size(), Compression::MODE_DEFLATE));
	int64_t size = Compression::compress(compressed.ptrw(), plane.bytes.ptr(), plane.bytes.size(), Compression::MODE_DEFLATE);
	if (size < 0) {
		plane.error = ERR_CANT_CREATE;
		return;
	}
	Ref<FileAccess> file = FileAccess::open(directory.path_join(vformat("frame_%08d_%s.bin.z", frame_index, plane.name)), FileAccess::WRITE, &plane.error);
	if (file.is_valid()) {
		file->store_buffer(compressed.ptr(), size);
		plane.error = file->get_error();
	}
}

Ref<ConfigFile> WRenderCapture::inputs(const RenderDataRD *p_data) {
	WRenderCapture *capture = _find(p_data);
	if (!capture || capture->sample_frame < 0 || !capture->detailed_inputs) {
		return Ref<ConfigFile>();
	}
	if (capture->input_data.is_null()) {
		capture->input_data.instantiate();
	}
	return capture->input_data;
}

String WRenderCapture::buffer(const RenderDataRD *p_data, RID p_buffer) {
	WRenderCapture *capture = _find(p_data);
	if (!capture || capture->sample_frame < 0 || !capture->detailed_inputs || p_buffer.is_null()) {
		return String();
	}
	String name = "buffer_" + uitos(p_buffer.get_id());
	if (!capture->input_buffers.has(p_buffer)) {
		Plane plane;
		plane.name = name;
		plane.buffer = true;
		plane.bytes = RD::get_singleton()->buffer_get_data(p_buffer);
		if (plane.bytes.is_empty()) {
			capture->failure.set(ERR_CANT_ACQUIRE_RESOURCE);
		}
		capture->planes.push_back(plane);
		capture->input_buffers.insert(p_buffer);
	}
	return name;
}

void WRenderCapture::_write_plane_task(void *p_userdata, uint32_t p_index) {
	static_cast<WRenderCapture *>(p_userdata)->_write_plane(p_index);
}

void WRenderCapture::finish_frame(const RenderDataRD *p_data) {
	WRenderCapture *capture = _find(p_data);
	if (!capture || capture->sample_frame < 0) {
		return;
	}
	Ref<RenderSceneBuffersRD> rb = p_data->render_buffers;
	for (uint32_t view = 0; view < rb->get_view_count(); view++) {
		capture->_read_texture("hdr_" + itos(view), rb->get_internal_texture(view));
		capture->_read_texture("depth_" + itos(view), rb->get_depth_texture(view));
		capture->_read_texture("scene_" + itos(view), RendererRD::TextureStorage::get_singleton()->render_target_get_rd_texture_slice(rb->get_render_target(), view));
	}
	// Bounded to one frame. Parallel compression/writing never builds a growing
	// readback queue. Deliberate capture stalls require a fixed simulation clock.
	if (!capture->planes.is_empty()) {
		WorkerThreadPool::GroupID task = WorkerThreadPool::get_singleton()->add_native_group_task(&WRenderCapture::_write_plane_task, capture, capture->planes.size());
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(task);
	}
	Ref<ConfigFile> frame;
	frame.instantiate();
	frame->set_value("frame", "process_frame", capture->sample_frame);
	frame->set_value("frame", "seconds", capture->sample_seconds);
	frame->set_value("frame", "render_seconds", p_data->scene_data->time);
	frame->set_value("frame", "camera_transform", p_data->scene_data->cam_transform);
	frame->set_value("frame", "camera_projection", p_data->scene_data->cam_projection);
	frame->set_value("frame", "jitter", p_data->scene_data->taa_jitter);
	frame->set_value("frame", "visible_layers", p_data->scene_data->camera_visible_layers);
	frame->set_value("frame", "geometry_instances", p_data->instances ? int64_t(p_data->instances->size()) : 0);
	frame->set_value("frame", "lights", p_data->lights ? int64_t(p_data->lights->size()) : 0);
	frame->set_value("frame", "directional_lights", int64_t(p_data->directional_light_count));
	frame->set_value("frame", "reflection_probes", p_data->reflection_probes ? int64_t(p_data->reflection_probes->size()) : 0);
	frame->set_value("frame", "decals", p_data->decals ? int64_t(p_data->decals->size()) : 0);
	frame->set_value("frame", "fog_volumes", p_data->fog_volumes ? int64_t(p_data->fog_volumes->size()) : 0);
	frame->set_value("frame", "render_region", p_data->render_region);
	if (capture->input_data.is_valid()) {
		String name = vformat("frame_%08d_inputs.ini", capture->frame_index);
		Error error = capture->input_data->save(capture->directory.path_join(name));
		if (error != OK) {
			capture->failure.set(error);
		}
		frame->set_value("frame", "inputs", name);
		capture->input_data.unref();
	}
	for (const Plane &plane : capture->planes) {
		frame->set_value(plane.name, "file", vformat("frame_%08d_%s.bin.z", capture->frame_index, plane.name));
		frame->set_value(plane.name, "width", plane.width);
		frame->set_value(plane.name, "height", plane.height);
		frame->set_value(plane.name, "format", plane.format);
		frame->set_value(plane.name, "bytes", plane.bytes.size());
		frame->set_value(plane.name, "encoding", "zlib");
		frame->set_value(plane.name, "kind", plane.buffer ? "buffer" : "texture");
		if (plane.error != OK) {
			capture->failure.set(plane.error);
		}
	}
	frame->set_value("frame", "error", capture->failure.get());
	Error error = frame->save(capture->directory.path_join(vformat("frame_%08d.ini", capture->frame_index)));
	if (error != OK) {
		capture->failure.set(error);
	}
	capture->planes.clear();
	capture->input_buffers.clear();
	capture->sample_frame = -1;
	capture->frame_index++;
}

void WRenderCapture::end() {
	if (completed.is_set()) {
		return;
	}
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_end));
}

void WRenderCapture::_end() {
	Ref<WRenderCapture> keep_alive(this);
	Ref<WRenderCapture> *registered = captures.getptr(target);
	if (registered && registered->ptr() == this) {
		captures.erase(target);
	}
	if (frame_index == 0 && failure.get() == OK) {
		failure.set(ERR_UNAVAILABLE);
	}
	Ref<ConfigFile> manifest;
	manifest.instantiate();
	manifest->set_value("capture", "state", failure.get() == OK ? "finished" : "failed");
	manifest->set_value("capture", "frames", frame_index);
	manifest->set_value("capture", "error", failure.get());
	manifest->set_value("capture", "ui_excluded", true);
	Error error = manifest->save(directory.path_join("capture.ini"));
	if (error != OK) {
		failure.set(error);
	}
	completed.set();
}
#endif
