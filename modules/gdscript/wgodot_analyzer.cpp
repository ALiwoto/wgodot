// wgodot-changes::file
/**************************************************************************/
/*  wgodot_analyzer.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "gdscript_analyzer.h"

#include "gdscript.h"
#include "wgodot_gd/interface_method_aliases.h"
#include "wgodot_stdlib.h"

#include "core/config/project_settings.h"

void GDScriptAnalyzer::wgodot_validate_readonly_variable(GDScriptParser::VariableNode *p_variable, bool p_is_local) {
	ERR_FAIL_NULL(p_variable);

	if (!p_variable->wgodot_readonly) {
		return;
	}

	if (p_is_local && p_variable->initializer == nullptr) {
		push_error(R"("@readonly" local variables must be assigned in-place.)", p_variable);
	}
}

bool GDScriptAnalyzer::wgodot_validate_readonly_assignment(GDScriptParser::AssignmentNode *p_assignment) {
	ERR_FAIL_NULL_V(p_assignment, true);

	const GDScriptParser::VariableNode *readonly_source = wgodot_get_readonly_assignment_source(p_assignment->assignee);
	if (readonly_source == nullptr || wgodot_can_assign_readonly_variable(readonly_source)) {
		return true;
	}

	push_error(vformat(R"(Cannot assign a new value to @readonly variable "%s".)", readonly_source->identifier->name), p_assignment->assignee);
	return false;
}

const GDScriptParser::VariableNode *GDScriptAnalyzer::wgodot_get_readonly_assignment_source(GDScriptParser::ExpressionNode *p_assignee) const {
	ERR_FAIL_NULL_V(p_assignee, nullptr);

	GDScriptParser::IdentifierNode *identifier = nullptr;
	if (p_assignee->type == GDScriptParser::Node::IDENTIFIER) {
		identifier = static_cast<GDScriptParser::IdentifierNode *>(p_assignee);
	} else if (p_assignee->type == GDScriptParser::Node::SUBSCRIPT) {
		GDScriptParser::SubscriptNode *subscript = static_cast<GDScriptParser::SubscriptNode *>(p_assignee);
		if (subscript->is_attribute) {
			identifier = subscript->attribute;
		}
	}

	if (identifier == nullptr) {
		return nullptr;
	}

	switch (identifier->source) {
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
		case GDScriptParser::IdentifierNode::MEMBER_VARIABLE:
		case GDScriptParser::IdentifierNode::STATIC_VARIABLE:
			if (identifier->variable_source != nullptr && identifier->variable_source->wgodot_readonly) {
				return identifier->variable_source;
			}
			break;
		default:
			break;
	}

	return nullptr;
}

bool GDScriptAnalyzer::wgodot_can_assign_readonly_variable(const GDScriptParser::VariableNode *p_variable) const {
	ERR_FAIL_NULL_V(p_variable, true);

	if (p_variable->is_static || parser->current_function == nullptr || parser->current_function->identifier == nullptr) {
		return false;
	}
	if (parser->current_function->identifier->name != SNAME("_init")) {
		return false;
	}
	if (parser->current_class == nullptr || p_variable->identifier == nullptr || !parser->current_class->has_member(p_variable->identifier->name)) {
		return false;
	}

	const GDScriptParser::ClassNode::Member member = parser->current_class->get_member(p_variable->identifier->name);
	return member.type == GDScriptParser::ClassNode::Member::VARIABLE && member.variable == p_variable;
}

void GDScriptAnalyzer::wgodot_validate_private_member_access(const GDScriptParser::ClassNode::Member &p_member, const GDScriptParser::ClassNode *p_owner_class, const GDScriptParser::Node *p_source) {
	if (!wgodot_is_private_member(p_member)) {
		return;
	}
	if (p_owner_class == nullptr || parser->current_class == nullptr || p_owner_class == parser->current_class) {
		return;
	}

	push_error(vformat(R"(Cannot access private %s "%s".)", p_member.get_type_name(), p_member.get_name()), p_source);
}

bool GDScriptAnalyzer::wgodot_is_private_member(const GDScriptParser::ClassNode::Member &p_member) const {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return p_member.m_class->wgodot_private;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return p_member.constant->wgodot_private;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return p_member.function->wgodot_private;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return p_member.signal->wgodot_private;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable->wgodot_private;
		default:
			return false;
	}
}

void GDScriptAnalyzer::wgodot_validate_protected_member_access(const GDScriptParser::ClassNode::Member &p_member, const GDScriptParser::ClassNode *p_owner_class, const GDScriptParser::Node *p_source) {
	if (!wgodot_is_protected_member(p_member)) {
		return;
	}
	if (p_owner_class == nullptr || parser->current_class == nullptr || wgodot_class_inherits_from(parser->current_class, p_owner_class)) {
		return;
	}

	push_error(vformat(R"(Cannot access protected %s "%s".)", p_member.get_type_name(), p_member.get_name()), p_source);
}

bool GDScriptAnalyzer::wgodot_is_protected_member(const GDScriptParser::ClassNode::Member &p_member) const {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return p_member.m_class->wgodot_protected;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return p_member.constant->wgodot_protected;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return p_member.function->wgodot_protected;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return p_member.signal->wgodot_protected;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable->wgodot_protected;
		default:
			return false;
	}
}

bool GDScriptAnalyzer::wgodot_class_inherits_from(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_base_class) const {
	for (const GDScriptParser::ClassNode *current = p_class; current != nullptr; current = current->base_type.class_type) {
		if (current == p_base_class) {
			return true;
		}
	}

	return false;
}

void GDScriptAnalyzer::wgodot_validate_override_annotation(GDScriptParser::FunctionNode *p_function, bool p_overrides_parent) {
	ERR_FAIL_NULL(p_function);

	if (p_function->is_static || p_function->identifier == nullptr) {
		return;
	}
	if (p_function->identifier->name == GDScriptLanguage::get_singleton()->strings._init ||
			p_function->identifier->name == GDScriptLanguage::get_singleton()->strings._static_init) {
		return;
	}

	if (p_function->wgodot_override && !p_overrides_parent) {
		push_error(vformat(R"*(Function "%s()" is marked "@override", but no parent function with that name exists.)*", p_function->identifier->name), p_function);
		return;
	}
	if (!p_function->wgodot_override && p_overrides_parent && wgodot_strict_override_checking_enabled()) {
		push_error(vformat(R"*(Function "%s()" overrides a parent function and must be marked with "@override".)*", p_function->identifier->name), p_function);
	}
}

bool GDScriptAnalyzer::wgodot_strict_override_checking_enabled() const {
	const char *setting = "wgodot/gdscript/strict_override_checking";
	if (!ProjectSettings::get_singleton()->has_setting(setting)) {
		return true;
	}

	return GLOBAL_GET_CACHED(bool, setting);
}

bool GDScriptAnalyzer::wgodot_strict_type_checking_enabled() const {
#ifndef TOOLS_ENABLED
	return false;
#else
	if (GLOBAL_GET_CACHED(bool, "wgodot/gdscript/disable_strict_type_checking_for_addons") &&
			parser->script_path.begins_with("res://addons/")) {
		return false;
	}

	const char *setting = "wgodot/gdscript/strict_type_checking";
	if (!ProjectSettings::get_singleton()->has_setting(setting)) {
		return true;
	}

	return GLOBAL_GET_CACHED(bool, setting);
#endif
}

bool GDScriptAnalyzer::wgodot_datatype_contains_variant(const GDScriptParser::DataType &p_datatype) const {
	if (!p_datatype.is_set() || p_datatype.has_no_type() || p_datatype.is_resolving()) {
		return true;
	}

	if (p_datatype.kind == GDScriptParser::DataType::VARIANT || p_datatype.kind == GDScriptParser::DataType::UNRESOLVED) {
		return true;
	}

	if (p_datatype.kind == GDScriptParser::DataType::BUILTIN) {
		if (p_datatype.builtin_type == Variant::ARRAY && !p_datatype.has_container_element_type(0)) {
			return true;
		}
		if (p_datatype.builtin_type == Variant::DICTIONARY && (!p_datatype.has_container_element_type(0) || !p_datatype.has_container_element_type(1))) {
			return true;
		}
	}

	for (int i = 0; i < p_datatype.get_container_element_type_count(); i++) {
		if (wgodot_datatype_contains_variant(p_datatype.get_container_element_type(i))) {
			return true;
		}
	}

	return false;
}

bool GDScriptAnalyzer::wgodot_validate_strict_datatype(const GDScriptParser::DataType &p_datatype, const GDScriptParser::Node *p_source, const String &p_context) {
	if (!wgodot_strict_type_checking_enabled() || !wgodot_datatype_contains_variant(p_datatype)) {
		return true;
	}

	push_error(vformat("Strict type checking requires %s to have a fully known non-Variant type.", p_context), p_source);
	return false;
}

void GDScriptAnalyzer::wgodot_validate_strict_dynamic_call(const GDScriptParser::DataType &p_base_type, const GDScriptParser::CallNode *p_call, bool p_is_self) {
	ERR_FAIL_NULL(p_call);

	if (!wgodot_strict_type_checking_enabled()) {
		return;
	}

	if (p_call->is_super || p_is_self || (p_base_type.is_hard_type() && p_base_type.kind == GDScriptParser::DataType::BUILTIN) || p_base_type.is_meta_type) {
		return;
	}

	push_error(vformat(R"*(Strict type checking does not allow dynamic call "%s()" on base "%s"; the method must exist on a fully known type.)*", p_call->function_name, p_base_type.to_string()), p_call->callee);
}

void GDScriptAnalyzer::wgodot_validate_strict_dynamic_property_access(const GDScriptParser::DataType &p_base_type, const GDScriptParser::SubscriptNode *p_subscript) {
	ERR_FAIL_NULL(p_subscript);

	if (!wgodot_strict_type_checking_enabled() || p_subscript->attribute == nullptr) {
		return;
	}

	push_error(vformat(R"*(Strict type checking does not allow dynamic property "%s" on base "%s"; the property must exist on a fully known type.)*", p_subscript->attribute->name, p_base_type.to_string()), p_subscript->attribute);
}

void GDScriptAnalyzer::wgodot_validate_strict_dynamic_index_access(const GDScriptParser::DataType &p_base_type, const GDScriptParser::SubscriptNode *p_subscript) {
	ERR_FAIL_NULL(p_subscript);

	if (!wgodot_strict_type_checking_enabled()) {
		return;
	}

	push_error(vformat(R"*(Strict type checking does not allow dynamic index access on base "%s"; the result type must be fully known and non-Variant.)*", p_base_type.to_string()), p_subscript);
}

bool GDScriptAnalyzer::wgodot_try_get_identifier_narrowing_key(const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::Node *&r_key) const {
	ERR_FAIL_NULL_V(p_identifier, false);

	r_key = nullptr;
	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			r_key = p_identifier->parameter_source;
			break;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			r_key = p_identifier->variable_source;
			break;
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
			r_key = p_identifier->constant_source;
			break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			r_key = p_identifier->bind_source;
			break;
		default:
			break;
	}

	return r_key != nullptr;
}

bool GDScriptAnalyzer::wgodot_try_extract_type_narrowing(GDScriptParser::ExpressionNode *p_condition, HashMap<const GDScriptParser::Node *, WGodotNarrowedType> &r_narrowing) {
	r_narrowing.clear();

	if (!wgodot_strict_type_checking_enabled() || p_condition == nullptr) {
		return false;
	}

	if (p_condition->type == GDScriptParser::Node::TYPE_TEST) {
		GDScriptParser::TypeTestNode *type_test = static_cast<GDScriptParser::TypeTestNode *>(p_condition);
		if (type_test->operand == nullptr || type_test->operand->type != GDScriptParser::Node::IDENTIFIER || !type_test->test_datatype.is_set() || !type_test->test_datatype.is_hard_type() || wgodot_datatype_contains_variant(type_test->test_datatype)) {
			return false;
		}

		const GDScriptParser::Node *key = nullptr;
		if (!wgodot_try_get_identifier_narrowing_key(static_cast<GDScriptParser::IdentifierNode *>(type_test->operand), key)) {
			return false;
		}

		WGodotNarrowedType narrowed_type;
		narrowed_type.alternatives.push_back(type_test->test_datatype);
		r_narrowing.insert(key, narrowed_type);
		return true;
	}

	if (p_condition->type != GDScriptParser::Node::BINARY_OPERATOR) {
		return false;
	}

	GDScriptParser::BinaryOpNode *binary_op = static_cast<GDScriptParser::BinaryOpNode *>(p_condition);
	if (binary_op->operation != GDScriptParser::BinaryOpNode::OP_LOGIC_AND && binary_op->operation != GDScriptParser::BinaryOpNode::OP_LOGIC_OR) {
		return false;
	}

	HashMap<const GDScriptParser::Node *, WGodotNarrowedType> left_narrowing;
	HashMap<const GDScriptParser::Node *, WGodotNarrowedType> right_narrowing;
	const bool has_left = wgodot_try_extract_type_narrowing(binary_op->left_operand, left_narrowing);
	const bool has_right = wgodot_try_extract_type_narrowing(binary_op->right_operand, right_narrowing);

	if (binary_op->operation == GDScriptParser::BinaryOpNode::OP_LOGIC_OR) {
		if (!has_left || !has_right || left_narrowing.size() != right_narrowing.size()) {
			return false;
		}

		for (const KeyValue<const GDScriptParser::Node *, WGodotNarrowedType> &left_kv : left_narrowing) {
			const WGodotNarrowedType *right_type = right_narrowing.getptr(left_kv.key);
			if (right_type == nullptr) {
				r_narrowing.clear();
				return false;
			}

			WGodotNarrowedType combined = left_kv.value;
			for (const GDScriptParser::DataType &right_alternative : right_type->alternatives) {
				bool already_present = false;
				for (const GDScriptParser::DataType &existing_alternative : combined.alternatives) {
					if (wgodot_datatypes_match_for_narrowed_access(existing_alternative, right_alternative)) {
						already_present = true;
						break;
					}
				}
				if (!already_present) {
					combined.alternatives.push_back(right_alternative);
				}
			}

			r_narrowing.insert(left_kv.key, combined);
		}
		return !r_narrowing.is_empty();
	}

	if (!has_left && !has_right) {
		return false;
	}

	r_narrowing = left_narrowing;
	for (const KeyValue<const GDScriptParser::Node *, WGodotNarrowedType> &right_kv : right_narrowing) {
		WGodotNarrowedType *existing = r_narrowing.getptr(right_kv.key);
		if (existing == nullptr) {
			r_narrowing.insert(right_kv.key, right_kv.value);
			continue;
		}

		Vector<GDScriptParser::DataType> intersection;
		for (const GDScriptParser::DataType &left_alternative : existing->alternatives) {
			for (const GDScriptParser::DataType &right_alternative : right_kv.value.alternatives) {
				if (check_type_compatibility(left_alternative, right_alternative)) {
					intersection.push_back(right_alternative);
				} else if (check_type_compatibility(right_alternative, left_alternative)) {
					intersection.push_back(left_alternative);
				}
			}
		}
		existing->alternatives = intersection;
	}

	return !r_narrowing.is_empty();
}

bool GDScriptAnalyzer::wgodot_try_get_narrowed_type(const GDScriptParser::IdentifierNode *p_identifier, WGodotNarrowedType &r_narrowed_type) const {
	if (!wgodot_strict_type_checking_enabled()) {
		return false;
	}

	const GDScriptParser::Node *key = nullptr;
	if (!wgodot_try_get_identifier_narrowing_key(p_identifier, key)) {
		return false;
	}

	for (int i = wgodot_narrowed_type_stack.size() - 1; i >= 0; i--) {
		if (const WGodotNarrowedType *narrowed_type = wgodot_narrowed_type_stack[i].getptr(key)) {
			r_narrowed_type = *narrowed_type;
			return !r_narrowed_type.alternatives.is_empty();
		}
	}

	return false;
}

bool GDScriptAnalyzer::wgodot_try_reduce_narrowed_attribute_access(GDScriptParser::SubscriptNode *p_subscript, bool p_can_be_pseudo_type, GDScriptParser::DataType &r_result_type, bool &r_valid) {
	ERR_FAIL_NULL_V(p_subscript, false);

	r_valid = false;
	if (!wgodot_strict_type_checking_enabled() || !p_subscript->is_attribute || p_subscript->base == nullptr || p_subscript->base->type != GDScriptParser::Node::IDENTIFIER || p_subscript->attribute == nullptr) {
		return false;
	}

	WGodotNarrowedType narrowed_type;
	if (!wgodot_try_get_narrowed_type(static_cast<GDScriptParser::IdentifierNode *>(p_subscript->base), narrowed_type)) {
		return false;
	}

	bool has_result_type = false;
	for (const GDScriptParser::DataType &alternative_type : narrowed_type.alternatives) {
		GDScriptParser::IdentifierNode attribute = *p_subscript->attribute;
		attribute.source = GDScriptParser::IdentifierNode::UNDEFINED_SOURCE;
		attribute.parameter_source = nullptr;
		attribute.function_source_is_static = false;
		attribute.is_constant = false;
		attribute.reduced_value = Variant();
		attribute.set_datatype(GDScriptParser::DataType());

		GDScriptParser::DataType base_type = alternative_type;
		reduce_identifier_from_base(&attribute, &base_type);
		GDScriptParser::DataType attribute_type = attribute.get_datatype();

		if (!attribute_type.is_set() || (!p_can_be_pseudo_type && attribute_type.is_pseudo_type)) {
			r_result_type.kind = GDScriptParser::DataType::VARIANT;
			push_error(vformat(R"*(Strict type checking cannot use narrowed property "%s"; the member does not exist on alternative "%s".)*", p_subscript->attribute->name, alternative_type.to_string()), p_subscript->attribute);
			return true;
		}

		if (wgodot_datatype_contains_variant(attribute_type)) {
			r_result_type.kind = GDScriptParser::DataType::VARIANT;
			push_error(vformat(R"*(Strict type checking cannot use narrowed property "%s"; the member resolves to Variant on alternative "%s".)*", p_subscript->attribute->name, alternative_type.to_string()), p_subscript->attribute);
			return true;
		}

		if (!has_result_type) {
			r_result_type = attribute_type;
			has_result_type = true;
			continue;
		}

		if (!wgodot_datatypes_match_for_narrowed_access(r_result_type, attribute_type)) {
			push_error(vformat(R"*(Strict type checking cannot use narrowed property "%s"; alternatives resolve to different types ("%s" and "%s").)*", p_subscript->attribute->name, r_result_type.to_string(), attribute_type.to_string()), p_subscript->attribute);
			r_result_type.kind = GDScriptParser::DataType::VARIANT;
			return true;
		}
	}

	if (!has_result_type) {
		return false;
	}

	p_subscript->attribute->set_datatype(r_result_type);
	r_valid = true;
	return true;
}

bool GDScriptAnalyzer::wgodot_datatypes_match_for_narrowed_access(const GDScriptParser::DataType &p_left, const GDScriptParser::DataType &p_right) const {
	if (!p_left.is_set() || !p_right.is_set() || wgodot_datatype_contains_variant(p_left) || wgodot_datatype_contains_variant(p_right)) {
		return false;
	}

	if (p_left.kind != p_right.kind || p_left.is_meta_type != p_right.is_meta_type || p_left.get_container_element_type_count() != p_right.get_container_element_type_count()) {
		return false;
	}

	for (int i = 0; i < p_left.get_container_element_type_count(); i++) {
		if (!wgodot_datatypes_match_for_narrowed_access(p_left.get_container_element_type(i), p_right.get_container_element_type(i))) {
			return false;
		}
	}

	switch (p_left.kind) {
		case GDScriptParser::DataType::BUILTIN:
			return p_left.builtin_type == p_right.builtin_type;
		case GDScriptParser::DataType::NATIVE:
			return p_left.native_type == p_right.native_type;
		case GDScriptParser::DataType::SCRIPT:
			return p_left.script_type == p_right.script_type;
		case GDScriptParser::DataType::CLASS:
			return p_left.class_type == p_right.class_type || (p_left.class_type != nullptr && p_right.class_type != nullptr && p_left.class_type->fqcn == p_right.class_type->fqcn);
		case GDScriptParser::DataType::ENUM:
			return p_left.native_type == p_right.native_type && p_left.enum_type == p_right.enum_type;
		case GDScriptParser::DataType::VARIANT:
		case GDScriptParser::DataType::RESOLVING:
		case GDScriptParser::DataType::UNRESOLVED:
			break;
	}

	return false;
}

void GDScriptAnalyzer::wgodot_validate_signal_callable_connection(GDScriptParser::CallNode *p_call) {
	ERR_FAIL_NULL(p_call);

	if (!wgodot_strict_signal_callable_checking_enabled()) {
		return;
	}

	MethodInfo signal_info;
	if (!wgodot_try_get_connect_signal_info(p_call, signal_info)) {
		return;
	}

	if (p_call->arguments.is_empty()) {
		return;
	}

	MethodInfo callable_info;
	if (!wgodot_try_get_callable_info(p_call->arguments[0], callable_info)) {
		return;
	}

	const int signal_arg_count = signal_info.arguments.size();
	const int callable_required_arg_count = callable_info.arguments.size() - callable_info.default_arguments.size();
	const bool callable_is_vararg = (callable_info.flags & METHOD_FLAG_VARARG) != 0;
	const int callable_max_arg_count = callable_is_vararg ? INT_MAX : callable_info.arguments.size();
	const String signal_name = signal_info.name == StringName() ? String("<unknown signal>") : String(signal_info.name);
	const String callable_name = callable_info.name == StringName() ? String("<anonymous callable>") : String(callable_info.name);

	if (signal_arg_count < callable_required_arg_count) {
		push_error(vformat(R"*(Cannot connect signal "%s" to callable "%s": the signal emits %d arguments, but the callable requires at least %d.)*",
						   signal_name, callable_name, signal_arg_count, callable_required_arg_count),
				p_call->arguments[0]);
		return;
	}

	if (signal_arg_count > callable_max_arg_count) {
		push_error(vformat(R"*(Cannot connect signal "%s" to callable "%s": the signal emits %d arguments, but the callable accepts at most %d.)*",
						   signal_name, callable_name, signal_arg_count, callable_max_arg_count),
				p_call->arguments[0]);
		return;
	}

	for (int i = 0; i < signal_arg_count && i < callable_info.arguments.size(); i++) {
		const GDScriptParser::DataType signal_arg_type = type_from_property(signal_info.arguments[i], true);
		const GDScriptParser::DataType callable_arg_type = type_from_property(callable_info.arguments[i], true);

		if (!signal_arg_type.is_hard_type() || !callable_arg_type.is_hard_type() ||
				signal_arg_type.is_variant() || callable_arg_type.is_variant()) {
			continue;
		}

		if (!is_type_compatible(callable_arg_type, signal_arg_type, true)) {
			push_error(vformat(R"*(Cannot connect signal "%s" to callable "%s": signal argument %d emits "%s", but the callable expects "%s".)*",
							   signal_name, callable_name, i + 1, signal_arg_type.to_string(), callable_arg_type.to_string()),
					p_call->arguments[0]);
			return;
		}
	}
}

bool GDScriptAnalyzer::wgodot_try_get_connect_signal_info(const GDScriptParser::CallNode *p_call, MethodInfo &r_signal_info) const {
	ERR_FAIL_NULL_V(p_call, false);

	if (p_call->function_name != SNAME("connect")) {
		return false;
	}
	if (p_call->callee == nullptr || p_call->callee->type != GDScriptParser::Node::SUBSCRIPT) {
		return false;
	}

	const GDScriptParser::SubscriptNode *callee = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee);
	if (!callee->is_attribute || callee->base == nullptr) {
		return false;
	}

	const GDScriptParser::DataType signal_type = callee->base->get_datatype();
	if (!signal_type.is_hard_type() ||
			signal_type.kind != GDScriptParser::DataType::BUILTIN ||
			signal_type.builtin_type != Variant::SIGNAL) {
		return false;
	}
	if (signal_type.method_info.name == StringName() && signal_type.method_info.arguments.is_empty()) {
		return false;
	}

	r_signal_info = signal_type.method_info;
	return true;
}

bool GDScriptAnalyzer::wgodot_try_get_callable_info(const GDScriptParser::ExpressionNode *p_expression, MethodInfo &r_callable_info) const {
	ERR_FAIL_NULL_V(p_expression, false);

	if (p_expression->type == GDScriptParser::Node::LAMBDA) {
		const GDScriptParser::LambdaNode *lambda = static_cast<const GDScriptParser::LambdaNode *>(p_expression);
		if (lambda->function == nullptr) {
			return false;
		}

		r_callable_info = lambda->function->info;
		return true;
	}

	const GDScriptParser::DataType callable_type = p_expression->get_datatype();
	if (!callable_type.is_hard_type() ||
			callable_type.kind != GDScriptParser::DataType::BUILTIN ||
			callable_type.builtin_type != Variant::CALLABLE) {
		return false;
	}
	if (callable_type.method_info.name == StringName() &&
			callable_type.method_info.arguments.is_empty() &&
			callable_type.method_info.default_arguments.is_empty() &&
			!(callable_type.method_info.flags & METHOD_FLAG_VARARG)) {
		return false;
	}

	r_callable_info = callable_type.method_info;
	return true;
}

bool GDScriptAnalyzer::wgodot_strict_signal_callable_checking_enabled() const {
	const char *setting = "wgodot/gdscript/strict_signal_callable_checking";
	if (!ProjectSettings::get_singleton()->has_setting(setting)) {
		return true;
	}

	return GLOBAL_GET_CACHED(bool, setting);
}

void GDScriptAnalyzer::wgodot_validate_interface_class(GDScriptParser::ClassNode *p_class) {
	ERR_FAIL_NULL(p_class);

	if (!p_class->wgodot_is_interface) {
		return;
	}

	if (p_class->extends_used) {
		push_error("Interfaces cannot extend classes yet.", p_class);
	}

	for (GDScriptParser::ClassNode::Member member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::GROUP) {
			continue;
		}

		if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
			push_error("Interfaces can only declare method signatures for now.", member.get_source_node());
			continue;
		}

		if (member.function->is_static) {
			push_error("Interface methods cannot be static.", member.function);
		}
		if (member.function->body != nullptr && !member.function->body->statements.is_empty()) {
			push_error("Interface methods cannot have a body.", member.function->body);
		}
	}
}

void GDScriptAnalyzer::wgodot_validate_implemented_interfaces(GDScriptParser::ClassNode *p_class) {
	ERR_FAIL_NULL(p_class);

	if (p_class->wgodot_is_interface || p_class->wgodot_implements.is_empty()) {
		return;
	}

	Vector<GDScriptParser::ClassNode *> interface_classes;
	for (const GDScriptParser::ClassNode::WGodotInterfaceReference &interface_reference : p_class->wgodot_implements) {
		GDScriptParser::ClassNode *interface_class = wgodot_resolve_interface_reference(p_class, interface_reference);
		if (interface_class == nullptr) {
			continue;
		}

		resolve_class_interface(interface_class, p_class);

		if (!interface_class->wgodot_is_interface) {
			push_error(vformat(R"("%s" is not an interface.)", wgodot_get_class_display_name(interface_class)), p_class);
			continue;
		}

		interface_classes.push_back(interface_class);
	}

	HashSet<StringName> conflicted_methods = wgodot_validate_implemented_interface_conflicts(p_class, interface_classes);

	for (GDScriptParser::ClassNode *interface_class : interface_classes) {
		const int builtin_interface_index = WGodotGDScriptStdLib::get_builtin_interface_index(interface_class->wgodot_interface_name);
		for (int interface_method_index = 0; interface_method_index < interface_class->members.size(); interface_method_index++) {
			const GDScriptParser::ClassNode::Member &interface_member = interface_class->members[interface_method_index];
			if (interface_member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			if (conflicted_methods.has(interface_member.function->identifier->name)) {
				continue;
			}

			const StringName interface_method_name = interface_member.function->identifier->name;
			const StringName interface_method_alias = WGodotGDScriptInterfaceMethodAliases::resolve_builtin_alias(builtin_interface_index, interface_method_index);
			const StringName implementation_method_name = interface_method_alias.is_empty() ? interface_method_name : interface_method_alias;
			GDScriptParser::FunctionNode *implementation_function = wgodot_find_function_in_class_hierarchy(p_class, implementation_method_name);
			if (implementation_function == nullptr) {
				push_error(vformat(R"*(Class "%s" implements interface "%s" but is missing method "%s()".)*",
								   wgodot_get_class_display_name(p_class), wgodot_get_class_display_name(interface_class), implementation_method_name),
						p_class);
				continue;
			}

			implementation_function->wgodot_interface_implementation = true;

			String signature_error;
			if (!wgodot_interface_method_signature_matches(interface_member.function, implementation_function, signature_error)) {
				push_error(vformat(R"*(Class "%s" implements interface "%s", but method "%s()" has an incompatible signature: %s)*",
								   wgodot_get_class_display_name(p_class), wgodot_get_class_display_name(interface_class), interface_member.function->identifier->name, signature_error),
						implementation_function);
			}
		}
	}
}

HashSet<StringName> GDScriptAnalyzer::wgodot_validate_implemented_interface_conflicts(GDScriptParser::ClassNode *p_class, const Vector<GDScriptParser::ClassNode *> &p_interfaces) {
	struct SeenInterfaceMethod {
		StringName name;
		GDScriptParser::FunctionNode *function = nullptr;
		GDScriptParser::ClassNode *interface_class = nullptr;
	};

	HashSet<StringName> conflicted_methods;
	Vector<SeenInterfaceMethod> seen_methods;

	for (GDScriptParser::ClassNode *interface_class : p_interfaces) {
		for (GDScriptParser::ClassNode::Member interface_member : interface_class->members) {
			if (interface_member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				continue;
			}

			const StringName method_name = interface_member.function->identifier->name;
			for (const SeenInterfaceMethod &seen_method : seen_methods) {
				if (seen_method.name != method_name) {
					continue;
				}

				String signature_error;
				if (wgodot_interface_methods_conflict(seen_method.function, interface_member.function, signature_error)) {
					conflicted_methods.insert(method_name);
					push_error(vformat(R"*(Class "%s" cannot implement interfaces "%s" and "%s" together because method "%s()" has conflicting signatures: %s)*",
									   wgodot_get_class_display_name(p_class),
									   wgodot_get_class_display_name(seen_method.interface_class),
									   wgodot_get_class_display_name(interface_class),
									   method_name,
									   signature_error),
							p_class);
				}
			}

			SeenInterfaceMethod seen_method;
			seen_method.name = method_name;
			seen_method.function = interface_member.function;
			seen_method.interface_class = interface_class;
			seen_methods.push_back(seen_method);
		}
	}

	return conflicted_methods;
}

void GDScriptAnalyzer::wgodot_validate_static_class(GDScriptParser::ClassNode *p_class) {
	ERR_FAIL_NULL(p_class);

	if (!p_class->wgodot_static_class) {
		return;
	}

	if (p_class->extends_used) {
		push_error("@static_class classes cannot use extends.", p_class);
	}
	if (!p_class->wgodot_implements.is_empty()) {
		push_error("@static_class classes cannot use implements.", p_class);
	}

	for (GDScriptParser::ClassNode::Member member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CONSTANT:
			case GDScriptParser::ClassNode::Member::ENUM:
			case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			case GDScriptParser::ClassNode::Member::GROUP:
				break;
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (!member.function->is_static) {
					push_error(vformat(R"*(@static_class class members must be static or const, but function "%s()" is not static.)*", member.function->identifier->name), member.function);
				}
				break;
			case GDScriptParser::ClassNode::Member::VARIABLE:
				if (!member.variable->is_static) {
					push_error(vformat(R"*(@static_class class members must be static or const, but variable "%s" is not static.)*", member.variable->identifier->name), member.variable);
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				push_error(vformat(R"*(@static_class class members must be static or const, but nested class "%s" is not static.)*", member.m_class->identifier->name), member.m_class);
				break;
			case GDScriptParser::ClassNode::Member::SIGNAL:
				push_error(vformat(R"*(@static_class class members must be static or const, but signal "%s" is not static.)*", member.signal->identifier->name), member.signal);
				break;
			case GDScriptParser::ClassNode::Member::UNDEFINED:
				break;
		}
	}
}

bool GDScriptAnalyzer::wgodot_validate_static_class_type_hint(GDScriptParser::TypeNode *p_type, const GDScriptParser::DataType &p_datatype) {
	ERR_FAIL_NULL_V(p_type, false);

	GDScriptParser::ClassNode *static_class = wgodot_get_static_class_from_datatype(p_datatype, p_type);
	if (static_class == nullptr) {
		return false;
	}

	push_error(vformat(R"(Cannot use @static_class "%s" as a type hint.)", wgodot_get_class_display_name(static_class)), p_type);
	return true;
}

bool GDScriptAnalyzer::wgodot_validate_static_class_constructor_call(GDScriptParser::CallNode *p_call, const GDScriptParser::DataType &p_base_type) {
	ERR_FAIL_NULL_V(p_call, false);

	GDScriptParser::ClassNode *static_class = wgodot_get_static_class_from_datatype(p_base_type, p_call);
	if (static_class == nullptr) {
		return false;
	}

	push_error(vformat(R"(Cannot construct @static_class "%s".)", wgodot_get_class_display_name(static_class)), p_call);
	return true;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::wgodot_get_static_class_from_datatype(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_source) {
	if (p_type.kind == GDScriptParser::DataType::CLASS) {
		if (p_type.class_type != nullptr && p_type.class_type->wgodot_static_class) {
			return p_type.class_type;
		}
		return nullptr;
	}

	if (p_type.kind == GDScriptParser::DataType::SCRIPT && !p_type.script_path.is_empty()) {
		Ref<GDScriptParserRef> script_parser_ref = parser->get_depended_parser_for(p_type.script_path);
		if (script_parser_ref.is_null() || script_parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED) != OK) {
			return nullptr;
		}

		GDScriptParser::ClassNode *script_class = script_parser_ref->get_parser()->head;
		if (script_class != nullptr && script_class->wgodot_static_class) {
			return script_class;
		}
	}

	(void)p_source;
	return nullptr;
}

bool GDScriptAnalyzer::wgodot_try_resolve_stdlib_interface_type(GDScriptParser::TypeNode *p_type, const StringName &p_type_name, GDScriptParser::DataType &r_datatype, bool &r_valid) {
	ERR_FAIL_NULL_V(p_type, false);
	r_valid = true;

	if (!WGodotGDScriptStdLib::has_global_interface(p_type_name)) {
		return false;
	}

	if (p_type->type_chain.size() > 1) {
		push_error(vformat(R"(Built-in interface "%s" does not contain nested types.)", p_type_name), p_type->type_chain[1]);
		r_valid = false;
		return true;
	}

	const String interface_path = WGodotGDScriptStdLib::get_global_interface_path(p_type_name);
	Ref<GDScriptParserRef> interface_parser_ref = parser->get_depended_parser_for(interface_path);
	if (interface_parser_ref.is_null() || interface_parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED) != OK) {
		push_error(vformat(R"(Could not resolve built-in interface "%s".)", p_type_name), p_type);
		r_valid = false;
		return true;
	}

	GDScriptParser::ClassNode *interface_class = interface_parser_ref->get_parser()->get_tree();
	if (interface_class == nullptr || !interface_class->wgodot_is_interface) {
		push_error(vformat(R"(Built-in type "%s" is not an interface.)", p_type_name), p_type);
		r_valid = false;
		return true;
	}

	r_datatype = interface_class->get_datatype();
	return true;
}

bool GDScriptAnalyzer::wgodot_try_resolve_value_container_type_hint(GDScriptParser::TypeNode *p_type, GDScriptParser::DataType &r_datatype, bool &r_valid) {
	ERR_FAIL_NULL_V(p_type, false);
	r_valid = true;

	if (!wgodot_is_value_container_type(r_datatype)) {
		return false;
	}

	if (p_type->container_types.size() != 1) {
		push_error("ValueContainer requires exactly one type parameter.", p_type);
		r_valid = false;
		return true;
	}

	GDScriptParser::DataType element_type = type_from_metatype(resolve_datatype(p_type->get_container_type_or_null(0)));
	if (!element_type.is_set() || element_type.has_no_type()) {
		r_valid = false;
		return true;
	}

	element_type.is_constant = false;
	element_type.is_meta_type = false;
	r_datatype.set_container_element_type(0, element_type);
	return true;
}

bool GDScriptAnalyzer::wgodot_try_get_value_container_function_signature(GDScriptParser::Node *p_source, bool p_is_constructor, const GDScriptParser::DataType &p_base_type, const StringName &p_function, GDScriptParser::DataType &r_return_type, List<GDScriptParser::DataType> &r_par_types, int &r_default_arg_count, BitField<MethodFlags> &r_method_flags) {
	if (!wgodot_is_value_container_type(p_base_type)) {
		return false;
	}

	r_default_arg_count = 0;
	r_method_flags = METHOD_FLAGS_DEFAULT;

	GDScriptParser::DataType value_type = wgodot_get_value_container_element_type(p_base_type);
	GDScriptParser::DataType void_type;
	void_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	void_type.kind = GDScriptParser::DataType::BUILTIN;
	void_type.builtin_type = Variant::NIL;

	if (p_is_constructor) {
		if (p_base_type.native_type == SNAME("ValueContainer")) {
			push_error(R"*(Use "ValueContainer.create(default_value)" instead of "ValueContainer.new()".)*", p_source);
			return false;
		}
	}

	if (p_function == SNAME("create") && p_base_type.is_meta_type) {
		r_method_flags.set_flag(METHOD_FLAG_STATIC);
		r_return_type = p_base_type;
		r_return_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
		r_return_type.is_meta_type = false;

		GDScriptParser::DataType default_value_type = p_base_type.has_container_element_type(0) ? value_type : GDScriptParser::DataType::get_variant_type();
		default_value_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;

		if (!p_base_type.has_container_element_type(0) && p_source != nullptr && p_source->type == GDScriptParser::Node::CALL) {
			GDScriptParser::CallNode *call = static_cast<GDScriptParser::CallNode *>(p_source);
			if (!call->arguments.is_empty()) {
				GDScriptParser::DataType first_arg_type = call->arguments[0]->get_datatype();
				if (first_arg_type.is_set() && !first_arg_type.has_no_type()) {
					default_value_type = first_arg_type;
					default_value_type.is_constant = false;
					default_value_type.is_meta_type = false;
					r_return_type.set_container_element_type(0, default_value_type);
				}
			}
		}

		r_par_types.push_back(default_value_type);
		return true;
	}

	if (p_function == SNAME("get_value")) {
		r_return_type = value_type;
		return true;
	}
	if (p_function == SNAME("change_value")) {
		r_par_types.push_back(value_type);
		r_return_type = void_type;
		return true;
	}
	if (p_function == SNAME("clear_value")) {
		r_return_type = void_type;
		return true;
	}

	return false;
}

bool GDScriptAnalyzer::wgodot_try_get_value_container_signal_type(const GDScriptParser::DataType &p_base_type, const StringName &p_signal, GDScriptParser::DataType &r_signal_type) const {
	if (p_signal != SNAME("value_changed") || !wgodot_is_value_container_type(p_base_type)) {
		return false;
	}

	GDScriptParser::DataType value_type = wgodot_get_value_container_element_type(p_base_type);
	MethodInfo signal_info("value_changed",
			PropertyInfo(Variant::OBJECT, "sender", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "ValueContainer"),
			value_type.to_property_info("old_value"),
			value_type.to_property_info("new_value"));

	r_signal_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	r_signal_type.kind = GDScriptParser::DataType::BUILTIN;
	r_signal_type.builtin_type = Variant::SIGNAL;
	r_signal_type.is_constant = true;
	r_signal_type.method_info = signal_info;
	return true;
}

void GDScriptAnalyzer::wgodot_validate_value_container_call(const GDScriptParser::DataType &p_base_type, const GDScriptParser::CallNode *p_call) {
	ERR_FAIL_NULL(p_call);

	if (!wgodot_is_value_container_type(p_base_type) || (p_call->function_name != SNAME("change_value") && p_call->function_name != SNAME("create")) || p_call->arguments.size() != 1 || !p_base_type.has_container_element_type(0)) {
		return;
	}

	GDScriptParser::DataType value_type = wgodot_get_value_container_element_type(p_base_type);
	if (!value_type.is_hard_type() || value_type.is_variant()) {
		return;
	}

	GDScriptParser::DataType arg_type = p_call->arguments[0]->get_datatype();
	if (!arg_type.is_hard_type() || arg_type.is_variant()) {
		push_error(vformat(R"*(Invalid argument for "%s()" function: argument 1 must be statically typed as "%s", but is "%s".)*", p_call->function_name, value_type.to_string(), arg_type.to_string_strict()), p_call->arguments[0]);
		return;
	}

	if (is_type_compatible(value_type, arg_type, true, p_call->arguments[0]) && !check_type_compatibility(value_type, arg_type, false, p_call->arguments[0])) {
		push_error(vformat(R"*(Invalid argument for "%s()" function: argument 1 should be exactly "%s" but is "%s".)*", p_call->function_name, value_type.to_string(), arg_type.to_string()), p_call->arguments[0]);
	}
}

bool GDScriptAnalyzer::wgodot_is_value_container_type(const GDScriptParser::DataType &p_type) {
	return p_type.kind == GDScriptParser::DataType::NATIVE && p_type.native_type == SNAME("ValueContainer");
}

GDScriptParser::DataType GDScriptAnalyzer::wgodot_get_value_container_element_type(const GDScriptParser::DataType &p_type) {
	if (p_type.has_container_element_type(0)) {
		return p_type.get_container_element_type(0);
	}

	GDScriptParser::DataType variant_type = GDScriptParser::DataType::get_variant_type();
	variant_type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	return variant_type;
}

GDScriptParser::ClassNode *GDScriptAnalyzer::wgodot_resolve_interface_reference(GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode::WGodotInterfaceReference &p_reference) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	GDScriptParser::Node *source = p_class;
	if (!p_reference.identifiers.is_empty()) {
		source = p_reference.identifiers[0];
	}

	GDScriptParser::ClassNode *interface_class = nullptr;
	Ref<GDScriptParserRef> interface_parser_ref;

	if (!p_reference.path.is_empty()) {
		String interface_path = p_reference.path;
		if (interface_path.is_relative_path()) {
			interface_path = parser->script_path.get_base_dir().path_join(interface_path).simplify_path();
		}

		interface_parser_ref = parser->get_depended_parser_for(interface_path);
		if (interface_parser_ref.is_null()) {
			push_error(vformat(R"(Could not resolve interface path "%s".)", interface_path), source);
			return nullptr;
		}
	} else {
		if (p_reference.identifiers.is_empty()) {
			push_error("Expected interface path or name after \"implements\".", source);
			return nullptr;
		}

		const StringName &interface_name = p_reference.identifiers[0]->name;
		if (WGodotGDScriptStdLib::has_global_interface(interface_name)) {
			String interface_path = WGodotGDScriptStdLib::get_global_interface_path(interface_name);
			interface_parser_ref = parser->get_depended_parser_for(interface_path);
			if (interface_parser_ref.is_null()) {
				push_error(vformat(R"(Could not resolve built-in interface "%s".)", interface_name), source);
				return nullptr;
			}
		} else if (ScriptServer::is_global_class(interface_name)) {
			String interface_path = ScriptServer::get_global_class_path(interface_name);
			if (GDScript::is_canonically_equal_paths(interface_path, parser->script_path)) {
				interface_class = parser->head;
			} else {
				interface_parser_ref = parser->get_depended_parser_for(interface_path);
				if (interface_parser_ref.is_null()) {
					push_error(vformat(R"(Could not resolve interface "%s".)", interface_name), source);
					return nullptr;
				}
			}
		} else {
			List<GDScriptParser::ClassNode *> script_classes;
			get_class_node_current_scope_classes(p_class, &script_classes, source);
			for (GDScriptParser::ClassNode *look_class : script_classes) {
				if (look_class->identifier != nullptr && look_class->identifier->name == interface_name) {
					interface_class = look_class;
					break;
				}
			}
			if (interface_class == nullptr) {
				push_error(vformat(R"(Could not find interface "%s".)", interface_name), source);
				return nullptr;
			}
		}
	}

	if (interface_parser_ref.is_valid()) {
		Error err = interface_parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED);
		if (err != OK) {
			push_error(vformat(R"(Could not resolve interface "%s".)", interface_parser_ref->get_path()), source);
			return nullptr;
		}
		interface_class = interface_parser_ref->get_parser()->head;
	}

	for (int i = 1; interface_class != nullptr && i < p_reference.identifiers.size(); i++) {
		resolve_class_interface(interface_class, source);
		const StringName &nested_name = p_reference.identifiers[i]->name;
		if (!interface_class->has_member(nested_name)) {
			push_error(vformat(R"(Could not find nested interface "%s".)", nested_name), p_reference.identifiers[i]);
			return nullptr;
		}
		GDScriptParser::ClassNode::Member nested_member = interface_class->get_member(nested_name);
		if (nested_member.type != GDScriptParser::ClassNode::Member::CLASS) {
			push_error(vformat(R"("%s" is not a nested interface.)", nested_name), p_reference.identifiers[i]);
			return nullptr;
		}
		interface_class = nested_member.m_class;
	}

	return interface_class;
}

GDScriptParser::FunctionNode *GDScriptAnalyzer::wgodot_find_function_in_class_hierarchy(GDScriptParser::ClassNode *p_class, const StringName &p_function_name) {
	for (GDScriptParser::ClassNode *current_class = p_class; current_class != nullptr;) {
		resolve_class_interface(current_class, p_class);

		if (current_class->has_member(p_function_name)) {
			resolve_class_member(current_class, p_function_name, p_class);
			GDScriptParser::ClassNode::Member member = current_class->get_member(p_function_name);
			if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
				return member.function;
			}
		}

		if (current_class->base_type.kind == GDScriptParser::DataType::CLASS) {
			current_class = current_class->base_type.class_type;
		} else if (current_class->base_type.kind == GDScriptParser::DataType::SCRIPT) {
			Ref<GDScriptParserRef> base_parser_ref = parser->get_depended_parser_for(current_class->base_type.script_path);
			if (base_parser_ref.is_null() || base_parser_ref->raise_status(GDScriptParserRef::INTERFACE_SOLVED) != OK) {
				return nullptr;
			}
			current_class = base_parser_ref->get_parser()->head;
		} else {
			current_class = nullptr;
		}
	}

	return nullptr;
}

bool GDScriptAnalyzer::wgodot_interface_methods_conflict(const GDScriptParser::FunctionNode *p_first_function, const GDScriptParser::FunctionNode *p_second_function, String &r_error) const {
	ERR_FAIL_NULL_V(p_first_function, false);
	ERR_FAIL_NULL_V(p_second_function, false);

	if (p_first_function->is_static != p_second_function->is_static) {
		r_error = "one method is static and the other is not";
		return true;
	}
	if (p_first_function->is_vararg() != p_second_function->is_vararg()) {
		r_error = "one method is vararg and the other is not";
		return true;
	}
	if (p_first_function->parameters.size() != p_second_function->parameters.size()) {
		r_error = vformat("one method has %d parameter(s), the other has %d", p_first_function->parameters.size(), p_second_function->parameters.size());
		return true;
	}
	if (p_first_function->default_arg_values.size() != p_second_function->default_arg_values.size()) {
		r_error = "the methods have different required parameter counts";
		return true;
	}

	for (int i = 0; i < p_first_function->parameters.size(); i++) {
		const String first_parameter_type = p_first_function->parameters[i]->datatype.to_string_strict();
		const String second_parameter_type = p_second_function->parameters[i]->datatype.to_string_strict();
		if (first_parameter_type != second_parameter_type) {
			r_error = vformat("parameter %d is \"%s\" in one interface but \"%s\" in the other", i + 1, first_parameter_type, second_parameter_type);
			return true;
		}
	}

	const String first_return_type = p_first_function->get_datatype().to_string_strict();
	const String second_return_type = p_second_function->get_datatype().to_string_strict();
	if (first_return_type != second_return_type) {
		r_error = vformat("return type is \"%s\" in one interface but \"%s\" in the other", first_return_type, second_return_type);
		return true;
	}

	return false;
}

bool GDScriptAnalyzer::wgodot_interface_method_signature_matches(const GDScriptParser::FunctionNode *p_interface_function, const GDScriptParser::FunctionNode *p_implementation_function, String &r_error) {
	ERR_FAIL_NULL_V(p_interface_function, false);
	ERR_FAIL_NULL_V(p_implementation_function, false);

	if (p_implementation_function->is_static) {
		r_error = "implementation is static";
		return false;
	}

	const int interface_min_argc = p_interface_function->parameters.size() - p_interface_function->default_arg_values.size();
	const int interface_max_argc = p_interface_function->is_vararg() ? INT_MAX : p_interface_function->parameters.size();
	const int implementation_min_argc = p_implementation_function->parameters.size() - p_implementation_function->default_arg_values.size();
	const int implementation_max_argc = p_implementation_function->is_vararg() ? INT_MAX : p_implementation_function->parameters.size();

	if (implementation_min_argc > interface_min_argc || interface_max_argc > implementation_max_argc) {
		r_error = vformat("expected callable argument range [%d, %s], got [%d, %s]",
				interface_min_argc,
				interface_max_argc == INT_MAX ? String("...") : itos(interface_max_argc),
				implementation_min_argc,
				implementation_max_argc == INT_MAX ? String("...") : itos(implementation_max_argc));
		return false;
	}

	for (int i = 0; i < p_interface_function->parameters.size() && i < p_implementation_function->parameters.size(); i++) {
		const GDScriptParser::DataType &interface_parameter_type = p_interface_function->parameters[i]->datatype;
		const GDScriptParser::DataType &implementation_parameter_type = p_implementation_function->parameters[i]->datatype;
		if (interface_parameter_type.is_hard_type() && implementation_parameter_type.is_hard_type() &&
				!is_type_compatible(implementation_parameter_type, interface_parameter_type)) {
			r_error = vformat("parameter %d expects \"%s\" in the interface, but implementation accepts \"%s\"",
					i + 1, interface_parameter_type.to_string(), implementation_parameter_type.to_string());
			return false;
		}
	}

	const GDScriptParser::DataType interface_return_type = p_interface_function->get_datatype();
	const GDScriptParser::DataType implementation_return_type = p_implementation_function->get_datatype();
	if (interface_return_type.is_hard_type() && implementation_return_type.is_hard_type() &&
			!is_type_compatible(interface_return_type, implementation_return_type)) {
		r_error = vformat("interface returns \"%s\", but implementation returns \"%s\"", interface_return_type.to_string(), implementation_return_type.to_string());
		return false;
	}

	return true;
}

String GDScriptAnalyzer::wgodot_get_class_display_name(const GDScriptParser::ClassNode *p_class) const {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	if (p_class->wgodot_interface_name != StringName()) {
		return p_class->wgodot_interface_name;
	}
	if (!p_class->fqcn.is_empty()) {
		return p_class->fqcn.get_file();
	}
	return "<anonymous>";
}
