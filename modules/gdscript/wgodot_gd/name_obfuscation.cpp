// wgodot-changes::file
/**************************************************************************/
/*  name_obfuscation.cpp                                                  */
/**************************************************************************/

#include "name_obfuscation.h"

#include "core/error/error_macros.h"

namespace {

String make_short_obfuscated_name(int p_index) {
	static const char *letters = "abcdefghijklmnopqrstuvwxyz";
	const int digit = p_index % 10;
	int group = p_index / 10;

	String prefix;
	do {
		prefix = String::chr(letters[group % 26]) + prefix;
		group = (group / 26) - 1;
	} while (group >= 0);

	return prefix + itos(digit);
}

String make_obfuscated_local_name(WGodotGDScriptExportTransform::RewriteContext &r_context) {
	if (r_context.options.obfuscation_strategy != WGodotGDScriptExportTransform::OBFUSCATION_STRATEGY_SHORT) {
		WARN_PRINT_ONCE("WGodot local variable obfuscation currently only supports the 'short' strategy. Falling back to 'short'.");
	}

	while (true) {
		const String candidate = make_short_obfuscated_name(r_context.obfuscated_local_counter++);
		const StringName candidate_name(candidate);
		if (!r_context.reserved_obfuscated_names.has(candidate_name)) {
			r_context.reserved_obfuscated_names.insert(candidate_name);
			return candidate;
		}
	}
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

bool should_obfuscate_function(const GDScriptParser::FunctionNode *p_function, bool p_no_mangle_scope) {
	return !p_no_mangle_scope &&
			p_function != nullptr &&
			p_function->identifier != nullptr &&
			p_function->wgodot_private &&
			!p_function->wgodot_no_mangle;
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

} // namespace

namespace WGodotGDScriptExportTransform {

void collect_private_function_name_obfuscation(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope) {
	if (!r_context.options.obfuscate_local_variables || p_class == nullptr) {
		return;
	}

	const bool no_mangle_scope = p_no_mangle_scope || p_class->wgodot_no_mangle;
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (!String(member.get_name()).is_empty()) {
			r_context.reserved_obfuscated_names.insert(StringName(member.get_name()));
		}
	}

	if (no_mangle_scope) {
		return;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CLASS:
				collect_private_function_name_obfuscation(r_context, member.m_class, no_mangle_scope);
				break;
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				if (!should_obfuscate_function(member.function, no_mangle_scope) || r_context.obfuscated_function_names.has(member.function)) {
					break;
				}

				const String obfuscated_name = make_obfuscated_local_name(r_context);
				r_context.obfuscated_function_names[member.function] = obfuscated_name;
				add_replacement(r_context, member.function->identifier, obfuscated_name);
			} break;
			default:
				break;
		}
	}
}

void add_private_function_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_local_variables || p_identifier == nullptr) {
		return;
	}

	if (p_identifier->source != GDScriptParser::IdentifierNode::MEMBER_FUNCTION || p_identifier->function_source == nullptr) {
		return;
	}

	const String *obfuscated_name = r_context.obfuscated_function_names.getptr(p_identifier->function_source);
	if (obfuscated_name == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, *obfuscated_name);
}

void add_local_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_local_variables) {
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

void collect_suite_local_name_obfuscation(RewriteContext &r_context, const GDScriptParser::SuiteNode *p_suite) {
	if (!r_context.options.obfuscate_local_variables || p_suite == nullptr) {
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
