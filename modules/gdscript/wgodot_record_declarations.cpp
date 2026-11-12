// wgodot-changes::file
#include "wgodot_record_declarations.h"
#include "core/object/wgodot_class_references.h"

using Parser = GDScriptParser;

namespace WGodotRecordDeclarations {
namespace {
bool constant_data(const Variant &p_value, int p_depth = 0) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH || p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL) {
		return false;
	}
	if (p_value.get_type() == Variant::OBJECT) {
		return p_value.operator ObjectID() == ObjectID() || WGodotClassReferences::name(p_value) != StringName();
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array values = p_value;
		for (const Variant &value : values) {
			if (!constant_data(value, p_depth + 1)) {
				return false;
			}
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary values = p_value;
		for (const auto &entry : values) {
			if (!constant_data(entry.key, p_depth + 1) || !constant_data(entry.value, p_depth + 1)) {
				return false;
			}
		}
	}
	return true;
}
} // namespace

bool default_literal(const Parser::ExpressionNode *p_expression, Variant &r_value) {
	const auto &type = p_expression->type_constraint;
	if (type.is_meta_type && (type.kind == Parser::DataType::CLASS || type.kind == Parser::DataType::NATIVE)) {
		r_value = type.kind == Parser::DataType::CLASS ? StringName(type.class_type->fqcn) : type.native_type;
		return true;
	}
	if (p_expression->is_constant) {
		r_value = p_expression->reduced_value;
		return constant_data(r_value);
	}
	if (p_expression->type == Parser::Node::ARRAY) {
		Array values;
		for (const auto *element : static_cast<const Parser::ArrayNode *>(p_expression)->elements) {
			Variant value;
			if (!default_literal(element, value)) {
				return false;
			}
			values.push_back(value);
		}
		r_value = values;
		return true;
	}
	if (p_expression->type == Parser::Node::DICTIONARY) {
		Dictionary values;
		for (const auto &entry : static_cast<const Parser::DictionaryNode *>(p_expression)->elements) {
			Variant key;
			Variant value;
			if (!default_literal(entry.key, key) || !default_literal(entry.value, value)) {
				return false;
			}
			values[key] = value;
		}
		r_value = values;
		return true;
	}
	if (p_expression->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(p_expression);
		if (call->get_callee_type() == Parser::Node::IDENTIFIER) {
			const Variant::Type builtin = Parser::get_builtin_type(call->function_name);
			if (builtin == Variant::VARIANT_MAX || builtin == Variant::OBJECT || builtin == Variant::CALLABLE || builtin == Variant::SIGNAL) {
				return false;
			}
			Vector<Variant> arguments;
			Vector<const Variant *> pointers;
			arguments.resize(call->arguments.size());
			pointers.resize(call->arguments.size());
			for (uint32_t index = 0; index < call->arguments.size(); ++index) {
				if (!default_literal(call->arguments[index], arguments.write[index])) {
					return false;
				}
				pointers.write[index] = &arguments[index];
			}
			Callable::CallError error;
			Variant::construct(builtin, r_value, pointers.ptrw(), pointers.size(), error);
			if (error.error == Callable::CallError::CALL_OK && call->type_constraint.wgodot_scalar != WGodotScalar::NONE) {
				r_value = WGodotScalar::narrow(r_value, call->type_constraint.wgodot_scalar);
			}
			return error.error == Callable::CallError::CALL_OK;
		}
		if (call->function_name != SNAME("new") || !call->arguments.is_empty() ||
				call->type_constraint.kind != Parser::DataType::CLASS ||
				!call->type_constraint.class_type->wgodot_record) {
			return false;
		}
		Dictionary fields;
		for (const auto &entry : call->wgodot_field_initializers) {
			Variant value;
			if (!default_literal(entry.value, value)) {
				return false;
			}
			fields[entry.name->name] = value;
		}
		r_value = fields;
		return true;
	}
	return false;
}
} // namespace WGodotRecordDeclarations
