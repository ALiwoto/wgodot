// wgodot-changes::file
/**************************************************************************/
/*  name_obfuscation.cpp                                                  */
/**************************************************************************/

#include "name_obfuscation.h"

#include "export_context.h"
#include "export_timing.h"
#include "obfuscation_names.h"

#include "core/error/error_macros.h"
#include "core/variant/variant.h"

namespace {

String make_obfuscated_local_name(WGodotGDScriptExportTransform::RewriteContext &r_context) {
	const uint64_t start_usec = r_context.timing_enabled ? WGodotGDScriptExportTransform::export_timing_get_ticks_usec() : 0;
	if (r_context.timing_enabled) {
		r_context.local_name_make_calls++;
	}
	String obfuscated_name;
	if (r_context.export_context != nullptr) {
		obfuscated_name = r_context.export_context->make_obfuscated_name_from_reserved_names(r_context.reserved_obfuscated_names, "local variable");
	} else {
		obfuscated_name = WGodotGDScriptExportTransform::make_obfuscated_name(r_context.options.obfuscation_strategy, r_context.obfuscation_random, r_context.reserved_obfuscated_names, "local variable", r_context.options.binary_tokens_export);
	}
	if (r_context.timing_enabled) {
		r_context.local_name_make_usec += WGodotGDScriptExportTransform::export_timing_get_ticks_usec() - start_usec;
	}
	return obfuscated_name;
}

const GDScriptParser::Node *get_local_declaration_node(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
			return p_local.parameter;
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return p_local.bind;
		default:
			return nullptr;
	}
}

const GDScriptParser::IdentifierNode *get_local_declaration_identifier(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable != nullptr ? p_local.variable->identifier : nullptr;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
			return p_local.parameter != nullptr ? p_local.parameter->identifier : nullptr;
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return p_local.bind;
		default:
			return nullptr;
	}
}

bool should_obfuscate_local(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable != nullptr && !p_local.variable->wgodot_no_mangle;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return true;
		default:
			return false;
	}
}

bool is_gdscript_magic_function_name(const StringName &p_name) {
	static const StringName magic_names[] = {
		SNAME("_init"),
		SNAME("_notification"),
		SNAME("_enter_tree"),
		SNAME("_ready"),
		SNAME("_process"),
		SNAME("_physics_process"),
		SNAME("_input"),
		SNAME("_shortcut_input"),
		SNAME("_unhandled_input"),
		SNAME("_unhandled_key_input"),
		SNAME("_exit_tree"),
		SNAME("_draw"),
		SNAME("_get"),
		SNAME("_set"),
		SNAME("_get_property_list"),
		SNAME("_validate_property"),
		SNAME("_property_can_revert"),
		SNAME("_property_get_revert"),
		SNAME("_to_string"),
		SNAME("_iter_init"),
		SNAME("_iter_next"),
		SNAME("_iter_get"),
	};

	for (const StringName &magic_name : magic_names) {
		if (p_name == magic_name) {
			return true;
		}
	}

	return false;
}

bool should_obfuscate_function(const GDScriptParser::FunctionNode *p_function, bool p_no_mangle_scope, bool p_obfuscate_scope) {
	return !p_no_mangle_scope &&
			p_function != nullptr &&
			p_function->identifier != nullptr &&
			!is_gdscript_magic_function_name(p_function->identifier->name) &&
			(p_obfuscate_scope || p_function->wgodot_private || p_function->wgodot_obfuscate) &&
			!p_function->wgodot_no_mangle;
}

bool should_obfuscate_variable(const GDScriptParser::VariableNode *p_variable, bool p_no_mangle_scope, bool p_obfuscate_scope) {
	return !p_no_mangle_scope &&
			p_variable != nullptr &&
			p_variable->identifier != nullptr &&
			(p_obfuscate_scope || p_variable->wgodot_private || p_variable->wgodot_obfuscate) &&
			!p_variable->wgodot_no_mangle;
}

bool should_obfuscate_signal(const GDScriptParser::SignalNode *p_signal, bool p_no_mangle_scope, bool p_obfuscate_scope) {
	return !p_no_mangle_scope &&
			p_signal != nullptr &&
			p_signal->identifier != nullptr &&
			(p_obfuscate_scope || p_signal->wgodot_private) &&
			!p_signal->wgodot_no_mangle;
}

bool should_obfuscate_class(const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope, bool p_obfuscate_scope) {
	return !p_no_mangle_scope &&
			p_class != nullptr &&
			p_class->outer != nullptr &&
			p_class->identifier != nullptr &&
			(p_obfuscate_scope || p_class->wgodot_private || p_class->wgodot_obfuscate) &&
			!p_class->wgodot_no_mangle;
}

String get_or_create_context_member_name(WGodotGDScriptExportTransform::RewriteContext &r_context, const Vector<String> &p_keys) {
	if (r_context.export_context == nullptr || p_keys.is_empty()) {
		return String();
	}

	for (const String &key : p_keys) {
		const String *existing = r_context.export_context->get_member_rename(key);
		if (existing != nullptr) {
			for (const String &alias_key : p_keys) {
				r_context.export_context->bind_member_rename(alias_key, *existing);
			}
			return *existing;
		}
	}

	const String obfuscated_name = r_context.export_context->get_or_create_member_rename(p_keys[0]);
	for (int i = 1; i < p_keys.size(); i++) {
		r_context.export_context->bind_member_rename(p_keys[i], obfuscated_name);
	}
	return obfuscated_name;
}

String get_datatype_class_key(const GDScriptParser::DataType &p_datatype) {
	if (p_datatype.class_type != nullptr && !p_datatype.class_type->fqcn.is_empty()) {
		return p_datatype.class_type->fqcn;
	}
	if (!p_datatype.script_path.is_empty()) {
		return p_datatype.script_path;
	}
	if (p_datatype.class_type != nullptr && p_datatype.class_type->identifier != nullptr) {
		return String(p_datatype.class_type->identifier->name);
	}
	return String();
}

bool class_has_no_mangle_scope(const GDScriptParser::ClassNode *p_class) {
	for (const GDScriptParser::ClassNode *script_class = p_class; script_class != nullptr; script_class = script_class->outer) {
		if (script_class->wgodot_no_mangle) {
			return true;
		}
	}
	return false;
}

bool is_no_mangle_datatype_class(const GDScriptParser::DataType &p_datatype) {
	return class_has_no_mangle_scope(p_datatype.class_type);
}

void reserve_datatype_class_member_names(WGodotGDScriptExportTransform::RewriteContext &r_context, const GDScriptParser::DataType &p_datatype) {
	if (r_context.export_context == nullptr || p_datatype.class_type == nullptr) {
		return;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_datatype.class_type->members) {
		const String member_name = member.get_name();
		if (!member_name.is_empty()) {
			r_context.export_context->reserve_member_name(StringName(member_name));
		}
	}
}

bool datatype_function_allows_context_member_obfuscation(const GDScriptParser::DataType &p_datatype, const StringName &p_function_name) {
	if (p_datatype.class_type == nullptr || p_function_name.is_empty()) {
		return false;
	}

	for (const GDScriptParser::ClassNode *script_class = p_datatype.class_type; script_class != nullptr; script_class = script_class->base_type.class_type) {
		if (class_has_no_mangle_scope(script_class)) {
			return false;
		}
		if (!script_class->has_member(p_function_name)) {
			continue;
		}

		const GDScriptParser::ClassNode::Member member = script_class->get_member(p_function_name);
		if (member.type != GDScriptParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
			return false;
		}

		return (script_class->wgodot_obfuscate || member.function->wgodot_obfuscate) && !member.function->wgodot_no_mangle;
	}

	return false;
}

bool datatype_attribute_allows_context_member_obfuscation(const GDScriptParser::DataType &p_datatype, const StringName &p_member_name) {
	if (p_datatype.class_type == nullptr || p_member_name.is_empty()) {
		return false;
	}

	for (const GDScriptParser::ClassNode *script_class = p_datatype.class_type; script_class != nullptr; script_class = script_class->base_type.class_type) {
		if (class_has_no_mangle_scope(script_class)) {
			return false;
		}
		if (!script_class->has_member(p_member_name)) {
			continue;
		}

		const GDScriptParser::ClassNode::Member member = script_class->get_member(p_member_name);
		if (member.type == GDScriptParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			return (script_class->wgodot_obfuscate || member.variable->wgodot_obfuscate) && !member.variable->wgodot_no_mangle;
		}
		if (member.type == GDScriptParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
			return (script_class->wgodot_obfuscate || member.function->wgodot_obfuscate) && !member.function->wgodot_no_mangle;
		}
		if (member.type == GDScriptParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
			return (script_class->wgodot_obfuscate || member.signal->wgodot_private) && !member.signal->wgodot_no_mangle;
		}

		return false;
	}

	return false;
}

const GDScriptParser::Node *get_local_identifier_source(const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return nullptr;
	}

	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			return p_identifier->parameter_source;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			return p_identifier->variable_source;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			return p_identifier->bind_source;
		default:
			return nullptr;
	}
}

String get_obfuscated_class_name(WGodotGDScriptExportTransform::RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->outer == nullptr || p_class->identifier == nullptr || class_has_no_mangle_scope(p_class)) {
		return String();
	}

	if (const String *local_obfuscated_name = r_context.obfuscated_class_names.getptr(p_class)) {
		return *local_obfuscated_name;
	}

	if (r_context.export_context == nullptr) {
		return String();
	}

	Vector<String> keys;
	WGodotGDScriptExportTransform::ExportContext::make_member_keys(p_class->outer, r_context.script_path, p_class->identifier->name, keys);
	for (const String &key : keys) {
		if (const String *context_obfuscated_name = r_context.export_context->get_member_rename(key)) {
			return *context_obfuscated_name;
		}
	}

	return String();
}

} // namespace

namespace WGodotGDScriptExportTransform {

void collect_member_name_obfuscation(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope, bool p_obfuscate_scope) {
	if (!r_context.options.obfuscate_names || p_class == nullptr) {
		return;
	}

	const bool no_mangle_scope = p_no_mangle_scope || p_class->wgodot_no_mangle;
	const bool obfuscate_scope = p_obfuscate_scope || p_class->wgodot_obfuscate;
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (!String(member.get_name()).is_empty()) {
			r_context.reserved_obfuscated_names.insert(StringName(member.get_name()));
			if (r_context.export_context != nullptr) {
				r_context.export_context->reserve_member_name(StringName(member.get_name()));
			}
		}
	}

	if (no_mangle_scope) {
		return;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CLASS: {
				if (should_obfuscate_class(member.m_class, no_mangle_scope, obfuscate_scope) && !r_context.obfuscated_class_names.has(member.m_class)) {
					String obfuscated_name;
					Vector<String> keys;
					ExportContext::make_member_keys(p_class, r_context.script_path, member.m_class->identifier->name, keys);
					obfuscated_name = get_or_create_context_member_name(r_context, keys);
					if (obfuscated_name.is_empty()) {
						obfuscated_name = make_obfuscated_local_name(r_context);
					}
					r_context.obfuscated_class_names[member.m_class] = obfuscated_name;
					add_replacement(r_context, member.m_class->identifier, obfuscated_name);
				}
				collect_member_name_obfuscation(r_context, member.m_class, no_mangle_scope, obfuscate_scope);
			} break;
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				if (!should_obfuscate_function(member.function, no_mangle_scope, obfuscate_scope) || r_context.obfuscated_function_names.has(member.function)) {
					break;
				}

				String obfuscated_name;
				if (member.function->wgodot_obfuscate || obfuscate_scope) {
					Vector<String> keys;
					ExportContext::make_member_keys(p_class, r_context.script_path, member.function->identifier->name, keys);
					obfuscated_name = get_or_create_context_member_name(r_context, keys);
				}
				if (obfuscated_name.is_empty()) {
					obfuscated_name = make_obfuscated_local_name(r_context);
				}
				r_context.obfuscated_function_names[member.function] = obfuscated_name;
				add_replacement(r_context, member.function->identifier, obfuscated_name);
			} break;
			case GDScriptParser::ClassNode::Member::SIGNAL: {
				if (!should_obfuscate_signal(member.signal, no_mangle_scope, obfuscate_scope) || r_context.obfuscated_signal_names.has(member.signal)) {
					break;
				}

				String obfuscated_name;
				if (member.signal->wgodot_private || obfuscate_scope) {
					Vector<String> keys;
					ExportContext::make_member_keys(p_class, r_context.script_path, member.signal->identifier->name, keys);
					obfuscated_name = get_or_create_context_member_name(r_context, keys);
				}
				if (obfuscated_name.is_empty()) {
					obfuscated_name = make_obfuscated_local_name(r_context);
				}
				r_context.obfuscated_signal_names[member.signal] = obfuscated_name;
				add_replacement(r_context, member.signal->identifier, obfuscated_name);
			} break;
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				if (!should_obfuscate_variable(member.variable, no_mangle_scope, obfuscate_scope) || r_context.obfuscated_variable_names.has(member.variable)) {
					break;
				}

				String obfuscated_name;
				if (member.variable->wgodot_obfuscate || obfuscate_scope) {
					Vector<String> keys;
					ExportContext::make_member_keys(p_class, r_context.script_path, member.variable->identifier->name, keys);
					obfuscated_name = get_or_create_context_member_name(r_context, keys);
				}
				if (obfuscated_name.is_empty()) {
					obfuscated_name = make_obfuscated_local_name(r_context);
				}
				r_context.obfuscated_variable_names[member.variable] = obfuscated_name;
				add_replacement(r_context, member.variable->identifier, obfuscated_name);
			} break;
			default:
				break;
		}
	}
}

void collect_member_name_obfuscation(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope) {
	collect_member_name_obfuscation(r_context, p_class, p_no_mangle_scope, false);
}

void add_class_declaration_name_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (!r_context.options.obfuscate_names ||
			r_context.export_context == nullptr ||
			p_class == nullptr ||
			p_class->outer != nullptr ||
			p_class->identifier == nullptr ||
			p_class->identifier->name.is_empty() ||
			p_class->wgodot_no_mangle) {
		return;
	}

	const StringName *obfuscated_name = r_context.export_context->get_global_class_rename(p_class->identifier->name);
	if (obfuscated_name == nullptr) {
		return;
	}

	add_replacement(r_context, p_class->identifier, String(*obfuscated_name));
}

void add_builtin_class_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	StringName target_name;
	const GDScriptParser::DataType datatype = p_identifier->get_datatype();
	if (p_identifier->source == GDScriptParser::IdentifierNode::NATIVE_CLASS) {
		target_name = datatype.native_type;
		if (target_name.is_empty()) {
			target_name = p_identifier->name;
		}
	} else if (p_identifier->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE && datatype.is_meta_type) {
		if (datatype.kind == GDScriptParser::DataType::BUILTIN) {
			target_name = Variant::get_type_name(datatype.builtin_type);
		} else if (datatype.kind == GDScriptParser::DataType::NATIVE) {
			target_name = datatype.native_type;
		}
	}

	const StringName *alias = r_context.export_context->get_builtin_class_alias(target_name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

bool add_class_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	return p_identifier != nullptr && add_class_member_name_reference_replacement(r_context, p_identifier, p_identifier->get_datatype());
}

bool add_class_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::DataType &p_datatype) {
	if (!r_context.options.obfuscate_names || p_identifier == nullptr) {
		return false;
	}

	const GDScriptParser::ClassNode *class_type = p_datatype.class_type;
	if (class_type == nullptr || class_type->outer == nullptr) {
		return false;
	}

	const String obfuscated_name = get_obfuscated_class_name(r_context, class_type);
	if (obfuscated_name.is_empty()) {
		return false;
	}

	add_replacement(r_context, p_identifier, obfuscated_name);
	return true;
}

void add_global_class_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_names || r_context.export_context == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	if (add_class_member_name_reference_replacement(r_context, p_identifier)) {
		return;
	}

	if (p_identifier->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
		return;
	}

	const StringName *obfuscated_name = r_context.export_context->get_global_class_rename(p_identifier->name);
	if (obfuscated_name == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*obfuscated_name));
}

void add_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_names || p_identifier == nullptr) {
		return;
	}

	if (p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_FUNCTION && p_identifier->function_source != nullptr) {
		const String *obfuscated_name = r_context.obfuscated_function_names.getptr(p_identifier->function_source);
		if (obfuscated_name != nullptr) {
			add_replacement(r_context, p_identifier, *obfuscated_name);
		}
	} else if (p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_SIGNAL && p_identifier->signal_source != nullptr) {
		const String *obfuscated_name = r_context.obfuscated_signal_names.getptr(p_identifier->signal_source);
		if (obfuscated_name != nullptr) {
			add_replacement(r_context, p_identifier, *obfuscated_name);
		}
	} else if ((p_identifier->source == GDScriptParser::IdentifierNode::MEMBER_VARIABLE ||
					   p_identifier->source == GDScriptParser::IdentifierNode::STATIC_VARIABLE ||
					   p_identifier->source == GDScriptParser::IdentifierNode::INHERITED_VARIABLE) &&
			p_identifier->variable_source != nullptr) {
		const String *obfuscated_name = r_context.obfuscated_variable_names.getptr(p_identifier->variable_source);
		if (obfuscated_name != nullptr) {
			add_replacement(r_context, p_identifier, *obfuscated_name);
		}
	}
}

void add_attribute_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier) {
	add_member_name_reference_replacement(r_context, p_identifier);

	if (!r_context.options.obfuscate_names || r_context.export_context == nullptr || p_base == nullptr || p_identifier == nullptr) {
		return;
	}

	const GDScriptParser::DataType base_type = p_base->get_datatype();
	if (is_no_mangle_datatype_class(base_type)) {
		return;
	}
	reserve_datatype_class_member_names(r_context, base_type);

	const String class_key = get_datatype_class_key(base_type);
	const String member_key = ExportContext::make_member_key(class_key, p_identifier->name);
	if (member_key.is_empty()) {
		return;
	}

	String obfuscated_name;
	const String *existing = r_context.export_context->get_member_rename(member_key);
	if (existing != nullptr) {
		obfuscated_name = *existing;
	} else if (datatype_attribute_allows_context_member_obfuscation(base_type, p_identifier->name)) {
		obfuscated_name = r_context.export_context->get_or_create_member_rename(member_key);
	}
	if (obfuscated_name.is_empty()) {
		return;
	}

	add_replacement(r_context, p_identifier, obfuscated_name);
}

void add_call_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_names || r_context.export_context == nullptr || p_call == nullptr || p_call->function_name.is_empty()) {
		return;
	}

	if (p_call->callee != nullptr && p_call->callee->type == GDScriptParser::Node::IDENTIFIER) {
		const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(p_call->callee);
		if (callee->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE &&
				callee->source != GDScriptParser::IdentifierNode::MEMBER_FUNCTION) {
			return;
		}
		if (r_context.current_class == nullptr || !r_context.current_class->has_function(p_call->function_name)) {
			return;
		}

		const GDScriptParser::ClassNode::Member member = r_context.current_class->get_member(p_call->function_name);
		if (member.function == nullptr || member.function->wgodot_no_mangle) {
			return;
		}

		String obfuscated_name;
		if (const String *local_obfuscated_name = r_context.obfuscated_function_names.getptr(member.function)) {
			obfuscated_name = *local_obfuscated_name;
		} else {
			Vector<String> keys;
			ExportContext::make_member_keys(r_context.current_class, r_context.script_path, p_call->function_name, keys);
			for (const String &key : keys) {
				if (const String *context_obfuscated_name = r_context.export_context->get_member_rename(key)) {
					obfuscated_name = *context_obfuscated_name;
					break;
				}
			}
		}

		if (!obfuscated_name.is_empty()) {
			add_replacement(r_context, callee, obfuscated_name);
		}
		return;
	}

	if (p_call->callee == nullptr || p_call->callee->type != GDScriptParser::Node::SUBSCRIPT) {
		return;
	}

	const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute || subscript->base == nullptr || subscript->attribute == nullptr) {
		return;
	}

	const GDScriptParser::DataType base_type = subscript->base->get_datatype();
	if (is_no_mangle_datatype_class(base_type)) {
		return;
	}
	reserve_datatype_class_member_names(r_context, base_type);

	const String class_key = get_datatype_class_key(base_type);
	const String member_key = ExportContext::make_member_key(class_key, p_call->function_name);
	if (member_key.is_empty()) {
		return;
	}

	String obfuscated_name;
	const String *existing = r_context.export_context->get_member_rename(member_key);
	if (existing != nullptr) {
		obfuscated_name = *existing;
	} else if (datatype_function_allows_context_member_obfuscation(base_type, p_call->function_name)) {
		obfuscated_name = r_context.export_context->get_or_create_member_rename(member_key);
	}

	if (obfuscated_name.is_empty()) {
		return;
	}

	add_replacement(r_context, subscript->attribute, obfuscated_name);
}

void add_function_pointer_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_names || p_class == nullptr || p_identifier == nullptr || !p_class->has_function(p_identifier->name)) {
		return;
	}

	const GDScriptParser::FunctionNode *function = p_class->get_member(p_identifier->name).function;
	const String *obfuscated_name = r_context.obfuscated_function_names.getptr(function);
	if (obfuscated_name != nullptr) {
		add_replacement(r_context, p_identifier, *obfuscated_name);
	}
}

void add_local_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_names) {
		return;
	}

	const GDScriptParser::Node *source = get_local_identifier_source(p_identifier);
	if (source == nullptr) {
		return;
	}

	const String *obfuscated_name = r_context.obfuscated_local_names.getptr(source);
	if (obfuscated_name == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, *obfuscated_name);
}

void add_signal_parameter_name_replacements(RewriteContext &r_context, const GDScriptParser::SignalNode *p_signal) {
	if (!r_context.options.obfuscate_names || p_signal == nullptr) {
		return;
	}

	for (const GDScriptParser::ParameterNode *parameter : p_signal->parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr || String(parameter->identifier->name).is_empty()) {
			continue;
		}

		const String obfuscated_name = make_obfuscated_local_name(r_context);
		add_replacement(r_context, parameter->identifier, obfuscated_name);
	}
}

void collect_suite_local_name_obfuscation(RewriteContext &r_context, const GDScriptParser::SuiteNode *p_suite) {
	if (!r_context.options.obfuscate_names || p_suite == nullptr) {
		return;
	}

	for (const GDScriptParser::SuiteNode::Local &local : p_suite->locals) {
		if (!String(local.name).is_empty()) {
			r_context.reserved_obfuscated_names.insert(local.name);
		}
	}

	for (const GDScriptParser::SuiteNode::Local &local : p_suite->locals) {
		if (!should_obfuscate_local(local)) {
			continue;
		}

		const GDScriptParser::Node *declaration = get_local_declaration_node(local);
		const GDScriptParser::IdentifierNode *identifier = get_local_declaration_identifier(local);
		if (declaration == nullptr || identifier == nullptr || String(identifier->name).is_empty()) {
			continue;
		}

		if (r_context.obfuscated_local_names.has(declaration)) {
			continue;
		}

		const String obfuscated_name = make_obfuscated_local_name(r_context);
		r_context.obfuscated_local_names[declaration] = obfuscated_name;
		add_replacement(r_context, identifier, obfuscated_name);
	}
}

} // namespace WGodotGDScriptExportTransform
