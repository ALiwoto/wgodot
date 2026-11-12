// wgodot-changes::file
#pragma once

#include "gdscript_parser.h"

namespace WGodotValuePaths {

// These expressions cannot call user code or change a containing value while
// its storage is borrowed. Other expressions retain ordinary get/set lowering.
inline bool stable(const GDScriptParser::ExpressionNode *p_expression) {
	using Parser = GDScriptParser;
	if (p_expression->is_constant) {
		return true;
	}
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto source = static_cast<const Parser::IdentifierNode *>(p_expression)->source;
		return source == Parser::IdentifierNode::FUNCTION_PARAMETER || source == Parser::IdentifierNode::LOCAL_VARIABLE ||
				source == Parser::IdentifierNode::LOCAL_ITERATOR || source == Parser::IdentifierNode::LOCAL_BIND;
	}
	if (p_expression->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(p_expression);
		const auto builtin = Parser::get_builtin_type(call->function_name);
		if (call->get_callee_type() != Parser::Node::IDENTIFIER || builtin == Variant::VARIANT_MAX || builtin == Variant::OBJECT || builtin == Variant::CALLABLE || builtin == Variant::SIGNAL) {
			return false;
		}
		for (const auto *argument : call->arguments) {
			if (argument->type_constraint.builtin_type == Variant::OBJECT || !stable(argument)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

inline bool owner(const GDScriptParser::DataType &p_type) {
	return (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type->wgodot_record) ||
			(p_type.wgodot_value && (p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY));
}

inline bool plain_data(const GDScriptParser::DataType &p_type, HashSet<const GDScriptParser::ClassNode *> &r_visiting) {
	using Parser = GDScriptParser;
	if (p_type.kind == Parser::DataType::CLASS && p_type.class_type->wgodot_record) {
		if (r_visiting.has(p_type.class_type)) {
			return true;
		}
		r_visiting.insert(p_type.class_type);
		for (const auto &member : p_type.class_type->members) {
			if (member.type == Parser::ClassNode::Member::VARIABLE && !plain_data(member.variable->type_constraint, r_visiting)) {
				return false;
			}
		}
		return true;
	}
	if (p_type.is_variant() || p_type.builtin_type == Variant::OBJECT || p_type.builtin_type == Variant::CALLABLE || p_type.builtin_type == Variant::SIGNAL) {
		return false;
	}
	if ((p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY) && !p_type.has_container_element_types()) {
		return false;
	}
	for (int index = 0; index < p_type.get_container_element_type_count(); ++index) {
		if (!plain_data(p_type.get_container_element_type(index), r_visiting)) {
			return false;
		}
	}
	return true;
}

inline bool plain_data(const GDScriptParser::DataType &p_type) {
	HashSet<const GDScriptParser::ClassNode *> visiting;
	return plain_data(p_type, visiting);
}

} // namespace WGodotValuePaths
