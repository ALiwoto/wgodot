// wgodot-changes::file
#include "wgodot_performance.h"

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

#include "core/debugger/engine_debugger.h"
#include "core/io/resource.h"
#include "core/os/os.h"
#include "main/performance.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/texture.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/wgodot_texture_capture.h"

namespace WGodotPerformance {
namespace {

constexpr int MAX_SAMPLES = 1800;
const Performance::Monitor MONITORS[] = {
	Performance::TIME_FPS,
	Performance::TIME_PROCESS,
	Performance::TIME_PHYSICS_PROCESS,
	Performance::TIME_NAVIGATION_PROCESS,
	Performance::MEMORY_STATIC,
	Performance::MEMORY_STATIC_MAX,
	Performance::OBJECT_COUNT,
	Performance::OBJECT_RESOURCE_COUNT,
	Performance::OBJECT_NODE_COUNT,
	Performance::OBJECT_ORPHAN_NODE_COUNT,
	Performance::RENDER_TOTAL_OBJECTS_IN_FRAME,
	Performance::RENDER_TOTAL_PRIMITIVES_IN_FRAME,
	Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME,
	Performance::RENDER_VIDEO_MEM_USED,
	Performance::RENDER_TEXTURE_MEM_USED,
	Performance::RENDER_BUFFER_MEM_USED,
	Performance::PIPELINE_COMPILATIONS_CANVAS,
	Performance::PIPELINE_COMPILATIONS_DRAW,
};

struct Capture {
	uint64_t request_id = 0;
	String command;
	Dictionary options;
	uint64_t started_usec = 0;
	uint64_t previous_usec = 0;
	uint64_t deadline_usec = 0;
	int frames = 0;
	int requested_frames = 0;
	Vector<double> frame_intervals;
	HashMap<String, Vector<double>> samples;
	HashMap<RID, String> nodes;
};

Capture capture;

Dictionary error(const String &p_command, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["error"] = "performance_request_failed";
	response["message"] = p_message;
	return response;
}

Dictionary statistics(Vector<double> p_values) {
	Dictionary stats;
	if (p_values.is_empty()) {
		return stats;
	}
	double sum = 0;
	for (double value : p_values) {
		sum += value;
	}
	stats["count"] = p_values.size();
	stats["first"] = p_values[0];
	stats["last"] = p_values[p_values.size() - 1];
	stats["change"] = p_values[p_values.size() - 1] - p_values[0];
	p_values.sort();
	stats["min"] = p_values[0];
	stats["max"] = p_values[p_values.size() - 1];
	stats["mean"] = sum / p_values.size();
	stats["p95"] = p_values[MAX(0, int(Math::ceil(p_values.size() * 0.95)) - 1)];
	return stats;
}

String monitor_unit(Performance::Monitor p_monitor) {
	switch (Performance::get_singleton()->get_monitor_type(p_monitor)) {
		case Performance::MONITOR_TYPE_MEMORY:
			return "bytes";
		case Performance::MONITOR_TYPE_TIME:
			return "seconds";
		case Performance::MONITOR_TYPE_PERCENTAGE:
			return "percent";
		default:
			return p_monitor == Performance::TIME_FPS ? "fps" : "count";
	}
}

Dictionary performance_snapshot() {
	Dictionary response;
	response["ok"] = true;
	response["command"] = "perf";
	Array monitors;
	Performance *performance = Performance::get_singleton();
	for (Performance::Monitor monitor : MONITORS) {
		Dictionary entry;
		entry["name"] = performance->get_monitor_name(monitor);
		entry["unit"] = monitor_unit(monitor);
		entry["value"] = performance->get_monitor(monitor);
		monitors.push_back(entry);
	}
	response["monitors"] = monitors;
	response["note"] = "Video memory includes textures and buffers; do not add these counters. Memory/static is engine-tracked memory, not total process RSS. Some monitors update less often than once per frame.";
	return response;
}

void collect_nodes(Node *p_node, HashMap<RID, String> &r_nodes) {
	if (CanvasItem *item = Object::cast_to<CanvasItem>(p_node)) {
		r_nodes.insert(item->get_canvas_item(), String(item->get_path()));
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_nodes(p_node->get_child(i), r_nodes);
	}
}

void collect_texture_object(Object *p_object, void *p_userdata) {
	// References can invoke script hooks. Only gather IDs inside ObjectDB's lock.
	if (Texture2D *texture = Object::cast_to<Texture2D>(p_object)) {
		static_cast<Vector<ObjectID> *>(p_userdata)->push_back(texture->get_instance_id());
	}
}

Array vector_array(const Vector2 &p_value) {
	return { p_value.x, p_value.y };
}

struct TextureRow {
	Dictionary data;
	double order = 0;
	String id;
	bool operator<(const TextureRow &p_other) const {
		return order == p_other.order ? id < p_other.id : order > p_other.order;
	}
};

Dictionary texture_report(const Dictionary &p_options, const WGodotTextureCapture::Result *p_capture, const HashMap<RID, String> &p_nodes) {
	List<RenderingServerTypes::TextureInfo> textures;
	RenderingServer::get_singleton()->texture_debug_usage(&textures);
	// The inventory call is queued when rendering uses a separate thread.
	RenderingServer::get_singleton()->sync();
	HashMap<RID, Dictionary> resources;
	Vector<ObjectID> texture_ids;
	ObjectDB::debug_objects(collect_texture_object, &texture_ids);
	for (ObjectID id : texture_ids) {
		const Ref<Texture2D> texture = ObjectDB::get_ref<Texture2D>(id);
		// Only inspect image-backed resources whose dimensions are already stored.
		// Generic Texture2D getters can allocate GPU textures: DPITexture even
		// rasterizes SVGs in get_width(), and AtlasTexture forwards to its source.
		// Other texture kinds remain in the renderer's allocation inventory.
		if (!Object::cast_to<ImageTexture>(texture.ptr()) && !Object::cast_to<CompressedTexture2D>(texture.ptr())) {
			continue;
		}
		if (texture->get_width() <= 0 || texture->get_height() <= 0) {
			continue;
		}
		const RID rid = texture->get_rid();
		Dictionary &resource = resources[rid];
		resource["resource_id"] = itos(texture->get_instance_id());
		resource["resource_class"] = texture->get_class();
		resource["resource_name"] = texture->get_name();
		resource["resource_path"] = texture->get_path();
	}
	HashMap<RID, String> node_paths(p_nodes);
	if (p_capture && SceneTree::get_singleton() && SceneTree::get_singleton()->get_root()) {
		collect_nodes(SceneTree::get_singleton()->get_root(), node_paths);
	}

	const String filter = String(p_options.get("filter", String())).to_lower();
	const String sort = p_options.get("sort", "bytes");
	const bool unobserved_only = p_options.get("unused", false);
	const double headroom = p_options.get("headroom", 1.25);
	const int limit = p_options.get("limit", 30);
	Vector<TextureRow> rows;
	int64_t allocation_bytes = 0;
	int64_t candidate_savings = 0;
	int aliases = 0;
	int observed = 0;
	for (const RenderingServerTypes::TextureInfo &info : textures) {
		TextureRow row;
		row.id = itos(info.texture.get_id());
		if (const Dictionary *resource = resources.getptr(info.texture)) {
			row.data = resource->duplicate();
		}
		row.data["rid"] = row.id;
		row.data["path"] = info.path;
		row.data["width"] = info.width;
		row.data["height"] = info.height;
		row.data["depth"] = info.depth;
		row.data["format"] = Image::get_format_name(info.format);
		row.data["mipmap_levels"] = info.debug_mipmaps;
		row.data["render_target"] = info.debug_render_target;
		const bool alias = info.debug_proxy_to.is_valid();
		row.data["alias_of"] = alias ? itos(info.debug_proxy_to.get_id()) : String();
		const int64_t bytes = alias ? 0 : info.bytes;
		row.data["estimated_bytes"] = bytes;
		allocation_bytes += bytes;
		aliases += alias ? 1 : 0;
		row.order = bytes;
		const WGodotTextureCapture::Usage *usage = p_capture ? p_capture->textures.getptr(info.texture) : nullptr;
		row.data["observation"] = !p_capture ? "not_captured" : (usage ? "observed_2d" : "not_observed_2d");
		int64_t savings = 0;
		double oversize = 0;
		if (usage) {
			observed++;
			const bool uncertain = usage->uncertain || usage->source_size != Vector2(info.width, info.height);
			row.data["draw_commands"] = int64_t(usage->commands);
			row.data["frames_seen"] = int64_t(usage->frames);
			row.data["max_rendered_pixels"] = vector_array(usage->max_screen_size);
			row.data["measurement_uncertain"] = uncertain;
			row.data["max_pixels_per_texel"] = usage->max_texel_scale;
			Array nodes;
			for (RID item : usage->items) {
				const String *path = node_paths.getptr(item);
				nodes.push_back(path ? *path : "<canvas RID " + itos(item.get_id()) + ">");
			}
			nodes.sort();
			row.data["nodes"] = nodes;
			row.data["nodes_truncated"] = usage->items_truncated;
			// Only the observed 2D mapping is known. Custom shaders, animated UVs,
			// render targets, and unsupported geometry are never resize candidates.
			if (!uncertain && !info.debug_render_target && !alias && info.depth == 0 && usage->max_texel_scale > 0.0) {
				const double factor = MIN(1.0, usage->max_texel_scale * headroom);
				const int width = MAX(1, int(Math::ceil(info.width * factor)));
				const int height = MAX(1, int(Math::ceil(info.height * factor)));
				const int64_t resized_bytes = Image::get_image_data_size(width, height, info.format, info.debug_mipmaps > 1);
				savings = MAX(int64_t(0), bytes - resized_bytes);
				oversize = 1.0 / usage->max_texel_scale;
				row.data["candidate_size"] = Array{ width, height };
				row.data["estimated_savings_bytes"] = savings;
				row.data["oversize_ratio"] = oversize;
				candidate_savings += savings;
			}
		}
		if (unobserved_only && usage) {
			continue;
		}
		const String label = info.path + " " + String(row.data.get("resource_path", String())) + " " + String(row.data.get("resource_name", String()));
		if (!filter.is_empty() && label.to_lower().find(filter) < 0) {
			continue;
		}
		if (sort == "savings") {
			row.order = savings;
		} else if (sort == "ratio") {
			row.order = oversize;
		}
		rows.push_back(row);
	}
	rows.sort();
	Array output;
	for (int i = 0; i < MIN(limit, rows.size()); i++) {
		output.push_back(rows[i].data);
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "textures";
	response["textures"] = output;
	response["total_textures"] = textures.size();
	response["aliases"] = aliases;
	response["matching_textures"] = rows.size();
	response["estimated_allocation_bytes"] = allocation_bytes;
	response["observed_textures"] = observed;
	response["estimated_candidate_savings_bytes"] = candidate_savings;
	response["headroom"] = headroom;
	response["frames"] = p_capture ? p_capture->frames : 0;
	response["capture_truncated"] = p_capture && p_capture->truncated;
	response["note"] = "Inventory lists textures resident at report time; allocation sizes are renderer estimates, not the total GPU counter. Capture measures submitted 2D commands in render-target pixels, after transforms and rectangular clipping, without occlusion testing. Aliases are resolved at capture start. Not observed does not mean unused: shaders, 3D, other screens and aliases may use a texture. Candidate sizes cover only sampled, understood root-viewport 2D uses; inspect every use before resizing.";
	return response;
}

void complete(bool p_timed_out) {
	Dictionary response;
	if (capture.command == "textures") {
		const WGodotTextureCapture::Result usage = WGodotTextureCapture::finish();
		response = texture_report(capture.options, &usage, capture.nodes);
	} else {
		response = performance_snapshot();
		Array monitors = response["monitors"];
		for (int i = 0; i < monitors.size(); i++) {
			Dictionary entry = monitors[i];
			const Vector<double> *values = capture.samples.getptr(entry["name"]);
			if (values) {
				entry["samples"] = statistics(*values);
			}
		}
		response["frame_interval_ms"] = statistics(capture.frame_intervals);
		response["frames"] = capture.frames;
	}
	response["requested_frames"] = capture.requested_frames;
	response["timed_out"] = p_timed_out;
	response["elapsed_seconds"] = (OS::get_singleton()->get_ticks_usec() - capture.started_usec) / 1000000.0;
	const uint64_t request_id = capture.request_id;
	capture = Capture();
	if (EngineDebugger::is_active()) {
		EngineDebugger::get_singleton()->send_message("wgodot:response", { request_id, response });
	}
}

} // namespace

Dictionary execute(uint64_t p_request_id, const String &p_command, const Dictionary &p_options, bool &r_deferred) {
	r_deferred = false;
	const int64_t frames = p_options.get("frames", 0);
	const int64_t timeout = p_options.get("timeout", 10);
	const int64_t limit = p_options.get("limit", 30);
	const double headroom = p_options.get("headroom", 1.25);
	const String sort = p_options.get("sort", "bytes");
	if (frames < 0 || frames > MAX_SAMPLES || timeout < 1 || timeout > 30 || limit < 1 || limit > 1000 || !Math::is_finite(headroom) || headroom < 1.0 || headroom > 4.0) {
		return error(p_command, "Frames must be 0..1800, timeout 1..30 seconds, limit 1..1000, and headroom 1..4.");
	}
	if (sort != "bytes" && sort != "savings" && sort != "ratio") {
		return error(p_command, "Texture sort must be bytes, savings, or ratio.");
	}
	if (p_command == "textures" && frames == 0 && ((bool)p_options.get("unused", false) || sort != "bytes")) {
		return error(p_command, "--unused and sorting by savings/ratio require --frames.");
	}
	if (frames == 0) {
		return p_command == "perf" ? performance_snapshot() : texture_report(p_options, nullptr, HashMap<RID, String>());
	}
	if (capture.request_id != 0) {
		return error(p_command, "Another performance or texture capture is running.");
	}
	RenderingServer::get_singleton()->sync();
	capture.request_id = p_request_id;
	capture.command = p_command;
	capture.options = p_options;
	capture.started_usec = OS::get_singleton()->get_ticks_usec();
	capture.previous_usec = capture.started_usec;
	capture.deadline_usec = capture.started_usec + uint64_t(timeout) * 1000000;
	capture.requested_frames = frames;
	if (p_command == "textures") {
		HashMap<RID, RID> canvas_textures;
		Vector<ObjectID> texture_ids;
		ObjectDB::debug_objects(collect_texture_object, &texture_ids);
		for (ObjectID id : texture_ids) {
			const Ref<Texture2D> texture = ObjectDB::get_ref<Texture2D>(id);
			const CanvasTexture *canvas_texture = Object::cast_to<CanvasTexture>(texture.ptr());
			if (canvas_texture && canvas_texture->get_diffuse_texture().is_valid()) {
				canvas_textures.insert(canvas_texture->get_rid(), canvas_texture->get_diffuse_texture()->get_rid());
			}
		}
		RID root_texture;
		if (SceneTree::get_singleton() && SceneTree::get_singleton()->get_root()) {
			collect_nodes(SceneTree::get_singleton()->get_root(), capture.nodes);
			root_texture = SceneTree::get_singleton()->get_root()->get_texture()->get_rid();
		}
		List<RenderingServerTypes::TextureInfo> textures;
		RenderingServer::get_singleton()->texture_debug_usage(&textures);
		RenderingServer::get_singleton()->sync();
		WGodotTextureCapture::begin(textures, canvas_textures, root_texture, frames);
	}
	r_deferred = true;
	return Dictionary();
}

void end_frame() {
	if (capture.request_id == 0) {
		return;
	}
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (capture.command == "perf") {
		if (capture.frames > 0) {
			capture.frame_intervals.push_back((now - capture.previous_usec) / 1000.0);
		}
		capture.previous_usec = now;
		Performance *performance = Performance::get_singleton();
		for (Performance::Monitor monitor : MONITORS) {
			capture.samples[performance->get_monitor_name(monitor)].push_back(performance->get_monitor(monitor));
		}
		capture.frames++;
	}
	const bool done = capture.command == "perf" ? capture.frames >= capture.requested_frames : WGodotTextureCapture::is_complete();
	if (done || now >= capture.deadline_usec) {
		complete(!done);
	}
}

void reset() {
	WGodotTextureCapture::cancel();
	capture = Capture();
}

} // namespace WGodotPerformance

#else

namespace WGodotPerformance {
Dictionary execute(uint64_t, const String &, const Dictionary &, bool &) {
	return Dictionary();
}
void end_frame() {
}
void reset() {
}
} // namespace WGodotPerformance

#endif // TOOLS_ENABLED && DEBUG_ENABLED
