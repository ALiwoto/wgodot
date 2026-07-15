// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_service.cpp                                              */
/**************************************************************************/

#include "wgodot_debug_service.h"

#include "../wgodot_type_filter.h"
#include "../wgodot_debug_value.h"

#include "core/config/project_settings.h"
#include "core/debugger/debugger_marshalls.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/variant_parser.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"

namespace WGodotDebugService {

namespace {

struct BreakpointRecord {
	int id = 0;
	String path;
	int line = 0;
	String name;
	String condition;
	bool enabled = true;
	bool one_shot = false;
};

struct FrameVariables {
	Array locals;
	Array members;
	Array user_members;
	Array globals;
	Array all_members;
	int64_t self_object_id = 0;
	int64_t target_object_id = 0;
	String target_path;
	String target_type;
	String target_value;
	bool target_terminal = false;
	bool ready = false;
	bool members_enriched = false;
};

struct SessionState {
	bool active = false;
	bool breaked = false;
	bool can_debug = false;
	bool has_stackdump = false;
	bool stack_ready = false;
	bool break_exposed = false;
	String reason;
	Array frames;
	Array matched_breakpoints;
	Array breakpoint_condition_errors;
	Dictionary pending_breakpoint_hit;
	HashMap<int, FrameVariables> frame_variables;
	int selected_frame = 0;
	int pending_variable_frame = -1;
	int pending_variable_count = -1;
	int received_variable_count = 0;
	int pending_member_frame = -1;
	int64_t pending_member_object_id = 0;
	int64_t pending_member_request_id = 0;
	PackedStringArray pending_member_segments;
	int pending_member_segment = 0;
	String pending_member_target_path;
	String pending_member_target_type;
	Dictionary pending_member_error;
	uint64_t break_generation = 0;
	uint64_t resume_generation = 0;
	uint64_t variable_generation = 0;
	uint64_t member_generation = 0;
};

HashMap<int, BreakpointRecord> breakpoints;
HashMap<int, SessionState> sessions;
HashSet<String> managed_breakpoint_locations;
int next_breakpoint_id = 1;
int64_t next_member_request_id = 0x4000000000000000LL;
int64_t breakpoint_sync_generation = 0;
bool suppress_breakpoint_sync = false;

constexpr const char *BREAKPOINT_METADATA_SECTION = "wgodot";
constexpr const char *BREAKPOINT_METADATA_KEY = "logical_breakpoints";

Dictionary make_error(const String &p_command, const String &p_action, const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["action"] = p_action;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

BreakpointRecord *find_plain_breakpoint(const String &p_path, int p_line) {
	for (KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		if (entry.value.path == p_path && entry.value.line == p_line && entry.value.name.is_empty() && entry.value.condition.is_empty() && !entry.value.one_shot) {
			return &entry.value;
		}
	}
	return nullptr;
}

BreakpointRecord *find_breakpoint_by_name(const String &p_name) {
	for (KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		if (entry.value.name == p_name) {
			return &entry.value;
		}
	}
	return nullptr;
}

Dictionary breakpoint_info(const BreakpointRecord &p_breakpoint, bool p_one_shot_removed = false) {
	Dictionary breakpoint;
	breakpoint["id"] = p_breakpoint.id;
	breakpoint["path"] = p_breakpoint.path;
	breakpoint["line"] = p_breakpoint.line;
	breakpoint["name"] = p_breakpoint.name;
	breakpoint["condition"] = p_breakpoint.condition;
	breakpoint["enabled"] = p_breakpoint.enabled;
	breakpoint["one_shot"] = p_breakpoint.one_shot;
	breakpoint["one_shot_removed"] = p_one_shot_removed;
	return breakpoint;
}

String normalize_script_path(const String &p_path);

String breakpoint_location_key(const String &p_path, int p_line) {
	return normalize_script_path(p_path) + ":" + itos(p_line);
}

void persist_breakpoints() {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		return;
	}
	Vector<int> ids;
	for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		ids.push_back(entry.key);
	}
	ids.sort();
	Array records;
	for (int id : ids) {
		records.push_back(breakpoint_info(breakpoints[id]));
	}
	Dictionary metadata;
	metadata["version"] = 1;
	metadata["records"] = records;
	settings->set_project_metadata(BREAKPOINT_METADATA_SECTION, BREAKPOINT_METADATA_KEY, metadata);
	settings->save_project_metadata();
}

bool restore_persisted_breakpoints() {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		return false;
	}
	const Variant stored = settings->get_project_metadata(BREAKPOINT_METADATA_SECTION, BREAKPOINT_METADATA_KEY, Variant());
	if (stored.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary metadata = stored;
	const Variant stored_records = metadata.get("records", Variant());
	if ((int)metadata.get("version", 0) != 1 || stored_records.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array records = stored_records;
	for (const Variant &record_variant : records) {
		if (record_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary stored_record = record_variant;
		BreakpointRecord record;
		record.id = stored_record.get("id", 0);
		record.path = normalize_script_path(stored_record.get("path", String()));
		record.line = stored_record.get("line", 0);
		record.name = stored_record.get("name", String());
		record.condition = stored_record.get("condition", String());
		record.enabled = stored_record.get("enabled", true);
		record.one_shot = stored_record.get("one_shot", false);
		if (record.id <= 0 || record.path.is_empty() || record.line <= 0 || breakpoints.has(record.id) || (!record.name.is_empty() && find_breakpoint_by_name(record.name) != nullptr)) {
			continue;
		}
		breakpoints[record.id] = record;
		next_breakpoint_id = MAX(next_breakpoint_id, record.id + 1);
	}
	return true;
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

void apply_physical_breakpoint(const String &p_path, int p_line, bool p_enabled) {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (debugger_node == nullptr) {
		return;
	}
	suppress_breakpoint_sync = true;
	debugger_node->set_breakpoint(p_path, p_line, p_enabled);
	Ref<Script> script = ResourceLoader::load(p_path, "Script");
	if (script.is_valid()) {
		debugger_node->emit_signal("breakpoint_set_in_tree", script, p_line - 1, p_enabled);
	}
	suppress_breakpoint_sync = false;
}

bool has_enabled_breakpoint_at(const String &p_path, int p_line) {
	for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		if (entry.value.path == p_path && entry.value.line == p_line && entry.value.enabled) {
			return true;
		}
	}
	return false;
}

void refresh_physical_breakpoint(const String &p_path, int p_line) {
	apply_physical_breakpoint(p_path, p_line, has_enabled_breakpoint_at(p_path, p_line));
}

Dictionary breakpoint_result(const String &p_action, const BreakpointRecord &p_breakpoint) {
	Dictionary response;
	response["ok"] = true;
	response["command"] = "breakpoint";
	response["action"] = p_action;
	response["breakpoint"] = breakpoint_info(p_breakpoint);
	return response;
}

ScriptEditorDebugger *get_debugger(int p_session) {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	return debugger_node && p_session >= 0 ? debugger_node->get_debugger(p_session) : nullptr;
}

Array serialized_breakpoints() {
	Vector<int> ids;
	for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		ids.push_back(entry.key);
	}
	ids.sort();
	Array records;
	for (int id : ids) {
		records.push_back(breakpoint_info(breakpoints[id]));
	}
	return records;
}

void send_breakpoint_sync(int p_session) {
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	if (debugger == nullptr || !debugger->is_session_active()) {
		return;
	}
	PackedStringArray locations;
	for (const String &location : managed_breakpoint_locations) {
		locations.push_back(location);
	}
	debugger->wgodot_send_debug_message("wgodot:conditional_breakpoints_sync", { breakpoint_sync_generation, serialized_breakpoints(), locations });
}

void sync_breakpoints_to_active_sessions() {
	breakpoint_sync_generation++;
	for (const KeyValue<int, SessionState> &entry : sessions) {
		if (entry.value.active) {
			send_breakpoint_sync(entry.key);
		}
	}
}

void commit_breakpoint_change(const String &p_path, int p_line) {
	managed_breakpoint_locations.insert(breakpoint_location_key(p_path, p_line));
	persist_breakpoints();
	// Send the logical state first. This leaves a tombstone in the game before
	// Godot removes the physical breakpoint, so an already queued line hit is skipped.
	sync_breakpoints_to_active_sessions();
	refresh_physical_breakpoint(p_path, p_line);
}

SessionState &get_session_state(int p_session) {
	return sessions[p_session];
}

void clear_breakpoint_hit_state(SessionState &p_state) {
	p_state.break_exposed = false;
	p_state.matched_breakpoints.clear();
	p_state.breakpoint_condition_errors.clear();
}

bool is_current_breakpoint_hit(const Dictionary &p_info) {
	const int id = p_info.get("id", 0);
	const BreakpointRecord *record = breakpoints.getptr(id);
	return record != nullptr && record->enabled && record->path == normalize_script_path(p_info.get("path", String())) && record->line == (int)p_info.get("line", 0);
}

void expose_pending_breakpoint_hit(int p_session, SessionState &p_state) {
	const Dictionary hit = p_state.pending_breakpoint_hit;
	p_state.pending_breakpoint_hit.clear();
	const Array incoming_matches = hit.get("matched_breakpoints", Array());
	const Array incoming_errors = hit.get("breakpoint_condition_errors", Array());
	Vector<BreakpointRecord> removed_one_shots;

	for (const Variant &match_variant : incoming_matches) {
		if (match_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary match = match_variant;
		if (!is_current_breakpoint_hit(match)) {
			continue;
		}
		const int id = match.get("id", 0);
		BreakpointRecord *record = breakpoints.getptr(id);
		if (record != nullptr && record->one_shot) {
			removed_one_shots.push_back(*record);
			breakpoints.erase(id);
			match["one_shot_removed"] = true;
		}
		p_state.matched_breakpoints.push_back(match);
	}
	for (const Variant &error_variant : incoming_errors) {
		if (error_variant.get_type() == Variant::DICTIONARY) {
			const Dictionary condition_error = error_variant;
			if (is_current_breakpoint_hit(condition_error)) {
				p_state.breakpoint_condition_errors.push_back(condition_error);
			}
		}
	}

	if (!removed_one_shots.is_empty()) {
		persist_breakpoints();
		sync_breakpoints_to_active_sessions();
		HashSet<String> refreshed_locations;
		for (const BreakpointRecord &record : removed_one_shots) {
			const String key = breakpoint_location_key(record.path, record.line);
			if (!refreshed_locations.has(key)) {
				refreshed_locations.insert(key);
				refresh_physical_breakpoint(record.path, record.line);
			}
		}
	}

	if (!p_state.breakpoint_condition_errors.is_empty() || !p_state.matched_breakpoints.is_empty()) {
		p_state.break_exposed = true;
		p_state.reason = hit.get("reason", "Breakpoint");
		return;
	}

	// The game may have entered the debugger using a configuration that was in
	// flight when the last logical record was removed. Do not expose that stale hit.
	p_state.break_exposed = false;
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	if (debugger != nullptr && debugger->is_breaked() && debugger->is_debuggable()) {
		debugger->debug_continue();
	}
}

void clear_debug_cache(SessionState &p_state) {
	p_state.frames.clear();
	p_state.frame_variables.clear();
	p_state.selected_frame = 0;
	p_state.pending_variable_frame = -1;
	p_state.pending_variable_count = -1;
	p_state.received_variable_count = 0;
	p_state.pending_member_frame = -1;
	p_state.pending_member_object_id = 0;
	p_state.pending_member_request_id = 0;
	p_state.pending_member_segments.clear();
	p_state.pending_member_segment = 0;
	p_state.pending_member_target_path.clear();
	p_state.pending_member_target_type.clear();
	p_state.pending_member_error.clear();
	p_state.variable_generation++;
	p_state.member_generation++;
}

Dictionary get_frame(const SessionState &p_state, int p_frame) {
	if (p_frame < 0 || p_frame >= p_state.frames.size()) {
		return Dictionary();
	}
	return p_state.frames[p_frame];
}

int64_t get_encoded_object_id(const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return 0;
	}
	Object *object = p_value.get_validated_object();
	if (EncodedObjectAsID *encoded = Object::cast_to<EncodedObjectAsID>(object)) {
		return static_cast<int64_t>(encoded->get_object_id());
	}
	return object ? static_cast<int64_t>(object->get_instance_id()) : 0;
}

String format_variable_value(const Variant &p_value, Variant::Type p_declared_type, const String &p_type_hint) {
	String compact;
	if (WGodotDebugValue::format_compact(p_value, compact)) {
		return compact;
	}
	if (p_declared_type == Variant::OBJECT && p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object();
		if (EncodedObjectAsID *encoded = Object::cast_to<EncodedObjectAsID>(object)) {
			const String type = p_type_hint.is_empty() ? "Object" : p_type_hint;
			return vformat("<%s#%d>", type, static_cast<int64_t>(encoded->get_object_id()));
		}
		return object ? vformat("<%s#%d>", object->get_class(), static_cast<int64_t>(object->get_instance_id())) : "null";
	}
	if (p_value.get_type() == Variant::NIL) {
		return "null";
	}

	String value;
	if (VariantWriter::write_to_string(p_value, value) != OK) {
		value = p_value.stringify();
	}
	return value;
}

String format_property_type(Variant::Type p_type, PropertyHint p_hint, const String &p_hint_string) {
	if (p_type == Variant::NIL) {
		return "Variant";
	}
	if (p_type == Variant::OBJECT) {
		return p_hint_string.is_empty() ? "Object" : p_hint_string;
	}
	if (p_type == Variant::ARRAY && p_hint == PROPERTY_HINT_ARRAY_TYPE && !p_hint_string.is_empty()) {
		return "Array[" + p_hint_string + "]";
	}
	if (p_type == Variant::DICTIONARY && p_hint == PROPERTY_HINT_DICTIONARY_TYPE && !p_hint_string.is_empty()) {
		return "Dictionary[" + p_hint_string.replace(";", ", ") + "]";
	}
	return Variant::get_type_name(p_type);
}

String format_property_type(const PropertyInfo &p_property) {
	if (p_property.type == Variant::OBJECT && !p_property.class_name.is_empty()) {
		return p_property.class_name;
	}
	return format_property_type(p_property.type, p_property.hint, p_property.hint_string);
}

Dictionary make_variable(const DebuggerMarshalls::ScriptStackVariable &p_variable) {
	Dictionary variable;
	variable["name"] = p_variable.name;
	variable["type"] = p_variable.var_type == Variant::NIL ? "Variant" : (p_variable.type_hint.is_empty() ? Variant::get_type_name(static_cast<Variant::Type>(p_variable.var_type)) : p_variable.type_hint);
	variable["variant_type"] = Variant::get_type_name(static_cast<Variant::Type>(p_variable.var_type));
	variable["value"] = format_variable_value(p_variable.value, static_cast<Variant::Type>(p_variable.var_type), p_variable.type_hint);
	variable["builtin"] = false;
	const int64_t object_id = get_encoded_object_id(p_variable.value);
	if (object_id != 0) {
		variable["object_id"] = object_id;
	}
	return variable;
}

void enrich_declared_member_types(const SessionState &p_state, int p_frame, FrameVariables &p_variables) {
	p_variables.user_members = p_variables.members.duplicate(true);
	const Dictionary frame = get_frame(p_state, p_frame);
	const String path = frame.get("file", String());
	if (path.is_empty()) {
		return;
	}
	Ref<Script> script = ResourceLoader::load(path, "Script");
	if (script.is_null()) {
		return;
	}

	HashMap<String, String> declared_types;
	List<PropertyInfo> properties;
	script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			continue;
		}
		declared_types[property.name] = format_property_type(property);
	}
	for (int i = 0; i < p_variables.members.size(); i++) {
		Dictionary member = p_variables.members[i];
		const String name = member.get("name", String());
		const String *declared_type = declared_types.getptr(name);
		if (declared_type != nullptr) {
			member["type"] = *declared_type;
			p_variables.members[i] = member;
		}
	}
	p_variables.user_members = p_variables.members.duplicate(true);
	HashSet<StringName> current_member_names;
	script->get_members(&current_member_names);
	Array current_members;
	for (const Variant &member_variant : p_variables.members) {
		const Dictionary member = member_variant;
		const StringName name = member.get("name", String());
		if (name == SNAME("self") || current_member_names.has(name)) {
			current_members.push_back(member);
		}
	}
	p_variables.members = current_members;
}

bool is_frame_action(const String &p_action) {
	return p_action == "frame" || p_action == "locals" || p_action == "members" || p_action == "globals" || p_action == "vars";
}

Dictionary make_stack_response(int p_session, const String &p_action, const SessionState &p_state) {
	Dictionary response;
	response["ok"] = true;
	response["command"] = "debug";
	response["action"] = p_action;
	response["session"] = p_session;
	response["frames"] = p_state.frames;
	response["selected_frame_index"] = p_state.selected_frame;
	response["selected_frame"] = get_frame(p_state, p_state.selected_frame);
	return response;
}

Dictionary make_frame_response(int p_session, const Dictionary &p_options, const SessionState &p_state) {
	const String action = p_options.get("action", String());
	Dictionary response = make_stack_response(p_session, action, p_state);
	response.erase("frames");
	const FrameVariables *variables = p_state.frame_variables.getptr(p_state.selected_frame);
	if (variables && !variables->target_path.is_empty()) {
		Dictionary target;
		target["path"] = variables->target_path;
		target["type"] = variables->target_type.is_empty() ? "Object" : variables->target_type;
		target["terminal"] = variables->target_terminal;
		if (variables->target_terminal) {
			target["value"] = variables->target_value;
		} else {
			target["object_id"] = variables->target_object_id;
			target["value"] = vformat("<%s#%d>", String(target["type"]), variables->target_object_id);
		}
		response["target"] = target;
	}
	if (action == "locals" || action == "vars") {
		response["locals"] = variables ? variables->locals : Array();
	}
	if (action == "members" || action == "vars") {
		Array members;
		if (variables) {
			members = action == "members" && (bool)p_options.get("all", false) ? variables->all_members : variables->members;
			const bool exclude_builtin = p_options.get("exclude_builtin", false);
			const String filter_type = String(p_options.get("filter_type", String())).strip_edges();
			const String filter_name = String(p_options.get("filter_name", String())).strip_edges();
			if (exclude_builtin || !filter_type.is_empty() || !filter_name.is_empty()) {
				Array filtered;
				for (const Variant &member_variant : members) {
					const Dictionary member = member_variant;
					if (exclude_builtin && (bool)member.get("builtin", false)) {
						continue;
					}
					const String member_type = member.get("type", "Variant");
					if (!filter_type.is_empty() && !WGodotTypeFilter::matches(member_type, filter_type)) {
						continue;
					}
					const String member_name = member.get("name", String());
					if (!filter_name.is_empty() && member_name.findn(filter_name) < 0) {
						continue;
					}
					filtered.push_back(member);
				}
				members = filtered;
			}
		}
		response["members"] = members;
	}
	if (action == "globals" || action == "vars") {
		response["globals"] = variables ? variables->globals : Array();
	}
	return response;
}

bool parse_member_target(const String &p_target, PackedStringArray &r_segments, String &r_error) {
	r_segments.clear();
	if (p_target.is_empty() || p_target == "self") {
		return true;
	}
	const PackedStringArray segments = p_target.split(".", true);
	for (int i = 0; i < segments.size(); i++) {
		const String segment = segments[i].strip_edges();
		if (segment.is_empty()) {
			r_error = "Nested member paths cannot contain empty segments: " + p_target;
			return false;
		}
		if (i == 0 && segment == "self") {
			continue;
		}
		r_segments.push_back(segment);
	}
	return true;
}

void request_debug_object(ScriptEditorDebugger *p_debugger, SessionState &p_state, int64_t p_object_id) {
	Dictionary options;
	options["object_id"] = p_object_id;
	if (!p_state.pending_member_segments.is_empty() && p_state.pending_member_segment == p_state.pending_member_segments.size() - 1) {
		options["expanded_member"] = p_state.pending_member_segments[p_state.pending_member_segment];
	}
	p_state.pending_member_request_id = next_member_request_id++;
	p_debugger->send_message("wgodot:request", { p_state.pending_member_request_id, "debug_inspect", options });
}

void populate_inspected_members(FrameVariables &p_variables, const Array &p_inspected_members, const String &p_type) {
	p_variables.target_terminal = false;
	p_variables.target_value.clear();
	Array current_members;
	Array user_members;
	Array all_members;
	for (const Variant &member_variant : p_inspected_members) {
		if (member_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary member = member_variant;
		all_members.push_back(member);
		if (!(bool)member.get("builtin", true)) {
			user_members.push_back(member);
			if ((bool)member.get("current", false)) {
				current_members.push_back(member);
			}
		}
	}

	Dictionary self;
	self["name"] = "self";
	self["type"] = p_type.is_empty() ? "Object" : p_type;
	self["variant_type"] = "Object";
	self["value"] = vformat("<%s#%d>", String(self["type"]), p_variables.target_object_id != 0 ? p_variables.target_object_id : p_variables.self_object_id);
	self["object_id"] = p_variables.target_object_id != 0 ? p_variables.target_object_id : p_variables.self_object_id;
	self["builtin"] = false;
	self["current"] = true;

	current_members.push_front(self);
	user_members.push_front(self);
	all_members.push_front(self);
	p_variables.members = current_members;
	p_variables.user_members = user_members;
	p_variables.all_members = all_members;
	p_variables.members_enriched = true;
}

Dictionary prepare_member_metadata(int p_session, ScriptEditorDebugger *p_debugger, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	SessionState &state = get_session_state(p_session);
	const String action = p_options.get("action", String());
	FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
	if (variables == nullptr || !variables->ready) {
		return make_error("debug", action, "variables_unavailable", "Variables for the selected stack frame are not loaded.");
	}
	PackedStringArray segments;
	String parse_error;
	const String requested_target = p_options.get("target", String());
	if (!parse_member_target(requested_target, segments, parse_error)) {
		return make_error("debug", action, "invalid_member_target", parse_error);
	}

	int64_t object_id = variables->self_object_id;
	String target_type = "Object";
	String target_path = segments.is_empty() ? String() : "self";
	if (object_id == 0) {
		return make_error("debug", action, "self_unavailable", "The selected frame has no live self object to inspect.");
	}
	if (state.pending_member_frame >= 0 && state.pending_member_frame != state.selected_frame) {
		return make_error("debug", action, "member_request_busy", vformat("Stack frame %d member metadata is still being loaded.", state.pending_member_frame));
	}
	if (state.pending_member_frame < 0) {
		state.pending_member_frame = state.selected_frame;
		state.pending_member_object_id = object_id;
		state.pending_member_segments = segments;
		state.pending_member_segment = 0;
		state.pending_member_target_path = target_path;
		state.pending_member_target_type = target_type;
		state.pending_member_error.clear();
		request_debug_object(p_debugger, state, object_id);
	}
	r_wait_kind = WAIT_MEMBERS;
	r_generation = state.member_generation;
	return Dictionary();
}

Dictionary prepare_ready_frame_action(int p_session, ScriptEditorDebugger *p_debugger, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	const String action = p_options.get("action", String());
	if (action == "members" || action == "vars") {
		return prepare_member_metadata(p_session, p_debugger, p_options, r_wait_kind, r_generation);
	}
	return make_frame_response(p_session, p_options, get_session_state(p_session));
}

Dictionary prepare_stack_action(int p_session, ScriptEditorDebugger *p_debugger, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	SessionState &state = get_session_state(p_session);
	const String action = p_options.get("action", String());
	if (action == "stack") {
		return make_stack_response(p_session, action, state);
	}
	if (!is_frame_action(action)) {
		return make_error("debug", action, "unknown_action", "Unknown debug action: " + action);
	}
	if (state.frames.is_empty()) {
		return make_error("debug", action, "stack_unavailable", "The current debugger break has no stack frames.");
	}

	const int frame = p_options.has("frame") ? (int)p_options["frame"] : state.selected_frame;
	if (frame < 0 || frame >= state.frames.size()) {
		return make_error("debug", action, "frame_not_found", vformat("Stack frame %d is outside the available range 0-%d.", frame, state.frames.size() - 1));
	}
	if (action == "frame" && !p_options.has("frame")) {
		return make_frame_response(p_session, p_options, state);
	}

	if (state.pending_variable_frame >= 0) {
		return make_error("debug", action, "variable_request_busy", vformat("Stack frame %d variables are still being loaded.", state.pending_variable_frame));
	}
	if (state.pending_member_frame >= 0) {
		return make_error("debug", action, "member_request_busy", vformat("Stack frame %d member metadata is still being loaded.", state.pending_member_frame));
	}
	state.selected_frame = frame;
	if (action == "frame") {
		return make_frame_response(p_session, p_options, state);
	}

	FrameVariables &variables = state.frame_variables[frame];
	variables.locals.clear();
	variables.members.clear();
	variables.user_members.clear();
	variables.globals.clear();
	variables.all_members.clear();
	variables.self_object_id = 0;
	variables.target_object_id = 0;
	variables.target_path.clear();
	variables.target_type.clear();
	variables.target_value.clear();
	variables.target_terminal = false;
	variables.ready = false;
	variables.members_enriched = false;
	state.pending_variable_frame = frame;
	state.pending_variable_count = -1;
	state.received_variable_count = 0;
	if (!p_debugger->request_stack_dump(frame)) {
		state.pending_variable_frame = -1;
		state.frame_variables.erase(frame);
		return make_error("debug", action, "variable_request_failed", "Could not request variables for the selected stack frame.");
	}

	r_wait_kind = WAIT_VARIABLES;
	r_generation = state.variable_generation;
	return Dictionary();
}

} // namespace

void initialize() {
	breakpoints.clear();
	sessions.clear();
	managed_breakpoint_locations.clear();
	next_breakpoint_id = 1;
	breakpoint_sync_generation = 0;
	suppress_breakpoint_sync = false;

	if (ScriptEditor::get_singleton() == nullptr) {
		return;
	}
	if (restore_persisted_breakpoints()) {
		for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
			managed_breakpoint_locations.insert(breakpoint_location_key(entry.value.path, entry.value.line));
			refresh_physical_breakpoint(entry.value.path, entry.value.line);
		}
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
		managed_breakpoint_locations.insert(breakpoint_location_key(record.path, record.line));
	}
	persist_breakpoints();
}

void reset() {
	breakpoints.clear();
	sessions.clear();
	managed_breakpoint_locations.clear();
	next_breakpoint_id = 1;
	breakpoint_sync_generation = 0;
	suppress_breakpoint_sync = false;
}

void sync_breakpoint(const String &p_path, int p_line, bool p_enabled) {
	if (suppress_breakpoint_sync) {
		return;
	}
	const String path = normalize_script_path(p_path);
	bool found = false;
	for (KeyValue<int, BreakpointRecord> &entry : breakpoints) {
		if (entry.value.path == path && entry.value.line == p_line) {
			entry.value.enabled = p_enabled;
			found = true;
		}
	}
	if (!found && p_enabled) {
		BreakpointRecord new_record;
		new_record.id = next_breakpoint_id++;
		new_record.path = path;
		new_record.line = p_line;
		new_record.enabled = true;
		breakpoints[new_record.id] = new_record;
	}
	managed_breakpoint_locations.insert(breakpoint_location_key(path, p_line));
	persist_breakpoints();
	sync_breakpoints_to_active_sessions();
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
		const String name = String(p_options.get("name", String())).strip_edges();
		const String condition = String(p_options.get("condition", String())).strip_edges();
		const bool one_shot = p_options.get("one_shot", false);
		if (p_options.has("name") && name.is_empty()) {
			return make_error("breakpoint", action, "invalid_breakpoint_name", "Breakpoint names cannot be empty.");
		}
		if (!name.is_empty() && name.is_valid_int()) {
			return make_error("breakpoint", action, "invalid_breakpoint_name", "Breakpoint names cannot be integers because integer selectors refer to breakpoint IDs.");
		}
		if (!name.is_empty() && find_breakpoint_by_name(name) != nullptr) {
			return make_error("breakpoint", action, "duplicate_breakpoint_name", "Breakpoint names must be unique: " + name);
		}
		if (p_options.has("condition") && condition.is_empty()) {
			return make_error("breakpoint", action, "invalid_breakpoint_condition", "Breakpoint conditions cannot be empty.");
		}

		BreakpointRecord *existing = name.is_empty() && condition.is_empty() && !one_shot ? find_plain_breakpoint(path, line) : nullptr;
		if (existing != nullptr) {
			existing->enabled = true;
			commit_breakpoint_change(path, line);
			return breakpoint_result(action, *existing);
		}
		BreakpointRecord record;
		record.id = next_breakpoint_id++;
		record.path = path;
		record.line = line;
		record.name = name;
		record.condition = condition;
		record.enabled = true;
		record.one_shot = one_shot;
		breakpoints[record.id] = record;
		commit_breakpoint_change(path, line);
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
			result.push_back(breakpoint_info(record));
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
		HashSet<String> removed_locations;
		for (const KeyValue<int, BreakpointRecord> &entry : breakpoints) {
			removed.push_back(entry.value);
			managed_breakpoint_locations.insert(breakpoint_location_key(entry.value.path, entry.value.line));
		}
		breakpoints.clear();
		persist_breakpoints();
		sync_breakpoints_to_active_sessions();
		for (const BreakpointRecord &record : removed) {
			const String key = breakpoint_location_key(record.path, record.line);
			if (!removed_locations.has(key)) {
				removed_locations.insert(key);
				apply_physical_breakpoint(record.path, record.line, false);
			}
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
		const String name = String(p_options.get("name", String())).strip_edges();
		BreakpointRecord *record = id > 0 ? breakpoints.getptr(id) : find_breakpoint_by_name(name);
		if (record == nullptr) {
			const String selector = id > 0 ? "ID " + itos(id) : "name " + name;
			return make_error("breakpoint", action, "breakpoint_not_found", "Breakpoint was not found by " + selector + ".");
		}
		const BreakpointRecord snapshot = *record;
		if (action == "remove") {
			breakpoints.erase(snapshot.id);
			commit_breakpoint_change(snapshot.path, snapshot.line);
			return breakpoint_result(action, snapshot);
		}
		record->enabled = action == "enable";
		const BreakpointRecord result = *record;
		commit_breakpoint_change(result.path, result.line);
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
	clear_breakpoint_hit_state(state);
	state.pending_breakpoint_hit.clear();
	clear_debug_cache(state);
	send_breakpoint_sync(p_session);
}

void debugger_stopped(int p_session) {
	SessionState &state = get_session_state(p_session);
	state.active = false;
	state.breaked = false;
	state.can_debug = false;
	state.stack_ready = false;
	state.reason.clear();
	clear_breakpoint_hit_state(state);
	state.pending_breakpoint_hit.clear();
	clear_debug_cache(state);
	state.resume_generation++;
}

void debugger_breaked(int p_session, bool p_breaked, bool p_can_debug, const String &p_reason, bool p_has_stackdump) {
	SessionState &state = get_session_state(p_session);
	state.active = true;
	state.breaked = p_breaked;
	state.can_debug = p_can_debug;
	state.reason = p_breaked ? p_reason : String();
	state.has_stackdump = p_has_stackdump;
	clear_breakpoint_hit_state(state);
	clear_debug_cache(state);
	if (p_breaked) {
		state.break_generation++;
		state.stack_ready = !p_has_stackdump;
		if (p_reason == "Breakpoint" && !state.pending_breakpoint_hit.is_empty()) {
			expose_pending_breakpoint_hit(p_session, state);
		} else {
			state.break_exposed = true;
		}
	} else {
		state.pending_breakpoint_hit.clear();
		state.resume_generation++;
		state.stack_ready = false;
	}
}

void capture_debugger_message(int p_session, const String &p_message, const Array &p_data) {
	SessionState &state = get_session_state(p_session);
	if (p_message == "wgodot:conditional_breakpoint_hit") {
		if (p_data.size() == 1 && p_data[0].get_type() == Variant::DICTIONARY) {
			state.pending_breakpoint_hit = p_data[0];
		}
		return;
	}
	if (p_message == "stack_dump") {
		DebuggerMarshalls::ScriptStackDump stack;
		if (!stack.deserialize(p_data)) {
			return;
		}
		clear_debug_cache(state);
		int index = 0;
		for (const ScriptLanguage::StackInfo &frame : stack.frames) {
			Dictionary frame_info;
			frame_info["index"] = index++;
			frame_info["file"] = frame.file;
			frame_info["line"] = frame.line;
			frame_info["function"] = frame.func;
			state.frames.push_back(frame_info);
		}
		state.stack_ready = true;
		return;
	}

	if (p_message == "stack_frame_vars") {
		if (state.pending_variable_frame < 0 || p_data.size() != 1) {
			return;
		}
		state.pending_variable_count = p_data[0];
		state.received_variable_count = 0;
		if (state.pending_variable_count == 0) {
			const int frame = state.pending_variable_frame;
			FrameVariables &variables = state.frame_variables[frame];
			enrich_declared_member_types(state, frame, variables);
			variables.ready = true;
			state.pending_variable_frame = -1;
			state.variable_generation++;
		}
		return;
	}
	if (p_message == "wgodot:response" && state.pending_member_frame >= 0 && p_data.size() == 2 &&
			p_data[0].get_type() == Variant::INT && (int64_t)p_data[0] == state.pending_member_request_id &&
			p_data[1].get_type() == Variant::DICTIONARY) {
		const Dictionary inspected = p_data[1];
		if (!(bool)inspected.get("ok", false)) {
			state.pending_member_error = make_error("debug", "members", inspected.get("error", "inspect_failed"), inspected.get("message", "The live object could not be inspected."));
		} else {
			const String inspected_type = inspected.get("type", "Object");
			const Array inspected_members = inspected.get("members", Array());
			if (state.pending_member_target_type == "Object" && !inspected_type.is_empty()) {
				state.pending_member_target_type = inspected_type;
			}

			if (state.pending_member_segment < state.pending_member_segments.size()) {
				const String segment = state.pending_member_segments[state.pending_member_segment];
				Dictionary member;
				for (const Variant &member_variant : inspected_members) {
					if (member_variant.get_type() != Variant::DICTIONARY) {
						continue;
					}
					const Dictionary candidate = member_variant;
					if (String(candidate.get("name", String())) == segment) {
						member = candidate;
						break;
					}
				}

				const String path = state.pending_member_target_path + "." + segment;
				if (member.is_empty()) {
					state.pending_member_error = make_error("debug", "members", "member_target_not_found", "Nested member was not found: " + path);
				} else {
					const int64_t object_id = member.get("object_id", 0);
					const String type = member.get("type", "Object");
					if (object_id == 0) {
						const String value = member.get("value", String());
						const String variant_type = member.get("variant_type", String());
						const bool final_segment = state.pending_member_segment == state.pending_member_segments.size() - 1;
						const bool expandable_value = variant_type == "String" || variant_type == "Array" || (variant_type.begins_with("Packed") && variant_type.ends_with("Array"));
						if (final_segment && expandable_value) {
							FrameVariables *variables = state.frame_variables.getptr(state.pending_member_frame);
							if (variables != nullptr && variables->ready) {
								variables->target_object_id = 0;
								variables->target_path = path;
								variables->target_type = type;
								variables->target_value = value;
								variables->target_terminal = true;
								variables->members.clear();
								variables->user_members.clear();
								variables->all_members.clear();
								variables->members_enriched = true;
							}
						} else {
							const String message = value == "null" ? path + " is null; no live object can be inspected. Hint: godot --wg list " + type : path + " is not a live Object.";
							state.pending_member_error = make_error("debug", "members", value == "null" ? "member_target_null" : "member_target_not_object", message);
						}
					} else {
						state.pending_member_object_id = object_id;
						state.pending_member_target_path = path;
						state.pending_member_target_type = type;
						state.pending_member_segment++;
						ScriptEditorDebugger *debugger = get_debugger(p_session);
						if (debugger != nullptr) {
							request_debug_object(debugger, state, object_id);
							return;
						}
						state.pending_member_error = make_error("debug", "members", "debugger_unavailable", "The debugger became unavailable while resolving nested members.");
					}
				}
			} else {
				FrameVariables *variables = state.frame_variables.getptr(state.pending_member_frame);
				if (variables != nullptr && variables->ready) {
					if (!state.pending_member_target_path.is_empty()) {
						variables->target_object_id = state.pending_member_object_id;
						variables->target_path = state.pending_member_target_path;
						variables->target_type = inspected_type;
					}
					populate_inspected_members(*variables, inspected_members, inspected_type);
				}
			}
		}

		state.pending_member_frame = -1;
		state.pending_member_object_id = 0;
		state.pending_member_request_id = 0;
		state.pending_member_segments.clear();
		state.pending_member_segment = 0;
		state.pending_member_target_path.clear();
		state.pending_member_target_type.clear();
		state.member_generation++;
		return;
	}

	if (p_message != "stack_frame_var" || state.pending_variable_frame < 0 || state.pending_variable_count < 0) {
		return;
	}
	DebuggerMarshalls::ScriptStackVariable variable;
	if (!variable.deserialize(p_data)) {
		return;
	}
	FrameVariables &variables = state.frame_variables[state.pending_variable_frame];
	const Dictionary variable_info = make_variable(variable);
	if (variable.type == 0) {
		variables.locals.push_back(variable_info);
	} else if (variable.type == 1 && !variable.name.begins_with("@")) {
		variables.members.push_back(variable_info);
		if (variable.name == "self") {
			variables.self_object_id = variable_info.get("object_id", 0);
		}
	} else if (variable.type == 2) {
		variables.globals.push_back(variable_info);
	}
	state.received_variable_count++;
	if (state.received_variable_count >= state.pending_variable_count) {
		enrich_declared_member_types(state, state.pending_variable_frame, variables);
		variables.ready = true;
		state.pending_variable_frame = -1;
		state.pending_variable_count = -1;
		state.variable_generation++;
	}
}

Dictionary get_state(int p_session, const String &p_action) {
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	const bool active = debugger != nullptr && debugger->is_session_active();
	const bool physical_breaked = active && debugger->is_breaked();
	SessionState *cached = sessions.getptr(p_session);
	const bool breaked = physical_breaked && (!cached || cached->break_exposed);

	Dictionary response;
	response["ok"] = true;
	response["command"] = "debug";
	response["action"] = p_action;
	response["session"] = p_session;
	response["active"] = active;
	response["breaked"] = breaked;
	response["can_debug"] = breaked && debugger->is_debuggable();
	response["state"] = !active ? "not_running" : (breaked ? "breaked" : "running");
	response["breakpoint_evaluating"] = false;
	response["reason"] = cached && breaked ? cached->reason : String();
	response["stack_ready"] = cached && breaked && cached->stack_ready;
	response["frame"] = cached && breaked ? get_frame(*cached, 0) : Dictionary();
	response["selected_frame_index"] = cached && breaked ? cached->selected_frame : 0;
	response["selected_frame"] = cached && breaked ? get_frame(*cached, cached->selected_frame) : Dictionary();
	response["matched_breakpoints"] = cached && breaked ? cached->matched_breakpoints : Array();
	response["breakpoint_condition_errors"] = cached && breaked ? cached->breakpoint_condition_errors : Array();
	return response;
}

Dictionary execute_debug(int p_session, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	r_wait_kind = WAIT_NONE;
	r_generation = 0;
	const String action = p_options.get("action", String());
	if (action == "members") {
		const String filter_type = String(p_options.get("filter_type", String())).strip_edges();
		if (!WGodotTypeFilter::is_known(filter_type)) {
			return make_error("debug", action, "invalid_type_filter", "Unknown type for --filter-type: " + filter_type);
		}
	}
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
			if (state.break_exposed && state.stack_ready) {
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
			if (state.break_exposed && state.stack_ready) {
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
	if (!state.break_exposed) {
		return make_error("debug", action, "debugger_resuming", "The debugger is already resuming from a stale logical breakpoint hit.");
	}

	if (action == "stack" || is_frame_action(action)) {
		if (!state.stack_ready) {
			r_wait_kind = WAIT_CURRENT_BREAK;
			r_generation = state.break_generation;
			return Dictionary();
		}
		return prepare_stack_action(p_session, debugger, p_options, r_wait_kind, r_generation);
	}

	if (action == "continue") {
		if (!debugger->is_debuggable()) {
			return make_error("debug", action, "debugger_cannot_continue", "The current debugger break cannot be continued or stepped.");
		}
		r_wait_kind = WAIT_RESUME;
		r_generation = state.resume_generation;
		debugger->debug_continue();
		return Dictionary();
	}
	if (action == "step_into" || action == "step_over" || action == "step_out") {
		if (!debugger->is_debuggable()) {
			return make_error("debug", action, "debugger_cannot_continue", "The current debugger break cannot be continued or stepped.");
		}
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

bool poll_debug_wait(int p_session, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation, Dictionary &r_response) {
	const String action = p_options.get("action", String());
	ScriptEditorDebugger *debugger = get_debugger(p_session);
	SessionState &state = get_session_state(p_session);
	const bool active = debugger != nullptr && debugger->is_session_active();
	if (!active) {
		if (r_wait_kind == WAIT_RESUME) {
			r_response = get_state(p_session, action);
			return true;
		}
		r_response = make_error("debug", action, "game_stopped", "The game stopped before the debugger request completed.");
		return true;
	}

	if (r_wait_kind == WAIT_RESUME) {
		if (state.resume_generation > r_generation || !debugger->is_breaked()) {
			r_response = get_state(p_session, action);
			return true;
		}
		return false;
	}
	if (r_wait_kind == WAIT_VARIABLES) {
		if (!debugger->is_breaked()) {
			r_response = make_error("debug", action, "debugger_resumed", "The debugger resumed before stack variables were loaded.");
			return true;
		}
		if (state.variable_generation > r_generation) {
			const int frame = state.selected_frame;
			const FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
			if (variables == nullptr || !variables->ready) {
				r_response = make_error("debug", action, "variable_request_interrupted", "The stack-variable request was interrupted by a debugger state change.");
			} else {
				r_response = prepare_ready_frame_action(p_session, debugger, p_options, r_wait_kind, r_generation);
			}
			const bool waiting_for_members = r_wait_kind == WAIT_MEMBERS && r_response.is_empty();
			if (!waiting_for_members) {
				state.frame_variables.erase(frame);
			}
			return !waiting_for_members;
		}
		return false;
	}
	if (r_wait_kind == WAIT_MEMBERS) {
		if (!debugger->is_breaked()) {
			r_response = make_error("debug", action, "debugger_resumed", "The debugger resumed before member metadata was loaded.");
			return true;
		}
		if (state.member_generation > r_generation) {
			const int frame = state.selected_frame;
			if (!state.pending_member_error.is_empty()) {
				r_response = state.pending_member_error;
				state.pending_member_error.clear();
			} else {
				const FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
				if (variables == nullptr || !variables->ready || !variables->members_enriched) {
					r_response = make_error("debug", action, "member_request_interrupted", "The member metadata request was interrupted by a debugger state change.");
				} else {
					r_response = make_frame_response(p_session, p_options, state);
				}
			}
			state.frame_variables.erase(frame);
			return true;
		}
		return false;
	}

	const bool generation_ready = r_wait_kind == WAIT_CURRENT_BREAK ? state.break_generation >= r_generation : state.break_generation > r_generation;
	if (generation_ready && debugger->is_breaked() && state.break_exposed && state.stack_ready) {
		if (action == "stack" || is_frame_action(action)) {
			r_response = prepare_stack_action(p_session, debugger, p_options, r_wait_kind, r_generation);
			return (r_wait_kind != WAIT_VARIABLES && r_wait_kind != WAIT_MEMBERS) || !r_response.is_empty();
		}
		r_response = get_state(p_session, action);
		return true;
	}
	return false;
}

void cancel_debug_wait(int p_session, WaitKind p_wait_kind) {
	SessionState *state = sessions.getptr(p_session);
	if (state == nullptr) {
		return;
	}
	if (p_wait_kind == WAIT_VARIABLES && state->pending_variable_frame >= 0) {
		const int frame = state->pending_variable_frame;
		state->pending_variable_frame = -1;
		state->pending_variable_count = -1;
		state->received_variable_count = 0;
		state->frame_variables.erase(frame);
		state->variable_generation++;
	} else if (p_wait_kind == WAIT_MEMBERS && state->pending_member_frame >= 0) {
		const int frame = state->pending_member_frame;
		state->pending_member_frame = -1;
		state->pending_member_object_id = 0;
		state->pending_member_request_id = 0;
		state->pending_member_segments.clear();
		state->pending_member_segment = 0;
		state->pending_member_target_path.clear();
		state->pending_member_target_type.clear();
		state->pending_member_error.clear();
		state->frame_variables.erase(frame);
		state->member_generation++;
	}
}

} // namespace WGodotDebugService
