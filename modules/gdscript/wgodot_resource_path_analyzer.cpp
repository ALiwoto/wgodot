// wgodot-changes::file
#include "gdscript_analyzer.h"

using Parser = GDScriptParser;

Parser::DataType GDScriptAnalyzer::wgodot_resource_path_runtime_type(const Parser::DataType &p_type) {
	// Ignored scripts run in the VM, where WResPath is an ordinary String.
	// Do not mutate types belonging to production-script dependencies.
	Parser::DataType result = p_type;
	result.wgodot_resource_path = false;
	for (int i = 0; i < result.get_container_element_type_count(); i++) {
		result.set_container_element_type(i, wgodot_resource_path_runtime_type(result.get_container_element_type(i)));
	}
	return result;
}

void GDScriptAnalyzer::wgodot_resource_container_signature(const Parser::DataType &p_container, const StringName &p_method, Parser::DataType &r_result, List<Parser::DataType> &r_arguments) {
	const auto key = p_container.get_container_element_type_or_variant(0);
	const auto value = p_container.get_container_element_type_or_variant(1);
	if (!key.wgodot_resource_path && !value.wgodot_resource_path) {
		return;
	}
	const String method = p_method;
	auto argument = [&](int p_index, const Parser::DataType &p_type) {
		if (p_index < r_arguments.size()) {
			r_arguments.get(p_index) = p_type;
		}
	};
	auto array_of = [](const Parser::DataType &p_element) {
		Parser::DataType array;
		array.kind = Parser::DataType::BUILTIN;
		array.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
		array.builtin_type = Variant::ARRAY;
		array.set_container_element_type(0, p_element);
		return array;
	};
	if (p_container.builtin_type == Variant::DICTIONARY) {
		if (method == "has" || method == "erase" || method == "get" || method == "get_or_add" || method == "set") {
			argument(0, key);
		}
		if (method == "get" || method == "get_or_add" || method == "set") {
			argument(1, value);
			if (method != "set") {
				r_result = value;
			}
		}
		if (method == "find_key") {
			argument(0, value);
			r_result = key;
		}
		if (method == "keys" || method == "values") {
			r_result = array_of(method == "keys" ? key : value);
		}
		if (method == "has_all") {
			argument(0, array_of(key));
		}
		if (method == "assign" || method == "merge" || method == "merged") {
			argument(0, p_container);
		}
		if (method == "duplicate" || method == "merged") {
			r_result = p_container;
		}
	} else if (p_container.builtin_type == Variant::ARRAY) {
		if (method == "append" || method == "push_back" || method == "push_front" || method == "erase" || method == "fill" || method == "has" || method == "find" || method == "rfind" || method == "count" || method == "bsearch") {
			argument(0, key);
		}
		if (method == "set" || method == "insert") {
			argument(1, key);
		}
		if (method == "get" || method == "front" || method == "back" || method == "pick_random" || method == "pop_back" || method == "pop_front" || method == "pop_at" || method == "min" || method == "max") {
			r_result = key;
		}
		if (method == "append_array" || method == "assign") {
			argument(0, p_container);
		}
		if (method == "duplicate" || method == "slice" || method == "filter") {
			r_result = p_container;
		}
	}
}

Parser::DataType GDScriptAnalyzer::wgodot_resource_path_type() {
	Parser::DataType result;
	result.kind = Parser::DataType::BUILTIN;
	result.builtin_type = Variant::STRING;
	result.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
	result.wgodot_resource_path = true;
	return result;
}

bool GDScriptAnalyzer::wgodot_validate_resource_path_argument(Parser::ExpressionNode *p_expression, const Parser::DataType &p_target) {
	if (wgodot_is_ignored_script() || !p_target.wgodot_resource_path) {
		return true;
	}
	if (p_expression->type_constraint.wgodot_resource_path) {
		return true;
	}
	if (p_expression->is_constant && p_expression->reduced_value.get_type() == Variant::STRING) {
		p_expression->type_constraint = wgodot_resource_path_type();
		p_expression->type_constraint.is_constant = true;
		return true;
	}
	push_error("WResPath requires a constant resource path or another WResPath; runtime strings are not resource identities.", p_expression);
	return false;
}

bool GDScriptAnalyzer::wgodot_validate_resource_utility(const Parser::CallNode *p_call, const MethodInfo &p_info) {
	if (wgodot_is_ignored_script()) {
		return true;
	}
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const Parser::ExpressionNode *argument = p_call->arguments[i];
		if (argument->type_constraint.wgodot_resource_path &&
				(i >= uint32_t(p_info.arguments.size()) || p_info.arguments.get(i).class_name != SNAME("WResPath"))) {
			push_error("WResPath cannot be passed to a generic utility function; it is an opaque resource identity.", argument);
			return false;
		}
	}
	return true;
}

bool GDScriptAnalyzer::wgodot_validate_resource_path_operation(Variant::Operator p_operator, const Parser::DataType &p_left, const Parser::DataType &p_right, const Parser::Node *p_source) {
	if (wgodot_is_ignored_script() || (!p_left.wgodot_resource_path && !p_right.wgodot_resource_path)) {
		return true;
	}
	if (p_operator == Variant::OP_NOT || p_operator == Variant::OP_AND || p_operator == Variant::OP_OR) {
		return true;
	}
	if ((p_operator == Variant::OP_EQUAL || p_operator == Variant::OP_NOT_EQUAL) && p_left.wgodot_resource_path && p_right.wgodot_resource_path) {
		return true;
	}
	if (p_operator == Variant::OP_IN && p_left.wgodot_resource_path &&
			(p_right.builtin_type == Variant::ARRAY || p_right.builtin_type == Variant::DICTIONARY) &&
			p_right.has_container_element_type(0) && p_right.get_container_element_type(0).wgodot_resource_path) {
		return true;
	}
	push_error("WResPath supports identity equality, container membership, and truth checks; string operations are forbidden.", p_source);
	return false;
}
