// wgodot-changes::file
#include "gdscript_analyzer.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;

bool GDScriptAnalyzer::wgodot_get_group_call_signature(Parser::CallNode *p_call, const Parser::DataType &p_base, Parser::DataType &r_return, List<Parser::DataType> &r_parameters, int &r_defaults, BitField<MethodFlags> &r_flags) {
	if (p_call->function_name != SNAME("call_group_as") || !ClassDB::is_parent_class(p_base.native_type, SNAME("SceneTree"))) {
		return false;
	}
	r_return = type_from_property(PropertyInfo(Variant::NIL, ""), false, p_call);
	// Validate the class token below. Native metatypes use GDScriptNativeClass,
	// which is not a registered ClassDB type for ordinary Object compatibility.
	r_parameters.push_back(type_from_property(PropertyInfo(Variant::NIL, "node_type"), true, p_call));
	r_parameters.push_back(type_from_property(PropertyInfo(Variant::STRING_NAME, "method"), true, p_call));
	r_defaults = 0;
	r_flags = METHOD_FLAGS_DEFAULT;
	if (p_call->arguments.size() < 2) {
		return true; // The ordinary argument checker reports the missing arguments.
	}
	const auto *class_value = p_call->arguments[0];
	const auto &meta = class_value->type_constraint;
	if (!class_value->is_constant || !meta.is_meta_type || !ClassDB::is_parent_class(meta.native_type, SNAME("Node")) ||
			(meta.kind == Parser::DataType::CLASS && meta.class_type->wgodot_is_interface) ||
			(meta.kind == Parser::DataType::SCRIPT && meta.script_type->wgodot_is_interface_type())) {
		push_error("call_group_as() requires a statically known Node-derived class, such as CanvasItem, as its first argument.", class_value);
		return true;
	}
	const auto *name_value = p_call->arguments[1];
	if (!name_value->is_constant || (name_value->reduced_value.get_type() != Variant::STRING && name_value->reduced_value.get_type() != Variant::STRING_NAME)) {
		push_error("call_group_as() requires a constant method name so its signature can be checked statically.", name_value);
		return true;
	}
	Parser::DataType target = type_from_metatype(meta);
	const StringName name = name_value->reduced_value;
	Parser::DataType result;
	List<Parser::DataType> parameters;
	BitField<MethodFlags> flags;
	if (!get_function_signature(p_call, false, target, name, result, parameters, r_defaults, flags)) {
		push_error(vformat("call_group_as(): method '%s' does not exist on '%s'.", name, target.to_string()), name_value);
		return true;
	}
	if (flags.has_flag(METHOD_FLAG_STATIC)) {
		push_error("call_group_as() requires an instance method.", name_value);
	}
	if (flags.has_flag(METHOD_FLAG_VARARG)) {
		push_error("call_group_as() requires a fixed method signature; use a typed wrapper for a variadic method.", name_value);
	}
	for (const auto &parameter : parameters) {
		r_parameters.push_back(parameter);
	}
	// Do not allow this entry point to bypass the restrictions on Object.call.
	Parser::CallNode target_call;
	target_call.function_name = name;
	target_call.start_line = p_call->start_line;
	target_call.start_column = p_call->start_column;
	wgodot_validate_strict_object_call(target, &target_call);
	return true;
}

void GDScriptAnalyzer::wgodot_validate_group_call_arguments(const Parser::CallNode *p_call, const List<Parser::DataType> &p_parameters) {
	if (!wgodot_strict_type_checking_enabled()) {
		return;
	}
	for (uint32_t i = 2; i < p_call->arguments.size() && i < uint32_t(p_parameters.size()); i++) {
		const auto *argument = p_call->arguments[i];
		const auto &target = p_parameters.get(i);
		if (!argument->type_constraint.is_hard_type() ||
				(wgodot_datatype_contains_variant(argument->type_constraint) && !wgodot_datatype_contains_variant(target)) ||
				!is_type_compatible(target, argument->type_constraint, true)) {
			push_error(vformat("Strict type checking: call_group_as() argument %d must be assignable to '%s'.", i + 1, target.to_string()), argument);
		}
	}
}
