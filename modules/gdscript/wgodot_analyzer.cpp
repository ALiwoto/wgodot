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
	return GLOBAL_GET_CACHED(bool, "debug/gdscript/wgodot/strict_override_checking");
}
