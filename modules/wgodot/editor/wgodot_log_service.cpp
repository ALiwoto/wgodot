// wgodot-changes::file
/**************************************************************************/
/*  wgodot_log_service.cpp                                                */
/**************************************************************************/

#include "wgodot_log_service.h"

#include "core/debugger/debugger_marshalls.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"

namespace WGodotLogService {

namespace {

Vector<Dictionary> debugger_entries;
uint64_t next_sequence = 1;

Dictionary make_error(const String &p_command, const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

String output_level(int p_type) {
	switch (p_type) {
		case EditorLog::MSG_TYPE_ERROR:
			return "error";
		case EditorLog::MSG_TYPE_WARNING:
			return "warning";
		case EditorLog::MSG_TYPE_EDITOR:
			return "editor";
		case EditorLog::MSG_TYPE_STD:
		case EditorLog::MSG_TYPE_STD_RICH:
		default:
			return "standard";
	}
}

bool parse_sources(const Dictionary &p_options, HashSet<String> &r_sources, String &r_error) {
	const PackedStringArray sources = p_options.get("sources", PackedStringArray());
	if (sources.is_empty()) {
		r_sources.insert("output");
		r_sources.insert("debugger");
		return true;
	}
	for (const String &source : sources) {
		if (source == "all") {
			r_sources.insert("output");
			r_sources.insert("debugger");
		} else if (source == "output" || source == "debugger") {
			r_sources.insert(source);
		} else {
			r_error = "Unknown log source: " + source;
			return false;
		}
	}
	return true;
}

bool parse_levels(const Dictionary &p_options, HashSet<String> &r_levels, String &r_error) {
	const PackedStringArray levels = p_options.get("levels", PackedStringArray());
	for (String level : levels) {
		if (level == "info") {
			level = "standard";
		}
		if (level != "standard" && level != "warning" && level != "error" && level != "editor") {
			r_error = "Unknown log level: " + level;
			return false;
		}
		r_levels.insert(level);
	}
	return true;
}

Array tail_entries(const Array &p_entries, int p_tail) {
	const int start = MAX(0, p_entries.size() - p_tail);
	Array result;
	for (int i = start; i < p_entries.size(); i++) {
		result.push_back(p_entries[i]);
	}
	return result;
}

Array get_output_entries(const HashSet<String> &p_levels, int p_tail, int &r_total, int &r_matching) {
	Array filtered;
	EditorLog *editor_log = EditorNode::get_log();
	const Array messages = editor_log ? editor_log->wgodot_get_messages() : Array();
	r_total = messages.size();
	for (const Variant &message_variant : messages) {
		const Dictionary message = message_variant;
		const int type = message.get("type", EditorLog::MSG_TYPE_STD);
		const String level = output_level(type);
		if (!p_levels.is_empty() && !p_levels.has(level)) {
			continue;
		}
		Dictionary entry;
		entry["source"] = "output";
		entry["level"] = level;
		entry["text"] = message.get("text", String());
		entry["count"] = message.get("count", 1);
		entry["rich"] = type == EditorLog::MSG_TYPE_STD_RICH;
		filtered.push_back(entry);
	}
	r_matching = filtered.size();
	return tail_entries(filtered, p_tail);
}

Array get_debugger_entries(const HashSet<String> &p_levels, int p_tail, int p_session, int &r_total, int &r_matching) {
	Array filtered;
	r_total = 0;
	for (const Dictionary &entry : debugger_entries) {
		if (p_session >= 0 && (int)entry.get("session", -1) != p_session) {
			continue;
		}
		r_total++;
		const String level = entry.get("level", String());
		if (!p_levels.is_empty() && !p_levels.has(level)) {
			continue;
		}
		filtered.push_back(entry);
	}
	r_matching = filtered.size();
	return tail_entries(filtered, p_tail);
}

int count_output_messages() {
	EditorLog *editor_log = EditorNode::get_log();
	return editor_log ? editor_log->wgodot_get_messages().size() : 0;
}

int count_debugger_messages(int p_session) {
	int count = 0;
	for (const Dictionary &entry : debugger_entries) {
		if (p_session < 0 || (int)entry.get("session", -1) == p_session) {
			count++;
		}
	}
	return count;
}

} // namespace

void capture_debugger_message(int p_session, const String &p_message, const Array &p_data) {
	if (p_message != "error") {
		return;
	}
	DebuggerMarshalls::OutputError error;
	if (!error.deserialize(p_data)) {
		return;
	}

	Dictionary entry;
	entry["source"] = "debugger";
	entry["level"] = error.warning ? "warning" : "error";
	entry["session"] = p_session;
	entry["sequence"] = static_cast<int64_t>(next_sequence++);
	entry["time"] = vformat("%d:%02d:%02d:%03d", error.hr, error.min, error.sec, error.msec);
	entry["message"] = error.error_descr.is_empty() ? error.error : error.error_descr;
	entry["condition"] = error.error_descr.is_empty() ? String() : error.error;
	entry["file"] = error.source_file;
	entry["line"] = error.source_line;
	entry["function"] = error.source_func;

	Array stack;
	for (const ScriptLanguage::StackInfo &frame : error.callstack) {
		Dictionary stack_frame;
		stack_frame["file"] = frame.file;
		stack_frame["line"] = frame.line;
		stack_frame["function"] = frame.func;
		stack.push_back(stack_frame);
	}
	entry["stack"] = stack;
	debugger_entries.push_back(entry);
}

void clear_debugger_session(int p_session) {
	Vector<Dictionary> retained;
	for (const Dictionary &entry : debugger_entries) {
		if ((int)entry.get("session", -1) != p_session) {
			retained.push_back(entry);
		}
	}
	debugger_entries = retained;
}

void reset() {
	debugger_entries.clear();
	next_sequence = 1;
}

Dictionary get_logs(const Dictionary &p_options) {
	HashSet<String> sources;
	HashSet<String> levels;
	String option_error;
	if (!parse_sources(p_options, sources, option_error) || !parse_levels(p_options, levels, option_error)) {
		return make_error("logs", "invalid_log_filter", option_error);
	}
	const int tail = p_options.get("tail", 100);
	const int session = p_options.get("session", -1);
	if (tail <= 0 || session < -1) {
		return make_error("logs", "invalid_log_filter", "Log tail must be positive and the optional session must be non-negative.");
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "logs";
	response["tail"] = tail;
	response["session"] = session;
	response["sources"] = p_options.get("sources", PackedStringArray());
	response["levels"] = p_options.get("levels", PackedStringArray());

	if (sources.has("output")) {
		int total = 0;
		int matching = 0;
		const Array entries = get_output_entries(levels, tail, total, matching);
		response["output"] = entries;
		response["output_total"] = total;
		response["output_matching"] = matching;
		response["output_returned"] = entries.size();
	}
	if (sources.has("debugger")) {
		int total = 0;
		int matching = 0;
		const Array entries = get_debugger_entries(levels, tail, session, total, matching);
		response["debugger"] = entries;
		response["debugger_total"] = total;
		response["debugger_matching"] = matching;
		response["debugger_returned"] = entries.size();
	}
	return response;
}

Dictionary clear_logs(const Dictionary &p_options) {
	HashSet<String> sources;
	String option_error;
	if (!parse_sources(p_options, sources, option_error)) {
		return make_error("clear_logs", "invalid_log_filter", option_error);
	}
	const int session = p_options.get("session", -1);
	if (session < -1) {
		return make_error("clear_logs", "invalid_log_filter", "The optional session must be non-negative.");
	}

	int cleared_output = 0;
	int cleared_debugger = 0;
	if (sources.has("output")) {
		cleared_output = count_output_messages();
		if (EditorLog *editor_log = EditorNode::get_log()) {
			editor_log->clear();
		}
	}
	if (sources.has("debugger")) {
		cleared_debugger = count_debugger_messages(session);
		if (session >= 0) {
			clear_debugger_session(session);
		} else {
			debugger_entries.clear();
		}
		EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
		if (debugger_node != nullptr) {
			for (int id = 0;; id++) {
				ScriptEditorDebugger *debugger = debugger_node->get_debugger(id);
				if (debugger == nullptr) {
					break;
				}
				if (session < 0 || session == id) {
					debugger->wgodot_clear_errors();
				}
			}
		}
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "clear_logs";
	response["session"] = session;
	response["cleared_output"] = cleared_output;
	response["cleared_debugger"] = cleared_debugger;
	return response;
}

} // namespace WGodotLogService
