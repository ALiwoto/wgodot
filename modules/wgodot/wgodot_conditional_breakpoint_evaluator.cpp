// wgodot-changes::file
/**************************************************************************/
/*  wgodot_conditional_breakpoint_evaluator.cpp                           */
/**************************************************************************/

#include "wgodot_conditional_breakpoint_evaluator.h"

#include "core/config/engine.h"
#include "core/debugger/engine_debugger.h"
#include "core/debugger/script_debugger.h"
#include "core/io/resource_loader.h"
#include "core/math/expression.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"

namespace WGodotConditionalBreakpointEvaluator {

namespace {

void send_result(int64_t p_request_id, bool p_ok, bool p_matched, const String &p_error) {
	EngineDebugger::get_singleton()->send_message("wgodot:conditional_breakpoint_result", { p_request_id, p_ok, p_matched, p_error });
}

} // namespace

Error evaluate(const Array &p_arguments) {
	if (p_arguments.size() != 3 || p_arguments[0].get_type() != Variant::INT || p_arguments[1].get_type() != Variant::STRING || p_arguments[2].get_type() != Variant::INT) {
		return ERR_INVALID_DATA;
	}

	const int64_t request_id = p_arguments[0];
	const String condition = p_arguments[1];
	const int frame = p_arguments[2];
	if (condition.is_empty() || frame < 0) {
		send_result(request_id, false, false, "The breakpoint condition and stack frame must be valid.");
		return OK;
	}

	ScriptDebugger *script_debugger = EngineDebugger::get_script_debugger();
	ScriptLanguage *script_language = script_debugger ? script_debugger->get_break_language() : nullptr;
	if (script_language == nullptr || frame >= script_language->debug_get_stack_level_count()) {
		send_result(request_id, false, false, "The paused stack frame is no longer available.");
		return OK;
	}

	PackedStringArray input_names;
	Array input_values;
	List<String> locals;
	List<Variant> local_values;
	script_language->debug_get_stack_level_locals(frame, &locals, &local_values);
	if (locals.size() != local_values.size()) {
		send_result(request_id, false, false, "The debugger returned mismatched local variable names and values.");
		return OK;
	}
	for (const String &local : locals) {
		input_names.push_back(local);
	}
	for (const Variant &value : local_values) {
		input_values.push_back(value);
	}

	List<String> globals;
	List<Variant> global_values;
	script_language->debug_get_globals(&globals, &global_values);
	if (globals.size() != global_values.size()) {
		send_result(request_id, false, false, "The debugger returned mismatched global variable names and values.");
		return OK;
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
	if (expression.parse(condition, input_names) != OK) {
		send_result(request_id, false, false, "Parse error: " + expression.get_error_text());
		return OK;
	}

	ScriptInstance *instance = script_language->debug_get_stack_level_instance(frame);
	Object *base = instance ? instance->get_owner() : nullptr;
	const Variant value = expression.execute(input_values, base, false);
	if (expression.has_execute_failed()) {
		send_result(request_id, false, false, "Evaluation error: " + expression.get_error_text());
		return OK;
	}
	if (value.get_type() != Variant::BOOL) {
		send_result(request_id, false, false, "Breakpoint conditions must return bool, but this condition returned " + Variant::get_type_name(value.get_type()) + ".");
		return OK;
	}

	send_result(request_id, true, (bool)value, String());
	return OK;
}

} // namespace WGodotConditionalBreakpointEvaluator
