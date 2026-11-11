// wgodot-changes::file
#include "gdscript_compiler.h"

#include "gdscript.h"
#include "wgodot_gd/ast.h"
#include "wgodot_gd/construction.h"

using Parser = GDScriptParser;
using Address = GDScriptCodeGenerator::Address;

Address GDScriptCompiler::_wgodot_parse_packed_literal(CodeGen &codegen, Error &r_error, const Parser::CallNode *p_call) {
	Address result = codegen.add_assignment_temporary(_gdtype_from_datatype(p_call->type_constraint, codegen.script));
	Vector<Address> elements;
	for (const Parser::ExpressionNode *element : static_cast<const Parser::ArrayNode *>(p_call->arguments[0])->elements) {
		elements.push_back(_parse_expression(codegen, r_error, element));
		if (r_error != OK) {
			return Address();
		}
	}
	codegen.generator->write_construct_packed_literal(result, p_call->type_constraint.builtin_type, elements);
	for (const Address &element : elements) {
		if (element.mode == Address::TEMPORARY) {
			codegen.generator->pop_temporary();
		}
	}
	return result;
}

void GDScriptCompiler::_wgodot_construct_fields(CodeGen &codegen, Error &r_error, const Parser::CallNode *p_call, const Address &p_result, const Vector<Address> &p_arguments) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	Address script = _parse_expression(codegen, r_error, base);
	if (r_error != OK) {
		return;
	}
	// A dependency can still be compiling its member table. The resolved AST
	// already owns the complete base-first field order; never insert missing
	// entries into another script's mutable compilation table.
	const auto declarations = WGodotGDScriptConstruction::fields(p_call->type_constraint.class_type);
	Vector<Address> values;
	Vector<int> fields;
	int temporaries = script.mode == Address::TEMPORARY ? 1 : 0;
	for (const auto &entry : p_call->wgodot_field_initializers) {
		Address value = _parse_expression(codegen, r_error, entry.value);
		if (r_error != OK) {
			return;
		}
		temporaries += value.mode == Address::TEMPORARY ? 1 : 0;
		const GDScriptDataType expected = _gdtype_from_datatype(entry.field->type_constraint, codegen.script);
		if (expected != value.type) {
			Address converted = codegen.add_temporary(expected);
			codegen.generator->write_assign_with_conversion(converted, value);
			value = converted;
			temporaries++;
		} else if (value.mode != Address::TEMPORARY && value.mode != Address::CONSTANT) {
			// Capture each expression before evaluating the next one. A later
			// initializer may rebind a local/member used by an earlier entry.
			Address captured = codegen.add_assignment_temporary(expected);
			codegen.generator->write_assign(captured, value);
			value = captured;
			temporaries++;
		}
		values.push_back(value);
		fields.push_back(declarations.find(entry.field));
	}
	codegen.generator->write_construct_script_fields(p_result, script, p_arguments, fields, values);
	for (int i = 0; i < temporaries; i++) {
		codegen.generator->pop_temporary();
	}
}

bool GDScriptCompiler::_wgodot_can_skip_empty_initializer(const Parser::ClassNode *p_class, const Parser::VariableNode *p_field) const {
	return (p_class->wgodot_data_class || p_field->is_static) && WGodotGDScriptConstruction::can_skip_empty(p_class, p_field);
}

void GDScriptCompiler::_wgodot_write_shared_empty(CodeGen &codegen, const Address &p_target) {
	const auto &type = p_target.type;
	Variant value;
	if (type.builtin_type == Variant::ARRAY) {
		Array array;
		if (type.has_container_element_type(0)) {
			const auto element = type.get_container_element_type(0);
			array.set_typed(element.builtin_type, element.native_type, element.script_type);
		}
		value = array;
	} else if (type.builtin_type == Variant::DICTIONARY) {
		Dictionary dictionary;
		if (type.has_container_element_types()) {
			const auto key = type.get_container_element_type_or_variant(0);
			const auto element = type.get_container_element_type_or_variant(1);
			dictionary.set_typed(key.builtin_type, key.native_type, key.script_type, element.builtin_type, element.native_type, element.script_type);
		}
		value = dictionary;
	} else {
		Callable::CallError call_error;
		Variant::construct(type.builtin_type, value, nullptr, 0, call_error);
	}
	codegen.generator->write_assign(p_target, codegen.add_constant(value));
}
