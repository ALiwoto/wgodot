// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_service.cpp                                              */
/**************************************************************************/

#include "wgodot_debug_service.h"

#include "core/config/project_settings.h"
#include "core/debugger/debugger_marshalls.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/script/script_editor_plugin.h"

namespace WGodotDebugService {

namespace {

struct BreakpointRecord {
	int id = 0;
	String path;
	int line = 0;
	bool enabled = true;
};

struct SessionState {
	bool active = false;
	bool breaked = false;
	bool can_debug = false;
	bool has_stackdump = false;
	bool stack_ready = false;
	String reason;
	Dictionary frame;
	uint64_t break_generation = 0;
	uint64_t resume_generation = 0;
};

HashMap<int, BreakpointRecord> breakpoints;
HashMap<int, SessionState> sessions;
int next_breakpoint_id = 1;
bool suppress_breakpoint_sync = false;

Dictionary make_error(const String &p_command, const String &p_action, const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["action"] = p_action;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

BreakpointRecord *find_breakpoint(const String &p_path, int p_line) {
	for (KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		if (entry.value.path == p_path && entry.value.line == p_line) {
			return &entry.value;
		}
	}
	return nullptr;
}

String normalize_script_path(const String &p_path) {
	String path = p_path;
	if (!path.begins_with("res://")) {
		path = ProjectSettings::get_singleton()->localize_path(path);
	}
	return path.replace_char('\\', '/').simplify_path();
}

bool validate_breakpoint_location(const String &p_path, int p_line, String &r_path, String &r_error) {
	r_path = normalize_script_path(p_path);
	if (!r_path.begins_with("res://") || r_path.get_extension().to_lower() != "gd") {
		r_error = "Breakpoints must point to a project .gd file.";
		return false;
	}
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(r_path, &read_error);
	if (read_error != OK) {
		r_error = "Could not read breakpoint script: " + r_path;
		return false;
	}
	const int line_count = source.is_empty() ? 0 : source.count("\n") + 1;
	if (p_line <= 0 || p_line > line_count) {
		r_error = vformat("Breakpoint line %d is outside %s (1-%d).", p_line, r_path, line_count);
		return false;
	}
	return true;
}

void apply_breakpoint(const BreakpointRecord &p_breakpoint, bool p_enabled) {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (debugger_node == nullptr) {
		return;
	}
	suppress_breakpoint_sync = true;
	debugger_node->set_breakpoint(p_breakpoint.path, p_breakpoint.line, p_enabled);
	Ref<Script> script = ResourceLoader::load(p_breakpoint.path, "Script");
	if (script.is_valid()) {
		debugger_node->emit_signal("breakpoint_set_in_tree", script, p_breakpoint.line - 1, p_enabled);
	}
	suppress_breakpoint_sync = false;
}

Dictionary breakpoint_result(const String &p_action, const BreakpointRecord &p_breakpoint) {
	Dictionary response;
	response["ok"] = true;
	response["command"] = "breakpoint";
	response["action"] = p_action;
	Dictionary breakpoint;
	breakpoint["id"] = p_breakpoint.id;
	breakpoint["path"] = p_breakpoint.path;
	breakpoint["line"] = p_breakpoint.line;
	breakpoint["enabled"] = p_breakpoint.enabled;
	response["breakpoint"] = breakpoint;
	return response;
}

ScriptEditorDebugger *get_debugger(int p_session) {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	return debugger_node && p_session >= 0 ? debugger_node->get_debugger(p_session) : nullptr;
}

SessionState &get_session_state(int p_session) {
	return sessions[p_session];
}

} // namespace

void initialize() {
	breakpoints.clear();
	sessions.clear();
	next_breakpoint_id = 1;
	suppress_breakpoint_sync = false;

	if (ScriptEditor::get_singleton() == nullptr) {
		return;
	}
	List<String> existing;
	ScriptEditor::get_singleton()->get_breakpoints(&existing);
	for (const String &location : existing) {
		const int separator = location.rfind(":");
		if (separator < 0) {
			continue;
		}
		const String line_text = location.substr(separator + 1);
		if (!line_text.is_valid_int() || line_text.to_int() <= 0) {
			continue;
		}
		BreakpointRecord record;
		record.id = next_breakpoint_id++;
		record.path = normalize_script_path(location.left(separator));
		record.line = line_text.to_int();
		record.enabled = true;
		breakpoints[record.id] = record;
	}
}

void reset() {
	breakpoints.clear();
	sessions.clear();
	next_breakpoint_id = 1;
	suppress_breakpoint_sync = false;
}

void sync_breakpoint(const String &p_path, int p_line, bool p_enabled) {
	if (suppress_breakpoint_sync) {
		return;
	}
	const String path = normalize_script_path(p_path);
	BreakpointRecord *record = find_breakpoint(path, p_line);
	if (record != nullptr) {
		record->enabled = p_enabled;
	} else if (p_enabled) {
		BreakpointRecord new_record;
		new_record.id = next_breakpoint_id++;
		new_record.path = path;
		new_record.line = p_line;
		new_record.enabled = true;
		breakpoints[new_record.id] = new_record;
	}
}

Dictionary execute_breakpoint(const Dictionary &p_options) {
	const String action = p_options.get("action", String());
	if (action == "add") {
		String path;
		String error;
		const int line = p_options.get("line", 0);
		if (!validate_breakpoint_location(String(p_options.get("path", String())), line, path, error)) {
			return make_error("breakpoint", action, "invalid_breakpoint", error);
		}
		BreakpointRecord *existing = find_breakpoint(path, line);
		if (existing != nullptr) {
			existing->enabled = true;
			apply_breakpoint(*existing, true);
			return breakpoint_result(action, *existing);
		}
		BreakpointRecord record;
		record.id = next_breakpoint_id++;
		record.path = path;
		record.line = line;
		record.enabled = true;
		breakpoints[record.id] = record;
		apply_breakpoint(record, true);
		return breakpoint_result(action, record);
	}

	if (action == "list") {
		Vector<int> ids;
		for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
			ids.push_back(entry.key);
		}
		ids.sort();
		Array result;
		for (int id : ids) {
			const BreakpointRecord &record = breakpoints[id];
			Dictionary breakpoint;
			breakpoint["id"] = record.id;
			breakpoint["path"] = record.path;
			breakpoint["line"] = record.line;
			breakpoint["enabled"] = record.enabled;
			result.push_back(breakpoint);
		}
		Dictionary response;
		response["ok"] = true;
		response["command"] = "breakpoint";
		response["action"] = action;
		response["breakpoints"] = result;
		response["count"] = result.size();
		return response;
	}

	if (action == "clear") {
		Vector<BreakpointRecord> removed;
		for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
			removed.push_back(entry.value);
		}
		breakpoints.clear();
		for (const BreakpointRecord &record : removed) {
			apply_breakpoint(record, false);
		}
		Dictionary response;
		response["ok"] = true;
		response["command"] = "breakpoint";
		response["action"] = action;
		response["cleared"] = removed.size();
		return response;
	}

	if (action == "remove" || action == "enable" || action == "disable") {
		const int id = p_options.get("id", 0);
		BreakpointRecord *record = breakpoints.getptr(id);
		if (record == nullptr) {
			return make_error("breakpoint", action, "breakpoint_not_found", "Breakpoint ID was not found: " + itos(id));
		}
		const BreakpointRecord snapshot = *record;
		if (action == "remove") {
			breakpoints.erase(id);
			apply_breakpoint(snapshot, false);
			return breakpoint_result(action, snapshot);
		}
		record->enabled = action == "enable";
		const BreakpointRecord result = *record;
		apply_breakpoint(result, result.enabled);
		return breakpoint_result(action, result);
	}

	return make_error("breakpoint", action, "unknown_action", "Unknown breakpoint action: " + action);
}

void debugger_started(int p_session) {
	SessionState &state = get_session_state(p_session);
	state.active = true;
	state.breaked = false;
	state.can_debug = false;
	state.stack_ready = false;
	state.reason.clear();
	state.frame.clear();
}

void debugger_stopped(int p_session) {
	SessionState &state = get_session_state(p_session);
	state.active = false;
	state.breaked = false;
	state.can_debug = false;
	state.stack_ready = false;
	state.reason.clear();
	state.frame.clear();
	state.resume_generation++;
}

void debugger_breaked(int p_session, bool p_breaked, bool p_can_debug, const String &p_reason, bool p_has_stackdump) {
	SessionState &state = get_session_state(p_session);
	state.active = true;
	state.breaked = p_breaked;
	state.can_debug = p_can_debug;
	state.reason = p_breaked ? p_reason : String();
	state.has_stackdump = p_has_stackdump;
	state.frame.clear();
	if (p_breaked) {
		state.break_generation++;
		state.stack_ready = !p_has_stackdump;
	} else {
		state.resume_generation++;
		state.stack_ready = false;
	}
}

void capture_debugger_message(int p_session, const String &p_message, const Array &p_data) {
	if (p_message != "stack_dump") {
		return;
	}
	DebuggerMarshalls::ScriptStackDump stack;
	if (!stack.deserialize(p_data)) {
		return;
	}
	SessionState &state = get_session_state(p_session);
	state.frame.clear();
	if (!stack.frames.is_empty()) {
		const ScriptLanguage::StackInfo &frame = stack.frames.front()->get();
		state.frame["index"] = 0;
		state.frame["file"] = frame.file;
		state.frame["line"] = frame.line;
		state.frame["function"] = frame.func;
	}
	state.stack_ready = true;
}

Dictionary get_state(int p_session, const String &p_action) {
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	const bool active = debugger != nullptr && debugger->is_session_active();
	const bool breaked = active && debugger->is_breaked();
	SessionState *cached = sessions.getptr(p_session);

	Dictionary response;
	response["ok"] = true;
	response["command"] = "debug";
	response["action"] = p_action;
	response["session"] = p_session;
	response["active"] = active;
	response["breaked"] = breaked;
	response["can_debug"] = breaked && debugger->is_debuggable();
	response["state"] = !active ? "not_running" : (breaked ? "breaked" : "running");
	response["reason"] = cached && breaked ? cached->reason : String();
	response["frame"] = cached && breaked ? cached->frame : Dictionary();
	return response;
}

Dictionary execute_debug(int p_session, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	r_wait_kind = WAIT_NONE;
	r_generation = 0;
	const String action = p_options.get("action", String());
	if (action == "state") {
		return get_state(p_session, action);
	}

	ScriptEditorDebugger *debugger = get_debugger(p_session);
	if (debugger == nullptr || !debugger->is_session_active()) {
		return make_error("debug", action, "game_not_running", "No active game debugger session was found.");
	}
	SessionState &state = get_session_state(p_session);
	state.active = true;

	if (action == "wait") {
		if (debugger->is_breaked()) {
			if (state.stack_ready) {
				return get_state(p_session, action);
			}
			r_wait_kind = WAIT_CURRENT_BREAK;
		} else {
			r_wait_kind = WAIT_NEXT_BREAK;
		}
		r_generation = state.break_generation;
		return Dictionary();
	}

	if (action == "pause") {
		if (debugger->is_breaked()) {
			if (state.stack_ready) {
				return get_state(p_session, action);
			}
			r_wait_kind = WAIT_CURRENT_BREAK;
		} else {
			r_wait_kind = WAIT_NEXT_BREAK;
			debugger->debug_break();
		}
		r_generation = state.break_generation;
		return Dictionary();
	}

	if (!debugger->is_breaked()) {
		return make_error("debug", action, "debugger_not_breaked", "The game must be stopped at a debugger break before using this action.");
	}
	if (!debugger->is_debuggable()) {
		return make_error("debug", action, "debugger_cannot_continue", "The current debugger break cannot be continued or stepped.");
	}

	if (action == "continue") {
		r_wait_kind = WAIT_RESUME;
		r_generation = state.resume_generation;
		debugger->debug_continue();
		return Dictionary();
	}
	if (action == "step_into" || action == "step_over" || action == "step_out") {
		r_wait_kind = WAIT_NEXT_BREAK;
		r_generation = state.break_generation;
		if (action == "step_into") {
			debugger->debug_step();
		} else if (action == "step_over") {
			debugger->debug_next();
		} else {
			debugger->debug_out();
		}
		return Dictionary();
	}

	return make_error("debug", action, "unknown_action", "Unknown debug action: " + action);
}

bool poll_debug_wait(int p_session, const String &p_action, WaitKind p_wait_kind, uint64_t p_generation, Dictionary &r_response) {
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	SessionState &state = get_session_state(p_session);
	const bool active = debugger != nullptr && debugger->is_session_active();
	if (!active) {
		if (p_wait_kind == WAIT_RESUME) {
			r_response = get_state(p_session, p_action);
			return true;
		}
		r_response = make_error("debug", p_action, "game_stopped", "The game stopped before reaching a debugger break.");
		return true;
	}

	if (p_wait_kind == WAIT_RESUME) {
		if (state.resume_generation > p_generation || !debugger->is_breaked()) {
			r_response = get_state(p_session, p_action);
			return true;
		}
		return false;
	}
	const bool generation_ready = p_wait_kind == WAIT_CURRENT_BREAK ? state.break_generation >= p_generation : state.break_generation > p_generation;
	if (generation_ready && debugger->is_breaked() && state.stack_ready) {
		r_response = get_state(p_session, p_action);
		return true;
	}
	return false;
}

} // namespace WGodotDebugService
