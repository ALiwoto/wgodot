// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "wgodot_cpp_names.h"
#include "wgodot_cpp_native_headers.gen.h"

#include "core/config/engine.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource.h"
#include "core/object/class_db.h"
#include "core/string/string_builder.h"

#include <charconv>

using Parser = GDScriptParser;
using namespace WGodotCppNames;

WGodotCppEmitter::WGodotCppEmitter(const WGodotCppProject &p_project) : project(p_project) {
	for (const auto &entry : native_cpp_headers) {
		native_headers.insert(entry.name, entry.header);
		native_cpp_names.insert(entry.name, entry.cpp_type);
	}
	initialize_native_methods();
}

void WGodotCppEmitter::unsupported(const Parser::Node *p_node, const String &p_feature) {
	if (!function_failed) {
		diagnostics.push_back(vformat("%s:%d:%d: C++ export does not yet implement %s", current_class->script_path, p_node->start_line, p_node->start_column, p_feature));
	}
	function_failed = true;
}

String WGodotCppEmitter::type(const Parser::DataType &p_type, const Parser::Node *p_origin) {
	if (p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type == Variant::ARRAY && !p_type.has_container_element_type(0)) {
		if (p_origin->type == Parser::Node::VARIABLE) {
			const auto resolved = variable_type(static_cast<const Parser::VariableNode *>(p_origin));
			if (is_warray(resolved)) {
				return type(resolved, p_origin);
			}
		} else if (p_origin->is_expression()) {
			const auto resolved = expression_type(static_cast<const Parser::ExpressionNode *>(p_origin));
			if (is_warray(resolved)) {
				return type(resolved, p_origin);
			}
		}
	}
	if (p_type.is_variant() || p_type.is_coroutine) {
		return "Variant";
	}
	if (p_type.kind == Parser::DataType::ENUM) {
		return "int64_t";
	}
	if (p_type.kind == Parser::DataType::BUILTIN) {
		if (p_type.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY) {
			class_native_headers.insert("modules/wgodot/native/wgodot_native_packed.h");
			return "WGodotNative::Packed<" + Variant::get_type_name(p_type.builtin_type) + ">";
		}
		if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
			const auto &element = p_type.get_container_element_type(0);
			if (element.is_variant()) {
				unsupported(p_origin, "WArray elements without a concrete type");
				return "Variant";
			}
			if (element.kind == Parser::DataType::BUILTIN && element.builtin_type == Variant::ARRAY) {
				unsupported(p_origin, "nested Array elements; WArray currently requires a non-Array element type");
				return "Variant";
			}
			class_native_headers.insert("modules/wgodot/native/wgodot_native_warray.h");
			return "WGodotNative::WArray<" + type(element, p_origin) + ">";
		}
		if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_type(0) && p_type.has_container_element_type(1)) {
			Vector<String> elements;
			for (int i = 0; i < 2; i++) {
				const auto &element = p_type.get_container_element_type(i);
				if (is_warray(element)) {
					unsupported(p_origin, "WArray stored inside a Godot Dictionary; this requires an explicit container boundary");
				}
				elements.push_back(element.kind == Parser::DataType::CLASS || element.kind == Parser::DataType::NATIVE ? class_name(element, p_origin) : type(element, p_origin));
			}
			return "TypedDictionary<" + String(", ").join(elements) + ">";
		}
		switch (p_type.builtin_type) {
			case Variant::NIL:
				return "void";
			case Variant::BOOL:
				return "bool";
			case Variant::INT:
				return "int64_t";
			case Variant::FLOAT:
				return "double";
			default:
				return Variant::get_type_name(p_type.builtin_type);
		}
	}
	const String name = class_name(p_type, p_origin);
	if (p_type.kind == Parser::DataType::CLASS && p_type.class_type->wgodot_is_interface) {
		return name;
	}
	if (ClassDB::is_parent_class(native_base(p_type), "RefCounted")) {
		return "Ref<" + name + ">";
	}
	class_native_headers.insert("modules/wgodot/native/wgodot_native_object.h");
	return "WGodotNative::ObjectValue<" + name + ">";
}

StringName WGodotCppEmitter::native_base(const Parser::DataType &p_type) const {
	return p_type.kind == Parser::DataType::CLASS ? native_base(p_type.class_type->base_type) : p_type.native_type;
}

String WGodotCppEmitter::class_name(const Parser::DataType &p_type, const Parser::Node *p_origin) {
	if (p_type.kind == Parser::DataType::CLASS) {
		const WGodotCppProject::Class *entry = project.find_class(p_type.class_type);
		if (!entry) {
			unsupported(p_origin, "external script type " + p_type.to_string());
			return "Variant";
		}
		class_dependencies.insert(entry->cpp_name);
		if (p_type.class_type->wgodot_is_interface) {
			class_native_headers.insert(entry->cpp_name + ".h");
		}
		return entry->cpp_name;
	} else if (p_type.kind == Parser::DataType::NATIVE) {
		const String *header = native_headers.getptr(p_type.native_type);
		if (!header) {
			unsupported(p_origin, "native C++ type mapping for " + String(p_type.native_type));
			return "Variant";
		}
		used_native_headers.insert(*header);
		class_native_headers.insert(*header);
		return native_cpp_names[p_type.native_type];
	} else {
		unsupported(p_origin, "type " + p_type.to_string());
		return "Variant";
	}
}

String WGodotCppEmitter::converted(const Parser::ExpressionNode *p_expression, const Parser::DataType &p_target) {
	const String target = type(p_target, p_expression);
	if (p_expression->type == Parser::Node::ARRAY && is_warray(p_target)) {
		return array_literal(static_cast<const Parser::ArrayNode *>(p_expression), p_target);
	}
	if (!validate_array_conversion(p_expression, p_target)) {
		return String();
	}
	if (p_expression->is_constant && p_expression->reduced && p_expression->reduced_value.get_type() == Variant::NIL) {
		if (p_target.kind == Parser::DataType::CLASS || p_target.kind == Parser::DataType::NATIVE) {
			return target + "()";
		}
		return "Variant()";
	}
	const String value = expression(p_expression);
	if (p_target.is_variant()) {
		return value;
	}
	// Flow analysis can narrow an object expression without changing the C++
	// storage type. Let the native helper preserve exact types or perform the cast.
	if (p_target.kind == Parser::DataType::CLASS || p_target.kind == Parser::DataType::NATIVE || p_expression->type_constraint.is_variant() || type(p_expression->type_constraint, p_expression) != target) {
		class_native_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "WGodotNative::convert<" + target + ">(" + value + ")";
	}
	return value;
}

String WGodotCppEmitter::truth(const Parser::ExpressionNode *p_expression) {
	if (p_expression->type == Parser::Node::SELF) {
		return "true";
	}
	const String value = expression(p_expression);
	const auto datatype = expression_type(p_expression);
	if (is_warray(datatype)) {
		return "!(" + value + ").is_empty()";
	}
	if (datatype.kind == Parser::DataType::CLASS || datatype.kind == Parser::DataType::NATIVE) {
		return ClassDB::is_parent_class(native_base(datatype), "RefCounted") ? "(" + value + ").is_valid()" : "Variant(" + value + ").booleanize()";
	}
	if (datatype.kind == Parser::DataType::BUILTIN && datatype.builtin_type == Variant::BOOL) {
		return value;
	}
	return "Variant(" + value + ").booleanize()";
}

const WGodotCppProject::Class *WGodotCppEmitter::member_owner(const Parser::ClassNode *p_class, const StringName &p_name) const {
	if (p_class->has_member(p_name)) {
		return project.find_class(p_class);
	}
	return p_class->base_type.kind == Parser::DataType::CLASS ? member_owner(p_class->base_type.class_type, p_name) : nullptr;
}

String WGodotCppEmitter::member(const Parser::ExpressionNode *p_base, const StringName &p_name, const Parser::ExpressionNode *p_origin) {
	const auto datatype = p_base ? expression_type(p_base) : current_class->node->self_type;
	if (datatype.kind == Parser::DataType::BUILTIN && Variant::has_builtin_method(datatype.builtin_type, p_name)) {
		if (is_warray(datatype)) {
			unsupported(p_origin, "WArray method references through Callable; call the typed method directly");
			return String();
		}
		if (!validate_builtin_arguments(datatype.builtin_type, p_name, p_origin)) {
			return String();
		}
		return "Callable::create(" + expression(p_base) + ", SNAME(" + quoted(p_name) + "))";
	}
	if (datatype.kind == Parser::DataType::BUILTIN && Variant::has_member(datatype.builtin_type, p_name)) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
		return "WGodotNative::get_member<" + type(p_origin->type_constraint, p_origin) + ">(" + expression(p_base) + ", SNAME(" + quoted(p_name) + "))";
	}
	if (datatype.kind != Parser::DataType::CLASS) {
		return native_property(p_base, p_name, p_origin);
	}
	const auto *owner = member_owner(datatype.class_type, p_name);
	if (!owner) {
		return native_property(p_base, p_name, p_origin);
	}
	class_dependencies.insert(owner->cpp_name);
	const auto entry = owner->node->get_member(p_name);
	if (entry.type == Parser::ClassNode::Member::SIGNAL) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "Signal(WGodotNative::object_pointer(" + (p_base ? expression(p_base) : "this") + "), SNAME(" + quoted(p_name) + "))";
	}
	if (entry.type == Parser::ClassNode::Member::FUNCTION) {
		if (has_warray_signature(entry.function)) {
			unsupported(p_origin, "method reference " + String(p_name) + " with a WArray signature through Callable");
			return String();
		}
		if (entry.function->is_static) {
			class_call_headers.insert("modules/wgodot/native/wgodot_native_callable.h");
			Vector<String> defaults;
			for (const auto *parameter : entry.function->parameters) {
				if (parameter->initializer) {
					defaults.push_back(expression(parameter->initializer));
				}
			}
			return "WGodotNative::static_callable(&" + owner->cpp_name + "::m_" + symbol(p_name) + ", SNAME(" + quoted(p_name) + "), {" + String(", ").join(defaults) + "})";
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "Callable(WGodotNative::object_pointer(" + (p_base ? expression(p_base) : "this") + "), SNAME(" + quoted(p_name) + "))";
	}
	if (entry.type == Parser::ClassNode::Member::CONSTANT) {
		return expression(entry.constant->initializer);
	}
	if (entry.type == Parser::ClassNode::Member::VARIABLE) {
		if (entry.variable->is_static && p_base && !datatype.is_meta_type) {
			return "([&]() { (void)(" + expression(p_base) + "); return " + property_access(datatype, p_name, p_origin, "") + "; }())";
		}
		return property_access(datatype, p_name, p_origin, p_base ? expression(p_base) : "this");
	}
	unsupported(p_origin, "member " + String(p_name));
	return String();
}

String WGodotCppEmitter::literal(const Variant &p_value, const Parser::Node *p_origin) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return "Variant()";
		case Variant::BOOL:
			return bool(p_value) ? "true" : "false";
		case Variant::INT:
			if (int64_t(p_value) == INT64_MIN) {
				return "(-int64_t(9223372036854775807) - int64_t(1))";
			}
			return "int64_t(" + itos(p_value) + ")";
		case Variant::FLOAT: {
			const double value = p_value;
			if (Math::is_nan(value)) {
				return "Math::NaN";
			}
			if (Math::is_inf(value)) {
				return value < 0 ? "-Math::INF" : "Math::INF";
			}
			char buffer[64];
			const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), value);
			String number = String::utf8(buffer, conversion.ptr - buffer);
			if (!number.contains(".") && !number.contains("e") && !number.contains("E")) {
				number += ".0";
			}
			return number;
		}
		case Variant::STRING:
			return "String::utf8(" + quoted(p_value) + ")";
		case Variant::STRING_NAME:
			return "StringName(String::utf8(" + quoted(p_value) + "))";
		case Variant::NODE_PATH:
			return "NodePath(String::utf8(" + quoted(p_value) + "))";
		case Variant::OBJECT: {
			Object *object = p_value;
			if (!object) {
				return "static_cast<Object *>(nullptr)";
			}
			const Resource *resource = Object::cast_to<Resource>(object);
			if (!resource || resource->get_path().is_empty() || Object::cast_to<Script>(object)) {
				unsupported(p_origin, "object constant without an exported resource path");
				return String();
			}
			Parser::DataType resource_type;
			resource_type.kind = Parser::DataType::NATIVE;
			resource_type.native_type = resource->get_class_name();
			class_resource_types.insert(resource->get_path(), type(resource_type, p_origin));
			return current_class->cpp_name + "::static_fields().resource_" + resource->get_path().sha256_text().substr(0, 16);
		}
		case Variant::CALLABLE:
			if (Callable(p_value).is_null()) {
				return "Callable()";
			}
			unsupported(p_origin, "nonempty callable constant");
			return String();
		case Variant::SIGNAL:
			if (Signal(p_value).is_null()) {
				return "Signal()";
			}
			unsupported(p_origin, "nonempty signal constant");
			return String();
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::COLOR: {
			const Variant::Type value_type = p_value.get_type();
			const int count = value_type == Variant::VECTOR2 || value_type == Variant::VECTOR2I ? 2 : value_type == Variant::VECTOR3 || value_type == Variant::VECTOR3I ? 3
																																										: 4;
			Vector<String> components;
			for (int i = 0; i < count; i++) {
				bool valid;
				bool out_of_bounds;
				components.push_back(literal(p_value.get_indexed(i, valid, out_of_bounds), p_origin));
			}
			return Variant::get_type_name(value_type) + "(" + String(", ").join(components) + ")";
		}
		case Variant::RECT2: {
			const Rect2 value = p_value;
			return "Rect2(" + literal(value.position, p_origin) + ", " + literal(value.size, p_origin) + ")";
		}
		case Variant::RECT2I: {
			const Rect2i value = p_value;
			return "Rect2i(" + literal(value.position, p_origin) + ", " + literal(value.size, p_origin) + ")";
		}
		case Variant::TRANSFORM2D: {
			const Transform2D value = p_value;
			return "Transform2D(" + literal(value[0], p_origin) + ", " + literal(value[1], p_origin) + ", " + literal(value[2], p_origin) + ")";
		}
		case Variant::BASIS: {
			const Basis value = p_value;
			return "Basis(" + literal(value.get_column(0), p_origin) + ", " + literal(value.get_column(1), p_origin) + ", " + literal(value.get_column(2), p_origin) + ")";
		}
		case Variant::TRANSFORM3D: {
			const Transform3D value = p_value;
			return "Transform3D(" + literal(value.basis, p_origin) + ", " + literal(value.origin, p_origin) + ")";
		}
		case Variant::DICTIONARY:
			if (Dictionary(p_value).is_empty()) {
				return "Dictionary()";
			}
			unsupported(p_origin, "dictionary constant");
			return String();
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
		case Variant::PACKED_COLOR_ARRAY: {
			Vector<String> elements;
			const uint64_t size = p_value.get_type() == Variant::ARRAY ? uint64_t(Array(p_value).size()) : p_value.get_indexed_size();
			for (uint64_t i = 0; i < size; i++) {
				bool valid;
				bool out_of_bounds;
				elements.push_back(literal(p_value.get_indexed(i, valid, out_of_bounds), p_origin));
			}
			return Variant::get_type_name(p_value.get_type()) + "{" + String(", ").join(elements) + "}";
		}
		default:
			unsupported(p_origin, "constant of type " + Variant::get_type_name(p_value.get_type()));
			return String();
	}
}

const Parser::Node *WGodotCppEmitter::local_source(const Parser::IdentifierNode *p_identifier) const {
	switch (p_identifier->source) {
		case Parser::IdentifierNode::FUNCTION_PARAMETER:
			return p_identifier->parameter_source;
		case Parser::IdentifierNode::LOCAL_VARIABLE:
			return p_identifier->variable_source;
		case Parser::IdentifierNode::LOCAL_ITERATOR:
		case Parser::IdentifierNode::LOCAL_BIND:
			return p_identifier->bind_source;
		default:
			return nullptr;
	}
}

String WGodotCppEmitter::expression(const Parser::ExpressionNode *p_expression) {
	if (!p_expression) {
		return String();
	}
	if (const String *replacement = expression_overrides.getptr(p_expression)) {
		return *replacement;
	}
	if (p_expression->type_constraint.kind == Parser::DataType::CLASS || p_expression->type_constraint.kind == Parser::DataType::NATIVE) {
		// A receiver can come from another class's field or method without a local
		// declaration. Its complete type is still needed for native calls/casts.
		(void)class_name(p_expression->type_constraint, p_expression);
	}
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		if ((identifier->source == Parser::IdentifierNode::UNDEFINED_SOURCE || identifier->source == Parser::IdentifierNode::NATIVE_CLASS) && identifier->type_constraint.kind == Parser::DataType::NATIVE && Engine::get_singleton()->has_singleton(identifier->name)) {
			class_native_headers.insert("core/config/engine.h");
			return "Object::cast_to<" + class_name(identifier->type_constraint, identifier) + ">(Engine::get_singleton()->get_singleton_object(SNAME(" + quoted(identifier->name) + ")))";
		}
	}
	if (p_expression->is_constant && p_expression->reduced && !p_expression->type_constraint.is_meta_type && p_expression->type != Parser::Node::ARRAY && p_expression->type != Parser::Node::DICTIONARY) {
		if (is_warray(p_expression->type_constraint) && p_expression->reduced_value.get_type() == Variant::ARRAY) {
			const Array values = p_expression->reduced_value;
			Vector<String> elements;
			const String element_type = type(p_expression->type_constraint.get_container_element_type(0), p_expression);
			for (int i = 0; i < values.size(); i++) {
				elements.push_back("WGodotNative::convert<" + element_type + ">(" + literal(values[i], p_expression) + ")");
			}
			class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
			const String value = type(p_expression->type_constraint, p_expression) + "{" + String(", ").join(elements) + "}";
			if (p_expression->type == Parser::Node::IDENTIFIER || p_expression->type == Parser::Node::SUBSCRIPT) {
				return "([&]() { auto value = " + value + "; value.make_read_only(); return value; }())";
			}
			return value;
		}
		return literal(p_expression->reduced_value, p_expression);
	}
	switch (p_expression->type) {
		case Parser::Node::LAMBDA:
			return lambda(static_cast<const Parser::LambdaNode *>(p_expression));
		case Parser::Node::SELF:
			return "this";
		case Parser::Node::LITERAL:
			return literal(static_cast<const Parser::LiteralNode *>(p_expression)->value, p_expression);
		case Parser::Node::IDENTIFIER: {
			const Parser::IdentifierNode *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
			if (const String *replacement = local_overrides.getptr(local_source(identifier))) {
				return *replacement;
			}
			if (identifier->type_constraint.is_meta_type && identifier->type_constraint.kind == Parser::DataType::CLASS) {
				return class_name(identifier->type_constraint, identifier);
			}
			switch (identifier->source) {
				case Parser::IdentifierNode::FUNCTION_PARAMETER:
				case Parser::IdentifierNode::LOCAL_VARIABLE:
				case Parser::IdentifierNode::LOCAL_ITERATOR:
				case Parser::IdentifierNode::LOCAL_BIND:
					return "v_" + symbol(identifier->name);
				case Parser::IdentifierNode::MEMBER_VARIABLE:
				case Parser::IdentifierNode::INHERITED_VARIABLE:
				case Parser::IdentifierNode::STATIC_VARIABLE:
				case Parser::IdentifierNode::MEMBER_SIGNAL:
				case Parser::IdentifierNode::MEMBER_FUNCTION:
					return member(nullptr, identifier->name, identifier);
				case Parser::IdentifierNode::LOCAL_CONSTANT:
				case Parser::IdentifierNode::MEMBER_CONSTANT:
					return expression(identifier->constant_source->initializer);
				default:
					unsupported(p_expression, "identifier " + String(identifier->name));
					return String();
			}
		}
		case Parser::Node::BINARY_OPERATOR: {
			const auto *binary = static_cast<const Parser::BinaryOpNode *>(p_expression);
			if (binary->operation == Parser::BinaryOpNode::OP_LOGIC_AND || binary->operation == Parser::BinaryOpNode::OP_LOGIC_OR) {
				return "(" + truth(binary->left_operand) + (binary->operation == Parser::BinaryOpNode::OP_LOGIC_AND ? " && " : " || ") + truth(binary->right_operand) + ")";
			}
			if (binary->variant_op == Variant::OP_MODULE && binary->left_operand->type_constraint.kind == Parser::DataType::BUILTIN && binary->left_operand->type_constraint.builtin_type == Variant::STRING && binary->right_operand->type == Parser::Node::ARRAY && !expression_overrides.has(binary->right_operand)) {
				class_call_headers.insert("modules/wgodot/native/wgodot_native_format.h");
				const auto *array = static_cast<const Parser::ArrayNode *>(binary->right_operand);
				Vector<String> arguments;
				for (const auto *element : array->elements) {
					arguments.push_back("Variant(" + engine_argument(element, Variant::NIL) + ")");
				}
				// Initializer-list elements are evaluated and captured in order. Mixed
				// format arguments need Variants, but no heap-allocated Godot Array.
				return "([&]() -> String { auto &&format = " + expression(binary->left_operand) + "; const std::array<Variant, " + itos(array->elements.size()) + "> arguments{ " + String(", ").join(arguments) + " }; return WGodotNative::format_string(format, Span<Variant>(arguments.data(), arguments.size())); }())";
			}
			auto left_type = expression_type(binary->left_operand);
			auto right_type = expression_type(binary->right_operand);
			if (is_warray(left_type) && binary->right_operand->type == Parser::Node::ARRAY) {
				right_type = left_type;
			} else if (is_warray(right_type) && binary->left_operand->type == Parser::Node::ARRAY) {
				left_type = right_type;
			}
			const auto result_type = expression_type(binary);
			return "([&]() -> " + type(result_type, binary) + " { auto &&left = " + converted(binary->left_operand, left_type) + "; auto &&right = " + converted(binary->right_operand, right_type) + "; return " + operation(binary->variant_op, result_type, left_type, right_type, "left", "right", binary) + "; }())";
		}
		case Parser::Node::UNARY_OPERATOR: {
			const auto *unary = static_cast<const Parser::UnaryOpNode *>(p_expression);
			if (unary->operation == Parser::UnaryOpNode::OP_LOGIC_NOT) {
				return "(!" + truth(unary->operand) + ")";
			}
			class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
			return "WGodotNative::evaluate<" + type(unary->type_constraint, unary) + ">(Variant::Operator(" + itos(unary->variant_op) + "), " + expression(unary->operand) + ", Variant())";
		}
		case Parser::Node::TERNARY_OPERATOR: {
			const auto *ternary = static_cast<const Parser::TernaryOpNode *>(p_expression);
			const auto datatype = expression_type(ternary);
			return "(" + truth(ternary->condition) + " ? " + converted(ternary->true_expr, datatype) + " : " + converted(ternary->false_expr, datatype) + ")";
		}
		case Parser::Node::CAST:
			return cast(static_cast<const Parser::CastNode *>(p_expression));
		case Parser::Node::TYPE_TEST:
			return type_test(static_cast<const Parser::TypeTestNode *>(p_expression));
		case Parser::Node::ASSIGNMENT:
			return assignment(static_cast<const Parser::AssignmentNode *>(p_expression));
		case Parser::Node::SUBSCRIPT: {
			const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute) {
				return member(subscript->base, subscript->attribute->name, subscript);
			}
			class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
			return "([&]() { auto &&base = " + expression(subscript->base) + "; auto &&index = " + expression(subscript->index) + "; return WGodotNative::get_index<" + type(subscript->type_constraint, subscript) + ">(base, index); }())";
		}
		case Parser::Node::ARRAY: {
			const auto *array = static_cast<const Parser::ArrayNode *>(p_expression);
			if (is_warray(array->type_constraint)) {
				return array_literal(array, array->type_constraint);
			}
			String body = "([&]() { ";
			Vector<String> elements;
			for (const auto *element : array->elements) {
				const String name = "element_" + itos(elements.size());
				body += "auto &&" + name + " = " + engine_argument(element, Variant::NIL) + "; ";
				elements.push_back(name);
			}
			return body + "return " + type(array->type_constraint, array) + "{" + String(", ").join(elements) + "}; }())";
		}
		case Parser::Node::DICTIONARY: {
			const auto *dictionary = static_cast<const Parser::DictionaryNode *>(p_expression);
			String body = "([&]() { ";
			String entries;
			int index = 0;
			for (const auto &element : dictionary->elements) {
				const String suffix = itos(index++);
				body += "auto &&key_" + suffix + " = " + engine_argument(element.key, Variant::NIL) + "; auto &&value_" + suffix + " = " + engine_argument(element.value, Variant::NIL) + "; ";
				entries += "dictionary[key_" + suffix + "] = value_" + suffix + "; ";
			}
			return body + type(dictionary->type_constraint, dictionary) + " dictionary; " + entries + "return dictionary; }())";
		}
		case Parser::Node::CALL: {
			const auto *call = static_cast<const Parser::CallNode *>(p_expression);
			if (call->is_super) {
				return this->call(call);
			}
			if (call->get_callee_type() == Parser::Node::SUBSCRIPT) {
				const auto &base_type = static_cast<const Parser::SubscriptNode *>(call->callee)->base->type_constraint;
				if (base_type.kind == Parser::DataType::CLASS || base_type.kind == Parser::DataType::NATIVE) {
					return this->call(call);
				}
				return builtin_call(call);
			}
			if (call->get_callee_type() == Parser::Node::IDENTIFIER) {
				const auto *owner = member_owner(current_class->node, call->function_name);
				if ((owner && owner->node->get_member(call->function_name).type == Parser::ClassNode::Member::FUNCTION) || ClassDB::has_method(native_base(current_class->node->self_type), call->function_name)) {
					return this->call(call);
				}
				return global_call(call);
			}
			unsupported(p_expression, "call " + String(call->function_name));
			return String();
		}
		default:
			unsupported(p_expression, "expression kind " + itos(p_expression->type));
			return String();
	}
}

Error WGodotCppEmitter::generate() {
	files.clear();
	diagnostics.clear();
	used_native_headers.clear();
	for (const WGodotCppProject::Class &entry : project.get_classes()) {
		emit_class(entry);
	}
	if (!diagnostics.is_empty()) {
		return ERR_UNAVAILABLE;
	}
	files.insert("game_types.h", "// wgodot-changes::file\n#pragma once\n#include \"modules/wgodot/native/wgodot_native_support.h\"\n#include \"core/object/ref_counted.h\"\n#include \"core/variant/variant_caster.h\"\n#include \"core/variant/typed_array.h\"\n#include \"core/variant/typed_dictionary.h\"\n");
	files.insert("SCsub", "# wgodot-changes::file\nImport('env')\nImport('env_modules')\nenv_game = env_modules.Clone()\nenv_game.add_source_files(env.modules_sources, '*.cpp')\nenv_game.add_source_files(env.modules_sources, [File('#modules/wgodot/native/wgodot_native_task.cpp')])\n");
	files.insert("config.py", "# wgodot-changes::file\ndef can_build(env, platform):\n    return not env.editor_build\n\ndef configure(env):\n    env.AppendUnique(CPPDEFINES=['WGODOT_NATIVE_GAME'])\n");
	files.insert("register_types.h", "// wgodot-changes::file\n#pragma once\n#include \"modules/register_module_types.h\"\nvoid initialize_main_game_module(ModuleInitializationLevel p_level);\nvoid uninitialize_main_game_module(ModuleInitializationLevel p_level);\n");
	String registration = "// wgodot-changes::file\n#include \"register_types.h\"\n#include \"modules/wgodot/native/wgodot_native_static.h\"\n#include \"modules/wgodot/native/wgodot_native_task.h\"\n#include \"core/object/wgodot_native_interfaces.h\"\n";
	String body = "\tGDREGISTER_INTERNAL_CLASS(WGodotNative::NativeTask);\n" + register_interfaces();
	HashSet<String> registered;
	for (const auto &entry : project.get_classes()) {
		if (!entry.node->wgodot_static_class) {
			registration += "#include \"" + entry.cpp_name + ".h\"\n";
			register_class(entry, registered, body);
		}
	}
	registration += "\nusing namespace WGodotGame;\nvoid initialize_main_game_module(ModuleInitializationLevel p_level) {\n\tif (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) { return; }\n" + body + "}\nvoid uninitialize_main_game_module(ModuleInitializationLevel p_level) {\n\tif (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {\n\t\tWGodotNative::NativeTask::clear_all();\n\t\tWGodotNative::StaticRegistry::get().clear();\n\t\tWGodotNativeInterfaces::clear();\n\t}\n}\n";
	// A project containing only static classes needs no class namespace here.
	if (registered.is_empty()) {
		registration = registration.replace("using namespace WGodotGame;\n", "");
	}
	files.insert("register_types.cpp", registration);
	return OK;
}

Error WGodotCppEmitter::write(const String &p_directory) const {
	ERR_FAIL_COND_V(!diagnostics.is_empty(), ERR_COMPILATION_FAILED);
	Error error = DirAccess::make_dir_recursive_absolute(p_directory);
	ERR_FAIL_COND_V(error != OK, error);
	const String manifest_path = p_directory.path_join("main_game.json");
	PackedStringArray previous_files;
	if (FileAccess::exists(manifest_path)) {
		JSON json;
		ERR_FAIL_COND_V(json.parse(FileAccess::get_file_as_string(manifest_path)) != OK || json.get_data().get_type() != Variant::DICTIONARY, ERR_FILE_CORRUPT);
		const Dictionary manifest = json.get_data();
		ERR_FAIL_COND_V(int(manifest.get("format", 0)) != 1, ERR_FILE_UNRECOGNIZED);
		previous_files = manifest.get("files", PackedStringArray());
		for (const String &name : previous_files) {
			ERR_FAIL_COND_V_MSG(name.is_empty() || name.get_file() != name || name == "." || name == "..", ERR_FILE_CORRUPT, "Invalid generated filename in " + manifest_path);
		}
	}
	for (const String &name : DirAccess::get_files_at(p_directory)) {
		ERR_FAIL_COND_V_MSG(name.ends_with(".cpp") && !files.has(name) && !previous_files.has(name), ERR_ALREADY_EXISTS, "Unmanaged C++ source in generated module: " + name);
	}
	// A partially written generation must never pass the -Game preflight.
	if (FileAccess::exists(manifest_path)) {
		error = DirAccess::remove_absolute(manifest_path);
		ERR_FAIL_COND_V(error != OK, error);
	}
	Vector<String> names;
	for (const KeyValue<String, String> &file : files) {
		names.push_back(file.key);
	}
	names.sort();
	StringBuilder fingerprint;
	for (const String &name : names) {
		const String &contents = files[name];
		fingerprint += name + "\n" + contents.sha256_text() + "\n";
		const String path = p_directory.path_join(name);
		if (FileAccess::exists(path) && FileAccess::get_file_as_string(path) == contents) {
			continue;
		}
		Ref<FileAccess> output = FileAccess::open(path, FileAccess::WRITE, &error);
		ERR_FAIL_COND_V(error != OK, error);
		output->store_string(contents);
		output->flush();
		ERR_FAIL_COND_V(output->get_error() != OK, output->get_error());
	}
	for (const String &name : previous_files) {
		if (!files.has(name) && FileAccess::exists(p_directory.path_join(name))) {
			error = DirAccess::remove_absolute(p_directory.path_join(name));
			ERR_FAIL_COND_V(error != OK, error);
		}
	}
	Dictionary manifest;
	manifest["format"] = 1;
	manifest["generation"] = fingerprint.as_string().sha256_text();
	manifest["files"] = names;
	Dictionary sources;
	Dictionary native_classes;
	for (const auto &entry : project.get_classes()) {
		if (entry.script_path.begins_with("res://") && !sources.has(entry.script_path)) {
			sources[entry.script_path] = FileAccess::get_sha256(entry.script_path);
		}
		if (!entry.node->outer && !entry.node->wgodot_static_class && !entry.node->wgodot_is_interface) {
			Dictionary native_class;
			native_class["class"] = entry.cpp_name;
			native_class["base"] = native_base(entry.node->self_type);
			native_classes[entry.script_path] = native_class;
		}
	}
	manifest["sources"] = sources;
	manifest["native_classes"] = native_classes;
	Ref<FileAccess> manifest_file = FileAccess::open(manifest_path, FileAccess::WRITE, &error);
	ERR_FAIL_COND_V(error != OK, error);
	manifest_file->store_string(JSON::stringify(manifest, "\t", true) + "\n");
	manifest_file->flush();
	ERR_FAIL_COND_V(manifest_file->get_error() != OK, manifest_file->get_error());
	return OK;
}
