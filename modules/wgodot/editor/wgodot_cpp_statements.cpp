// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/string/string_builder.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::assertion_failure(const Parser::AssertNode *p_assert, const String &p_message, int p_indent, bool p_async) {
	class_call_headers.insert("core/error/error_macros.h");
	const String indent = String("\t").repeat(p_indent);
	String code = indent + "_err_print_error(__FUNCTION__, " + quoted("res://" + itos(resource_id(current_class->script_path))) + ", " + itos(p_assert->start_line) + ", \"Assertion failed.\", " + p_message + ", false, ERR_HANDLER_SCRIPT);\n";
	if (p_async) {
		return code + indent + "task.cancel();\n" + indent + "return;\n";
	}
	const bool returns_void = current_function->identifier && current_function->identifier->name == SNAME("_init");
	return code + indent + (returns_void || type(current_function->return_type_constraint, current_function) == "void" ? "return;\n" : "return {};\n");
}

String WGodotCppEmitter::suite(const Parser::SuiteNode *p_suite, int p_indent) {
	StringBuilder code;
	const String indent = String("\t").repeat(p_indent);
	for (const Parser::Node *statement : p_suite->statements) {
		switch (statement->type) {
			case Parser::Node::ASSERT: {
				const auto *node = static_cast<const Parser::AssertNode *>(statement);
				const String test = "assert_condition_" + itos(temporary_index++);
				const String message = "assert_message_" + itos(temporary_index++);
				code += "#ifdef DEBUG_ENABLED\n";
				code += indent + "const bool " + test + " = " + truth(node->condition) + ";\n";
				// GDScript evaluates the optional message even when the test passes.
				code += indent + "const String " + message + " = " + (node->message ? expression(node->message) : "String()") + ";\n";
				code += indent + "if (!" + test + ") {\n";
				code += assertion_failure(node, message, p_indent + 1, false);
				code += indent + "}\n#endif\n";
				break;
			}
			case Parser::Node::RETURN: {
				const auto *node = static_cast<const Parser::ReturnNode *>(statement);
				if (node->return_value) {
					code += lower_converted(node->return_value, current_function->return_type_constraint, current_function).statement(p_indent, true);
					break;
				}
				const bool implicit_nil = (!current_function->identifier || current_function->identifier->name != SNAME("_init")) && current_function->return_type_constraint.is_variant();
				code += indent + (implicit_nil ? "return Variant();\n" : "return;\n");
				break;
			}
			case Parser::Node::VARIABLE: {
				const auto *node = static_cast<const Parser::VariableNode *>(statement);
				const auto datatype = variable_type(node);
				code += indent + type(datatype, node) + " v_" + symbol(node->identifier->name);
				code += node->initializer ? " = " + converted(node->initializer, datatype, node) + ";\n" : "{};\n";
				break;
			}
			case Parser::Node::CONSTANT:
			case Parser::Node::PASS:
				break;
			case Parser::Node::IF: {
				const auto *node = static_cast<const Parser::IfNode *>(statement);
				code += indent + "if (" + truth(node->condition) + ") {\n";
				code += suite(node->true_block, p_indent + 1) + indent + "}";
				if (node->false_block) {
					code += " else {\n" + suite(node->false_block, p_indent + 1) + indent + "}";
				}
				code += "\n";
				break;
			}
			case Parser::Node::WHILE: {
				const auto *node = static_cast<const Parser::WhileNode *>(statement);
				code += indent + "while (" + truth(node->condition) + ") {\n";
				begin_loop();
				code += suite(node->loop, p_indent + 1) + indent + "}\n";
				code += end_loop(p_indent);
				break;
			}
			case Parser::Node::FOR: {
				code += iteration(static_cast<const Parser::ForNode *>(statement), p_indent);
				break;
			}
			case Parser::Node::BREAK:
				code += indent + loop_break() + "\n";
				break;
			case Parser::Node::CONTINUE:
				code += indent + "continue;\n";
				break;
			case Parser::Node::MATCH: {
				const auto *node = static_cast<const Parser::MatchNode *>(statement);
				code += match_statement(node, p_indent);
				break;
			}
			default:
				if (statement->is_expression()) {
					code += lower(static_cast<const Parser::ExpressionNode *>(statement)).statement(p_indent);
				} else {
					unsupported(statement, "statement kind " + itos(statement->type));
				}
				break;
		}
	}
	return code.as_string();
}
