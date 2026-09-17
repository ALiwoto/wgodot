// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "core/config/engine.h"
#include "core/io/resource.h"
#include "core/object/class_db.h"

using Parser = GDScriptParser;

namespace {
String indented(const String &p_code, int p_indent) {
	const String indent = String("\t").repeat(p_indent);
	return indent + p_code.replace("\n", "\n" + indent) + "\n";
}
} //namespace

String WGodotCppExpression::expression() const {
	if (setup.is_empty()) {
		return code;
	}
	String result = "([&]()" + (cpp_type.is_empty() ? "" : " -> " + cpp_type) + " {\n";
	for (const String &step : setup) {
		result += indented(step, 1);
	}
	WGodotCppExpression value = *this;
	value.setup.clear();
	result += value.statement(1, true);
	return result + "}())";
}

String WGodotCppExpression::block(const String &p_body, int p_indent) const {
	String result;
	const bool scope = !setup.is_empty();
	if (scope) {
		result += indented("{", p_indent++);
	}
	for (const String &step : setup) {
		result += indented(step, p_indent);
	}
	if (!p_body.is_empty()) {
		result += indented(p_body, p_indent);
	}
	if (scope) {
		result += indented("}", --p_indent);
	}
	return result;
}

String WGodotCppExpression::statement(int p_indent, bool p_return) const {
	const bool return_value = p_return && cpp_type != "void";
	String result = block(code.is_empty() ? String() : String(return_value ? "return " : "") + code + ";", p_indent);
	if (p_return && !return_value) {
		result += indented("return;", p_indent);
	}
	return result;
}

String WGodotCppEmitter::convert_value(const Value &p_value, const String &p_target) const {
	return p_value.cpp_type == p_target ? p_value.code : "WGodotNative::convert<" + p_target + ">(" + p_value.code + ")";
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_literal(const Variant &p_value, const Parser::Node *p_origin) {
	Value result(literal(p_value, p_origin), native_argument_type(PropertyInfo(p_value.get_type(), String()), p_origin));
	result.effects = p_value.get_type() == Variant::OBJECT;
	result.invariant = !result.effects;
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value;
		if (object) {
			Parser::DataType datatype;
			datatype.kind = Parser::DataType::NATIVE;
			datatype.native_type = object->get_class_name();
			result.cpp_type = type(datatype, p_origin);
		} else {
			result.cpp_type = "Object *";
			result.object_pointer = true;
			result.invariant = true;
			result.effects = false;
		}
	} else if (p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL) {
		result.cpp_type = signature_type(p_origin, p_value.get_type() == Variant::SIGNAL);
	}
	return result;
}

bool WGodotCppEmitter::receiver_needs_cast(const Value &p_value, const Parser::DataType &p_type, const Parser::Node *p_origin) {
	// Native engine methods operate on the interface handle's Godot base,
	// while interface methods explicitly use its adjusted interface pointer.
	if (is_interface_type(p_type)) {
		return false;
	}
	const String target = class_name(p_type, p_origin);
	return p_value.cpp_type != target + " *" && p_value.cpp_type != type(p_type, p_origin) && p_value.cpp_type != "WGodotNative::ObjectView<" + target + ">";
}

String WGodotCppEmitter::receiver_pointer(const Value &p_value, const Parser::DataType &p_type, const Parser::Node *p_origin) {
	const String pointer = p_value.object_pointer ? p_value.code : p_value.code + ".ptr()";
	if (!receiver_needs_cast(p_value, p_type, p_origin)) {
		return pointer;
	}
	return "static_cast<" + class_name(p_type, p_origin) + " *>(" + pointer + ")";
}

String WGodotCppEmitter::materialize_receiver(Value &r_call, const Value &p_receiver, const String &p_pointer, bool p_as_argument) {
	// sequence() already orders source evaluations. A member call evaluates its
	// receiver before C++ argument conversions, so a pure pointer read can stay
	// inline. Free-function adapters need a capture to preserve that order.
	if (p_receiver.nonnull || (!p_as_argument && !p_receiver.effects)) {
		return p_pointer;
	}
	const String instance = "instance_" + itos(temporary_index++);
	// Capture the receiver once at its evaluation point. Dereferencing it is
	// unconditional; a null receiver must not produce a fabricated result.
	r_call.setup.push_back("auto *" + instance + " = " + p_pointer + ";");
	return instance;
}

void WGodotCppEmitter::materialize(Value &r_value, Vector<String> &r_setup) {
	r_setup.append_array(r_value.setup);
	r_value.setup.clear();
	if (!r_value.storage_type.is_empty() && r_value.storage_type != r_value.cpp_type) {
		r_value.code = r_value.storage_type + "(" + r_value.code + ")";
		r_value.cpp_type = r_value.storage_type;
		r_value.object_pointer = false;
		r_value.borrowed = false;
	}
	const String name = "temporary_" + itos(temporary_index++);
	r_setup.push_back(String(r_value.borrowed ? "auto &" : "auto ") + name + " = " + r_value.code + ";");
	r_value.code = name;
	r_value.effects = false;
	r_value.borrowed = true;
}

WGodotCppEmitter::Value WGodotCppEmitter::sequence(Vector<Value> &r_operands) {
	Value result;
	result.effects = false;
	int dependent = 0;
	bool setup = false;
	for (const Value &value : r_operands) {
		dependent += !value.invariant;
		result.effects |= value.effects;
		setup |= !value.setup.is_empty();
	}
	// C++ argument/operand order is not generally GDScript's order. Locals and
	// direct fields remain borrowed slots; computed results are retained values.
	const bool ordered = setup || (result.effects && dependent > 1);
	for (Value &value : r_operands) {
		if (ordered && !value.invariant && (!value.borrowed || value.effects || !value.setup.is_empty())) {
			materialize(value, result.setup);
		} else {
			result.setup.append_array(value.setup);
			value.setup.clear();
		}
	}
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::value_facts(const Parser::ExpressionNode *p_expression, const String &p_code) {
	Value result(p_code);
	const auto datatype = expression_type(p_expression);
	result.cpp_type = type(datatype, p_expression);
	if (expression_overrides.has(p_expression)) {
		result.effects = false;
		result.borrowed = true;
		return result;
	}
	if (p_expression->type == Parser::Node::SELF) {
		result.cpp_type = class_name(datatype, p_expression) + " *";
		result.effects = false;
		result.invariant = true;
		result.object_pointer = true;
		result.nonnull = true;
		return result;
	}
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		if ((identifier->source == Parser::IdentifierNode::UNDEFINED_SOURCE || identifier->source == Parser::IdentifierNode::NATIVE_CLASS) && datatype.kind == Parser::DataType::NATIVE && Engine::get_singleton()->has_singleton(identifier->name)) {
			result.cpp_type = class_name(datatype, p_expression) + " *";
			result.object_pointer = true;
			return result;
		}
	}
	if (p_expression->is_constant && p_expression->reduced) {
		// Resource constants can initialize another generated class's retained
		// resources on first access. They are not reorderable literal reads.
		result.effects = p_expression->reduced_value.get_type() == Variant::OBJECT;
		result.invariant = !result.effects;
		if (p_expression->reduced_value.get_type() == Variant::NIL && !datatype.is_meta_type) {
			result.cpp_type = "Variant";
		} else if (p_expression->reduced_value.get_type() >= Variant::PACKED_BYTE_ARRAY) {
			// literal() constructs the engine packed array, before its shared
			// script-storage wrapper is built at the destination.
			result.cpp_type = Variant::get_type_name(p_expression->reduced_value.get_type());
		} else if (p_expression->reduced_value.get_type() == Variant::DICTIONARY && p_expression->type != Parser::Node::DICTIONARY && !is_wdictionary(datatype)) {
			result.cpp_type = "Dictionary";
		} else if (p_expression->reduced_value.get_type() == Variant::OBJECT && !datatype.is_meta_type) {
			const Resource *resource = Object::cast_to<Resource>(static_cast<Object *>(p_expression->reduced_value));
			if (resource) {
				Parser::DataType resource_type;
				resource_type.kind = Parser::DataType::NATIVE;
				resource_type.native_type = resource->get_class_name();
				result.cpp_type = type(resource_type, p_expression);
			}
		}
		return result;
	}
	StringName member_name;
	Parser::DataType base_type = current_class->node->self_type;
	bool direct_self = true;
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		const auto *source = local_source(identifier);
		if (source) {
			if (source->type == Parser::Node::PARAMETER) {
				result.cpp_type = type(static_cast<const Parser::ParameterNode *>(source)->type_constraint, source);
			} else if (source->type == Parser::Node::VARIABLE) {
				result.cpp_type = type(variable_type(static_cast<const Parser::VariableNode *>(source)), source);
			} else if (source->is_expression()) {
				result.cpp_type = type(static_cast<const Parser::ExpressionNode *>(source)->type_constraint, source);
			}
			result.effects = false;
			result.borrowed = !object_views.has(source);
			return result;
		}
		member_name = identifier->name;
	} else if (p_expression->type == Parser::Node::SUBSCRIPT) {
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
		if (!subscript->is_attribute) {
			return result;
		}
		member_name = subscript->attribute->name;
		base_type = expression_type(subscript->base);
		direct_self = subscript->base->type == Parser::Node::SELF;
	}
	if (!member_name.is_empty() && base_type.kind == Parser::DataType::CLASS) {
		const auto *owner = member_owner(base_type.class_type, member_name);
		if (owner && !owner->node->wgodot_is_interface) {
			const auto entry = owner->node->get_member(member_name);
			if (entry.type == Parser::ClassNode::Member::VARIABLE) {
				result.cpp_type = type(variable_type(entry.variable), entry.variable);
				const StringName getter = accessor_name(entry.variable, false);
				const bool own_getter = p_expression->type == Parser::Node::IDENTIFIER && current_function && current_function->identifier && current_function->identifier->name == getter;
				result.effects = !direct_self || (!getter.is_empty() && !own_getter) || entry.variable->is_static;
				result.borrowed = direct_self && !entry.variable->is_static && (getter.is_empty() || own_getter);
			} else if (entry.type == Parser::ClassNode::Member::SIGNAL) {
				result.effects = !direct_self;
			}
		}
	}
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::lower(const Parser::ExpressionNode *p_expression) {
	if (!p_expression) {
		return Value();
	}
	if (const String *replacement = expression_overrides.getptr(p_expression)) {
		return value_facts(p_expression, *replacement);
	}
	Value result;
	if (p_expression->is_constant && p_expression->reduced && p_expression->type != Parser::Node::ARRAY && p_expression->type != Parser::Node::DICTIONARY) {
		return value_facts(p_expression, leaf_expression(p_expression));
	}
	switch (p_expression->type) {
		case Parser::Node::ASSIGNMENT:
			return assignment(static_cast<const Parser::AssignmentNode *>(p_expression));
		case Parser::Node::CALL:
			result = lower_call(static_cast<const Parser::CallNode *>(p_expression));
			break;
		case Parser::Node::BINARY_OPERATOR:
			return lower_binary(static_cast<const Parser::BinaryOpNode *>(p_expression));
		case Parser::Node::ARRAY:
			return array_literal(static_cast<const Parser::ArrayNode *>(p_expression), expression_type(p_expression));
		case Parser::Node::DICTIONARY:
			return lower_dictionary(static_cast<const Parser::DictionaryNode *>(p_expression));
		case Parser::Node::SUBSCRIPT: {
			const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
			if (!subscript->is_attribute) {
				return lower_index(subscript);
			}
			result = member(subscript->base, subscript->attribute->name, subscript);
			break;
		}
		case Parser::Node::IDENTIFIER: {
			const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
			if (identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE || identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE || identifier->source == Parser::IdentifierNode::STATIC_VARIABLE || identifier->source == Parser::IdentifierNode::MEMBER_SIGNAL || identifier->source == Parser::IdentifierNode::MEMBER_FUNCTION) {
				result = member(nullptr, identifier->name, identifier);
				break;
			}
			return value_facts(p_expression, leaf_expression(p_expression));
		}
		default:
			return value_facts(p_expression, leaf_expression(p_expression));
	}
	if (result.cpp_type.is_empty()) {
		result.cpp_type = type(expression_type(p_expression), p_expression);
	}
	return result;
}

String WGodotCppEmitter::expression(const Parser::ExpressionNode *p_expression) {
	return lower(p_expression).expression();
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_receiver(const Parser::ExpressionNode *p_expression) {
	if (!p_expression) {
		Value self("this", current_class->cpp_name + " *");
		self.effects = false;
		self.invariant = self.object_pointer = self.nonnull = true;
		return self;
	}
	if (p_expression->type == Parser::Node::IDENTIFIER && !expression_overrides.has(p_expression)) {
		const auto *source = local_source(static_cast<const Parser::IdentifierNode *>(p_expression));
		if (const String *view = object_views.getptr(source)) {
			const auto &storage_type = static_cast<const Parser::IdentifierNode *>(source)->type_constraint;
			Value result(*view, "WGodotNative::ObjectView<" + class_name(storage_type, source) + ">");
			result.effects = false;
			result.borrowed = true;
			return result;
		}
	}
	return lower(p_expression);
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_call(const Parser::CallNode *p_call) {
	if (p_call->is_super) {
		return call(p_call);
	}
	if (p_call->get_callee_type() == Parser::Node::SUBSCRIPT) {
		const auto &base_type = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base->type_constraint;
		return base_type.kind == Parser::DataType::CLASS || base_type.kind == Parser::DataType::NATIVE ? call(p_call) : builtin_call(p_call);
	}
	if (p_call->get_callee_type() == Parser::Node::IDENTIFIER) {
		const auto *owner = member_owner(current_class->node, p_call->function_name);
		if ((owner && owner->node->get_member(p_call->function_name).type == Parser::ClassNode::Member::FUNCTION) || ClassDB::has_method(native_base(current_class->node->self_type), p_call->function_name)) {
			return call(p_call);
		}
		return global_call(p_call);
	}
	unsupported(p_call, "call " + String(p_call->function_name));
	return Value();
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_index(const Parser::SubscriptNode *p_subscript) {
	Vector<Value> operands{ lower(p_subscript->base), lower(p_subscript->index) };
	Value result = sequence(operands);
	result.cpp_type = type(p_subscript->type_constraint, p_subscript);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	result.code = "WGodotNative::get_index<" + result.cpp_type + ">(" + operands[0].code + ", " + operands[1].code + ")";
	// Indexing can run user code for Object values. Keep it ordered with calls.
	result.effects = true;
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_converted(const Parser::ExpressionNode *p_expression, const Parser::DataType &p_target, const Parser::Node *p_target_origin, bool p_parameter) {
	const String target = type(p_target, p_target_origin ? p_target_origin : p_expression);
	if (p_expression->type == Parser::Node::ARRAY && is_warray(p_target)) {
		return array_literal(static_cast<const Parser::ArrayNode *>(p_expression), p_target);
	}
	if (p_expression->type == Parser::Node::DICTIONARY && is_wdictionary(p_target)) {
		return dictionary_literal(static_cast<const Parser::DictionaryNode *>(p_expression), p_target);
	}
	if (!validate_dictionary_conversion(p_expression, p_target, p_target_origin)) {
		return Value();
	}
	if (is_packed(p_target) && (p_expression->type == Parser::Node::ARRAY || is_warray(expression_type(p_expression)))) {
		return packed_array(p_expression, p_target);
	}
	if (!validate_array_conversion(p_expression, p_target, p_target_origin)) {
		return Value();
	}
	if (p_expression->is_constant && p_expression->reduced && p_expression->reduced_value.get_type() == Variant::NIL) {
		Value result(p_target.kind == Parser::DataType::CLASS || p_target.kind == Parser::DataType::NATIVE ? target + "()" : "Variant()", target);
		result.effects = false;
		result.invariant = true;
		return result;
	}
	if (p_target.kind == Parser::DataType::BUILTIN && p_target.builtin_type == Variant::CALLABLE) {
		if (p_expression->is_constant && p_expression->reduced && p_expression->reduced_value.get_type() == Variant::CALLABLE && Callable(p_expression->reduced_value).is_null()) {
			Value result(target + "()", target);
			result.effects = false;
			result.invariant = true;
			return result;
		}
		Value result = lower(p_expression);
		if (const auto *signature = signatures.get(p_target_origin ? p_target_origin : p_expression)) {
			if (!validate_callback(p_expression, *signature)) {
				return Value();
			}
		}
		if (result.cpp_type != target) {
			result.code = target + "::adapt(" + result.code + ")";
			result.cpp_type = target;
			result.borrowed = false;
			result.effects = true;
		}
		return result;
	}
	Value result = lower(p_expression);
	if (p_target.is_variant() || result.cpp_type == target) {
		return result;
	}
	// Fixed C++ parameter types already construct object handles from compatible
	// raw pointers. Keep the actual pointer type until that parameter is built.
	if (p_parameter && result.object_pointer && !is_interface_type(p_target) && (p_target.kind == Parser::DataType::CLASS || p_target.kind == Parser::DataType::NATIVE)) {
		return result;
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
	result.code = convert_value(result, target);
	result.cpp_type = target;
	result.borrowed = false;
	result.object_pointer = false;
	result.storage_type = String();
	result.effects = true;
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_engine_argument(const Parser::ExpressionNode *p_expression, Variant::Type p_target) {
	const auto datatype = expression_type(p_expression);
	Parser::DataType target;
	target.kind = Parser::DataType::BUILTIN;
	target.builtin_type = p_target;
	if (is_packed(target) && (p_expression->type == Parser::Node::ARRAY || is_warray(datatype))) {
		return packed_array(p_expression, target);
	}
	if (!is_warray(datatype) && !is_wdictionary(datatype) && datatype.builtin_type != Variant::CALLABLE) {
		return lower(p_expression);
	}
	// These adapters deliberately cross a container/callback representation
	// boundary. Their result type is the engine type, not the script storage type.
	Value result(engine_argument(p_expression, p_target));
	result.cpp_type = datatype.builtin_type == Variant::CALLABLE ? "Callable" : is_wdictionary(datatype) ? "Dictionary" : "Array";
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_dictionary(const Parser::DictionaryNode *p_dictionary) {
	if (is_wdictionary(p_dictionary->type_constraint)) {
		return dictionary_literal(p_dictionary, p_dictionary->type_constraint);
	}
	Vector<Value> operands;
	for (const auto &element : p_dictionary->elements) {
		operands.push_back(lower_engine_argument(element.key, Variant::NIL));
		operands.push_back(lower_engine_argument(element.value, Variant::NIL));
	}
	Value result = sequence(operands);
	result.cpp_type = type(p_dictionary->type_constraint, p_dictionary);
	if (operands.is_empty()) {
		result.code = result.cpp_type + "()";
		return result;
	}
	const String name = "temporary_" + itos(temporary_index++);
	result.setup.push_back(result.cpp_type + " " + name + ";");
	for (int i = 0; i < operands.size(); i += 2) {
		result.setup.push_back(name + "[" + operands[i].code + "] = " + operands[i + 1].code + ";");
	}
	result.code = name;
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::lower_binary(const Parser::BinaryOpNode *p_binary) {
	const auto datatype = expression_type(p_binary);
	if (p_binary->operation == Parser::BinaryOpNode::OP_LOGIC_AND || p_binary->operation == Parser::BinaryOpNode::OP_LOGIC_OR) {
		// Each operand retains its own setup so the RHS remains conditional.
		return Value("(" + truth(p_binary->left_operand) + (p_binary->operation == Parser::BinaryOpNode::OP_LOGIC_AND ? " && " : " || ") + truth(p_binary->right_operand) + ")", "bool");
	}
	if (p_binary->variant_op == Variant::OP_MODULE && p_binary->left_operand->type_constraint.kind == Parser::DataType::BUILTIN && p_binary->left_operand->type_constraint.builtin_type == Variant::STRING && p_binary->right_operand->type == Parser::Node::ARRAY && !expression_overrides.has(p_binary->right_operand)) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_format.h");
		Value result;
		Value format = lower(p_binary->left_operand);
		materialize(format, result.setup);
		const String text = format.code;
		const auto *array = static_cast<const Parser::ArrayNode *>(p_binary->right_operand);
		Vector<Value> operands;
		for (const auto *element : array->elements) {
			operands.push_back(lower_engine_argument(element, Variant::NIL));
		}
		result.setup.append_array(sequence(operands).setup);
		Vector<String> arguments;
		for (const Value &element : operands) {
			arguments.push_back("Variant(" + element.code + ")");
		}
		const String name = "temporary_" + itos(temporary_index++);
		result.setup.push_back("const std::array<Variant, " + itos(array->elements.size()) + "> " + name + "{ " + String(", ").join(arguments) + " };");
		result.cpp_type = "String";
		result.code = "WGodotNative::format_string(" + text + ", Span<Variant>(" + name + ".data(), " + name + ".size()))";
		return result;
	}
	auto left_type = expression_type(p_binary->left_operand);
	auto right_type = expression_type(p_binary->right_operand);
	if (is_warray(left_type) && p_binary->right_operand->type == Parser::Node::ARRAY) {
		right_type = left_type;
	} else if (is_warray(right_type) && p_binary->left_operand->type == Parser::Node::ARRAY) {
		left_type = right_type;
	}
	if (is_wdictionary(left_type) && p_binary->right_operand->type == Parser::Node::DICTIONARY) {
		right_type = left_type;
	} else if (is_wdictionary(right_type) && p_binary->left_operand->type == Parser::Node::DICTIONARY) {
		left_type = right_type;
	}
	Vector<Value> operands{ lower_converted(p_binary->left_operand, left_type), lower_converted(p_binary->right_operand, right_type) };
	Value result = sequence(operands);
	result.cpp_type = type(datatype, p_binary);
	result.code = operation(p_binary->variant_op, datatype, left_type, right_type, operands[0].code, operands[1].code, p_binary);
	auto numeric = [](const Parser::DataType &p_type) {
		return p_type.kind == Parser::DataType::ENUM || (p_type.kind == Parser::DataType::BUILTIN && (p_type.builtin_type == Variant::INT || p_type.builtin_type == Variant::FLOAT));
	};
	result.effects |= !numeric(left_type) || !numeric(right_type);
	return result;
}
