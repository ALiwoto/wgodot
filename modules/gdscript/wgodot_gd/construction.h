// wgodot-changes::file
#pragma once

#include "ast.h"

namespace WGodotGDScriptConstruction {

inline bool shared_empty_initializer(const GDScriptParser::VariableNode *p_field) {
	using Parser = GDScriptParser;
	const auto *value = p_field->initializer;
	if (!p_field->type_constraint.wgodot_immutable || !value) {
		return false;
	}
	if (value->type == Parser::Node::ARRAY) {
		return static_cast<const Parser::ArrayNode *>(value)->elements.is_empty();
	}
	if (value->type == Parser::Node::DICTIONARY) {
		return static_cast<const Parser::DictionaryNode *>(value)->elements.is_empty();
	}
	if (value->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(value);
		if (call->get_callee_type() != Parser::Node::IDENTIFIER || Parser::get_builtin_type(call->function_name) != p_field->type_constraint.builtin_type) {
			return false;
		}
		return call->arguments.is_empty() || (call->arguments.size() == 1 && call->arguments[0]->type == Parser::Node::ARRAY &&
				static_cast<const Parser::ArrayNode *>(call->arguments[0])->elements.is_empty());
	}
	return false;
}

inline Vector<const GDScriptParser::VariableNode *> fields(const GDScriptParser::ClassNode *p_class) {
	Vector<const GDScriptParser::VariableNode *> result;
	if (p_class->base_type.class_type) {
		result = fields(p_class->base_type.class_type);
	}
	for (const auto &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::VARIABLE && !member.variable->is_static) {
			result.push_back(member.variable);
		}
	}
	return result;
}

// Empty initialization can be omitted only when no preceding initializer can
// observe that slot. Keep forward reads and calls into the instance conservative.
inline bool can_skip_empty(const GDScriptParser::ClassNode *p_class, const GDScriptParser::VariableNode *p_field) {
	using Parser = GDScriptParser;
	if (!p_field->initializer || p_field->onready) {
		return false;
	}
	class ReadsFields : public WGodotGDScriptAstVisitor {
		bool visit(const Parser::Node *p_node) override {
			if (static_fields && p_node->type == Parser::Node::CALL) {
				const auto *call = static_cast<const Parser::CallNode *>(p_node);
				if (call->get_callee_type() != Parser::Node::IDENTIFIER || Parser::get_builtin_type(call->function_name) == Variant::VARIANT_MAX) {
					return false;
				}
			}
			if (p_node->type == Parser::Node::SELF) {
				return false;
			}
			if (p_node->type == Parser::Node::IDENTIFIER) {
				const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_node);
				if (identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE || identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE ||
						(static_fields && identifier->source == Parser::IdentifierNode::STATIC_VARIABLE) ||
						(identifier->source == Parser::IdentifierNode::MEMBER_FUNCTION && (static_fields || !identifier->function_source_is_static))) {
					return false;
				}
			}
			return true;
		}

	public:
		bool static_fields = false;
	} visitor;
	visitor.static_fields = p_field->is_static;
	Vector<const Parser::VariableNode *> declarations;
	if (p_field->is_static) {
		for (const auto &member : p_class->members) {
			if (member.type == Parser::ClassNode::Member::VARIABLE && member.variable->is_static) {
				declarations.push_back(member.variable);
			}
		}
	} else {
		declarations = fields(p_class);
	}
	for (const auto *field : declarations) {
		if (!visitor.walk(field->initializer)) {
			return false;
		}
		if (field == p_field) {
			return true;
		}
	}
	return false;
}

} // namespace WGodotGDScriptConstruction
