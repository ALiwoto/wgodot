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
	const char *setting = "debug/gdscript/wgodot/strict_override_checking";
	if (!ProjectSettings::get_singleton()->has_setting(setting)) {
		return true;
	}

	return GLOBAL_GET_CACHED(bool, setting);
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
	const char *setting = "debug/gdscript/wgodot/strict_signal_callable_checking";
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

		for (GDScriptParser::ClassNode::Member interface_member : interface_class->members) {
			if (interface_member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				continue;
			}

			GDScriptParser::FunctionNode *implementation_function = wgodot_find_function_in_class_hierarchy(p_class, interface_member.function->identifier->name);
			if (implementation_function == nullptr) {
				push_error(vformat(R"*(Class "%s" implements interface "%s" but is missing method "%s()".)*",
								   wgodot_get_class_display_name(p_class), wgodot_get_class_display_name(interface_class), interface_member.function->identifier->name),
						p_class);
				continue;
			}

			String signature_error;
			if (!wgodot_interface_method_signature_matches(interface_member.function, implementation_function, signature_error)) {
				push_error(vformat(R"*(Class "%s" implements interface "%s", but method "%s()" has an incompatible signature: %s)*",
								   wgodot_get_class_display_name(p_class), wgodot_get_class_display_name(interface_class), interface_member.function->identifier->name, signature_error),
						implementation_function);
			}
		}
	}
}

GDScriptParser::ClassNode *GDScriptAnalyzer::wgodot_resolve_interface_reference(GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode::WGodotInterfaceReference &p_reference) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	const GDScriptParser::Node *source = p_class;
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
		if (ScriptServer::is_global_class(interface_name)) {
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
