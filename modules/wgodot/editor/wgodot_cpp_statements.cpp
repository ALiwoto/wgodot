// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/string/string_builder.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::suite(const Parser::SuiteNode *p_suite, int p_indent) {
	StringBuilder code;
	const String indent = String("\t").repeat(p_indent);
	for (const Parser::Node *statement : p_suite->statements) {
		switch (statement->type) {
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
