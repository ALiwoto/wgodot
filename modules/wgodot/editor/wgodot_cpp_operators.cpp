// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::operation(Variant::Operator p_operation, const Parser::DataType &p_result, const Parser::DataType &p_left_type, const Parser::DataType &p_right_type, const String &p_left, const String &p_right, const Parser::Node *p_origin) {
	if (p_operation == Variant::OP_MODULE && p_left_type.kind == Parser::DataType::BUILTIN && p_left_type.builtin_type == Variant::STRING && p_right_type.kind == Parser::DataType::BUILTIN && p_right_type.builtin_type == Variant::ARRAY) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_format.h");
		return "WGodotNative::format_string(" + p_left + ", " + p_right + (is_warray(p_right_type) ? ")" : ".span())");
	}
	if (WGodotCppSignatures::contains_signature(p_left_type) && p_left_type.builtin_type != Variant::ARRAY && (p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL)) {
		return "(" + p_left + (p_operation == Variant::OP_EQUAL ? " == " : " != ") + p_right + ")";
	}
	if (is_warray(p_left_type) || is_warray(p_right_type)) {
		if (p_operation == Variant::OP_IN && is_warray(p_right_type) && !is_warray(p_left_type)) {
			return p_right + ".has(WGodotNative::convert<" + type(p_right_type.get_container_element_type(0), p_origin) + ">(" + p_left + "))";
		}
		if (is_warray(p_left_type) && is_warray(p_right_type) && type(p_left_type, p_origin) == type(p_right_type, p_origin)) {
			if (p_operation == Variant::OP_ADD || p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL) {
				return "(" + p_left + (p_operation == Variant::OP_ADD ? " + " : p_operation == Variant::OP_EQUAL ? " == "
																												 : " != ") +
						p_right + ")";
			}
		}
		unsupported(p_origin, "WArray operator " + Variant::get_operator_name(p_operation) + " for these operand types");
		return String();
	}
	auto numeric = [](const Parser::DataType &p_type) {
		return p_type.kind == Parser::DataType::ENUM || (p_type.kind == Parser::DataType::BUILTIN && (p_type.builtin_type == Variant::INT || p_type.builtin_type == Variant::FLOAT));
	};
	// The caller has already evaluated both operands in language order. Keep the
	// common numeric operations directly visible to the native optimizer.
	if (numeric(p_left_type) && numeric(p_right_type)) {
		String symbol;
		switch (p_operation) {
			case Variant::OP_ADD:
				symbol = "+";
				break;
			case Variant::OP_SUBTRACT:
				symbol = "-";
				break;
			case Variant::OP_MULTIPLY:
				symbol = "*";
				break;
			case Variant::OP_EQUAL:
				symbol = "==";
				break;
			case Variant::OP_NOT_EQUAL:
				symbol = "!=";
				break;
			case Variant::OP_LESS:
				symbol = "<";
				break;
			case Variant::OP_LESS_EQUAL:
				symbol = "<=";
				break;
			case Variant::OP_GREATER:
				symbol = ">";
				break;
			case Variant::OP_GREATER_EQUAL:
				symbol = ">=";
				break;
			default:
				break;
		}
		if (!symbol.is_empty()) {
			return "(" + p_left + " " + symbol + " " + p_right + ")";
		}
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	return "WGodotNative::evaluate<" + type(p_result, p_origin) + ">(Variant::Operator(" + itos(p_operation) + "), " + p_left + ", " + p_right + ")";
}

String WGodotCppEmitter::cast(const Parser::CastNode *p_cast) {
	const auto &target = p_cast->type_constraint;
	if (is_warray(target) || is_warray(expression_type(p_cast->operand))) {
		if (is_warray(target) && p_cast->operand->type == Parser::Node::ARRAY) {
			return array_literal(static_cast<const Parser::ArrayNode *>(p_cast->operand), target);
		}
		return converted(p_cast->operand, target);
	}
	if (target.is_variant()) {
		return expression(p_cast->operand);
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	if (target.kind == Parser::DataType::CLASS && target.class_type->wgodot_is_interface) {
		return class_name(target, p_cast) + "::cast(" + expression(p_cast->operand) + ")";
	}
	if (target.kind == Parser::DataType::BUILTIN || target.kind == Parser::DataType::ENUM) {
		const Variant::Type builtin = target.kind == Parser::DataType::ENUM ? Variant::INT : target.builtin_type;
		return "WGodotNative::construct<" + type(target, p_cast) + ", " + variant_type(builtin) + ">(" + expression(p_cast->operand) + ")";
	}
	const String pointer = "Object::cast_to<" + class_name(target, p_cast) + ">(WGodotNative::object_pointer(" + expression(p_cast->operand) + "))";
	return type(target, p_cast) + "(" + pointer + ")";
}

String WGodotCppEmitter::type_test(const Parser::TypeTestNode *p_test) {
	const auto &target = p_test->test_datatype;
	const String value = receiver_expression(p_test->operand);
	if (is_warray(expression_type(p_test->operand))) {
		const bool same = target.is_variant() || (target.kind == Parser::DataType::BUILTIN && target.builtin_type == Variant::ARRAY && (!target.has_container_element_type(0) || type(target, p_test) == type(expression_type(p_test->operand), p_test)));
		return "([&]() { (void)(" + value + "); return " + (same ? "true" : "false") + "; }())";
	}
	if (target.is_variant()) {
		return "([&]() { (void)(" + value + "); return true; }())";
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	if (target.kind == Parser::DataType::CLASS && target.class_type->wgodot_is_interface) {
		return "([&]() { const Variant &value = " + value + "; return value.get_validated_object() && " + class_name(target, p_test) + "::accepts(value); }())";
	}
	if (target.kind == Parser::DataType::CLASS || target.kind == Parser::DataType::NATIVE) {
		return "(Object::cast_to<" + class_name(target, p_test) + ">(WGodotNative::object_pointer(" + value + ")) != nullptr)";
	}
	const Variant::Type builtin = target.kind == Parser::DataType::ENUM ? Variant::INT : target.builtin_type;
	const String test = "value.get_type() == " + variant_type(builtin);
	String body = "([&]() { const Variant &value = " + value + "; ";
	if (!target.has_container_element_types()) {
		return body + "return " + test + "; }())";
	}
	const bool array = builtin == Variant::ARRAY;
	body += "if (!(" + test + ")) { return false; } const " + String(array ? "Array" : "Dictionary") + " collection = value; return ";
	Vector<String> conditions;
	for (int i = 0; i < (array ? 1 : 2); i++) {
		const auto &element = target.get_container_element_type_or_variant(i);
		const bool object = element.kind == Parser::DataType::CLASS || element.kind == Parser::DataType::NATIVE;
		const Variant::Type element_type = element.is_variant() ? Variant::NIL : object ? Variant::OBJECT
				: element.kind == Parser::DataType::ENUM								? Variant::INT
																						: element.builtin_type;
		const String prefix = String("collection.get_typed_") + (array ? "" : i == 0 ? "key_"
																					 : "value_");
		conditions.push_back(prefix + "builtin() == " + variant_type(element_type));
		conditions.push_back(prefix + "class_name() == " + (object ? class_name(element, p_test) + "::get_class_static()" : "StringName()"));
		conditions.push_back(prefix + "script().is_null()");
	}
	return body + String(" && ").join(conditions) + "; }())";
}
