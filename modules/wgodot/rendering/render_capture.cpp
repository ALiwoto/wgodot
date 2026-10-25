// wgodot-changes::file
#include "render_capture.h"
#include "capture_paths.h"
#include "scene_snapshot.h"
#include "capture_pack.h"
#include "gpu_profile.h"
#include "device_capture/recorder.h"
#include "../wgodot_particle_snapshot.h"

#ifdef TOOLS_ENABLED
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_3d.h"
#endif
#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_viewport.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/mesh_storage.h"
#include "servers/rendering/storage/utilities.h"


HashMap<RID, Ref<WRenderCapture>> WRenderCapture::captures;

void WRenderCapture::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("finish_device_capture"), &WRenderCapture::finish_device_capture);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("store_image", "image", "path"), &WRenderCapture::store_image);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("read_texture_image", "texture"), &WRenderCapture::read_texture_image);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("finish_write_task", "task"), &WRenderCapture::finish_write_task);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_snapshot", "snapshot", "path"), &WRenderCapture::save_snapshot);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_snapshots", "snapshots", "paths"), &WRenderCapture::save_snapshots);
	ClassDB::bind_method(D_METHOD("snapshot_material", "material"), &WRenderCapture::snapshot_material);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("skeleton_palettes", "roots"), &WRenderCapture::skeleton_palettes);
	ClassDB::bind_method(D_METHOD("queue_skeleton_palettes", "roots"), &WRenderCapture::queue_skeleton_palettes);
	ClassDB::bind_method(D_METHOD("get_skeleton_palettes"), &WRenderCapture::get_skeleton_palettes);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("instance_uniforms", "instance", "output", "section", "prefix"), &WRenderCapture::instance_uniforms, DEFVAL(String()));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("begin", "viewport", "directory", "passes"), &WRenderCapture::begin, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_sample", "frame", "seconds", "render_time", "time_step", "detailed_inputs"), &WRenderCapture::set_sample, DEFVAL(-1), DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_submitted_frame_count"), &WRenderCapture::get_submitted_frame_count);
	ClassDB::bind_method(D_METHOD("track_particles", "particles", "path"), &WRenderCapture::track_particles);
	ClassDB::bind_method(D_METHOD("end"), &WRenderCapture::end);
	ClassDB::bind_method(D_METHOD("is_completed"), &WRenderCapture::is_completed);
	ClassDB::bind_method(D_METHOD("get_error"), &WRenderCapture::get_error);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("render_transform", "node"), &WRenderCapture::render_transform);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("render_visible", "node"), &WRenderCapture::render_visible);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_skeleton", "skeleton", "output", "section"), &WRenderCapture::write_skeleton);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_skeletons", "root", "node", "frame", "output"), &WRenderCapture::write_skeletons);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_skeleton_list", "root", "skeletons", "frame", "output"), &WRenderCapture::write_skeleton_list);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_spatial", "node", "output", "section"), &WRenderCapture::write_spatial);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_mesh", "node", "output", "section", "palettes"), &WRenderCapture::write_mesh);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_transforms", "nodes", "output", "section"), &WRenderCapture::write_transforms);
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_particle", "particle", "output", "section"), &WRenderCapture::write_particle);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("write_particle_transforms", "particles", "output", "section"), &WRenderCapture::write_particle_transforms);
#endif
}

Error WRenderCapture::finish_device_capture() {
	return WGPUCapture::Recorder::finish();
}

Error WRenderCapture::store_image(const Ref<Image> &p_image, const String &p_path) {
	return WCapturePack::store_image(p_image, p_path);
}

Ref<ShaderMaterial> WRenderCapture::snapshot_material(const Ref<ShaderMaterial> &p_material) {
	ERR_FAIL_COND_V(p_material.is_null(), Ref<ShaderMaterial>());
	const WShaderParameters &state = p_material->get_shader_parameters();
	const Ref<Shader> shader = p_material->get_shader();
	const uint64_t revision = shader.is_valid() ? shader->get_parameter_revision() : 0;
	Ref<Material> next;
	if (p_material->get_next_pass().is_valid()) {
		Ref<ShaderMaterial> shader_pass = p_material->get_next_pass();
		next = shader_pass.is_valid() ? Ref<Material>(snapshot_material(shader_pass)) : Ref<Material>(p_material->get_next_pass()->duplicate(true));
	}
	MaterialSnapshot &previous = material_snapshots[p_material->get_instance_id()];
	const WShaderParameterBuffer &a = previous.parameters;
	const WShaderParameterBuffer &b = state.buffer;
	if (previous.frozen.is_valid() && previous.shader == shader && previous.revision == revision &&
			previous.frozen->get_render_priority() == p_material->get_render_priority() && previous.frozen->get_next_pass() == next &&
			a.words == b.words && a.linear_words == b.linear_words && a.textures == b.textures &&
			a.texture_counts == b.texture_counts && a.overridden == b.overridden && a.colors == b.colors) {
		return previous.frozen;
	}
	previous.shader = shader;
	previous.revision = revision;
	previous.parameters = state.buffer;
	previous.frozen = p_material->duplicate();
	previous.frozen->set_next_pass(next);
	return previous.frozen;
}

void WRenderCapture::instance_uniforms(GeometryInstance3D *p_instance, const Ref<WSceneSnapshot> &p_output, const String &p_section, const String &p_prefix) {
	ERR_FAIL_NULL(p_instance);
	ERR_FAIL_COND(p_output.is_null());
	// Capture after frame publication. The node and renderer already share this
	// typed bank; snapshot it under one lock instead of synchronizing the render
	// thread for every value. Include defaults and exclude stale declarations.
	for (const auto &item : p_instance->get_instance_shader_parameters()->snapshot()) {
		if (item.gpu_index >= 0) {
			p_output->set_value(p_section, p_prefix + String(item.info.name), item.value.to_variant());
		}
	}
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
	error = payload_writer.open(directory.path_join("payloads.wgp"));
	if (error != OK) {
		failure.set(error);
		completed.set();
		return;
	}
	// Query once per recording, outside sampling. "free" is physical RAM;
	// Windows' "available" field is the commit limit and includes the pagefile.
	const int64_t free_memory = OS::get_singleton()->get_memory_info()["free"];
	spool_budget_bytes = free_memory > 0 ? uint64_t(free_memory) / 4 : 0;
#ifdef THREADS_ENABLED
	if (spool_thread.start(_spool_entry, this) == Thread::UNASSIGNED_ID) {
		failure.set(ERR_CANT_CREATE);
		payload_writer.close();
		completed.set();
		return;
	}
#else
	failure.set(ERR_UNAVAILABLE);
	payload_writer.close();
	completed.set();
	return;
#endif
	captures.insert(target, Ref<WRenderCapture>(this));
	Ref<ConfigFile> manifest;
	manifest.instantiate();
	manifest->set_value("capture", "state", "running");
	manifest->set_value("capture", "metadata_phase", "after_sampling");
	manifest->set_value("capture", "scope", "complete 3D color before UI, HDR color and depth; optional pass checkpoints");
	error = manifest->save(directory.path_join("capture.ini"));
	if (error != OK) {
		failure.set(error);
	}
}

void WRenderCapture::set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs) {
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_set_sample).bind(p_frame, p_seconds, p_render_time, p_time_step, p_detailed_inputs));
}

void WRenderCapture::track_particles(RID p_particles, const String &p_path) {
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_track_particles).bind(p_particles, p_path));
}

void WRenderCapture::_track_particles(RID p_particles, const String &p_path) {
	if (p_path.is_empty()) {
		particle_sources.erase(p_particles);
	} else {
		particle_sources.insert(p_particles, p_path);
	}
}

void WRenderCapture::_read_particles(const Ref<ConfigFile> &p_frame) {
	p_frame->set_value("frame", "gpu_particle_states", true);
	for (const KeyValue<RID, String> &entry : particle_sources) {
		WGPUParticleSnapshot::Layout layout;
		Error error = WGPUParticleSnapshot::describe(entry.key, layout);
		if (error != OK) {
			failure.set(error);
			continue;
		}
		String section = "particles:" + uitos(entry.key.get_id());
		p_frame->set_value(section, "kind", "particle_state");
		p_frame->set_value(section, "path", entry.value);
		p_frame->set_value(section, "allocated", layout.buffer.is_valid());
		p_frame->set_value(section, "stride", layout.stride);
		p_frame->set_value(section, "userdata_count", layout.userdata_count);
		p_frame->set_value(section, "capacity", layout.count);
		if (layout.buffer.is_null()) {
			continue;
		}
		Plane plane;
		plane.name = "particles_" + uitos(entry.key.get_id());
		plane.buffer = true;
		plane.byte_count = int64_t(layout.stride) * layout.count;
		p_frame->set_value(section, "buffer", plane.name);
		const int index = readback_frame->planes.size();
		readback_frame->planes.push_back(plane);
		readback_frame->readback_bytes += plane.byte_count;
		pending_readback_bytes.add(plane.byte_count);
		readback_frame->pending++;
		error = RenderingDevice::get_singleton()->buffer_get_data_async(layout.buffer, callable_mp(this, &WRenderCapture::_texture_readback).bind(readback_frame, index));
		if (error != OK) {
			readback_frame->pending--;
			readback_frame->planes.write[index].error = error;
			failure.set(error);
		}
	}
}

void WRenderCapture::_set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs) {
	ERR_FAIL_COND_MSG(ending || completed.is_set(), "Cannot append samples after a rendering capture has ended.");
	_pump_frames(false);
	if (readback_frame.is_valid()) {
		failure.set(ERR_BUSY);
		return;
	}
	readback_frame.instantiate();
	readback_frame->index = frame_index;
	readback_frame->metadata.instantiate();
	readback_frame->metadata->set_value("frame", "sample_ticks_usec", int64_t(OS::get_singleton()->get_ticks_usec()));
	if (WGodotGPUProfile::is_enabled()) {
		readback_frame->metadata->set_value("frame", "native_gpu_profile", true);
	}
	WGodotGPUProfile::mark("capture.sample", directory, p_frame);
	sample_frame = p_frame;
	sample_detailed_inputs = p_detailed_inputs;
	sample_seconds = p_seconds;
	sample_render_time = p_render_time;
	sample_time_step = p_time_step;
	WGPUCapture::Recorder *journal = WGPUCapture::Recorder::get();
	if (journal) {
		const uint64_t begin = WGPUCapture::Recorder::record(RD::get_singleton(), WGPUCapture::Command::SAMPLE_BEGIN, directory, p_frame, p_seconds);
		readback_frame->metadata->set_value("frame", "device_journal", journal->get_path());
		readback_frame->metadata->set_value("frame", "device_begin", int64_t(begin));
	} else if (p_detailed_inputs) {
		ERR_PRINT("Complete GPU inputs require launching with --native-render-capture <directory>.");
		failure.set(ERR_UNCONFIGURED);
	}
}

WRenderCapture *WRenderCapture::_find(const RenderDataRD *p_data) {
	if (captures.is_empty() || p_data->render_buffers.is_null()) {
		return nullptr;
	}
	Ref<WRenderCapture> *capture = captures.getptr(p_data->render_buffers->get_render_target());
	return capture && !(*capture)->ending ? capture->ptr() : nullptr;
}

void WRenderCapture::_read_texture(const String &p_name, RID p_texture) {
	WGPUCapture::Recorder::DiagnosticScope diagnostic_commands;
	WGodotGPUProfile::Scope profile_scope("capture.queue_texture_readback");
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (p_texture.is_null()) {
		failure.set(ERR_UNAVAILABLE);
		return;
	}
	const uint64_t revision = rd->texture_get_content_revision(p_texture);
	const ReadbackFrame::TextureReadback *captured = readback_frame->textures.getptr(p_texture);
	if (revision && captured && captured->revision == revision) {
		Plane plane = readback_frame->planes[captured->plane];
		plane.name = p_name;
		plane.readback_alias = captured->plane;
		plane.alias = captured->plane;
		plane.read_usec = 0;
		readback_frame->planes.push_back(plane);
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
		const RID *cached = readback_textures.getptr(p_texture);
		RID resolved = cached ? *cached : RID();
		if (resolved.is_null()) {
			resolved = rd->texture_create(destination, RD::TextureView());
			if (resolved.is_valid()) {
				readback_textures.insert(p_texture, resolved);
			}
		}
		if (resolved.is_null()) {
			failure.set(ERR_CANT_CREATE);
			return;
		}
		Error error = rd->texture_resolve_multisample(p_texture, resolved);
		if (error == OK) {
			const int index = readback_frame->planes.size();
			_read_texture(p_name, resolved);
			if (revision && readback_frame->planes.size() > index) {
				readback_frame->textures.insert(p_texture, { revision, index });
			}
		} else {
			failure.set(error);
		}
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
		const RID *cached = readback_textures.getptr(p_texture);
		RID depth = cached ? *cached : RID();
		if (depth.is_null()) {
			depth = rd->texture_create(destination, RD::TextureView());
			if (depth.is_valid()) {
				readback_textures.insert(p_texture, depth);
			}
		}
		if (depth.is_null()) {
			failure.set(ERR_CANT_CREATE);
			return;
		}
		RendererRD::CopyEffects::get_singleton()->copy_depth_to_rect(p_texture, depth, Rect2i(0, 0, format.width, format.height));
		const int index = readback_frame->planes.size();
		_read_texture(p_name, depth);
		if (revision && readback_frame->planes.size() > index) {
			readback_frame->textures.insert(p_texture, { revision, index });
		}
		return;
	}
	Plane plane;
	plane.name = p_name;
	plane.width = format.width;
	plane.height = format.height;
	plane.format = format.format;
	plane.byte_count = RD::get_image_format_required_size(format.format, format.width, format.height, format.depth, format.mipmaps);
	const int index = readback_frame->planes.size();
	readback_frame->planes.push_back(plane);
	readback_frame->readback_bytes += plane.byte_count;
	pending_readback_bytes.add(plane.byte_count);
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	// Enqueue the copy at this exact pass boundary. Waiting until finish_frame
	// to enqueue would read overwritten attachments instead of intermediate data.
	readback_frame->pending++;
	Error error = rd->texture_get_data_async(p_texture, 0, callable_mp(this, &WRenderCapture::_texture_readback).bind(readback_frame, index));
	readback_frame->planes.write[index].read_usec = OS::get_singleton()->get_ticks_usec() - started;
	if (error != OK) {
		readback_frame->pending--;
		readback_frame->planes.write[index].error = error;
		failure.set(error);
	} else if (revision) {
		readback_frame->textures.insert(p_texture, { revision, index });
	}
}

void WRenderCapture::_texture_readback(const PackedByteArray &p_bytes, Ref<ReadbackFrame> p_frame, int p_index) {
	// The device invokes this on its render thread when the corresponding
	// fence retires. Later samples cannot overwrite this frame's ownership.
	Plane &plane = p_frame->planes.write[p_index];
	plane.bytes = p_bytes;
	if (p_bytes.size() != plane.byte_count) {
		plane.error = ERR_CANT_ACQUIRE_RESOURCE;
		failure.set(plane.error);
	}
	p_frame->pending--;
	if (p_frame->pending == 0) {
		p_frame->readbacks_finished_usec = OS::get_singleton()->get_ticks_usec();
	}
	if (ending) {
		_finish_readbacks();
	}
}

uint64_t WRenderCapture::_collect_readbacks() {
	WGodotGPUProfile::Scope profile_scope("capture.readback_drain");
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	RenderingDevice::get_singleton()->flush_pending_readbacks();
	return OS::get_singleton()->get_ticks_usec() - started;
}

void WRenderCapture::_pump_frames(bool p_wait) {
	while (!writers.is_empty() && writers.front()->get()->written.is_set()) {
		_drain_write(false);
	}
	while (planning_frame.is_valid() || !readback_frames.is_empty()) {
		if (planning_frame.is_valid()) {
			if (!p_wait && !WorkerThreadPool::get_singleton()->is_task_completed(planning_task)) {
				return;
			}
			{
				WGodotGPUProfile::Scope waiting("capture.wait_planner", directory, planning_frame->index);
				WorkerThreadPool::get_singleton()->wait_for_task_completion(planning_task);
			}
			planning_task = WorkerThreadPool::INVALID_TASK_ID;
			Ref<ReadbackFrame> planned = planning_frame;
			planning_frame.unref();
			writers.push_back(planned);
		}
		if (readback_frames.is_empty()) {
			return;
		}
		Ref<ReadbackFrame> frame = readback_frames.front()->get();
		if (frame->pending) {
			if (!p_wait) {
				return;
			}
			_collect_readbacks();
			if (frame->pending) {
				failure.set(ERR_CANT_ACQUIRE_RESOURCE);
				pending_readback_bytes.sub(frame->readback_bytes);
				readback_frames.pop_front();
				return;
			}
		}
		frame->metadata->set_value("frame", "readback_ticks_usec", int64_t(frame->readbacks_finished_usec));
		for (Plane &plane : frame->planes) {
			if (plane.readback_alias >= 0) {
				const Plane &original = frame->planes[plane.readback_alias];
				plane.bytes = original.bytes;
				plane.error = original.error;
			}
		}
		frame->textures.clear();
		frame->owner = this;
		readback_frames.pop_front();
		// Compare immutable bytes on a worker. One planner preserves chronological
		// file aliases; immutable raw writes run independently.
		planning_frame = frame;
		planning_task = WorkerThreadPool::get_singleton()->add_native_task(
				&WRenderCapture::_plan_write_task, frame.ptr(), true, "Plan capture payload reuse");
		if (p_wait) {
			return;
		}
	}
}

uint64_t WRenderCapture::_queued_bytes() const {
	// Reserve readbacks from submission through packet preparation, including
	// GPU copies that have not reached their callbacks yet. The last frame's
	// immutable comparison data remains resident after its disk packet retires.
	return pending_readback_bytes.get() + spool_pending_bytes.get() + retained_payload_bytes.get();
}

void WRenderCapture::_limit_queued_bytes() {
	queue_peak_bytes.exchange_if_greater(_queued_bytes());
	if (_queued_bytes() <= spool_budget_bytes) {
		return;
	}
	WGodotGPUProfile::Scope waiting("capture.wait_capacity", directory, frame_index - 1);
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	while (_queued_bytes() > spool_budget_bytes) {
		if (!writers.is_empty()) {
			_drain_write(false);
		} else if (planning_frame.is_valid() || !readback_frames.is_empty()) {
			_pump_frames(true);
		} else {
			// One frame can itself exceed the budget. Its immutable comparison
			// data cannot be retired until the next frame replaces it.
			break;
		}
	}
	if (!ending) {
		// Finalization also drains this queue, but that wait is on its own I/O
		// thread. Report only backpressure that actually held a sampled frame.
		const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - started;
		sampling_wait_usec += elapsed;
		sampling_wait_max_usec = MAX(sampling_wait_max_usec, elapsed);
		sampling_wait_count++;
	}
}

bool WRenderCapture::prepare_frame(const RenderDataRD *p_data) {
	WRenderCapture *capture = _find(p_data);
	if (capture && capture->sample_render_time >= 0) {
		// Hold shader time on intervening display frames too. The sampler owns
		// the clock until end(), including while it records a multi-frame scene
		// snapshot; alternating with wall time would flicker animated materials.
		p_data->scene_data->time = capture->sample_render_time;
		p_data->scene_data->time_step = capture->sample_frame >= 0 ? capture->sample_time_step : 0.0;
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

void WRenderCapture::compositor_texture(const RenderDataRD *p_data, const StringName &p_name, RID p_texture, int p_view) {
	WRenderCapture *capture = _find(p_data);
	if (capture && capture->passes && capture->sample_frame >= 0 && capture->sample_detailed_inputs) {
		capture->_read_texture("compositor_" + String(p_name) + "_" + itos(p_view), p_texture);
	}
}

void WRenderCapture::_write_plane(ReadbackFrame *p_frame, uint32_t p_index) {
	Plane &plane = p_frame->planes.write[p_index];
	if (plane.alias >= 0) {
		return;
	}
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	if (plane.needs_write && plane.error == OK) {
		_store_plane(plane);
	}
	plane.write_usec = OS::get_singleton()->get_ticks_usec() - started;
}

void WRenderCapture::_store_plane(Plane &plane) {
	WGodotGPUProfile::Scope profile_scope("capture.store_plane");
	if (plane.byte_count > INT32_MAX) {
		plane.error = ERR_PARAMETER_RANGE_ERROR;
		return;
	}
	const uint64_t write_started = OS::get_singleton()->get_ticks_usec();
	{
		// Capture stores raw bytes only. The session finalizer compresses this
		// archive after every recording has sealed, preserving these entry IDs.
		// Only spool_thread writes this file. No per-plane archive lock.
		WGodotGPUProfile::Scope writing("capture.write_archive");
		plane.error = payload_writer.store(plane.packet);
	}
	plane.file_usec = OS::get_singleton()->get_ticks_usec() - write_started;
}

void WRenderCapture::finish_frame(const RenderDataRD *p_data) {
	WRenderCapture *capture = _find(p_data);
	if (!capture || capture->sample_frame < 0) {
		return;
	}
	WGodotGPUProfile::Scope profile_scope("capture.finish_frame");
	capture->_capture_skeleton_palettes();
	RENDER_TIMESTAMP("Capture Final Readbacks");
	Ref<RenderSceneBuffersRD> rb = p_data->render_buffers;
	for (uint32_t view = 0; view < rb->get_view_count(); view++) {
		capture->_read_texture("hdr_" + itos(view), rb->get_internal_texture(view));
		capture->_read_texture("depth_" + itos(view), rb->get_depth_texture(view));
		capture->_read_texture("scene_" + itos(view), RendererRD::TextureStorage::get_singleton()->render_target_get_rd_texture_slice(rb->get_render_target(), view));
	}
	Ref<ConfigFile> frame = capture->readback_frame->metadata;
	if (WGPUCapture::Recorder::get()) {
		const uint64_t end = WGPUCapture::Recorder::record(RD::get_singleton(), WGPUCapture::Command::SAMPLE_END, capture->directory, capture->sample_frame);
		frame->set_value("frame", "device_end", int64_t(end));
	}
	capture->_read_particles(frame);
	RENDER_TIMESTAMP("Capture Readbacks Queued");
	frame->set_value("frame", "readback_mode", "pipelined");
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
	capture->readback_frame->metadata = frame;
	capture->readback_frames.push_back(capture->readback_frame);
	capture->readback_frame.unref();
	// Reuse capture-owned resolve/depth textures. Each readback copy stays at
	// its pass boundary; command dependencies protect it from later writes.
	capture->sample_frame = -1;
	capture->frame_index++;
	capture->_pump_frames(false);
	// Completed CPU readbacks are not GPU frames in flight. A frame-count cap
	// incorrectly joined the planner after a brief CPU scheduling delay. Bound
	// the entire queue by bytes instead, letting normal draws retire GPU copies.
	capture->_limit_queued_bytes();
	capture->submitted_frame_count.set(capture->frame_index);
}

void WRenderCapture::_drain_write(bool p_all) {
	while (!writers.is_empty()) {
		WGodotGPUProfile::Scope profile_scope("capture.wait_spool", directory, writers.front()->get()->index);
		Ref<ReadbackFrame> frame = writers.front()->get();
		const uint64_t started = frame->written.is_set() ? 0 : OS::get_singleton()->get_ticks_usec();
		{
			MutexLock lock(spool_mutex);
			while (!frame->written.is_set()) {
				spool_changed.wait(lock);
			}
		}
		if (started) {
			const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - started;
			storage_wait_usec += elapsed;
			storage_wait_max_usec = MAX(storage_wait_max_usec, elapsed);
			storage_wait_count++;
		}
		metadata_frames.push_back(frame);
		writers.pop_front();
		if (!p_all) {
			return;
		}
	}
}

void WRenderCapture::_compare_plane(uint32_t p_index, Plane *p_planes) const {
	Plane &plane = p_planes[p_index];
	if (!plane.buffer && !plane.texture_input && plane.byte_count % 8 == 0 &&
			(plane.format == RD::DATA_FORMAT_R16G16B16A16_SFLOAT || plane.format == RD::DATA_FORMAT_R32_SFLOAT ||
					plane.format == RD::DATA_FORMAT_R8G8B8A8_UNORM || plane.format == RD::DATA_FORMAT_R8G8B8A8_SRGB ||
					plane.format == RD::DATA_FORMAT_B8G8R8A8_UNORM || plane.format == RD::DATA_FORMAT_B8G8R8A8_SRGB)) {
		plane.shuffle_stride = 8;
	}
	if (plane.readback_alias >= 0 || !plane.file.is_empty() || plane.error != OK) {
		return;
	}
	for (uint32_t i = 0; i < p_index; i++) {
		const Plane &other = p_planes[i];
		if (!plane.buffer && !plane.texture_input && !other.buffer && !other.texture_input &&
				other.error == OK && other.width == plane.width && other.height == plane.height &&
				other.format == plane.format && other.bytes == plane.bytes) {
			// Read only immutable descriptor/pixel fields from other planes.
			// Their assigned IDs and shared byte ownership are resolved after join.
			plane.alias = i;
			return;
		}
	}
	const OutputPayload *previous = output_payloads.getptr(plane.name);
	if (previous && previous->shuffle_stride == plane.shuffle_stride &&
			(previous->tiles.entries.is_empty() || (previous->tiles.width == uint32_t(plane.width) && previous->tiles.height == uint32_t(plane.height))) &&
			previous->bytes == plane.bytes) {
		plane.file = previous->file;
		plane.entry = previous->entry;
		plane.tiles = previous->tiles;
		plane.reused = true;
		return;
	}
	plane.needs_write = true;
	if (!plane.buffer && !plane.texture_input) {
		plane.tiles = WCapturePayload::compare_tiles(plane.bytes, plane.width, plane.height,
				RD::get_image_format_pixel_size(RD::DataFormat(plane.format)),
				previous ? previous->bytes : PackedByteArray(),
				previous ? previous->tiles : WCapturePayload::Tiles());
		// Intermediate passes usually change only part of the attachment. Match
		// exact tiles against earlier passes as well as the previous sample.
		// Only immutable pixels are read here; IDs are resolved after workers join.
		for (int i = int(p_index) - 1; i >= 0 && !plane.tiles.changed.is_empty(); i--) {
			const Plane &other = p_planes[i];
			if (other.buffer || other.texture_input || other.error != OK || other.readback_alias >= 0 ||
					other.width != plane.width || other.height != plane.height || other.format != plane.format) {
				continue;
			}
			for (uint32_t tile : WCapturePayload::reuse_tiles(plane.bytes, other.bytes, plane.tiles)) {
				plane.tile_aliases.push_back({ tile, uint32_t(i) });
			}
		}
	}
}

void WRenderCapture::_plan_write(ReadbackFrame *p_frame) {
	WGodotGPUProfile::Scope profile_scope("capture.plan_write", directory, p_frame->index);
	Plane *planes = p_frame->planes.ptrw();
	{
		WGodotGPUProfile::Scope comparing("capture.compare_planes", directory, p_frame->index);
		if (p_frame->planes.size() > 1) {
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			WorkerThreadPool::GroupID group = pool->add_template_group_task(
					this, &WRenderCapture::_compare_plane, planes, p_frame->planes.size(), -1, true, "Compare capture planes");
			pool->wait_for_group_task_completion(group);
		} else if (p_frame->planes.size() == 1) {
			_compare_plane(0, planes);
		}
	}
	HashMap<String, OutputPayload> next_payloads;
	uint64_t retained_bytes = 0;
	int packets = 0;
	for (int i = 0; i < p_frame->planes.size(); i++) {
		Plane &plane = planes[i];
		if (plane.alias >= 0) {
			const Plane &other = planes[plane.alias];
			plane.file = other.file;
			plane.entry = other.entry;
			plane.tiles = other.tiles;
			plane.bytes = other.bytes;
			plane.reused = true;
		} else if (plane.needs_write) {
			plane.file = "payloads.wgp";
			plane.entry = next_payload_entry++;
			uint64_t *entries = plane.tiles.entries.ptrw();
			for (uint32_t tile : plane.tiles.changed) {
				entries[tile] = next_payload_entry++;
			}
			for (const Plane::TileAlias &alias : plane.tile_aliases) {
				entries[alias.tile] = planes[alias.plane].tiles.entries[alias.tile];
			}
			packets++;
		} else if (plane.reused) {
			plane.bytes = output_payloads[plane.name].bytes;
		}
		if (!plane.texture_input && plane.error == OK && !plane.bytes.is_empty()) {
			next_payloads.insert(plane.name, { plane.bytes, plane.file, plane.shuffle_stride, plane.entry, plane.tiles });
			if (plane.alias < 0) {
				retained_bytes += plane.bytes.size();
			}
		}
	}
	// References name the owning job's deterministic output, even if it is
	// still being written. end() joins every job before publishing success.
	output_payloads = std::move(next_payloads);
	retained_payload_bytes.set(retained_bytes);
	{
		WGodotGPUProfile::Scope preparing("capture.prepare_packets", directory, p_frame->index);
		// IDs and aliases are now fixed in frame order. Packet construction only
		// reads immutable pixels and writes its own plane; no vector mutation or
		// archive access crosses worker boundaries.
		if (packets > 1) {
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			WorkerThreadPool::GroupID group = pool->add_native_group_task(
					_prepare_plane_task, planes, p_frame->planes.size(), -1, true, "Prepare capture packets");
			pool->wait_for_group_task_completion(group);
		} else if (packets == 1) {
			for (int i = 0; i < p_frame->planes.size(); i++) {
				_prepare_plane_task(planes, i);
			}
		}
	}
	for (Plane &plane : p_frame->planes) {
		p_frame->write_bytes += plane.packet.bytes.size();
		// The next comparison owns its own reference in output_payloads. Disk
		// work must never retain full images or an earlier frame's tile arrays.
		plane.bytes.clear();
		plane.tiles = WCapturePayload::Tiles();
		plane.tile_aliases.clear();
	}
}

void WRenderCapture::_prepare_plane_task(void *p_userdata, uint32_t p_index) {
	Plane &plane = static_cast<Plane *>(p_userdata)[p_index];
	if (plane.needs_write && plane.error == OK) {
		plane.error = WCapturePayload::prepare(plane.entry, plane.bytes, plane.shuffle_stride, plane.tiles, plane.packet);
	}
}

void WRenderCapture::_plan_write_task(void *p_userdata) {
	ReadbackFrame *frame = static_cast<ReadbackFrame *>(p_userdata);
	frame->metadata->set_value("frame", "planning_started_ticks_usec", int64_t(OS::get_singleton()->get_ticks_usec()));
	frame->owner->_plan_write(frame);
	frame->metadata->set_value("frame", "planned_ticks_usec", int64_t(OS::get_singleton()->get_ticks_usec()));
	// The spool can start immediately; planning_frame retains ownership until
	// the render thread joins this planner and transfers the frame to writers.
	{
		MutexLock lock(frame->owner->spool_mutex);
		const uint64_t queued = frame->owner->spool_pending_bytes.add(frame->write_bytes);
		frame->owner->pending_readback_bytes.sub(frame->readback_bytes);
		frame->owner->spool_peak_bytes.exchange_if_greater(queued);
		frame->owner->queue_peak_bytes.exchange_if_greater(frame->owner->_queued_bytes());
		frame->owner->spool_pending.push_back(Ref<ReadbackFrame>(frame));
		frame->owner->spool_changed.notify_all();
	}
}

void WRenderCapture::_spool_entry(void *p_userdata) {
	static_cast<WRenderCapture *>(p_userdata)->_spool_frames();
}

void WRenderCapture::_spool_frames() {
	Thread::set_name("Rendering capture spool");
	while (true) {
		Ref<ReadbackFrame> frame;
		{
			MutexLock lock(spool_mutex);
			while (spool_pending.is_empty() && !spool_closing) {
				spool_changed.wait(lock);
			}
			if (spool_pending.is_empty()) {
				break;
			}
			frame = spool_pending.front()->get();
			spool_pending.pop_front();
		}
		_write_frame(frame.ptr());
		{
			MutexLock lock(spool_mutex);
			spool_pending_bytes.sub(frame->write_bytes);
			frame->written.set();
			spool_changed.notify_all();
		}
	}
}

void WRenderCapture::_write_frame(ReadbackFrame *p_frame) {
	WGodotGPUProfile::Scope profile_scope("capture.write_frame", directory, p_frame->index);
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	Vector<Plane> &write_planes = p_frame->planes;
	Ref<ConfigFile> write_frame = p_frame->metadata;
	write_frame->set_value("frame", "writer_started_ticks_usec", int64_t(started));
	if (!write_planes.is_empty()) {
		// Disk appends only; no shuffling or compression can delay sampling.
		for (int i = 0; i < write_planes.size(); i++) {
			_write_plane(p_frame, i);
		}
		for (Plane &plane : write_planes) {
			if (plane.alias >= 0) {
				const Plane &original = write_planes[plane.alias];
				plane.file = original.file;
				plane.entry = original.entry;
				plane.byte_count = original.byte_count;
				plane.error = original.error;
				plane.reused = true;
			}
		}
	}

	for (const Plane &plane : write_planes) {
		write_frame->set_value(plane.name, "file", plane.file);
		write_frame->set_value(plane.name, "entry", int64_t(plane.entry));
		write_frame->set_value(plane.name, "width", plane.width);
		write_frame->set_value(plane.name, "height", plane.height);
		write_frame->set_value(plane.name, "format", plane.format);
		write_frame->set_value(plane.name, "bytes", plane.byte_count);
		write_frame->set_value(plane.name, "encoding", "archive");
		if (plane.shuffle_stride) {
			write_frame->set_value(plane.name, "shuffle_stride", plane.shuffle_stride);
			write_frame->set_value(plane.name, "shuffle_block_bytes", WCapturePack::BLOCK_BYTES);
		}
		write_frame->set_value(plane.name, "read_usec", int64_t(plane.read_usec));
		write_frame->set_value(plane.name, "write_usec", int64_t(plane.write_usec));
		write_frame->set_value(plane.name, "file_usec", int64_t(plane.file_usec));
		write_frame->set_value(plane.name, "raw_file_bytes", int64_t(plane.packet.bytes.size()));
		write_frame->set_value(plane.name, "reused", plane.reused);
		write_frame->set_value(plane.name, "readback_reused", plane.readback_alias >= 0);
		write_frame->set_value(plane.name, "kind", plane.buffer ? "buffer" : (plane.texture_input ? "texture_input" : "texture"));
		if (plane.texture_input) {
			write_frame->set_value(plane.name, "depth", plane.depth);
			write_frame->set_value(plane.name, "layer", plane.layer);
			write_frame->set_value(plane.name, "mip_offsets", plane.mip_offsets);
			write_frame->set_value(plane.name, "mip_sizes", plane.mip_sizes);
		}
		if (plane.error != OK) {
			failure.set(plane.error);
		}
	}
	write_frame->set_value("frame", "writer_usec", int64_t(OS::get_singleton()->get_ticks_usec() - started));
	write_frame->set_value("frame", "error", failure.get());
	write_planes.clear();
	write_frame.unref();
}

void WRenderCapture::end() {
	if (completed.is_set()) {
		return;
	}
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_end));
}

void WRenderCapture::_end() {
	if (ending || completed.is_set()) {
		return;
	}
	ending = true;
	// Idle frames retire the remaining copies normally. The scene needs no
	// download result to resume; do not force a device-wide flush here.
	_finish_readbacks();
}

void WRenderCapture::_finish_readbacks() {
	if (finalizing || completed.is_set() || (readback_frame.is_valid() && readback_frame->pending)) {
		return;
	}
	for (const Ref<ReadbackFrame> &frame : readback_frames) {
		if (frame->pending) {
			return;
		}
	}
	finalizing = true;
	WGodotGPUProfile::Scope ending_scope("capture.detach_readbacks");
	if (readback_frame.is_valid()) {
		// A viewport disappeared midway through a frame. Its callbacks have
		// drained, but incomplete semantic/frame metadata cannot be published.
		pending_readback_bytes.sub(readback_frame->readback_bytes);
		readback_frame.unref();
		failure.set(ERR_CANT_ACQUIRE_RESOURCE);
	}
	{
		WGPUCapture::Recorder::DiagnosticScope diagnostic_commands;
		for (const KeyValue<RID, RID> &texture : readback_textures) {
			RenderingDevice::get_singleton()->free_rid(texture.value);
		}
	}
	readback_textures.clear();
	// The device has invoked all readback callbacks. Transfer only completed
	// CPU data; the finalizer must never need RenderingDevice or live scene state.
	palette_request.unref();
	{
		MutexLock lock(palette_mutex);
		frame_palettes.unref();
	}
	// captures retains ownership until _complete_end. Ordinary rendering resumes
	// immediately; planning, spool joins, journal flushes and publication run here.
	// Disk finalization can wait for an older planner. A pool task cannot safely
	// await older pool tasks; use an I/O thread, leaving CPU workers available.
	if (finish_thread.start(_finish_cpu, this) == Thread::UNASSIGNED_ID) {
		failure.set(ERR_CANT_CREATE);
		// Exceptional startup failure still has to release queued readbacks/files.
		_finalize_files();
		_complete_end(Ref<WRenderCapture>(this));
	}
}

void WRenderCapture::_finish_cpu(void *p_userdata) {
	Thread::set_name("Rendering capture finalization");
	WRenderCapture *capture = static_cast<WRenderCapture *>(p_userdata);
	capture->_finalize_files();
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(_complete_end).bind(Ref<WRenderCapture>(capture)));
}

void WRenderCapture::_finalize_files() {
	WGodotGPUProfile::Scope finalizing_scope("capture.finalize_files");
	while (planning_frame.is_valid() || !readback_frames.is_empty()) {
		_pump_frames(true);
		_limit_queued_bytes();
	}
	_drain_write();
	if (spool_thread.is_started()) {
		{
			MutexLock lock(spool_mutex);
			spool_closing = true;
			spool_changed.notify_all();
		}
		spool_thread.wait_to_finish();
	}
	{
		WGodotGPUProfile::Scope metadata_scope("capture.write_frame_metadata");
		for (const Ref<ReadbackFrame> &frame : metadata_frames) {
			frame->metadata->set_value("frame", "published_ticks_usec", int64_t(OS::get_singleton()->get_ticks_usec()));
			const Error error = frame->metadata->save(directory.path_join(vformat("frame_%08d.ini", frame->index)));
			if (error != OK) {
				failure.set(error);
			}
		}
		metadata_frames.clear();
	}
	const Error payload_error = payload_writer.close();
	if (payload_error != OK) {
		failure.set(payload_error);
	}
	output_payloads.clear();
	retained_payload_bytes.set(0);
	if (WGPUCapture::Recorder *journal = WGPUCapture::Recorder::get()) {
		const Error error = journal->flush();
		if (error != OK) {
			failure.set(error);
		}
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
	manifest->set_value("capture", "native_profile_tracked", true);
	manifest->set_value("capture", "payload_archive", "payloads.wgp");
	manifest->set_value("capture", "payload_raw_bytes", int64_t(payload_writer.get_raw_bytes()));
	manifest->set_value("capture", "payload_archive_bytes", int64_t(payload_writer.get_archive_bytes()));
	manifest->set_value("capture", "payload_entries", int64_t(payload_writer.get_entry_count()));
	manifest->set_value("capture", "spool_budget_bytes", int64_t(spool_budget_bytes));
	manifest->set_value("capture", "spool_peak_bytes", int64_t(spool_peak_bytes.get()));
	manifest->set_value("capture", "queue_peak_bytes", int64_t(queue_peak_bytes.get()));
	manifest->set_value("capture", "compression_phase", "after_session");
	manifest->set_value("capture", "metadata_phase", "after_sampling");
	manifest->set_value("capture", "storage_wait_usec", int64_t(storage_wait_usec));
	manifest->set_value("capture", "storage_wait_max_usec", int64_t(storage_wait_max_usec));
	manifest->set_value("capture", "storage_wait_count", int64_t(storage_wait_count));
	manifest->set_value("capture", "sampling_wait_usec", int64_t(sampling_wait_usec));
	manifest->set_value("capture", "sampling_wait_max_usec", int64_t(sampling_wait_max_usec));
	manifest->set_value("capture", "sampling_wait_count", int64_t(sampling_wait_count));
	if (WGPUCapture::Recorder *journal = WGPUCapture::Recorder::get()) {
		const WGPUCapture::Statistics statistics = journal->get_statistics();
		manifest->set_value("capture", "device_journal", journal->get_path());
		manifest->set_value("capture", "device_commands", int64_t(statistics.commands));
		manifest->set_value("capture", "device_raw_bytes", int64_t(statistics.raw_bytes));
		manifest->set_value("capture", "device_stored_bytes", int64_t(statistics.stored_bytes));
		manifest->set_value("capture", "device_queue_peak_bytes", int64_t(statistics.peak_queue_bytes));
		manifest->set_value("capture", "device_queue_wait_usec", int64_t(statistics.queue_wait_usec));
	}
	Error error = manifest->save(directory.path_join("capture.ini"));
	if (error != OK) {
		failure.set(error);
	}
}

void WRenderCapture::_complete_end(Ref<WRenderCapture> p_capture) {
	if (p_capture->completed.is_set()) {
		return;
	}
	if (p_capture->finish_thread.is_started()) {
		p_capture->finish_thread.wait_to_finish();
	}
	Ref<WRenderCapture> *registered = captures.getptr(p_capture->target);
	if (registered && *registered == p_capture) {
		captures.erase(p_capture->target);
	}
	p_capture->completed.set();
}

void WRenderCapture::_end_all() {
	Vector<Ref<WRenderCapture>> pending;
	for (const KeyValue<RID, Ref<WRenderCapture>> &entry : captures) {
		pending.push_back(entry.value);
	}
	for (const Ref<WRenderCapture> &capture : pending) {
		capture->_end();
	}
	// Application shutdown has no future draws to retire copies. Drain once
	// for all captures, then join their CPU-only finalizers before device teardown.
	if (!pending.is_empty()) {
		RenderingDevice::get_singleton()->flush_pending_readbacks();
	}
	for (const Ref<WRenderCapture> &capture : pending) {
		_complete_end(capture);
	}
}

void WRenderCapture::finish_all() {
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(_end_all));
	RenderingServer::get_singleton()->sync();
}
#endif
