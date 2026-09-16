// wgodot-changes::file
#include "wgodot_cpp_ast.h"

using Parser = GDScriptParser;

bool WGodotCppAstVisitor::walk(const Parser::Node *p_node) {
	if (!p_node) {
		return true;
	}
	if (!visit(p_node)) {
		return false;
	}
	if (!descend(p_node)) {
		return true;
	}
	switch (p_node->type) {
		case Parser::Node::CLASS: {
			const auto *node = static_cast<const Parser::ClassNode *>(p_node);
			for (const auto &member : node->members) {
				if (!walk(member.get_source_node())) {
					return false;
				}
			}
			return true;
		}
		case Parser::Node::FUNCTION: {
			const auto *node = static_cast<const Parser::FunctionNode *>(p_node);
			for (const auto *parameter : node->parameters) {
				if (!walk(parameter)) {
					return false;
				}
			}
			return walk(node->rest_parameter) && walk(node->body);
		}
		case Parser::Node::VARIABLE: {
			const auto *node = static_cast<const Parser::VariableNode *>(p_node);
			return walk(node->initializer) && (node->property != Parser::VariableNode::PROP_INLINE || (walk(node->getter) && walk(node->setter)));
		}
		case Parser::Node::CONSTANT:
		case Parser::Node::PARAMETER:
			return walk(static_cast<const Parser::AssignableNode *>(p_node)->initializer);
		case Parser::Node::SUITE:
			for (const auto *statement : static_cast<const Parser::SuiteNode *>(p_node)->statements) {
				if (!walk(statement)) {
					return false;
				}
			}
			return true;
		case Parser::Node::ARRAY:
			for (const auto *element : static_cast<const Parser::ArrayNode *>(p_node)->elements) {
				if (!walk(element)) {
					return false;
				}
			}
			return true;
		case Parser::Node::DICTIONARY:
			for (const auto &element : static_cast<const Parser::DictionaryNode *>(p_node)->elements) {
				if (!walk(element.key) || !walk(element.value)) {
					return false;
				}
			}
			return true;
		case Parser::Node::ASSIGNMENT: {
			const auto *node = static_cast<const Parser::AssignmentNode *>(p_node);
			return walk(node->assignee) && walk(node->assigned_value);
		}
		case Parser::Node::AWAIT:
			return walk(static_cast<const Parser::AwaitNode *>(p_node)->to_await);
		case Parser::Node::BINARY_OPERATOR: {
			const auto *node = static_cast<const Parser::BinaryOpNode *>(p_node);
			return walk(node->left_operand) && walk(node->right_operand);
		}
		case Parser::Node::UNARY_OPERATOR:
			return walk(static_cast<const Parser::UnaryOpNode *>(p_node)->operand);
		case Parser::Node::CALL: {
			const auto *node = static_cast<const Parser::CallNode *>(p_node);
			if (!walk(node->callee)) {
				return false;
			}
			for (const auto *argument : node->arguments) {
				if (!walk(argument)) {
					return false;
				}
			}
			return true;
		}
		case Parser::Node::CAST:
			return walk(static_cast<const Parser::CastNode *>(p_node)->operand);
		case Parser::Node::TYPE_TEST:
			return walk(static_cast<const Parser::TypeTestNode *>(p_node)->operand);
		case Parser::Node::FOR: {
			const auto *node = static_cast<const Parser::ForNode *>(p_node);
			return walk(node->list) && walk(node->loop);
		}
		case Parser::Node::WHILE: {
			const auto *node = static_cast<const Parser::WhileNode *>(p_node);
			return walk(node->condition) && walk(node->loop);
		}
		case Parser::Node::IF: {
			const auto *node = static_cast<const Parser::IfNode *>(p_node);
			return walk(node->condition) && walk(node->true_block) && walk(node->false_block);
		}
		case Parser::Node::TERNARY_OPERATOR: {
			const auto *node = static_cast<const Parser::TernaryOpNode *>(p_node);
			return walk(node->condition) && walk(node->true_expr) && walk(node->false_expr);
		}
		case Parser::Node::LAMBDA:
			return walk(static_cast<const Parser::LambdaNode *>(p_node)->function);
		case Parser::Node::RETURN:
			return walk(static_cast<const Parser::ReturnNode *>(p_node)->return_value);
		case Parser::Node::SUBSCRIPT: {
			const auto *node = static_cast<const Parser::SubscriptNode *>(p_node);
			return walk(node->base) && walk(node->is_attribute ? node->attribute : node->index);
		}
		case Parser::Node::PRELOAD:
			return walk(static_cast<const Parser::PreloadNode *>(p_node)->path);
		case Parser::Node::ASSERT: {
			const auto *node = static_cast<const Parser::AssertNode *>(p_node);
			return walk(node->condition) && walk(node->message);
		}
		case Parser::Node::MATCH: {
			const auto *node = static_cast<const Parser::MatchNode *>(p_node);
			if (!walk(node->test)) {
				return false;
			}
			for (const auto *branch : node->branches) {
				if (!walk(branch)) {
					return false;
				}
			}
			return true;
		}
		case Parser::Node::MATCH_BRANCH: {
			const auto *node = static_cast<const Parser::MatchBranchNode *>(p_node);
			for (const auto *pattern : node->patterns) {
				if (!walk(pattern)) {
					return false;
				}
			}
			return walk(node->guard_body) && walk(node->block);
		}
		case Parser::Node::PATTERN: {
			const auto *node = static_cast<const Parser::PatternNode *>(p_node);
			switch (node->pattern_type) {
				case Parser::PatternNode::PT_LITERAL:
					return walk(node->literal);
				case Parser::PatternNode::PT_EXPRESSION:
					return walk(node->expression);
				case Parser::PatternNode::PT_ARRAY:
					for (const auto *element : node->array) {
						if (!walk(element)) {
							return false;
						}
					}
					return true;
				case Parser::PatternNode::PT_DICTIONARY:
					for (const auto &element : node->dictionary) {
						if (!walk(element.key) || !walk(element.value_pattern)) {
							return false;
						}
					}
					return true;
				case Parser::PatternNode::PT_BIND:
				case Parser::PatternNode::PT_REST:
				case Parser::PatternNode::PT_WILDCARD:
					return true;
			}
			return false;
		}
		case Parser::Node::ANNOTATION:
		case Parser::Node::BREAK:
		case Parser::Node::BREAKPOINT:
		case Parser::Node::CONTINUE:
		case Parser::Node::ENUM:
		case Parser::Node::GET_NODE:
		case Parser::Node::IDENTIFIER:
		case Parser::Node::LITERAL:
		case Parser::Node::PASS:
		case Parser::Node::SELF:
		case Parser::Node::SIGNAL:
		case Parser::Node::TYPE:
			return true;
		case Parser::Node::NONE:
			return false;
	}
	return false;
}
