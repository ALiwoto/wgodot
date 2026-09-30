// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"
#include "wgodot_cpp_operator_api.gen.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

namespace {
String operator_expression(const String &p_template, const String &p_left, const String &p_right) {
	String result;
	for (int i = 0; i < p_template.length();) {
		const String marker = p_template.substr(i, 6);
		if (marker == "{left}") {
			result += p_left;
			i += 6;
		} else if (p_template.substr(i, 7) == "{right}") {
			result += p_right;
			i += 7;
		} else {
			result += p_template[i++];
		}
	}
	return result;
}
} // namespace

bool WGodotCppEmitter::literal_membership(const Parser::BinaryOpNode *p_binary, Value &r_result) {
	if (p_binary->variant_op != Variant::OP_IN || p_binary->right_operand->type != Parser::Node::ARRAY || expression_overrides.has(p_binary->right_operand)) {
		return false;
	}
	const auto *array = static_cast<const Parser::ArrayNode *>(p_binary->right_operand);
	if (array->type_constraint.has_container_element_type(0)) {
		return false; // Typed containers also validate/convert the searched value.
	}
	const auto comparable = [](Variant::Type p_kind) {
		// Unlike language ==, Array.find distinguishes NIL from an OBJECT with
		// a null pointer. Object handles can contain either, so keep that path.
		// Containers and callbacks also retain their existing implementations.
		return p_kind < Variant::OBJECT;
	};
	const auto left_type = expression_type(p_binary->left_operand);
	const Variant::Type left_kind = native_value_kind(left_type);
	if (!comparable(left_kind)) {
		return false;
	}
	for (const auto *element : array->elements) {
		if (!comparable(native_value_kind(expression_type(element)))) {
			return false;
		}
	}
	r_result.cpp_type = "bool";
	Value left = lower(p_binary->left_operand);
	if (!left.invariant && (!left.borrowed || left.effects || !left.setup.is_empty())) {
		materialize(left, r_result.setup);
	} else {
		r_result.setup.append_array(left.setup);
	}
	Vector<String> comparisons;
	class_call_headers.insert("core/templates/hashfuncs.h");
	for (const auto *element : array->elements) {
		const auto right_type = expression_type(element);
		const Variant::Type right_kind = native_value_kind(right_type);
		Value right = lower_converted(element, right_type);
		if (!right.invariant) {
			// Array construction snapshots each element before evaluating the next.
			// Evaluate every element even if an earlier comparison would succeed.
			right.borrowed = false;
			materialize(right, r_result.setup);
		} else {
			r_result.setup.append_array(right.setup);
		}
		const bool strings = (left_kind == Variant::STRING || left_kind == Variant::STRING_NAME) && (right_kind == Variant::STRING || right_kind == Variant::STRING_NAME);
		if (strings) {
			Parser::DataType boolean;
			boolean.kind = Parser::DataType::BUILTIN;
			boolean.builtin_type = Variant::BOOL;
			comparisons.push_back(operation(Variant::OP_EQUAL, boolean, left_type, right_type, left.code, right.code, p_binary));
		} else if (left_kind == right_kind) {
			if (left_kind == Variant::NIL) {
				comparisons.push_back("true");
			} else {
				// Array.find uses semantic equality: equal NaNs, but distinct numeric
				// kinds remain unequal. Godot's native hash comparators implement it.
				comparisons.push_back("HashMapComparatorDefault<" + type(left_type, p_binary->left_operand) + ">::compare(" + left.code + ", " + right.code + ")");
			}
		}
	}
	r_result.code = comparisons.is_empty() ? "false" : "(" + String(" || ").join(comparisons) + ")";
	return true;
}

String WGodotCppEmitter::operation(Variant::Operator p_operation, const Parser::DataType &p_result, const Parser::DataType &p_left_type, const Parser::DataType &p_right_type, const String &p_left, const String &p_right, const Parser::Node *p_origin) {
	const Variant::Type left_type = native_value_kind(p_left_type);
	const Variant::Type right_type = native_value_kind(p_right_type);
	if ((p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL) &&
			((left_type == Variant::OBJECT && (right_type == Variant::OBJECT || right_type == Variant::NIL)) ||
					(right_type == Variant::OBJECT && left_type == Variant::NIL))) {
		// Godot's language equality uses validated pointers, including for freed
		// objects. Keep handle ownership intact and avoid Variant operator dispatch.
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		const String left = left_type == Variant::NIL ? "((void)(" + p_left + "), nullptr)" : "static_cast<const Object *>(WGodotNative::object_pointer(" + p_left + "))";
		const String right = right_type == Variant::NIL ? "((void)(" + p_right + "), nullptr)" : "static_cast<const Object *>(WGodotNative::object_pointer(" + p_right + "))";
		return "(" + left + (p_operation == Variant::OP_EQUAL ? " == " : " != ") + right + ")";
	}
	if (p_operation == Variant::OP_MODULE && p_left_type.kind == Parser::DataType::BUILTIN && p_left_type.builtin_type == Variant::STRING && p_right_type.kind == Parser::DataType::BUILTIN && p_right_type.builtin_type == Variant::ARRAY) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_format.h");
		return "WGodotNative::format_string(" + p_left + ", " + p_right + (is_warray(p_right_type) ? ")" : ".span())");
	}
	if (WGodotCppSignatures::contains_signature(p_left_type) && p_left_type.builtin_type != Variant::ARRAY && (p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL)) {
		return "(" + p_left + (p_operation == Variant::OP_EQUAL ? " == " : " != ") + p_right + ")";
	}
	if (is_wdictionary(p_right_type) && p_operation == Variant::OP_IN) {
		return "(" + p_right + ").has(WGodotNative::convert<" + type(p_right_type.get_container_element_type(0), p_origin) + ">(" + p_left + "))";
	}
	if (is_wdictionary(p_left_type) || is_wdictionary(p_right_type)) {
		if (is_wdictionary(p_left_type) && is_wdictionary(p_right_type) && type(p_left_type, p_origin) == type(p_right_type, p_origin) && (p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL)) {
			return "(" + p_left + (p_operation == Variant::OP_EQUAL ? " == " : " != ") + p_right + ")";
		}
		unsupported(p_origin, "WDictionary operator " + Variant::get_operator_name(p_operation) + " for these operand types");
		return String();
	}
	if (is_packed(p_right_type) && p_operation == Variant::OP_IN) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "WGodotNative::convert<" + Variant::get_type_name(p_right_type.builtin_type) + ">(" + p_right + ").has(WGodotNative::convert<" + packed_element_type(p_right_type.builtin_type) + ">(" + p_left + "))";
	}
	if (is_packed(p_left_type) && is_packed(p_right_type) && p_left_type.builtin_type == p_right_type.builtin_type) {
		if (p_operation == Variant::OP_ADD || p_operation == Variant::OP_EQUAL || p_operation == Variant::OP_NOT_EQUAL) {
			class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
			const String operand_type = type(p_left_type, p_origin);
			const String symbol = p_operation == Variant::OP_ADD ? " + " : p_operation == Variant::OP_EQUAL ? " == "
																											: " != ";
			return "(WGodotNative::convert<" + operand_type + ">(" + p_left + ")" + symbol + "WGodotNative::convert<" + operand_type + ">(" + p_right + "))";
		}
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
	// The caller has already evaluated both operands in language order. Use the
	// registered signature to expose the corresponding native operation.
	for (const auto &entry : builtin_operators) {
		if (entry.operation == p_operation && entry.left == left_type && entry.right == right_type) {
			class_call_headers.insert("modules/wgodot/native/wgodot_native_builtin.h");
			if (p_operation == Variant::OP_MODULE && (left_type == Variant::STRING || left_type == Variant::STRING_NAME)) {
				class_call_headers.insert("modules/wgodot/native/wgodot_native_format.h");
			}
			return operator_expression(entry.expression, p_left, p_right);
		}
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	return "WGodotNative::evaluate<" + type(p_result, p_origin) + ">(Variant::Operator(" + itos(p_operation) + "), " + p_left + ", " + p_right + ")";
}

String WGodotCppEmitter::cast(const Parser::CastNode *p_cast) {
	const auto &target = p_cast->type_constraint;
	if (is_wdictionary(target) || is_wdictionary(expression_type(p_cast->operand))) {
		return converted(p_cast->operand, target);
	}
	if (is_packed(target) && (p_cast->operand->type == Parser::Node::ARRAY || is_warray(expression_type(p_cast->operand)))) {
		return packed_array(p_cast->operand, target).expression();
	}
	if (is_packed(target) && is_packed(expression_type(p_cast->operand)) && target.builtin_type == expression_type(p_cast->operand).builtin_type) {
		return converted(p_cast->operand, target);
	}
	if (is_warray(target) || is_warray(expression_type(p_cast->operand))) {
		if (is_warray(target) && p_cast->operand->type == Parser::Node::ARRAY) {
			return array_literal(static_cast<const Parser::ArrayNode *>(p_cast->operand), target).expression();
		}
		return converted(p_cast->operand, target);
	}
	if (target.is_variant()) {
		return expression(p_cast->operand);
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	if (is_interface_type(target)) {
		return class_name(target, p_cast) + "::cast(" + expression(p_cast->operand) + ")";
	}
	if (target.kind == Parser::DataType::BUILTIN || target.kind == Parser::DataType::ENUM) {
		const Variant::Type builtin = target.kind == Parser::DataType::ENUM ? Variant::INT : target.builtin_type;
		const Value operand = lower(p_cast->operand);
		const String value = operand.expression();
		const String native = operand.cpp_type == "Variant" ? String() : native_constructor(builtin, { expression_type(p_cast->operand) }, { value });
		if (!native.is_empty()) {
			return native;
		}
		return "WGodotNative::construct<" + type(target, p_cast) + ", " + variant_type(builtin) + ">(" + value + ")";
	}
	const String pointer = "Object::cast_to<" + class_name(target, p_cast) + ">(WGodotNative::object_pointer(" + expression(p_cast->operand) + "))";
	return type(target, p_cast) + "(" + pointer + ")";
}

String WGodotCppEmitter::type_test(const Parser::TypeTestNode *p_test) {
	const auto &target = p_test->test_datatype;
	const String value = receiver_expression(p_test->operand);
	if (is_wdictionary(expression_type(p_test->operand))) {
		const bool same = target.is_variant() || (target.kind == Parser::DataType::BUILTIN && target.builtin_type == Variant::DICTIONARY && (!target.has_container_element_types() || type(target, p_test) == type(expression_type(p_test->operand), p_test)));
		return "([&]() { (void)(" + value + "); return " + (same ? "true" : "false") + "; }())";
	}
	if (is_warray(expression_type(p_test->operand))) {
		const bool same = target.is_variant() || (target.kind == Parser::DataType::BUILTIN && target.builtin_type == Variant::ARRAY && (!target.has_container_element_type(0) || type(target, p_test) == type(expression_type(p_test->operand), p_test)));
		return "([&]() { (void)(" + value + "); return " + (same ? "true" : "false") + "; }())";
	}
	if (target.is_variant()) {
		return "([&]() { (void)(" + value + "); return true; }())";
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	if (is_interface_type(target)) {
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
