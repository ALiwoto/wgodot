// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::is_warray(const Parser::DataType &p_type) const {
	return !p_type.is_coroutine && p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0);
}

Parser::DataType WGodotCppEmitter::variable_type(const Parser::VariableNode *p_variable) const {
	const auto &declared = p_variable->type_constraint;
	if (declared.kind == Parser::DataType::BUILTIN && declared.builtin_type == Variant::ARRAY && !declared.has_container_element_type(0) && p_variable->initializer) {
		return expression_type(p_variable->initializer);
	}
	return declared;
}

Parser::DataType WGodotCppEmitter::expression_type(const Parser::ExpressionNode *p_expression) const {
	if (p_expression->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(p_expression);
		if (call->get_callee_type() == Parser::Node::IDENTIFIER && call->function_name == SNAME("Array") && call->arguments.size() == 1) {
			const auto source = expression_type(call->arguments[0]);
			if (is_packed(source)) {
				auto result = p_expression->type_constraint;
				Parser::DataType element;
				element.kind = Parser::DataType::BUILTIN;
				element.builtin_type = Variant::get_indexed_element_type(source.builtin_type);
				result.set_container_element_type(0, element);
				return result;
			}
		}
		if (call->get_callee_type() == Parser::Node::SUBSCRIPT && call->function_name == SNAME("call")) {
			const auto *base = static_cast<const Parser::SubscriptNode *>(call->callee)->base;
			if (const auto *signature = signatures.get(base); signature && !signature->signal) {
				return signature->result.type;
			}
		}
	}
	if (p_expression->type == Parser::Node::AWAIT) {
		const auto value = expression_type(static_cast<const Parser::AwaitNode *>(p_expression)->to_await);
		if (value.is_coroutine) {
			auto result = value;
			result.is_coroutine = false;
			return result;
		}
		if (value.kind == Parser::DataType::BUILTIN && value.builtin_type == Variant::SIGNAL) {
			if (const auto *signature = signatures.get(static_cast<const Parser::AwaitNode *>(p_expression)->to_await); signature && signature->arguments.size() == 1) {
				return signature->arguments[0].type;
			}
		} else {
			return value;
		}
	}
	if (p_expression->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(p_expression);
		if (call->get_callee_type() == Parser::Node::SUBSCRIPT) {
			const auto base = expression_type(static_cast<const Parser::SubscriptNode *>(call->callee)->base);
			// Godot's builtin method metadata erases these element types. Native
			// code keeps them without changing the editor's cached GDScript AST.
			if (is_warray(base) && call->function_name == SNAME("duplicate")) {
				return base;
			}
			if (is_warray(base) && (call->function_name == SNAME("get") || call->function_name == SNAME("front") || call->function_name == SNAME("back") || call->function_name == SNAME("pop_back") || call->function_name == SNAME("pop_front") || call->function_name == SNAME("pop_at"))) {
				return base.get_container_element_type(0);
			}
			if (base.kind == Parser::DataType::BUILTIN && base.builtin_type == Variant::DICTIONARY && (call->function_name == SNAME("keys") || call->function_name == SNAME("values"))) {
				const int index = call->function_name == SNAME("keys") ? 0 : 1;
				if (base.has_container_element_type(index)) {
					auto result = p_expression->type_constraint;
					result.set_container_element_type(0, base.get_container_element_type(index));
					return result;
				}
			}
		}
	}
	if (p_expression->type == Parser::Node::IDENTIFIER && p_expression->type_constraint.kind == Parser::DataType::BUILTIN && p_expression->type_constraint.builtin_type == Variant::ARRAY) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		if (identifier->source == Parser::IdentifierNode::LOCAL_VARIABLE || identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE || identifier->source == Parser::IdentifierNode::STATIC_VARIABLE) {
			return variable_type(identifier->variable_source);
		}
		if (identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE) {
			const auto *owner = member_owner(current_class->node, identifier->name);
			if (owner && owner->node->get_member(identifier->name).type == Parser::ClassNode::Member::VARIABLE) {
				return variable_type(owner->node->get_member(identifier->name).variable);
			}
		}
	}
	if (p_expression->type == Parser::Node::SUBSCRIPT) {
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && subscript->base->type_constraint.kind == Parser::DataType::CLASS) {
			const auto *owner = member_owner(subscript->base->type_constraint.class_type, subscript->attribute->name);
			if (owner && owner->node->get_member(subscript->attribute->name).type == Parser::ClassNode::Member::VARIABLE) {
				const auto resolved = variable_type(owner->node->get_member(subscript->attribute->name).variable);
				if (is_warray(resolved)) {
					return resolved;
				}
			}
		}
	}
	if (p_expression->type == Parser::Node::BINARY_OPERATOR) {
		const auto *binary = static_cast<const Parser::BinaryOpNode *>(p_expression);
		if (binary->variant_op == Variant::OP_ADD) {
			const auto left = expression_type(binary->left_operand);
			const auto right = expression_type(binary->right_operand);
			if (is_warray(left) && (is_warray(right) || binary->right_operand->type == Parser::Node::ARRAY)) {
				return left;
			}
			if (is_warray(right) && binary->left_operand->type == Parser::Node::ARRAY) {
				return right;
			}
		}
	}
	if (p_expression->type == Parser::Node::TERNARY_OPERATOR) {
		const auto *ternary = static_cast<const Parser::TernaryOpNode *>(p_expression);
		const auto left = expression_type(ternary->true_expr);
		const auto right = expression_type(ternary->false_expr);
		if (is_warray(left) && (is_warray(right) || ternary->false_expr->type == Parser::Node::ARRAY)) {
			return left;
		}
		if (is_warray(right) && ternary->true_expr->type == Parser::Node::ARRAY) {
			return right;
		}
	}
	return p_expression->type_constraint;
}

bool WGodotCppEmitter::has_native_value_signature(const Parser::FunctionNode *p_function) const {
	if (native_only(p_function->return_type_constraint)) {
		return true;
	}
	for (const auto *parameter : p_function->parameters) {
		if (native_only(parameter->type_constraint)) {
			return true;
		}
	}
	return false;
}

bool WGodotCppEmitter::validate_array_conversion(const Parser::ExpressionNode *p_value, const Parser::DataType &p_target, const Parser::Node *p_target_origin) {
	const auto source_type = expression_type(p_value);
	if (is_packed(p_target) && (is_warray(source_type) || p_value->type == Parser::Node::ARRAY)) {
		return true; // Packed construction copies elements into independent storage.
	}
	if (is_warray(source_type) || is_warray(p_target)) {
		if (p_value->type == Parser::Node::ARRAY && is_warray(p_target)) {
			return true; // A literal is constructed directly in its destination type.
		}
		if (!is_warray(source_type) || !is_warray(p_target) || type(source_type, p_value) != type(p_target, p_target_origin ? p_target_origin : p_value)) {
			unsupported(p_value, "implicit container conversion from " + p_value->type_constraint.to_string() + " to " + p_target.to_string() + ". WArray sharing cannot cross an Array/Variant boundary; use .duplicate() at a supported Godot API boundary");
			return false;
		}
	}
	return true;
}

WGodotCppEmitter::Value WGodotCppEmitter::array_literal(const Parser::ArrayNode *p_array, const Parser::DataType &p_target) {
	Vector<Value> operands;
	for (const auto *element : p_array->elements) {
		operands.push_back(is_warray(p_target) ? lower_converted(element, p_target.get_container_element_type(0), p_array) : lower_engine_argument(element, Variant::NIL));
	}
	Value result = sequence(operands);
	Vector<String> elements;
	for (const Value &element : operands) {
		elements.push_back(element.code);
	}
	result.cpp_type = type(p_target, p_array);
	result.code = result.cpp_type + "{" + String(", ").join(elements) + "}";
	return result;
}

bool WGodotCppEmitter::is_array_duplicate(const Parser::ExpressionNode *p_value) const {
	if (p_value->type != Parser::Node::CALL) {
		return false;
	}
	const auto *call = static_cast<const Parser::CallNode *>(p_value);
	return call->function_name == SNAME("duplicate") && call->get_callee_type() == Parser::Node::SUBSCRIPT && is_warray(expression_type(static_cast<const Parser::SubscriptNode *>(call->callee)->base));
}

String WGodotCppEmitter::engine_argument(const Parser::ExpressionNode *p_value, Variant::Type p_target) {
	const auto value_type = expression_type(p_value);
	if (value_type.kind == Parser::DataType::BUILTIN && value_type.builtin_type == Variant::CALLABLE) {
		const auto *signature = signatures.get(p_value);
		if (!signature) {
			(void)signature_type(p_value);
			return String();
		}
		if (native_only(signature->result.type)) {
			unsupported(p_value, "native callback result at a Godot Callable boundary");
			return String();
		}
		for (const auto &argument : signature->arguments) {
			if (native_only(argument.type)) {
				unsupported(p_value, "native callback container/signature at a Godot Callable boundary");
				return String();
			}
		}
		return "(" + expression(p_value) + ").to_callable()";
	}
	if (!is_warray(expression_type(p_value))) {
		return expression(p_value);
	}
	if ((p_target == Variant::ARRAY || p_target == Variant::NIL) && is_array_duplicate(p_value)) {
		const auto &element = value_type.get_container_element_type(0);
		if (element.builtin_type == Variant::SIGNAL) {
			unsupported(p_value, "copying native game signal handles into a Godot Array");
			return String();
		}
		if (element.builtin_type == Variant::CALLABLE) {
			const auto *signature = signatures.get(p_value);
			if (!signature) {
				(void)signature_type(p_value);
				return String();
			}
			if (native_only(signature->result.type)) {
				unsupported(p_value, "copying callbacks with native results into a Godot Array");
				return String();
			}
			for (const auto &argument : signature->arguments) {
				if (native_only(argument.type)) {
					unsupported(p_value, "copying callbacks with native arguments into a Godot Array");
					return String();
				}
			}
		}
		const auto *call = static_cast<const Parser::CallNode *>(p_value);
		// Async lowering may already have evaluated the explicit duplicate.
		if (expression_overrides.has(call)) {
			return "(" + expression(call) + ").duplicate_to_array()";
		}
		return warray_call(call, true).expression();
	}
	unsupported(p_value, "passing WArray to a Godot API without an explicit copy. Use .duplicate() for an Array/Variant argument, or add a native handler for this API");
	return String();
}

WGodotCppEmitter::Value WGodotCppEmitter::warray_call(const Parser::CallNode *p_call, bool p_to_array) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	const auto base_type = expression_type(base);
	const auto &element_type = base_type.get_container_element_type(0);
	const StringName name = p_call->function_name;
	String method = name;
	int value_argument = -1;
	bool array_argument = false;
	if (name == SNAME("append") || name == SNAME("push_back") || name == SNAME("push_front") || name == SNAME("erase") || name == SNAME("has") || name == SNAME("find") || name == SNAME("rfind") || name == SNAME("count") || name == SNAME("fill")) {
		value_argument = 0;
		if (name == SNAME("push_back")) {
			method = "append";
		}
	} else if (name == SNAME("insert") || name == SNAME("set")) {
		value_argument = 1;
	} else if (name == SNAME("append_array") || name == SNAME("assign")) {
		array_argument = true;
	} else if (name == SNAME("duplicate")) {
		method = p_to_array ? "duplicate_to_array" : "duplicate";
	} else if (name == SNAME("sort")) {
		if (element_type.kind != Parser::DataType::ENUM && !(element_type.kind == Parser::DataType::BUILTIN && (element_type.builtin_type == Variant::INT || element_type.builtin_type == Variant::FLOAT || element_type.builtin_type == Variant::STRING))) {
			unsupported(p_call, "WArray.sort for this element type; use an explicit comparator");
			return String();
		}
	} else if (name != SNAME("size") && name != SNAME("is_empty") && name != SNAME("clear") && name != SNAME("resize") && name != SNAME("reserve") && name != SNAME("remove_at") && name != SNAME("reverse") && name != SNAME("sort_custom") && name != SNAME("front") && name != SNAME("back") && name != SNAME("pop_back") && name != SNAME("pop_front") && name != SNAME("pop_at") && name != SNAME("get") && name != SNAME("is_read_only") && name != SNAME("make_read_only")) {
		unsupported(p_call, "WArray." + String(name) + "; this method needs a typed native implementation");
		return String();
	}
	Vector<Value> operands;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const auto *source = p_call->arguments[i];
		operands.push_back(int(i) == value_argument ? lower_converted(source, element_type, base, true) : array_argument ? lower_converted(source, base_type)
																														 : lower(source));
	}
	operands.push_back(lower(base));
	Value result = sequence(operands);
	const bool nullable = name == SNAME("pop_back") || name == SNAME("pop_front") || name == SNAME("pop_at") || name == SNAME("front") || name == SNAME("back") || name == SNAME("get");
	const bool variant_result = nullable && expression_type(p_call).is_variant();
	if (variant_result) {
		// The bounds check and invocation share these values; never evaluate an
		// index or receiver expression a second time inside the check.
		for (Value &operand : operands) {
			if (!operand.invariant && !operand.borrowed) {
				materialize(operand, result.setup);
			}
		}
	}
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		arguments.push_back(operands[i].code);
	}
	const String receiver = "(" + operands[operands.size() - 1].code + ")";
	const String invoke = receiver + "." + method + "(" + String(", ").join(arguments) + ")";
	if (variant_result) {
		String body = "([&]() -> Variant { auto &&receiver = " + receiver + "; ";
		const bool pop = name == SNAME("pop_back") || name == SNAME("pop_front") || name == SNAME("pop_at");
		body += "if (receiver.is_empty()" + String(pop ? " || receiver.is_read_only()" : "") + ") { (void)" + invoke + "; return Variant(); } ";
		if (name == SNAME("pop_at") || name == SNAME("get")) {
			body += "if (" + arguments[0] + " < -receiver.size() || " + arguments[0] + " >= receiver.size()) { (void)" + invoke + "; return Variant(); } ";
		}
		body += "return Variant(" + invoke + "); }())";
		result.code = body;
	} else {
		result.code = invoke;
	}
	result.cpp_type = p_to_array ? "Array" : type(expression_type(p_call), p_call);
	result.effects = true;
	return result;
}
