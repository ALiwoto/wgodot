// wgodot-changes::file

#include "export_ast_visitor.h"

namespace WGodotGDScriptExportTransform {

using Parser = GDScriptParser;

void ExportASTVisitor::walk(const Parser::Node *p_node) {
	visited.clear();
	walk_node(p_node, ExportScope());
}

void ExportASTVisitor::walk_node(const Parser::Node *p_node, ExportScope p_scope) {
	if (p_node == nullptr || visited.has(p_node)) {
		return;
	}
	visited.insert(p_node);
	if (p_node->type == Parser::Node::CLASS) {
		const auto *node = static_cast<const Parser::ClassNode *>(p_node);
		p_scope.current_class = node;
		p_scope.no_mangle |= node->wgodot_no_mangle;
		p_scope.no_string_mangle |= node->wgodot_no_string_mangle;
	} else if (p_node->type == Parser::Node::FUNCTION) {
		const auto *node = static_cast<const Parser::FunctionNode *>(p_node);
		p_scope.no_mangle |= node->wgodot_no_mangle;
		p_scope.no_string_mangle |= node->wgodot_no_string_mangle;
	} else if (p_node->type == Parser::Node::SIGNAL) {
		p_scope.no_mangle |= static_cast<const Parser::SignalNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == Parser::Node::VARIABLE) {
		const auto *node = static_cast<const Parser::VariableNode *>(p_node);
		p_scope.no_mangle |= node->wgodot_no_mangle && node->property != Parser::VariableNode::PROP_NONE;
	}
	if (!enter(p_node, p_scope)) {
		return;
	}
	for (const auto *annotation : p_node->annotations) {
		walk_node(annotation, p_scope);
	}
	switch (p_node->type) {
		case Parser::Node::CLASS: {
			const Parser::ClassNode *node = static_cast<const Parser::ClassNode *>(p_node);
			for (const auto &contract : node->wgodot_implements) {
				walk_node(contract.path_literal, p_scope);
			}
			for (uint32_t i = 0; i < node->wgodot_get_own_member_count(); i++) {
				const Parser::ClassNode::Member &member = node->members[i];
				walk_node(member.type == Parser::ClassNode::Member::ENUM_VALUE ? member.enum_value.parent_enum : member.get_source_node(), p_scope);
			}
		} break;
		case Parser::Node::FUNCTION: {
			const Parser::FunctionNode *node = static_cast<const Parser::FunctionNode *>(p_node);
			for (const Parser::ParameterNode *parameter : node->parameters) {
				walk_node(parameter, p_scope);
			}
			walk_node(node->rest_parameter, p_scope);
			walk_node(node->return_type, p_scope);
			walk_node(node->body, p_scope);
		} break;
		case Parser::Node::SUITE: {
			for (const Parser::Node *statement : static_cast<const Parser::SuiteNode *>(p_node)->statements) {
				walk_node(statement, p_scope);
			}
		} break;
		case Parser::Node::VARIABLE: {
			const Parser::VariableNode *node = static_cast<const Parser::VariableNode *>(p_node);
			walk_node(node->datatype_specifier, p_scope);
			walk_node(node->initializer, p_scope);
			if (node->property == Parser::VariableNode::PROP_INLINE) {
				walk_node(node->setter, p_scope);
				walk_node(node->getter, p_scope);
			}
		} break;
		case Parser::Node::CONSTANT: {
			const auto *node = static_cast<const Parser::ConstantNode *>(p_node);
			walk_node(node->datatype_specifier, p_scope);
			walk_node(node->initializer, p_scope);
		} break;
		case Parser::Node::CALL: {
			const Parser::CallNode *node = static_cast<const Parser::CallNode *>(p_node);
			walk_node(node->callee, p_scope);
			for (const Parser::ExpressionNode *argument : node->arguments) {
				walk_node(argument, p_scope);
			}
		} break;
		case Parser::Node::LAMBDA:
			walk_node(static_cast<const Parser::LambdaNode *>(p_node)->function, p_scope);
			break;
		case Parser::Node::IF: {
			const Parser::IfNode *node = static_cast<const Parser::IfNode *>(p_node);
			walk_node(node->condition, p_scope);
			walk_node(node->true_block, p_scope);
			walk_node(node->false_block, p_scope);
		} break;
		case Parser::Node::FOR: {
			const Parser::ForNode *node = static_cast<const Parser::ForNode *>(p_node);
			walk_node(node->datatype_specifier, p_scope);
			walk_node(node->list, p_scope);
			walk_node(node->loop, p_scope);
		} break;
		case Parser::Node::WHILE: {
			const Parser::WhileNode *node = static_cast<const Parser::WhileNode *>(p_node);
			walk_node(node->condition, p_scope);
			walk_node(node->loop, p_scope);
		} break;
		case Parser::Node::MATCH: {
			const Parser::MatchNode *node = static_cast<const Parser::MatchNode *>(p_node);
			walk_node(node->test, p_scope);
			for (const Parser::MatchBranchNode *branch : node->branches) {
				walk_node(branch, p_scope);
			}
		} break;
		case Parser::Node::RETURN:
			walk_node(static_cast<const Parser::ReturnNode *>(p_node)->return_value, p_scope);
			break;
		case Parser::Node::ASSERT: {
			const Parser::AssertNode *node = static_cast<const Parser::AssertNode *>(p_node);
			walk_node(node->condition, p_scope);
			walk_node(node->message, p_scope);
		} break;
		case Parser::Node::ASSIGNMENT: {
			const Parser::AssignmentNode *node = static_cast<const Parser::AssignmentNode *>(p_node);
			walk_node(node->assignee, p_scope);
			walk_node(node->assigned_value, p_scope);
		} break;
		case Parser::Node::ARRAY: {
			for (const Parser::ExpressionNode *element : static_cast<const Parser::ArrayNode *>(p_node)->elements) {
				walk_node(element, p_scope);
			}
		} break;
		case Parser::Node::DICTIONARY: {
			for (const Parser::DictionaryNode::Pair &pair : static_cast<const Parser::DictionaryNode *>(p_node)->elements) {
				walk_node(pair.key, p_scope);
				walk_node(pair.value, p_scope);
			}
		} break;
		case Parser::Node::SUBSCRIPT: {
			const Parser::SubscriptNode *node = static_cast<const Parser::SubscriptNode *>(p_node);
			walk_node(node->base, p_scope);
			if (!node->is_attribute) {
				walk_node(node->index, p_scope);
			}
		} break;
		case Parser::Node::BINARY_OPERATOR: {
			const Parser::BinaryOpNode *node = static_cast<const Parser::BinaryOpNode *>(p_node);
			walk_node(node->left_operand, p_scope);
			walk_node(node->right_operand, p_scope);
		} break;
		case Parser::Node::UNARY_OPERATOR:
			walk_node(static_cast<const Parser::UnaryOpNode *>(p_node)->operand, p_scope);
			break;
		case Parser::Node::TERNARY_OPERATOR: {
			const Parser::TernaryOpNode *node = static_cast<const Parser::TernaryOpNode *>(p_node);
			walk_node(node->condition, p_scope);
			walk_node(node->true_expr, p_scope);
			walk_node(node->false_expr, p_scope);
		} break;
		case Parser::Node::AWAIT:
			walk_node(static_cast<const Parser::AwaitNode *>(p_node)->to_await, p_scope);
			break;
		case Parser::Node::CAST: {
			const auto *node = static_cast<const Parser::CastNode *>(p_node);
			walk_node(node->operand, p_scope);
			walk_node(node->cast_type, p_scope);
		} break;
		case Parser::Node::TYPE_TEST: {
			const auto *node = static_cast<const Parser::TypeTestNode *>(p_node);
			walk_node(node->operand, p_scope);
			walk_node(node->test_type, p_scope);
		} break;
		case Parser::Node::ANNOTATION:
			for (const auto *argument : static_cast<const Parser::AnnotationNode *>(p_node)->arguments) {
				walk_node(argument, p_scope);
			}
			break;
		case Parser::Node::PARAMETER: {
			const auto *node = static_cast<const Parser::ParameterNode *>(p_node);
			walk_node(node->datatype_specifier, p_scope);
			walk_node(node->initializer, p_scope);
		} break;
		case Parser::Node::SIGNAL:
			for (const auto *parameter : static_cast<const Parser::SignalNode *>(p_node)->parameters) {
				walk_node(parameter, p_scope);
			}
			break;
		case Parser::Node::TYPE:
			for (const auto *type : static_cast<const Parser::TypeNode *>(p_node)->container_types) {
				walk_node(type, p_scope);
			}
			break;
		case Parser::Node::ENUM:
			for (const auto &value : static_cast<const Parser::EnumNode *>(p_node)->values) {
				walk_node(value.custom_value, p_scope);
			}
			break;
		case Parser::Node::PRELOAD:
			walk_node(static_cast<const Parser::PreloadNode *>(p_node)->path, p_scope);
			break;
		case Parser::Node::MATCH_BRANCH: {
			const auto *node = static_cast<const Parser::MatchBranchNode *>(p_node);
			for (const auto *pattern : node->patterns) {
				walk_node(pattern, p_scope);
			}
			walk_node(node->guard_body, p_scope);
			walk_node(node->block, p_scope);
		} break;
		case Parser::Node::PATTERN: {
			const auto *node = static_cast<const Parser::PatternNode *>(p_node);
			switch (node->pattern_type) {
				case Parser::PatternNode::PT_EXPRESSION:
					walk_node(node->expression, p_scope);
					break;
				case Parser::PatternNode::PT_ARRAY:
					for (const auto *element : node->array) {
						walk_node(element, p_scope);
					}
					break;
				case Parser::PatternNode::PT_DICTIONARY:
					for (const auto &pair : node->dictionary) {
						walk_node(pair.key, p_scope);
						walk_node(pair.value_pattern, p_scope);
					}
					break;
				default:
					break;
			}
		} break;
		default:
			break;
	}
}

} // namespace WGodotGDScriptExportTransform
