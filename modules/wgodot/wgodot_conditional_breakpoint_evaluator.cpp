// wgodot-changes::file
/**************************************************************************/
/*  wgodot_conditional_breakpoint_evaluator.cpp                           */
/**************************************************************************/

#include "wgodot_conditional_breakpoint_evaluator.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/math/expression.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_function.h"

namespace WGodotConditionalBreakpointEvaluator {

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

HashMap<String, Vector<BreakpointRecord>> breakpoints_by_location;
HashSet<String> managed_locations;
Mutex breakpoint_mutex;
int64_t sync_generation = 0;
thread_local bool suppress_break_presentation = false;
thread_local Dictionary pending_breakpoint_hit;

String location_key(const String &p_path, int p_line) {
	return p_path.replace_char('\\', '/').simplify_path() + ":" + itos(p_line);
}

Dictionary breakpoint_info(const BreakpointRecord &p_breakpoint) {
	Dictionary info;
	info["id"] = p_breakpoint.id;
	info["path"] = p_breakpoint.path;
	info["line"] = p_breakpoint.line;
	info["name"] = p_breakpoint.name;
	info["condition"] = p_breakpoint.condition;
	info["enabled"] = p_breakpoint.enabled;
	info["one_shot"] = p_breakpoint.one_shot;
	info["one_shot_removed"] = false;
	return info;
}

bool evaluate_condition(GDScriptFunction *p_function, GDScriptInstance *p_instance, Variant *p_stack, int p_line, const String &p_condition, bool &r_matched, String &r_error) {
	r_matched = false;
	if (p_function == nullptr || p_stack == nullptr) {
		r_error = "The live GDScript stack frame is not available.";
		return false;
	}

	PackedStringArray input_names;
	Array input_values;
	List<Pair<StringName, int>> locals;
	p_function->debug_get_stack_member_state(p_line, &locals);
	for (const Pair<StringName, int> &local : locals) {
		if (local.second < 0 || local.second >= p_function->get_max_stack_size()) {
			continue;
		}
		input_names.push_back(local.first);
		input_values.push_back(p_stack[local.second]);
	}

	List<String> globals;
	List<Variant> global_values;
	GDScriptLanguage::get_singleton()->debug_get_globals(&globals, &global_values);
	if (globals.size() != global_values.size()) {
		r_error = "The debugger returned mismatched global variable names and values.";
		return false;
	}
	for (const String &global : globals) {
		input_names.push_back(global);
	}
	for (const Variant &value : global_values) {
		input_values.push_back(value);
	}

	LocalVector<StringName> native_types;
	ClassDB::get_class_list(native_types);
	for (const StringName &class_name : native_types) {
		if (!ClassDB::is_class_exposed(class_name) || !Engine::get_singleton()->has_singleton(class_name) || Engine::get_singleton()->is_singleton_editor_only(class_name)) {
			continue;
		}
		input_names.push_back(class_name);
		input_values.push_back(Engine::get_singleton()->get_singleton_object(class_name));
	}

	LocalVector<StringName> user_types;
	ScriptServer::get_global_class_list(user_types);
	for (const StringName &class_name : user_types) {
		const Ref<Script> script = ResourceLoader::load(ScriptServer::get_global_class_path(class_name), "Script");
		if (script.is_null()) {
			continue;
		}
		input_names.push_back(class_name);
		input_values.push_back(script);
	}

	Expression expression;
	if (expression.parse(p_condition, input_names) != OK) {
		r_error = "Parse error: " + expression.get_error_text();
		return false;
	}
	Object *base = p_instance ? p_instance->get_owner() : nullptr;
	const Variant value = expression.execute(input_values, base, false);
	if (expression.has_execute_failed()) {
		r_error = "Evaluation error: " + expression.get_error_text();
		return false;
	}
	if (value.get_type() != Variant::BOOL) {
		r_error = "Breakpoint conditions must return bool, but this condition returned " + Variant::get_type_name(value.get_type()) + ".";
		return false;
	}
	r_matched = (bool)value;
	return true;
}

} // namespace

Error sync_breakpoints(const Array &p_arguments) {
	if (p_arguments.size() != 3 || p_arguments[0].get_type() != Variant::INT || p_arguments[1].get_type() != Variant::ARRAY || p_arguments[2].get_type() != Variant::PACKED_STRING_ARRAY) {
		return ERR_INVALID_DATA;
	}
	const int64_t generation = p_arguments[0];
	MutexLock lock(breakpoint_mutex);
	if (generation < sync_generation) {
		return OK;
	}
	sync_generation = generation;
	breakpoints_by_location.clear();
	managed_locations.clear();
	const PackedStringArray locations = p_arguments[2];
	for (const String &location : locations) {
		managed_locations.insert(location);
	}

	const Array records = p_arguments[1];
	for (const Variant &record_variant : records) {
		if (record_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary record_data = record_variant;
		BreakpointRecord record;
		record.id = record_data.get("id", 0);
		record.path = record_data.get("path", String());
		record.line = record_data.get("line", 0);
		record.name = record_data.get("name", String());
		record.condition = record_data.get("condition", String());
		record.enabled = record_data.get("enabled", true);
		record.one_shot = record_data.get("one_shot", false);
		if (record.id <= 0 || record.path.is_empty() || record.line <= 0) {
			continue;
		}
		const String key = location_key(record.path, record.line);
		managed_locations.insert(key);
		breakpoints_by_location[key].push_back(record);
	}
	return OK;
}

BreakDecision evaluate_breakpoint(const String &p_path, int p_line, GDScriptFunction *p_function, GDScriptInstance *p_instance, Variant *p_stack) {
	const String key = location_key(p_path, p_line);
	Vector<BreakpointRecord> records;
	int64_t evaluation_generation = 0;
	{
		MutexLock lock(breakpoint_mutex);
		if (!managed_locations.has(key)) {
			return BREAK_NOT_MANAGED;
		}
		const Vector<BreakpointRecord> *stored_records = breakpoints_by_location.getptr(key);
		if (stored_records == nullptr) {
			return BREAK_SKIP;
		}
		records = *stored_records;
		evaluation_generation = sync_generation;
	}
	Array matches;
	Array condition_errors;
	for (const BreakpointRecord &record : records) {
		if (!record.enabled) {
			continue;
		}
		if (record.condition.is_empty()) {
			matches.push_back(breakpoint_info(record));
			continue;
		}
		bool matched = false;
		String error;
		if (!evaluate_condition(p_function, p_instance, p_stack, p_line, record.condition, matched, error)) {
			Dictionary condition_error = breakpoint_info(record);
			condition_error["error"] = error;
			condition_errors.push_back(condition_error);
		} else if (matched) {
			matches.push_back(breakpoint_info(record));
		}
	}

	if (matches.is_empty() && condition_errors.is_empty()) {
		return BREAK_SKIP;
	}
	bool matched_condition = false;
	bool matched_name = false;
	for (const Variant &match_variant : matches) {
		const Dictionary match = match_variant;
		matched_condition = matched_condition || !String(match.get("condition", String())).is_empty();
		matched_name = matched_name || !String(match.get("name", String())).is_empty();
	}
	Dictionary hit;
	hit["reason"] = !condition_errors.is_empty() ? "Conditional breakpoint condition error" : (matched_condition ? "Conditional breakpoint" : (matched_name ? "Named breakpoint" : "Breakpoint"));
	hit["matched_breakpoints"] = matches;
	hit["breakpoint_condition_errors"] = condition_errors;
	hit["path"] = p_path;
	hit["line"] = p_line;
	hit["sync_generation"] = evaluation_generation;
	pending_breakpoint_hit = hit;
	suppress_break_presentation = true;
	return BREAK_STOP;
}

Dictionary consume_breakpoint_hit() {
	const Dictionary hit = pending_breakpoint_hit;
	pending_breakpoint_hit.clear();
	return hit;
}

bool consume_break_presentation_suppressed() {
	const bool suppressed = suppress_break_presentation;
	suppress_break_presentation = false;
	return suppressed;
}

void reset() {
	MutexLock lock(breakpoint_mutex);
	breakpoints_by_location.clear();
	managed_locations.clear();
	sync_generation = 0;
	suppress_break_presentation = false;
	pending_breakpoint_hit.clear();
}

} // namespace WGodotConditionalBreakpointEvaluator
