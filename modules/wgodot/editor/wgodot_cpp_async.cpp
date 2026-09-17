// wgodot-changes::file
#include "wgodot_cpp_async.h"

#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

Vector<const Parser::ExpressionNode *> WGodotCppAsync::children(const Parser::ExpressionNode *p_expression) const {
	Vector<const Parser::ExpressionNode *> result;
	auto add = [&](const Parser::ExpressionNode *p_child) { if (p_child) { result.push_back(p_child); } };
	switch (p_expression->type) {
		case Parser::Node::AWAIT:
			add(static_cast<const Parser::AwaitNode *>(p_expression)->to_await);
			break;
		case Parser::Node::BINARY_OPERATOR: {
			const auto *node = static_cast<const Parser::BinaryOpNode *>(p_expression);
			add(node->left_operand);
			add(node->right_operand);
			break;
		}
		case Parser::Node::UNARY_OPERATOR:
			add(static_cast<const Parser::UnaryOpNode *>(p_expression)->operand);
			break;
		case Parser::Node::TERNARY_OPERATOR: {
			const auto *node = static_cast<const Parser::TernaryOpNode *>(p_expression);
			add(node->condition);
			add(node->true_expr);
			add(node->false_expr);
			break;
		}
		case Parser::Node::CAST:
			add(static_cast<const Parser::CastNode *>(p_expression)->operand);
			break;
		case Parser::Node::TYPE_TEST:
			add(static_cast<const Parser::TypeTestNode *>(p_expression)->operand);
			break;
		case Parser::Node::SUBSCRIPT: {
			const auto *node = static_cast<const Parser::SubscriptNode *>(p_expression);
			add(node->base);
			if (!node->is_attribute) {
				add(node->index);
			}
			break;
		}
		case Parser::Node::CALL: {
			const auto *node = static_cast<const Parser::CallNode *>(p_expression);
			for (const auto *argument : node->arguments) {
				add(argument);
			}
			if (node->get_callee_type() == Parser::Node::SUBSCRIPT) {
				add(static_cast<const Parser::SubscriptNode *>(node->callee)->base);
			}
			break;
		}
		case Parser::Node::ARRAY:
			for (const auto *element : static_cast<const Parser::ArrayNode *>(p_expression)->elements) {
				add(element);
			}
			break;
		case Parser::Node::DICTIONARY:
			for (const auto &element : static_cast<const Parser::DictionaryNode *>(p_expression)->elements) {
				add(element.key);
				add(element.value);
			}
			break;
		case Parser::Node::ASSIGNMENT: {
			const auto *node = static_cast<const Parser::AssignmentNode *>(p_expression);
			add(node->assignee);
			add(node->assigned_value);
			break;
		}
		default:
			break; // A lambda's body belongs to a separate invocation.
	}
	return result;
}

bool WGodotCppAsync::has_await(const Parser::ExpressionNode *p_expression) {
	if (const bool *cached = await_nodes.getptr(p_expression)) {
		return *cached;
	}
	bool found = p_expression->type == Parser::Node::AWAIT;
	for (const auto *child : children(p_expression)) {
		found = has_await(child) || found;
	}
	await_nodes.insert(p_expression, found);
	return found;
}

String WGodotCppAsync::add_field(const String &p_type, const String &p_name) {
	const String name = p_name + "_" + itos(field_count++);
	fields += "\t" + p_type + " " + name + "{};\n";
	const String access = "frame." + name;
	field_types.insert(access, p_type);
	return access;
}

String WGodotCppAsync::local(const Parser::Node *p_node, const StringName &p_name, const Parser::DataType &p_type) {
	if (const String *existing = emitter.local_overrides.getptr(p_node)) {
		return *existing;
	}
	const String access = add_field(emitter.type(p_type, p_node), "v_" + symbol(p_name));
	emitter.local_overrides.insert(p_node, access);
	return access;
}

void WGodotCppAsync::line(int p_indent, const String &p_code) {
	code += String("\t").repeat(p_indent) + p_code + "\n";
}

void WGodotCppAsync::clear_temporaries(int p_indent) {
	for (const String &field : temporary_fields) {
		line(p_indent, field + " = " + field_types[field] + "();");
	}
	temporary_fields.clear();
	emitter.expression_overrides.clear();
}

bool WGodotCppAsync::borrows_slot(const Parser::ExpressionNode *p_expression) const {
	StringName member_name;
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		const auto source = identifier->source;
		if (source == Parser::IdentifierNode::FUNCTION_PARAMETER || source == Parser::IdentifierNode::LOCAL_VARIABLE || source == Parser::IdentifierNode::LOCAL_ITERATOR || source == Parser::IdentifierNode::LOCAL_BIND) {
			return true;
		}
		if (source == Parser::IdentifierNode::MEMBER_VARIABLE || source == Parser::IdentifierNode::INHERITED_VARIABLE) {
			member_name = identifier->name;
		}
	} else if (p_expression->type == Parser::Node::SUBSCRIPT) {
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && subscript->base->type == Parser::Node::SELF) {
			member_name = subscript->attribute->name;
		}
	}
	if (!member_name.is_empty()) {
		const auto *owner = emitter.member_owner(emitter.current_class->node, member_name);
		if (owner) {
			const auto member = owner->node->get_member(member_name);
			if (member.type == Parser::ClassNode::Member::VARIABLE && !member.variable->is_static) {
				const StringName getter = emitter.accessor_name(member.variable, false);
				return getter.is_empty() || (p_expression->type == Parser::Node::IDENTIFIER && emitter.current_function->identifier && emitter.current_function->identifier->name == getter);
			}
		}
	}
	return false;
}

String WGodotCppAsync::preserve(const Parser::ExpressionNode *p_expression, int p_indent) {
	const String value = expression(p_expression, p_indent);
	// Locals and direct instance fields stay addresses while later operands run,
	// including across suspension. Getter results and other temporaries are saved.
	if (emitter.expression_overrides.has(p_expression) || p_expression->is_constant || p_expression->type_constraint.is_meta_type || p_expression->type == Parser::Node::SELF || borrows_slot(p_expression)) {
		return value;
	}
	const String type = emitter.type(p_expression->type_constraint, p_expression);
	if (type == "void") {
		line(p_indent, value + ";");
		return "Variant()";
	}
	const String field = add_field(type, "saved");
	line(p_indent, field + " = " + value + ";");
	temporary_fields.push_back(field);
	emitter.expression_overrides.insert(p_expression, field);
	return field;
}

String WGodotCppAsync::expression(const Parser::ExpressionNode *p_expression, int p_indent) {
	if (const String *saved = emitter.expression_overrides.getptr(p_expression)) {
		return *saved;
	}
	if (!has_await(p_expression)) {
		return emitter.expression(p_expression);
	}
	if (p_expression->type == Parser::Node::AWAIT) {
		const auto *node = static_cast<const Parser::AwaitNode *>(p_expression);
		if (node->to_await->type == Parser::Node::CALL && emitter.interface_method(static_cast<const Parser::CallNode *>(node->to_await))) {
			emitter.unsupported(node, "await through the current interface Variant ABI; this requires typed interface dispatch");
			return String();
		}
		const auto awaited_type = emitter.expression_type(node->to_await);
		const bool signal = awaited_type.kind == Parser::DataType::BUILTIN && awaited_type.builtin_type == Variant::SIGNAL;
		if (!signal && !awaited_type.is_coroutine) {
			return expression(node->to_await, p_indent);
		}
		String value = expression(node->to_await, p_indent);
		String result_type;
		if (signal) {
			const auto *signature = emitter.signatures.get(node->to_await);
			if (!signature) {
				(void)emitter.signature_type(node->to_await, true);
				return String();
			}
			if (signature->arguments.is_empty()) {
				result_type = "WGodotNative::Unit";
			} else if (signature->arguments.size() == 1) {
				result_type = emitter.type(signature->arguments[0].type, signature->arguments[0].origin);
			} else {
				Vector<String> arguments;
				for (const auto &argument : signature->arguments) {
					arguments.push_back(emitter.type(argument.type, argument.origin));
				}
				result_type = "std::tuple<" + String(", ").join(arguments) + ">";
			}
		} else {
			auto result = awaited_type;
			result.is_coroutine = false;
			result_type = emitter.type(result, node->to_await);
			if (result_type == "void") {
				result_type = "WGodotNative::Unit";
			}
		}
		const String field = add_field(result_type, "await_result");
		const int resume = ++resume_count;
		line(p_indent, "frame.continuation = " + itos(resume) + ";");
		line(p_indent, "if (WGodotNative::" + String(signal ? "await_signal" : "await_task") + "(task, " + value + ", " + field + ")) { return; }");
		line(p_indent, "resume_" + itos(resume) + ":;");
		temporary_fields.push_back(field);
		emitter.expression_overrides.insert(p_expression, field);
		return field;
	}
	if (p_expression->type == Parser::Node::BINARY_OPERATOR) {
		const auto *node = static_cast<const Parser::BinaryOpNode *>(p_expression);
		if (node->operation == Parser::BinaryOpNode::OP_LOGIC_AND || node->operation == Parser::BinaryOpNode::OP_LOGIC_OR) {
			(void)expression(node->left_operand, p_indent);
			const String result = add_field("bool", "condition");
			line(p_indent, result + " = bool(" + emitter.truth(node->left_operand) + ");");
			line(p_indent, "if (" + String(node->operation == Parser::BinaryOpNode::OP_LOGIC_OR ? "!" : "") + result + ") {");
			(void)expression(node->right_operand, p_indent + 1);
			line(p_indent + 1, result + " = bool(" + emitter.truth(node->right_operand) + ");");
			line(p_indent, "}");
			emitter.expression_overrides.insert(p_expression, result);
			return result;
		}
	}
	if (p_expression->type == Parser::Node::TERNARY_OPERATOR) {
		const auto *node = static_cast<const Parser::TernaryOpNode *>(p_expression);
		(void)expression(node->condition, p_indent);
		const auto result_type = emitter.expression_type(node);
		const String result = add_field(emitter.type(result_type, node), "choice");
		line(p_indent, "if (" + emitter.truth(node->condition) + ") {");
		(void)expression(node->true_expr, p_indent + 1);
		line(p_indent + 1, result + " = " + emitter.converted(node->true_expr, result_type) + ";");
		line(p_indent, "} else {");
		(void)expression(node->false_expr, p_indent + 1);
		line(p_indent + 1, result + " = " + emitter.converted(node->false_expr, result_type) + ";");
		line(p_indent, "}");
		temporary_fields.push_back(result);
		emitter.expression_overrides.insert(p_expression, result);
		return result;
	}
	if (p_expression->type == Parser::Node::ASSIGNMENT) {
		const auto *node = static_cast<const Parser::AssignmentNode *>(p_expression);
		Vector<const Parser::SubscriptNode *> chain;
		const auto *root = node->assignee;
		while (root->type == Parser::Node::SUBSCRIPT) {
			const auto *subscript = static_cast<const Parser::SubscriptNode *>(root);
			chain.insert(0, subscript);
			root = subscript->base;
		}
		if (!chain.is_empty()) {
			(void)preserve(root, p_indent);
			for (int i = 0; i + 1 < chain.size(); i++) {
				if (!chain[i]->is_attribute) {
					(void)preserve(chain[i]->index, p_indent);
				}
				(void)preserve(chain[i], p_indent);
			}
		}
		(void)preserve(node->assigned_value, p_indent);
		if (!chain.is_empty() && !chain[chain.size() - 1]->is_attribute) {
			(void)preserve(chain[chain.size() - 1]->index, p_indent);
		}
		return emitter.expression(p_expression);
	}
	for (const auto *child : children(p_expression)) {
		(void)preserve(child, p_indent);
	}
	return emitter.expression(p_expression);
}

String WGodotCppAsync::condition(const Parser::ExpressionNode *p_expression, int p_indent) {
	(void)expression(p_expression, p_indent);
	const String field = add_field("bool", "branch");
	line(p_indent, field + " = bool(" + emitter.truth(p_expression) + ");");
	clear_temporaries(p_indent);
	return field;
}

void WGodotCppAsync::suite(const Parser::SuiteNode *p_suite, int p_indent, bool p_clear_locals) {
	Vector<String> locals;
	for (const auto *statement : p_suite->statements) {
		if (statement->type == Parser::Node::VARIABLE) {
			const auto *node = static_cast<const Parser::VariableNode *>(statement);
			locals.push_back(local(node, node->identifier->name, emitter.variable_type(node)));
		}
	}
	for (const auto *statement : p_suite->statements) {
		switch (statement->type) {
			case Parser::Node::VARIABLE: {
				const auto *node = static_cast<const Parser::VariableNode *>(statement);
				const String name = emitter.local_overrides[node];
				if (node->initializer) {
					(void)expression(node->initializer, p_indent);
				}
				line(p_indent, name + " = " + (node->initializer ? emitter.converted(node->initializer, emitter.variable_type(node), node) : field_types[name] + "()") + ";");
				break;
			}
			case Parser::Node::RETURN: {
				const auto *node = static_cast<const Parser::ReturnNode *>(statement);
				if (node->return_value) {
					(void)expression(node->return_value, p_indent);
				}
				if (node->void_return && node->return_value) {
					line(p_indent, emitter.expression(node->return_value) + ";");
				}
				line(p_indent, "task.complete(" + (node->return_value && !node->void_return ? emitter.converted(node->return_value, emitter.current_function->return_type_constraint, emitter.current_function) : "") + ");");
				line(p_indent, "return;");
				break;
			}
			case Parser::Node::IF: {
				const auto *node = static_cast<const Parser::IfNode *>(statement);
				const String test = condition(node->condition, p_indent);
				line(p_indent, "if (" + test + ") {");
				suite(node->true_block, p_indent + 1);
				if (node->false_block) {
					line(p_indent, "} else {");
					suite(node->false_block, p_indent + 1);
				}
				line(p_indent, "}");
				break;
			}
			case Parser::Node::WHILE: {
				const auto *node = static_cast<const Parser::WhileNode *>(statement);
				line(p_indent, "while (true) {");
				const String test = condition(node->condition, p_indent + 1);
				line(p_indent + 1, "if (!" + test + ") { break; }");
				suite(node->loop, p_indent + 1, false);
				line(p_indent, "}");
				for (const auto *item : node->loop->statements) {
					if (item->type == Parser::Node::VARIABLE) {
						const String &field = emitter.local_overrides[item];
						line(p_indent, field + " = " + field_types[field] + "();");
					}
				}
				break;
			}
			case Parser::Node::FOR: {
				const auto *node = static_cast<const Parser::ForNode *>(statement);
				const auto collection_type = emitter.expression_type(node->list);
				WGodotCppEmitter::Value collection;
				bool array_range = false;
				bool range = node->list->type_constraint.kind == Parser::DataType::BUILTIN && node->list->type_constraint.builtin_type == Variant::INT;
				if (node->list->type == Parser::Node::CALL && static_cast<const Parser::CallNode *>(node->list)->function_name == "range") {
					Vector<String> arguments;
					for (const auto *argument : static_cast<const Parser::CallNode *>(node->list)->arguments) {
						arguments.push_back(preserve(argument, p_indent));
					}
					collection.code = String(", ").join(arguments);
					range = true;
				} else {
					const auto *outer_expression = emitter.iterated_expression;
					const bool outer_range = emitter.emitted_array_range;
					emitter.iterated_expression = emitter.is_warray(collection_type) ? node->list : nullptr;
					emitter.emitted_array_range = false;
					collection = has_await(node->list) ? WGodotCppEmitter::Value(expression(node->list, p_indent)) : emitter.lower(node->list);
					array_range = emitter.emitted_array_range;
					emitter.iterated_expression = outer_expression;
					emitter.emitted_array_range = outer_range;
				}
				String iterator_type;
				if (array_range) {
					iterator_type = "WGodotNative::ArrayIterator<" + emitter.array_iteration_element(node->list) + ">";
				} else if (range) {
					iterator_type = "WGodotNative::Range";
				} else if (emitter.is_warray(collection_type)) {
					iterator_type = "WGodotNative::WArrayIterator<" + emitter.type(collection_type.get_container_element_type(0), node) + ">";
				} else if (emitter.is_wdictionary(collection_type)) {
					iterator_type = "WGodotNative::WDictionaryIterator<" + emitter.type(collection_type.get_container_element_type(0), node) + ", " + emitter.type(collection_type.get_container_element_type(1), node) + ">";
				} else {
					iterator_type = "WGodotNative::Iterator";
				}
				const String iterator = add_field("std::optional<" + iterator_type + ">", "iterator");
				collection.code = iterator + ".emplace(" + collection.code + ")";
				code += collection.statement(p_indent);
				clear_temporaries(p_indent);
				const String variable = local(node->variable, node->variable->name, node->variable->type_constraint);
				line(p_indent, "for (; " + iterator + "->has_value(); " + iterator + "->next()) {");
				line(p_indent + 1, variable + " = " + iterator + (range ? "->get();" : "->get<" + field_types[variable] + ">();"));
				suite(node->loop, p_indent + 1, false);
				line(p_indent, "}");
				line(p_indent, iterator + ".reset();");
				line(p_indent, variable + " = " + field_types[variable] + "();");
				for (const auto *item : node->loop->statements) {
					if (item->type == Parser::Node::VARIABLE) {
						const String &field = emitter.local_overrides[item];
						line(p_indent, field + " = " + field_types[field] + "();");
					}
				}
				break;
			}
			case Parser::Node::BREAK:
				line(p_indent, "break;");
				break;
			case Parser::Node::CONTINUE:
				line(p_indent, "continue;");
				break;
			case Parser::Node::MATCH: {
				const auto *node = static_cast<const Parser::MatchNode *>(statement);
				if (emitter.is_warray(emitter.expression_type(node->test)) || emitter.is_wdictionary(emitter.expression_type(node->test))) {
					emitter.unsupported(node, "matching native containers through the current Variant pattern matcher");
					break;
				}
				const String value = add_field(emitter.type(node->test->type_constraint, node->test), "match_value");
				const String matched = add_field("bool", "matched");
				line(p_indent, value + " = " + expression(node->test, p_indent) + ";");
				clear_temporaries(p_indent);
				line(p_indent, matched + " = false;");
				for (const auto *branch : node->branches) {
					Vector<String> patterns;
					for (const auto *pattern : branch->patterns) {
						patterns.push_back(emitter.match_condition(pattern, value));
					}
					line(p_indent, "if (!" + matched + " && (" + String(" || ").join(patterns) + ")) {");
					if (branch->guard_body) {
						const auto *guard = static_cast<const Parser::ExpressionNode *>(branch->guard_body->statements[0]);
						line(p_indent + 1, matched + " = " + condition(guard, p_indent + 1) + ";");
					} else {
						line(p_indent + 1, matched + " = true;");
					}
					line(p_indent + 1, "if (" + matched + ") {");
					suite(branch->block, p_indent + 2);
					line(p_indent + 1, "}");
					line(p_indent, "}");
				}
				line(p_indent, value + " = " + field_types[value] + "();");
				break;
			}
			case Parser::Node::CONSTANT:
			case Parser::Node::PASS:
				break;
			default:
				if (statement->is_expression()) {
					const auto *value = static_cast<const Parser::ExpressionNode *>(statement);
					if (!has_await(value)) {
						code += emitter.lower(value).statement(p_indent);
					} else {
						line(p_indent, "(void)(" + expression(value, p_indent) + ");");
					}
				} else {
					emitter.unsupported(statement, "async statement kind " + itos(statement->type));
				}
				break;
		}
		clear_temporaries(p_indent);
	}
	if (p_clear_locals) {
		for (const String &field : locals) {
			line(p_indent, field + " = " + field_types[field] + "();");
		}
	}
}

String WGodotCppAsync::generate(const Parser::FunctionNode *p_function, const String &p_name, const Vector<String> &p_parameters, String &r_declaration) {
	emitter.class_native_headers.insert("modules/wgodot/native/wgodot_native_task.h");
	emitter.class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	emitter.class_call_headers.insert("optional");
	const String result_type = emitter.type(p_function->return_type_constraint, p_function);
	const String task_type = "WGodotNative::TaskState<" + result_type + ">";
	const String handle_type = "WGodotNative::Task<" + result_type + ">";
	const String owner = emitter.current_class->cpp_name;
	const bool is_static = p_function->source_lambda ? !p_function->source_lambda->use_self : p_function->is_static;
	const String frame_name = "Async_" + p_name;
	const String resume_name = "resume_" + p_name;
	r_declaration += "\tstruct " + frame_name + ";\n\t" + String(is_static ? "static " : "") + "void " + resume_name + "(" + frame_name + " &frame, " + task_type + " &task);\n";
	String initialize;
	for (const auto *parameter : p_function->parameters) {
		const String field = local(parameter, parameter->identifier->name, parameter->type_constraint);
		initialize += "\t" + field.replace("frame.", "frame->") + " = std::move(v_" + symbol(parameter->identifier->name) + ");\n";
	}
	if (p_function->source_lambda) {
		// Capture references retain their original declaration in the AST. Inside
		// this invocation they address the lambda's synthetic capture parameters.
		for (uint32_t i = 0; i < p_function->source_lambda->captures.size(); i++) {
			const auto *capture = p_function->source_lambda->captures[i];
			emitter.local_overrides.insert(emitter.local_source(capture), emitter.local_overrides[p_function->parameters[i]]);
		}
	}
	suite(p_function->body, 1, false);
	line(1, "task.complete();");
	String definition = "struct " + owner + "::" + frame_name + " : WGodotNative::TaskFrame {\n\tint continuation = 0;\n" + (is_static ? "" : "\tObjectID owner;\n") + fields;
	definition += "\tvoid resume(WGodotNative::NativeTask &task) override {\n";
	if (is_static) {
		definition += "\t\t" + owner + "::" + resume_name + "(*this, static_cast<" + task_type + " &>(task));\n";
	} else {
		definition += "\t\tauto *instance = Object::cast_to<" + owner + ">(ObjectDB::get_instance(owner));\n\t\tif (!instance) { task.cancel(); return; }\n\t\tinstance->" + resume_name + "(*this, static_cast<" + task_type + " &>(task));\n";
	}
	definition += "\t}\n};\n\n";
	String body = "\tauto *frame = memnew(" + frame_name + ");\n" + initialize;
	if (!is_static) {
		emitter.class_uses_tasks = true;
		body += "\tframe->owner = get_instance_id();\n";
	}
	body += "\treturn " + handle_type + "::start(frame" + String(is_static ? "" : ", &game_tasks") + ");\n";
	// The frame must be complete before emitting the start method. Its static
	// preparation is decided once all class resources have been lowered.
	emitter.class_function_definitions.push_back({ handle_type + " " + owner + "::" + p_name + "(" + String(", ").join(p_parameters) + ")", body, is_static });
	definition += "void " + owner + "::" + resume_name + "(" + frame_name + " &frame, " + task_type + " &task) {\n\tswitch (frame.continuation) {\n";
	for (int i = 1; i <= resume_count; i++) {
		definition += "\t\tcase " + itos(i) + ": goto resume_" + itos(i) + ";\n";
	}
	definition += "\t\tdefault: break;\n\t}\n" + code + "}\n\n";
	emitter.local_overrides.clear();
	emitter.expression_overrides.clear();
	return definition;
}
