// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_service.cpp                                              */
/**************************************************************************/

#include "wgodot_debug_service.h"

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

namespace WGodotDebugService {

namespace {

struct BreakpointRecord {
	int id = 0;
	String path;
	int line = 0;
	bool enabled = true;
};

struct FrameVariables {
	Array locals;
	Array members;
	Array user_members;
	Array globals;
	Array all_members;
	int64_t self_object_id = 0;
	bool ready = false;
	bool members_enriched = false;
};

struct SessionState {
	bool active = false;
	bool breaked = false;
	bool can_debug = false;
	bool has_stackdump = false;
	bool stack_ready = false;
	String reason;
	Array frames;
	HashMap<int, FrameVariables> frame_variables;
	int selected_frame = 0;
	int pending_variable_frame = -1;
	int pending_variable_count = -1;
	int received_variable_count = 0;
	int pending_member_frame = -1;
	int64_t pending_member_object_id = 0;
	uint64_t break_generation = 0;
	uint64_t resume_generation = 0;
	uint64_t variable_generation = 0;
	uint64_t member_generation = 0;
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

void clear_debug_cache(SessionState &p_state) {
	p_state.frames.clear();
	p_state.frame_variables.clear();
	p_state.selected_frame = 0;
	p_state.pending_variable_frame = -1;
	p_state.pending_variable_count = -1;
	p_state.received_variable_count = 0;
	p_state.pending_member_frame = -1;
	p_state.pending_member_object_id = 0;
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
	if (action == "locals" || action == "vars") {
		response["locals"] = variables ? variables->locals : Array();
	}
	if (action == "members" || action == "vars") {
		Array members;
		if (variables) {
			members = action == "members" && (bool)p_options.get("all", false) ? variables->all_members : variables->members;
			if ((bool)p_options.get("exclude_builtin", false)) {
				Array filtered;
				for (const Variant &member_variant : members) {
					const Dictionary member = member_variant;
					if (!(bool)member.get("builtin", false)) {
						filtered.push_back(member);
					}
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

void enrich_frame_members(FrameVariables &p_variables, const Array &p_properties) {
	HashMap<String, Dictionary> script_properties;
	Array native_members;
	for (const Variant &property_variant : p_properties) {
		if (property_variant.get_type() != Variant::ARRAY) {
			continue;
		}
		const Array property = property_variant;
		if (property.size() != 6) {
			continue;
		}
		String name = property[0];
		const Variant::Type declared_type = static_cast<Variant::Type>((int)property[1]);
		const PropertyHint hint = static_cast<PropertyHint>((int)property[2]);
		const String hint_string = property[3];
		const PropertyUsageFlags usage = static_cast<PropertyUsageFlags>((int)property[4]);
		const Variant value = property[5];
		if (usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY) || name.begins_with("Constants/")) {
			continue;
		}

		bool builtin = true;
		if (name.begins_with("Members/")) {
			name = name.get_slicec('/', name.get_slice_count("/") - 1);
			builtin = false;
		} else if (usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
			builtin = false;
		} else if (name.contains("/")) {
			continue;
		}

		Dictionary member;
		member["name"] = name;
		member["type"] = format_property_type(declared_type, hint, hint_string);
		member["variant_type"] = Variant::get_type_name(value.get_type());
		member["value"] = format_variable_value(value, declared_type, hint_string);
		member["builtin"] = builtin;
		const int64_t object_id = get_encoded_object_id(value);
		if (object_id != 0) {
			member["object_id"] = object_id;
		}
		if (builtin) {
			native_members.push_back(member);
		} else {
			script_properties[name] = member;
		}
	}

	auto enrich_script_members = [&script_properties](Array &r_members) {
		for (int i = 0; i < r_members.size(); i++) {
			Dictionary member = r_members[i];
			const String name = member.get("name", String());
			const Dictionary *declared = script_properties.getptr(name);
			const String current_type = member.get("type", "Variant");
			if (declared != nullptr && (current_type.is_empty() || current_type == "Variant" || current_type == "Nil")) {
				member["type"] = declared->get("type", "Variant");
			}
			member["builtin"] = false;
			r_members[i] = member;
		}
	};
	enrich_script_members(p_variables.members);
	enrich_script_members(p_variables.user_members);

	HashSet<String> names;
	for (const Variant &member_variant : p_variables.user_members) {
		const Dictionary member = member_variant;
		names.insert(member.get("name", String()));
	}
	p_variables.all_members = p_variables.user_members.duplicate(true);
	for (const Variant &member_variant : native_members) {
		const Dictionary member = member_variant;
		const String name = member.get("name", String());
		if (!names.has(name)) {
			p_variables.all_members.push_back(member);
			names.insert(name);
		}
	}
	p_variables.members_enriched = true;
}

Dictionary prepare_member_metadata(int p_session, ScriptEditorDebugger *p_debugger, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation) {
	SessionState &state = get_session_state(p_session);
	const String action = p_options.get("action", String());
	FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
	if (variables == nullptr || !variables->ready) {
		return make_error("debug", action, "variables_unavailable", "Variables for the selected stack frame are not loaded.");
	}
	if (variables->members_enriched) {
		return make_frame_response(p_session, p_options, state);
	}
	if (variables->self_object_id == 0) {
		variables->all_members = variables->user_members.duplicate(true);
		variables->members_enriched = true;
		return make_frame_response(p_session, p_options, state);
	}
	if (state.pending_member_frame >= 0 && state.pending_member_frame != state.selected_frame) {
		return make_error("debug", action, "member_request_busy", vformat("Stack frame %d member metadata is still being loaded.", state.pending_member_frame));
	}
	if (state.pending_member_frame < 0) {
		state.pending_member_frame = state.selected_frame;
		state.pending_member_object_id = variables->self_object_id;
		TypedArray<uint64_t> object_ids;
		object_ids.append(static_cast<uint64_t>(variables->self_object_id));
		p_debugger->request_remote_objects(object_ids, false);
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

	if (state.pending_variable_frame >= 0 && state.pending_variable_frame != frame) {
		return make_error("debug", action, "variable_request_busy", vformat("Stack frame %d variables are still being loaded.", state.pending_variable_frame));
	}
	if (state.pending_member_frame >= 0 && state.pending_member_frame != frame) {
		return make_error("debug", action, "member_request_busy", vformat("Stack frame %d member metadata is still being loaded.", state.pending_member_frame));
	}
	state.selected_frame = frame;
	FrameVariables *cached = state.frame_variables.getptr(frame);
	if (cached != nullptr && cached->ready) {
		return prepare_ready_frame_action(p_session, p_debugger, p_options, r_wait_kind, r_generation);
	}
	if (state.pending_variable_frame < 0) {
		FrameVariables &variables = state.frame_variables[frame];
		variables.locals.clear();
		variables.members.clear();
		variables.user_members.clear();
		variables.globals.clear();
		variables.all_members.clear();
		variables.self_object_id = 0;
		variables.ready = false;
		variables.members_enriched = false;
		state.pending_variable_frame = frame;
		state.pending_variable_count = -1;
		state.received_variable_count = 0;
		if (!p_debugger->request_stack_dump(frame)) {
			state.pending_variable_frame = -1;
			return make_error("debug", action, "variable_request_failed", "Could not request variables for the selected stack frame.");
		}
	}

	r_wait_kind = WAIT_VARIABLES;
	r_generation = state.variable_generation;
	return Dictionary();
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
	clear_debug_cache(state);
}

void debugger_stopped(int p_session) {
	SessionState &state = get_session_state(p_session);
	state.active = false;
	state.breaked = false;
	state.can_debug = false;
	state.stack_ready = false;
	state.reason.clear();
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
	clear_debug_cache(state);
	if (p_breaked) {
		state.break_generation++;
		state.stack_ready = !p_has_stackdump;
	} else {
		state.resume_generation++;
		state.stack_ready = false;
	}
}

void capture_debugger_message(int p_session, const String &p_message, const Array &p_data) {
	SessionState &state = get_session_state(p_session);
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
	if (p_message == "scene:inspect_objects" && state.pending_member_frame >= 0) {
		for (const Variant &object_variant : p_data) {
			if (object_variant.get_type() != Variant::ARRAY) {
				continue;
			}
			const Array object = object_variant;
			if (object.size() < 3 || (int64_t)object[0] != state.pending_member_object_id || object[2].get_type() != Variant::ARRAY) {
				continue;
			}
			FrameVariables *variables = state.frame_variables.getptr(state.pending_member_frame);
			if (variables != nullptr && variables->ready) {
				enrich_frame_members(*variables, object[2]);
			}
			state.pending_member_frame = -1;
			state.pending_member_object_id = 0;
			state.member_generation++;
			return;
		}
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
	response["stack_ready"] = cached && breaked && cached->stack_ready;
	response["frame"] = cached && breaked ? get_frame(*cached, 0) : Dictionary();
	response["selected_frame_index"] = cached && breaked ? cached->selected_frame : 0;
	response["selected_frame"] = cached && breaked ? get_frame(*cached, cached->selected_frame) : Dictionary();
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
			const FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
			if (variables == nullptr || !variables->ready) {
				r_response = make_error("debug", action, "variable_request_interrupted", "The stack-variable request was interrupted by a debugger state change.");
			} else {
				r_response = prepare_ready_frame_action(p_session, debugger, p_options, r_wait_kind, r_generation);
			}
			return r_wait_kind != WAIT_MEMBERS || !r_response.is_empty();
		}
		return false;
	}
	if (r_wait_kind == WAIT_MEMBERS) {
		if (!debugger->is_breaked()) {
			r_response = make_error("debug", action, "debugger_resumed", "The debugger resumed before member metadata was loaded.");
			return true;
		}
		if (state.member_generation > r_generation) {
			const FrameVariables *variables = state.frame_variables.getptr(state.selected_frame);
			if (variables == nullptr || !variables->ready || !variables->members_enriched) {
				r_response = make_error("debug", action, "member_request_interrupted", "The member metadata request was interrupted by a debugger state change.");
			} else {
				r_response = make_frame_response(p_session, p_options, state);
			}
			return true;
		}
		return false;
	}

	const bool generation_ready = r_wait_kind == WAIT_CURRENT_BREAK ? state.break_generation >= r_generation : state.break_generation > r_generation;
	if (generation_ready && debugger->is_breaked() && state.stack_ready) {
		if (action == "stack" || is_frame_action(action)) {
			r_response = prepare_stack_action(p_session, debugger, p_options, r_wait_kind, r_generation);
			return (r_wait_kind != WAIT_VARIABLES && r_wait_kind != WAIT_MEMBERS) || !r_response.is_empty();
		}
		r_response = get_state(p_session, action);
		return true;
	}
	return false;
}

} // namespace WGodotDebugService
