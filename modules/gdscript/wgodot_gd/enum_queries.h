// wgodot-changes::file
#pragma once

#include "modules/gdscript/gdscript_parser.h"

namespace WGodotGDScriptEnumQueries {
inline bool is_values(const GDScriptParser::ExpressionNode *p_expression) {
	if (!p_expression || p_expression->type != GDScriptParser::Node::CALL) {
		return false;
	}
	const auto *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
	if (call->is_super || call->function_name != SNAME("values") || !call->arguments.is_empty() || call->get_callee_type() != GDScriptParser::Node::SUBSCRIPT) {
		return false;
	}
	const auto *receiver = static_cast<const GDScriptParser::SubscriptNode *>(call->callee)->base;
	return receiver && receiver->type_constraint.kind == GDScriptParser::DataType::ENUM && receiver->type_constraint.is_meta_type;
}

inline bool is_membership(const GDScriptParser::DataType &p_type, const StringName &p_method) {
	return p_type.kind == GDScriptParser::DataType::ENUM && p_type.is_meta_type && p_method == SNAME("has_value");
}

inline const GDScriptParser::DataType *membership_type(const GDScriptParser::CallNode *p_call) {
	if (p_call->is_super || p_call->get_callee_type() != GDScriptParser::Node::SUBSCRIPT || p_call->arguments.size() != 1) {
		return nullptr;
	}
	const auto *receiver = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee)->base;
	return receiver && is_membership(receiver->type_constraint, p_call->function_name) ? &receiver->type_constraint : nullptr;
}

// Compilation-time data only. Runtime membership never constructs a container.
inline Vector<int64_t> unique_values(const GDScriptParser::DataType &p_type) {
	Vector<int64_t> values;
	for (const auto &entry : p_type.enum_values) {
		values.push_back(entry.value);
	}
	values.sort();
	int count = 0;
	for (int i = 0; i < values.size(); ++i) {
		if (count == 0 || values[i] != values[count - 1]) {
			values.write[count++] = values[i];
		}
	}
	values.resize(count);
	return values;
}

inline bool is_contiguous(const Vector<int64_t> &p_values) {
	// Unsigned subtraction avoids signed overflow at either int64 boundary.
	return !p_values.is_empty() && uint64_t(p_values[p_values.size() - 1]) - uint64_t(p_values[0]) == uint64_t(p_values.size() - 1);
}
} // namespace WGodotGDScriptEnumQueries
