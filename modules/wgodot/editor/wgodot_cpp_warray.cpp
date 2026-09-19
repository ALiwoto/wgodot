// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_array_api.h"

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
	if (const auto *constant = container_constant_source(p_expression)) {
		return container_constant_type(constant);
	}
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
			if (is_wdictionary(base) && call->function_name == SNAME("duplicate")) {
				return base;
			}
			if (is_wdictionary(base)) {
				if (call->function_name == SNAME("get") || call->function_name == SNAME("get_or_add")) {
					return base.get_container_element_type(1);
				}
				if (call->function_name == SNAME("merged")) {
					return base;
				}
			}
			if (is_warray(base)) {
				using APIType = WGodotCppArrayAPI::Type;
				if (const auto *method = WGodotCppArrayAPI::find(call->function_name, call->arguments.size())) {
					switch (method->result) {
						case APIType::ELEMENT:
							return base.get_container_element_type(0);
						case APIType::ARRAY:
							return base;
						case APIType::ACCUMULATOR:
							for (int i = 0; i < method->total; i++) {
								if (method->arguments[i] == APIType::ACCUMULATOR) {
									const auto accumulator = expression_type(call->arguments[i]);
									return accumulator.kind == Parser::DataType::BUILTIN && accumulator.builtin_type == Variant::NIL ? base.get_container_element_type(0) : accumulator;
								}
							}
							break;
						case APIType::MAPPED_ARRAY:
							if (const auto *callback = signatures.get(call->arguments[method->callback_index()])) {
								auto result = base;
								result.set_container_element_type(0, callback->result.type);
								return result;
							}
							break;
						default:
							break;
					}
				}
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
	if (initializing_container_constant) {
		// Also freeze literal children, not just the outer declared constant.
		const String name = "temporary_" + itos(temporary_index++);
		result.setup.push_back("auto " + name + " = " + result.code + ";");
		result.setup.push_back(name + ".make_read_only();");
		result.code = name;
		result.read_only = true;
	}
	return result;
}

bool WGodotCppEmitter::is_array_duplicate(const Parser::ExpressionNode *p_value) const {
	if (p_value->type != Parser::Node::CALL) {
		return false;
	}
	const auto *call = static_cast<const Parser::CallNode *>(p_value);
	const auto *method = WGodotCppArrayAPI::find(call->function_name, call->arguments.size());
	return method && method->copy_method[0] && call->get_callee_type() == Parser::Node::SUBSCRIPT && is_warray(expression_type(static_cast<const Parser::SubscriptNode *>(call->callee)->base));
}

String WGodotCppEmitter::engine_argument(const Parser::ExpressionNode *p_value, Variant::Type p_target) {
	const auto value_type = expression_type(p_value);
	if (is_wdictionary(value_type)) {
		return dictionary_engine_argument(p_value, p_target);
	}
	if (value_type.kind == Parser::DataType::BUILTIN && value_type.builtin_type == Variant::CALLABLE) {
		if (p_value->type == Parser::Node::CALL) {
			const auto *call = static_cast<const Parser::CallNode *>(p_value);
			if (call->function_name == SNAME("unbind") && call->get_callee_type() == Parser::Node::SUBSCRIPT) {
				const auto *base = static_cast<const Parser::SubscriptNode *>(call->callee)->base;
				const auto base_type = expression_type(base);
				if (base_type.kind == Parser::DataType::BUILTIN && base_type.builtin_type == Variant::CALLABLE && !base_type.is_meta_type) {
					// At the engine boundary, discard arguments before the typed
					// callback adapter decodes them (e.g. JavaScript's argument Array).
					Vector<Value> operands{ lower(call->arguments[0]), lower_engine_argument(base, Variant::CALLABLE) };
					Value result = sequence(operands);
					result.code = "(" + operands[1].code + ").unbind(" + convert_value(operands[0], "int") + ")";
					result.cpp_type = "Callable";
					result.effects = true;
					return result.expression();
				}
			}
		}
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
		if (is_wdictionary(element)) {
			unsupported(p_value, "copying WDictionary elements into a Godot Array; this requires an explicit entry adapter");
			return String();
		}
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
	using APIType = WGodotCppArrayAPI::Type;
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	const auto base_type = expression_type(base);
	const auto &element_type = base_type.get_container_element_type(0);
	const auto *method = WGodotCppArrayAPI::find(p_call->function_name, p_call->arguments.size());
	if (!method || method->unsupported[0]) {
		unsupported(p_call, "WArray." + String(p_call->function_name) + ": " + (method ? String(method->unsupported) : "no native API declaration for this signature"));
		return String();
	}
	if (p_to_array && !method->copy_method[0]) {
		unsupported(p_call, "this native Array method has no engine copy adapter");
		return String();
	}
	if (method->ordered) {
		const bool ordered_builtin = element_type.kind == Parser::DataType::BUILTIN &&
				element_type.builtin_type != Variant::ARRAY &&
				Variant::get_operator_return_type(Variant::OP_LESS, element_type.builtin_type, element_type.builtin_type) == Variant::BOOL;
		if (element_type.kind != Parser::DataType::ENUM && !ordered_builtin) {
			unsupported(p_call, "WArray." + String(p_call->function_name) + " requires a native element ordering; use an explicit typed comparator");
			return String();
		}
	}
	const auto result_type = expression_type(p_call);
	const int callback_index = method->callback_index();
	if (callback_index >= 0) {
		const auto *callback_node = p_call->arguments[callback_index];
		const auto *callback = signatures.get(callback_node);
		if (!callback) {
			(void)signature_type(callback_node);
			return String();
		}
		if (callback->result.type.is_variant() || callback->result.type.is_coroutine ||
				(callback->result.type.kind == Parser::DataType::BUILTIN && callback->result.type.builtin_type == Variant::NIL)) {
			unsupported(callback_node, "native Array callbacks require a concrete, synchronous result");
			return String();
		}
		WGodotCppSignatures::Signature expected;
		expected.arguments.push_back({ element_type, base });
		const APIType role = method->arguments[callback_index];
		if (role == APIType::REDUCER) {
			expected.arguments.write[0] = { result_type, p_call };
			expected.arguments.push_back({ element_type, base });
			expected.result = { result_type, p_call };
		} else if (role == APIType::MAPPER) {
			expected.result = callback->result;
		} else {
			if (role == APIType::COMPARATOR) {
				expected.arguments.push_back({ element_type, base });
			}
			expected.result.type.kind = Parser::DataType::BUILTIN;
			expected.result.type.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
			expected.result.type.builtin_type = Variant::BOOL;
			expected.result.origin = p_call;
		}
		if (!validate_callback(callback_node, expected)) {
			return String();
		}
		if (type(callback->result.type, callback->result.origin) != type(expected.result.type, expected.result.origin)) {
			unsupported(callback_node, "native Array predicates/comparators must return bool, and reducers must preserve the accumulator type");
			return String();
		}
	}
	Vector<Value> operands;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const auto *source = p_call->arguments[i];
		switch (method->arguments[i]) {
			case APIType::ELEMENT:
				operands.push_back(lower_converted(source, element_type, base, true));
				break;
			case APIType::ARRAY:
				operands.push_back(lower_converted(source, base_type, base));
				break;
			case APIType::OTHER_ARRAY:
				if (!is_warray(expression_type(source))) {
					unsupported(source, "native Array type comparison with a dynamic Array");
					return String();
				}
				operands.push_back(lower(source));
				break;
			case APIType::ACCUMULATOR: {
				const auto source_type = expression_type(source);
				// An explicit null has the same semantics as omitting the seed.
				if (source_type.kind == Parser::DataType::BUILTIN && source_type.builtin_type == Variant::NIL) {
					if (!source->is_constant) {
						unsupported(source, "a dynamically computed null accumulator; use a concrete initial accumulator or omit it");
						return String();
					}
				} else {
					operands.push_back(lower_converted(source, result_type, p_call));
				}
			} break;
			default:
				operands.push_back(lower(source));
				break;
		}
	}
	operands.push_back(lower(base));
	Value result = sequence(operands);
	Vector<String> arguments;
	for (int i = 0; i < operands.size() - 1; i++) {
		arguments.push_back(operands[i].code);
	}
	const String receiver = "(" + operands[operands.size() - 1].code + ")";
	result.code = receiver + "." + (p_to_array ? String(method->copy_method) : String(method->name)) + "(" + String(", ").join(arguments) + ")";
	result.cpp_type = p_to_array ? "Array" : type(result_type, p_call);
	result.effects = true;
	return result;
}
