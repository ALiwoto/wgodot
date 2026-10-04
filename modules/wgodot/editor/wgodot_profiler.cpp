// wgodot-changes::file
#include "wgodot_profiler.h"

#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/editor_profiler.h"
#include "editor/debugger/script_editor_debugger.h"

namespace {

Dictionary failure(const String &p_code, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = p_code;
	response["message"] = p_message;
	return response;
}

Dictionary status(EditorProfiler *p_profiler, int p_session) {
	const EditorProfiler::CaptureInfo &capture = p_profiler->wgodot_capture;
	Dictionary response;
	response["ok"] = true;
	response["command"] = "profile";
	response["session"] = p_session;
	response["profiling"] = p_profiler->is_profiling();
	response["generation"] = int64_t(capture.generation);
	response["frames_received"] = int64_t(capture.frames_received);
	response["first_frame"] = capture.first_frame;
	response["last_frame"] = capture.last_frame;
	response["retained_records"] = p_profiler->wgodot_get_metric_count();
	response["awaiting_total"] = capture.awaiting_total;
	response["has_total"] = capture.has_total;
	response["max_functions"] = capture.max_functions;
	response["native_calls"] = capture.native_calls;
	return response;
}

struct RankedItem {
	const EditorProfiler::Metric::Category::Item *item = nullptr;
	double score = 0;
	bool operator<(const RankedItem &p_other) const {
		if (score != p_other.score) {
			return score > p_other.score;
		}
		return String(item->signature) < String(p_other.item->signature);
	}
};

Dictionary frame_summary(const EditorProfiler::Metric &p_metric) {
	Dictionary row;
	row["frame"] = p_metric.frame_number;
	row["frame_ms"] = p_metric.frame_time * 1000.0;
	row["process_ms"] = p_metric.process_time * 1000.0;
	row["physics_ms"] = p_metric.physics_time * 1000.0;
	row["physics_frame_ms"] = p_metric.physics_frame_time * 1000.0;
	return row;
}

struct RankedFrame {
	const EditorProfiler::Metric *metric = nullptr;
	double score = 0;
	bool operator<(const RankedFrame &p_other) const {
		if (score != p_other.score) {
			return score > p_other.score;
		}
		return metric->frame_number > p_other.metric->frame_number;
	}
};

Dictionary report(EditorProfiler *p_profiler, int p_session, const Dictionary &p_options) {
	Dictionary response = status(p_profiler, p_session);
	const int limit = p_options.get("limit", 30);
	const String action = p_options.get("action", "report");
	const String sort = p_options.get("sort", action == "frames" ? "frame" : "self");
	const int count = p_profiler->wgodot_get_metric_count();
	if (action == "frames") {
		Vector<RankedFrame> ranked;
		for (int i = 0; i < count; i++) {
			const EditorProfiler::Metric &metric = p_profiler->wgodot_get_metric(i);
			if (!metric.valid || metric.accumulated) {
				continue;
			}
			RankedFrame row;
			row.metric = &metric;
			row.score = metric.frame_number;
			if (sort == "frame_time") {
				row.score = metric.frame_time;
			} else if (sort == "process") {
				row.score = metric.process_time;
			} else if (sort == "physics") {
				row.score = metric.physics_time;
			}
			ranked.push_back(row);
		}
		ranked.sort();
		Array frames;
		for (int i = 0; i < MIN(limit, ranked.size()); i++) {
			frames.push_back(frame_summary(*ranked[i].metric));
		}
		response["view"] = "frames";
		response["frames"] = frames;
		response["matching_frames"] = ranked.size();
		response["sort"] = sort;
		return response;
	}

	const String view = p_options.get("view", "total");
	const int requested_frame = p_options.get("frame", -1);
	if (view == "total" && (!p_profiler->wgodot_capture.has_total || p_profiler->is_profiling())) {
		return failure("total_unavailable", "No completed accumulated capture. Use profile stop, or report --view frame while profiling.");
	}
	const EditorProfiler::Metric *metric = nullptr;
	for (int i = count - 1; i >= 0; i--) {
		const EditorProfiler::Metric &candidate = p_profiler->wgodot_get_metric(i);
		if (candidate.valid && candidate.accumulated == (view == "total") && (requested_frame < 0 || candidate.frame_number == requested_frame)) {
			metric = &candidate;
			break;
		}
	}
	if (metric == nullptr) {
		return failure("metric_unavailable", "That profiler record is unavailable. Frame history is bounded; use profile frames to list retained frames.");
	}

	response["view"] = metric->accumulated ? "accumulated" : "frame";
	response["frame"] = metric->frame_number;
	if (!metric->accumulated) {
		response["timing"] = frame_summary(*metric);
	}
	const String filter = String(p_options.get("filter", String())).to_lower();
	Vector<RankedItem> ranked;
	Array categories;
	int available_functions = 0;
	for (const EditorProfiler::Metric::Category &category : metric->categories) {
		Dictionary category_row;
		category_row["name"] = category.name;
		category_row["signature"] = String(category.signature);
		category_row["total_ms"] = category.total_time * 1000.0;
		categories.push_back(category_row);
		if (category.signature != SNAME("script_functions")) {
			continue;
		}
		available_functions = category.items.size();
		for (const EditorProfiler::Metric::Category::Item &item : category.items) {
			if (!filter.is_empty() && !String(item.signature).to_lower().contains(filter) && !item.name.to_lower().contains(filter)) {
				continue;
			}
			RankedItem row;
			row.item = &item;
			row.score = item.self;
			if (sort == "inclusive") {
				row.score = item.total;
			} else if (sort == "calls") {
				row.score = item.calls;
			} else if (sort == "native") {
				row.score = item.internal;
			}
			ranked.push_back(row);
		}
	}
	ranked.sort();
	Array functions;
	for (int i = 0; i < MIN(limit, ranked.size()); i++) {
		const EditorProfiler::Metric::Category::Item &item = *ranked[i].item;
		Dictionary row;
		row["signature"] = String(item.signature);
		row["name"] = item.name;
		row["script"] = item.script;
		row["line"] = item.line;
		row["calls"] = item.calls;
		row["self_ms"] = item.self * 1000.0;
		row["inclusive_ms"] = item.total * 1000.0;
		row["native_ms"] = item.internal * 1000.0;
		row["average_self_ms"] = item.calls > 0 ? item.self * 1000.0 / item.calls : 0.0;
		row["average_inclusive_ms"] = item.calls > 0 ? item.total * 1000.0 / item.calls : 0.0;
		functions.push_back(row);
	}
	response["functions"] = functions;
	response["categories"] = categories;
	response["available_functions"] = available_functions;
	response["matching_functions"] = ranked.size();
	response["sort"] = sort;
	response["possibly_truncated"] = available_functions >= p_profiler->wgodot_capture.max_functions && p_profiler->wgodot_capture.max_functions > 0;
	response["note"] = "Times are milliseconds. Inclusive times overlap; do not sum them. Functions are capped by the runtime before filtering/sorting. Accumulated totals come from the runtime, not from summing retained frames.";
	return response;
}

Dictionary validate(const Dictionary &p_options) {
	const String action = p_options.get("action", "report");
	if (action != "start" && action != "stop" && action != "capture" && action != "status" && action != "report" && action != "frames" && action != "clear") {
		return failure("invalid_action", "Use profile start, stop, capture, status, report, frames, or clear.");
	}
	const String sort = p_options.get("sort", action == "frames" ? "frame" : "self");
	const bool valid_sort = action == "frames" ? (sort == "frame" || sort == "frame_time" || sort == "process" || sort == "physics") : (sort == "self" || sort == "inclusive" || sort == "calls" || sort == "native");
	const String view = p_options.get("view", "total");
	if (!valid_sort || (view != "total" && view != "frame")) {
		return failure("invalid_arguments", "Invalid sort/view. Functions: self|inclusive|calls|native; frames: frame|frame_time|process|physics; view: total|frame.");
	}
	const int frames = p_options.get("frames", 120);
	const int timeout = p_options.get("timeout", 10);
	const int limit = p_options.get("limit", 30);
	const int max_functions = p_options.get("max-functions", 512);
	if (frames < 1 || frames > 1800 || timeout < 1 || timeout > 30 || limit < 1 || limit > 1000 || max_functions < 16 || max_functions > 512) {
		return failure("invalid_arguments", "Ranges: --frames 1..1800, --timeout 1..30 seconds, --limit 1..1000, --max-functions 16..512.");
	}
	if (p_options.has("frame") && (view != "frame" || int64_t(p_options["frame"]) < 0)) {
		return failure("invalid_arguments", "--frame requires --view frame and a nonnegative frame number.");
	}
	if ((action == "stop" || action == "capture") && view != "total") {
		return failure("invalid_arguments", "stop and capture return accumulated totals. Use report --view frame for individual frames.");
	}
	return Dictionary();
}

} // namespace

Dictionary WGodotProfiler::start(int p_session, const Dictionary &p_options) {
	const Dictionary invalid = validate(p_options);
	if (!invalid.is_empty()) {
		return invalid;
	}
	session = p_session;
	options = p_options;
	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_debugger(session);
	if (debugger == nullptr) {
		return failure("invalid_session", "No debugger session is available.");
	}
	EditorProfiler *profiler = debugger->wgodot_get_profiler();
	const String action = options.get("action", "report");
	if (action == "status") {
		return status(profiler, session);
	}
	if (action == "report" || action == "frames") {
		return report(profiler, session, options);
	}
	if (action == "clear") {
		if (profiler->is_profiling() || (profiler->wgodot_capture.awaiting_total && debugger->is_session_active())) {
			return failure("profiler_busy", "Stop profiling and wait for its accumulated report before clearing it.");
		}
		profiler->clear(false);
		return status(profiler, session);
	}
	if (!debugger->is_session_active()) {
		return failure("game_not_running", "Start the game before controlling the profiler.");
	}
	if (debugger->is_breaked()) {
		return failure("debugger_paused", "Resume the debugger before starting or stopping profiling.");
	}
	if (action == "start" || action == "capture") {
		if (profiler->is_profiling() || profiler->wgodot_capture.awaiting_total) {
			return failure("profiler_busy", "A capture is active or awaiting totals. Use profile stop/report before starting another.");
		}
		profiler->clear(false);
		profiler->wgodot_next_max_functions = options.get("max-functions", 512);
		profiler->wgodot_next_native_calls = options.get("native", false);
		profiler->set_profiling(true);
		if (action == "start") {
			return status(profiler, session);
		}
		owns_capture = true;
		target_frames = options.get("frames", 120);
	} else {
		if (!profiler->is_profiling() && !profiler->wgodot_capture.awaiting_total) {
			return report(profiler, session, options);
		}
		if (profiler->is_profiling()) {
			profiler->set_profiling(false);
		}
		stopping = true;
	}
	debugger_id = debugger->get_instance_id();
	generation = profiler->wgodot_capture.generation;
	deadline_msec = OS::get_singleton()->get_ticks_msec() + uint64_t(int(options.get("timeout", 10))) * 1000;
	return Dictionary();
}

Dictionary WGodotProfiler::poll() {
	ScriptEditorDebugger *debugger = Object::cast_to<ScriptEditorDebugger>(ObjectDB::get_instance(debugger_id));
	if (debugger == nullptr || !debugger->is_session_active()) {
		owns_capture = false;
		return failure("session_ended", "The game session ended before the accumulated profile arrived.");
	}
	EditorProfiler *profiler = debugger->wgodot_get_profiler();
	if (profiler->wgodot_capture.generation != generation) {
		owns_capture = false;
		return failure("capture_changed", "The profiler was cleared or restarted while this request was waiting.");
	}
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (!stopping && (!profiler->is_profiling() || profiler->wgodot_capture.frames_received >= uint64_t(target_frames) || now >= deadline_msec)) {
		timed_out = now >= deadline_msec && profiler->wgodot_capture.frames_received < uint64_t(target_frames);
		if (profiler->is_profiling()) {
			profiler->set_profiling(false);
		}
		stopping = true;
		deadline_msec = now + 5000;
	}
	if (stopping && profiler->wgodot_capture.has_total) {
		owns_capture = false;
		Dictionary response = report(profiler, session, options);
		response["timed_out"] = timed_out;
		if (target_frames > 0) {
			response["requested_frames"] = target_frames;
			response["partial"] = profiler->wgodot_capture.frames_received < uint64_t(target_frames);
		}
		return response;
	}
	if (stopping && now >= deadline_msec) {
		owns_capture = false;
		return failure("profiler_timeout", "Stop was sent, but accumulated totals did not arrive. Resume a paused debugger and retry profile report/status.");
	}
	return Dictionary();
}

void WGodotProfiler::cancel() {
	if (!owns_capture) {
		return;
	}
	ScriptEditorDebugger *debugger = Object::cast_to<ScriptEditorDebugger>(ObjectDB::get_instance(debugger_id));
	if (debugger != nullptr && debugger->is_session_active()) {
		EditorProfiler *profiler = debugger->wgodot_get_profiler();
		if (profiler->wgodot_capture.generation == generation && profiler->is_profiling()) {
			profiler->set_profiling(false);
		}
	}
	owns_capture = false;
}
