// wgodot-changes::file
/**************************************************************************/
/*  export_rewriter.cpp                                                   */
/**************************************************************************/

#include "export_transform_internal.h"

#include "deconst_transform.h"
#include "deenum_transform.h"
#include "export_timing.h"
#include "name_obfuscation.h"

#include "../gdscript_tokenizer.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptExportTransform {

void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression, bool p_no_mangle_scope);
void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);

void add_builtin_class_alias_type_replacement(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr || p_type->type_chain.is_empty()) {
		return;
	}

	add_builtin_class_alias_name_replacement(r_context, p_type->type_chain[0]);
}

void collect_type_replacements(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	if (add_enum_type_replacement(r_context, p_type)) {
		return;
	}

	add_builtin_class_alias_type_replacement(r_context, p_type);
	if (!p_type->type_chain.is_empty()) {
		if (p_type->type_chain.size() != 1 || !add_class_member_name_reference_replacement(r_context, p_type->type_chain[0], p_type->get_datatype())) {
			add_global_class_name_reference_replacement(r_context, p_type->type_chain[0]);
		}
	}
	for (int i = 1; i < p_type->type_chain.size(); i++) {
		add_class_member_name_reference_replacement(r_context, p_type->type_chain[i]);
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		collect_type_replacements(r_context, container_type);
	}
}

void add_extends_path_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (r_context.export_context == nullptr || p_class == nullptr || p_class->extends_path.is_empty()) {
		return;
	}

	const String text = get_export_string_literal_replacement(r_context, Variant::STRING, p_class->extends_path);
	if (text.is_empty()) {
		return;
	}

	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(r_context.source);

	bool expect_extends_path = false;
	GDScriptTokenizer::Token token = tokenizer.scan();
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		const bool in_class_range = token.start_line >= p_class->start_line && token.end_line <= p_class->end_line;
		if (!in_class_range) {
			token = tokenizer.scan();
			continue;
		}

		if (expect_extends_path) {
			if (token.type == GDScriptTokenizer::Token::LITERAL && token.literal.get_type() == Variant::STRING && String(token.literal) == p_class->extends_path) {
				const int start = get_offset(r_context, token.start_line, token.start_column);
				const int end = get_offset(r_context, token.end_line, token.end_column);
				if (start >= 0 && end >= start && !overlaps_existing_replacement(r_context, start, end)) {
					Replacement replacement;
					replacement.start = start;
					replacement.end = end;
					replacement.text = text;
					r_context.replacements.push_back(replacement);
				}
				return;
			}
			expect_extends_path = false;
		}

		if (token.type == GDScriptTokenizer::Token::EXTENDS) {
			expect_extends_path = true;
		}

		token = tokenizer.scan();
	}
}

void collect_extends_replacements(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}

	add_extends_path_replacement(r_context, p_class);

	for (const GDScriptParser::IdentifierNode *identifier : p_class->extends) {
		add_builtin_class_alias_name_replacement(r_context, identifier);
		add_global_class_name_reference_replacement(r_context, identifier);
	}
}

bool is_no_mangle_property_scope(const GDScriptParser::VariableNode *p_variable) {
	return p_variable != nullptr && p_variable->wgodot_no_mangle && (p_variable->setter != nullptr || p_variable->getter != nullptr);
}

bool is_removed_export_member(const RewriteContext &p_context, const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return should_deconst_constant(p_context, p_member.constant);
		case GDScriptParser::ClassNode::Member::ENUM:
			return should_deenum_enum(p_context, p_member.m_enum);
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return should_deenum_enum(p_context, p_member.enum_value.parent_enum);
		default:
			return false;
	}
}

void collect_parameter_replacements(RewriteContext &r_context, const GDScriptParser::ParameterNode *p_parameter, bool p_no_mangle_scope) {
	if (p_parameter == nullptr) {
		return;
	}

	collect_type_replacements(r_context, p_parameter->datatype_specifier);
	collect_expression_replacements(r_context, p_parameter->initializer, p_no_mangle_scope);
}

void collect_pattern_replacements(RewriteContext &r_context, const GDScriptParser::PatternNode *p_pattern, bool p_no_mangle_scope) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			collect_expression_replacements(r_context, p_pattern->expression, p_no_mangle_scope);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (const GDScriptParser::PatternNode *sub_pattern : p_pattern->array) {
				collect_pattern_replacements(r_context, sub_pattern, p_no_mangle_scope);
			}
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				collect_expression_replacements(r_context, pair.key, p_no_mangle_scope);
				collect_pattern_replacements(r_context, pair.value_pattern, p_no_mangle_scope);
			}
			break;
		default:
			break;
	}
}

void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression, bool p_no_mangle_scope) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::LITERAL:
			add_string_literal_replacement(r_context, static_cast<const GDScriptParser::LiteralNode *>(p_expression));
			break;
		case GDScriptParser::Node::IDENTIFIER: {
			const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
			if (add_enum_identifier_reference_replacement(r_context, identifier)) {
				break;
			}
			if (is_declared_constant_identifier(r_context, identifier)) {
				add_constant_reference_replacement(r_context, identifier, identifier->constant_source);
			} else {
				add_builtin_class_alias_reference_replacement(r_context, identifier);
				add_global_class_name_reference_replacement(r_context, identifier);
				add_member_name_reference_replacement(r_context, identifier);
				if (!p_no_mangle_scope) {
					add_local_name_reference_replacement(r_context, identifier);
				}
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (add_enum_attribute_reference_replacement(r_context, subscript)) {
				break;
			}
			bool replaced_whole_constant_index = false;
			if (add_constant_indexed_reference_replacement(r_context, subscript, replaced_whole_constant_index)) {
				if (!replaced_whole_constant_index) {
					collect_expression_replacements(r_context, subscript->index, p_no_mangle_scope);
				}
				break;
			}
			if (subscript->is_attribute && is_declared_constant_identifier(r_context, subscript->attribute)) {
				add_constant_reference_replacement(r_context, subscript, subscript->attribute->constant_source);
				break;
			}

			collect_expression_replacements(r_context, subscript->base, p_no_mangle_scope);
			// Attribute names are member/property lookups on the base expression, not local
			// variable references. Local name obfuscation must not turn `position.x`
			// into `position.a0`.
			if (!subscript->is_attribute) {
				collect_expression_replacements(r_context, subscript->index, p_no_mangle_scope);
			} else {
				add_builtin_property_alias_reference_replacement(r_context, subscript->base, subscript->attribute);
				add_global_class_name_reference_replacement(r_context, subscript->attribute);
				add_attribute_member_name_reference_replacement(r_context, subscript->base, subscript->attribute);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_expression_replacements(r_context, element, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_expression_replacements(r_context, assignment->assignee, p_no_mangle_scope);
			collect_expression_replacements(r_context, assignment->assigned_value, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			if (add_string_concat_replacement(r_context, binary)) {
				break;
			}
			collect_expression_replacements(r_context, binary->left_operand, p_no_mangle_scope);
			collect_expression_replacements(r_context, binary->right_operand, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			add_builtin_function_alias_call_replacement(r_context, call);
			add_builtin_method_alias_call_replacement(r_context, call);
			collect_expression_replacements(r_context, call->callee, p_no_mangle_scope);
			add_call_member_name_reference_replacement(r_context, call);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_expression_replacements(r_context, argument, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_expression_replacements(r_context, cast->operand, p_no_mangle_scope);
			collect_type_replacements(r_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_expression_replacements(r_context, pair.key, p_no_mangle_scope);
				collect_expression_replacements(r_context, pair.value, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::GET_NODE:
			break;
		case GDScriptParser::Node::LAMBDA:
			collect_node_replacements(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::PRELOAD:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_expression_replacements(r_context, ternary->condition, p_no_mangle_scope);
			collect_expression_replacements(r_context, ternary->true_expr, p_no_mangle_scope);
			collect_expression_replacements(r_context, ternary->false_expr, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			collect_expression_replacements(r_context, type_test->operand, p_no_mangle_scope);
			collect_type_replacements(r_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand, p_no_mangle_scope);
			break;
		default:
			break;
	}
}

void collect_annotation_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (p_node == nullptr) {
		return;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		if (should_strip_export_annotation(annotation)) {
			add_annotation_strip_replacement(r_context, annotation);
		}
		for (const GDScriptParser::ExpressionNode *argument : annotation->arguments) {
			collect_expression_replacements(r_context, argument, p_no_mangle_scope);
		}
	}
}

void collect_constant_contents_replacements(RewriteContext &r_context, const GDScriptParser::ConstantNode *p_constant, bool p_no_mangle_scope) {
	if (p_constant == nullptr) {
		return;
	}

	collect_type_replacements(r_context, p_constant->datatype_specifier);
	collect_expression_replacements(r_context, p_constant->initializer, p_no_mangle_scope);
}

void collect_enum_contents_replacements(RewriteContext &r_context, const GDScriptParser::EnumNode *p_enum, bool p_no_mangle_scope) {
	if (p_enum == nullptr || r_context.visited_enum_declarations.has(p_enum)) {
		return;
	}

	r_context.visited_enum_declarations.insert(p_enum);
	collect_annotation_replacements(r_context, p_enum, p_no_mangle_scope);
	for (const GDScriptParser::EnumNode::Value &value : p_enum->values) {
		collect_expression_replacements(r_context, value.custom_value, p_no_mangle_scope);
	}
}

void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (p_node == nullptr) {
		return;
	}

	bool no_mangle_scope = p_no_mangle_scope;
	const bool previous_no_string_mangle_scope = r_context.no_string_mangle_scope;
	if (p_node->type == GDScriptParser::Node::CLASS) {
		const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
		no_mangle_scope = no_mangle_scope || class_node->wgodot_no_mangle;
		r_context.no_string_mangle_scope = r_context.no_string_mangle_scope || class_node->wgodot_no_string_mangle;
	} else if (p_node->type == GDScriptParser::Node::FUNCTION) {
		const GDScriptParser::FunctionNode *function_node = static_cast<const GDScriptParser::FunctionNode *>(p_node);
		no_mangle_scope = no_mangle_scope || function_node->wgodot_no_mangle;
		r_context.no_string_mangle_scope = r_context.no_string_mangle_scope || function_node->wgodot_no_string_mangle;
	} else if (p_node->type == GDScriptParser::Node::SIGNAL) {
		no_mangle_scope = no_mangle_scope || static_cast<const GDScriptParser::SignalNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == GDScriptParser::Node::VARIABLE) {
		no_mangle_scope = no_mangle_scope || is_no_mangle_property_scope(static_cast<const GDScriptParser::VariableNode *>(p_node));
	}

	collect_annotation_replacements(r_context, p_node, no_mangle_scope);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			const GDScriptParser::ClassNode *previous_class = r_context.current_class;
			r_context.current_class = class_node;
			add_class_declaration_name_replacement(r_context, class_node);
			collect_extends_replacements(r_context, class_node);
			bool has_removed_members = false;
			bool has_remaining_members = false;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				if (is_removed_export_member(r_context, member)) {
					has_removed_members = true;
				} else {
					has_remaining_members = true;
				}
			}

			bool leave_pass_for_first_removed_member = class_node->outer != nullptr && has_removed_members && !has_remaining_members;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				switch (member.type) {
					case GDScriptParser::ClassNode::Member::CLASS:
						collect_node_replacements(r_context, member.m_class, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::CONSTANT:
						if (should_deconst_constant(r_context, member.constant)) {
							add_constant_declaration_replacement(r_context, member.constant, leave_pass_for_first_removed_member);
							leave_pass_for_first_removed_member = false;
						} else {
							collect_annotation_replacements(r_context, member.constant, no_mangle_scope);
							collect_constant_contents_replacements(r_context, member.constant, no_mangle_scope);
						}
						break;
					case GDScriptParser::ClassNode::Member::FUNCTION:
						collect_node_replacements(r_context, member.function, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::VARIABLE:
						if (!no_mangle_scope && member.variable != nullptr && member.variable->property == GDScriptParser::VariableNode::PROP_SETGET) {
							add_function_pointer_replacement(r_context, class_node, member.variable->setter_pointer);
							add_function_pointer_replacement(r_context, class_node, member.variable->getter_pointer);
						}
						collect_node_replacements(r_context, member.variable, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::SIGNAL:
						collect_node_replacements(r_context, member.signal, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::ENUM:
						if (should_deenum_enum(r_context, member.m_enum)) {
							if (add_enum_declaration_replacement(r_context, member.m_enum, leave_pass_for_first_removed_member)) {
								leave_pass_for_first_removed_member = false;
							}
						} else {
							collect_enum_contents_replacements(r_context, member.m_enum, no_mangle_scope);
						}
						break;
					case GDScriptParser::ClassNode::Member::ENUM_VALUE:
						if (should_deenum_enum(r_context, member.enum_value.parent_enum)) {
							if (add_enum_declaration_replacement(r_context, member.enum_value.parent_enum, leave_pass_for_first_removed_member)) {
								leave_pass_for_first_removed_member = false;
							}
						} else {
							collect_enum_contents_replacements(r_context, member.enum_value.parent_enum, no_mangle_scope);
						}
						break;
					default:
						break;
				}
			}
			r_context.current_class = previous_class;
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			if (should_deconst_constant(r_context, constant)) {
				add_constant_declaration_replacement(r_context, constant, false);
			} else {
				collect_constant_contents_replacements(r_context, constant, no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::FUNCTION: {
			const GDScriptParser::FunctionNode *function = static_cast<const GDScriptParser::FunctionNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				collect_parameter_replacements(r_context, parameter, no_mangle_scope);
			}
			collect_type_replacements(r_context, function->return_type);
			collect_node_replacements(r_context, function->body, no_mangle_scope);
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			if (!no_mangle_scope) {
				add_signal_parameter_name_replacements(r_context, signal);
			}
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				collect_parameter_replacements(r_context, parameter, no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			if (!no_mangle_scope) {
				collect_suite_local_name_obfuscation(r_context, suite);
			}

			bool has_mangled_constants = false;
			bool has_remaining_statements = false;
			for (const GDScriptParser::Node *statement : suite->statements) {
				const GDScriptParser::ConstantNode *constant = statement != nullptr && statement->type == GDScriptParser::Node::CONSTANT ? static_cast<const GDScriptParser::ConstantNode *>(statement) : nullptr;
				if (should_deconst_constant(r_context, constant)) {
					has_mangled_constants = true;
				} else {
					has_remaining_statements = true;
				}
			}

			bool leave_pass_for_first_constant = has_mangled_constants && !has_remaining_statements;
			for (const GDScriptParser::Node *statement : suite->statements) {
				const GDScriptParser::ConstantNode *constant = statement != nullptr && statement->type == GDScriptParser::Node::CONSTANT ? static_cast<const GDScriptParser::ConstantNode *>(statement) : nullptr;
				if (should_deconst_constant(r_context, constant)) {
					add_constant_declaration_replacement(r_context, constant, leave_pass_for_first_constant);
					leave_pass_for_first_constant = false;
				} else {
					collect_node_replacements(r_context, statement, no_mangle_scope);
				}
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			collect_type_replacements(r_context, variable->datatype_specifier);
			collect_expression_replacements(r_context, variable->initializer, no_mangle_scope);
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE && variable->setter != nullptr) {
				collect_node_replacements(r_context, variable->setter, no_mangle_scope);
			}
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE && variable->getter != nullptr) {
				collect_node_replacements(r_context, variable->getter, no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_node);
			collect_expression_replacements(r_context, assert_node->condition, no_mangle_scope);
			collect_expression_replacements(r_context, assert_node->message, no_mangle_scope);
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_node);
			collect_type_replacements(r_context, for_node->datatype_specifier);
			collect_expression_replacements(r_context, for_node->list, no_mangle_scope);
			collect_node_replacements(r_context, for_node->loop, no_mangle_scope);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_node);
			collect_expression_replacements(r_context, if_node->condition, no_mangle_scope);
			collect_node_replacements(r_context, if_node->true_block, no_mangle_scope);
			collect_node_replacements(r_context, if_node->false_block, no_mangle_scope);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match = static_cast<const GDScriptParser::MatchNode *>(p_node);
			collect_expression_replacements(r_context, match->test, no_mangle_scope);
			for (const GDScriptParser::MatchBranchNode *branch : match->branches) {
				collect_node_replacements(r_context, branch, no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::MATCH_BRANCH: {
			const GDScriptParser::MatchBranchNode *branch = static_cast<const GDScriptParser::MatchBranchNode *>(p_node);
			for (const GDScriptParser::PatternNode *pattern : branch->patterns) {
				collect_pattern_replacements(r_context, pattern, no_mangle_scope);
			}
			collect_node_replacements(r_context, branch->guard_body, no_mangle_scope);
			collect_node_replacements(r_context, branch->block, no_mangle_scope);
		} break;
		case GDScriptParser::Node::RETURN:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::ReturnNode *>(p_node)->return_value, no_mangle_scope);
			break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_node);
			collect_expression_replacements(r_context, while_node->condition, no_mangle_scope);
			collect_node_replacements(r_context, while_node->loop, no_mangle_scope);
		} break;
		default:
			if (p_node->is_expression()) {
				collect_expression_replacements(r_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node), no_mangle_scope);
			}
			break;
	}

	r_context.no_string_mangle_scope = previous_no_string_mangle_scope;
}

void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (!r_context.options.obfuscate_names || p_node == nullptr) {
		return;
	}

	if (p_node->type == GDScriptParser::Node::CLASS) {
		collect_member_name_obfuscation(r_context, static_cast<const GDScriptParser::ClassNode *>(p_node), p_no_mangle_scope);
	}
}

void collect_export_replacements(RewriteContext &r_context, const GDScriptParser &p_parser, ExportReplacementTiming *r_timing) {
	const GDScriptParser::ClassNode *tree = p_parser.get_tree();
	uint64_t phase_start_usec = r_timing != nullptr ? export_timing_get_ticks_usec() : 0;
	build_line_offsets(r_context);
	if (r_timing != nullptr) {
		r_timing->build_line_offsets_usec = export_timing_get_ticks_usec() - phase_start_usec;
	}

	phase_start_usec = r_timing != nullptr ? export_timing_get_ticks_usec() : 0;
	collect_member_names(r_context, tree, false);
	if (r_timing != nullptr) {
		r_timing->member_names_usec = export_timing_get_ticks_usec() - phase_start_usec;
	}

	phase_start_usec = r_timing != nullptr ? export_timing_get_ticks_usec() : 0;
	collect_node_replacements(r_context, tree, false);
	if (r_timing != nullptr) {
		r_timing->node_replacements_usec = export_timing_get_ticks_usec() - phase_start_usec;
	}

	phase_start_usec = r_timing != nullptr ? export_timing_get_ticks_usec() : 0;
	collect_comment_replacements(r_context, p_parser);
	if (r_timing != nullptr) {
		r_timing->comment_replacements_usec = export_timing_get_ticks_usec() - phase_start_usec;
	}

	phase_start_usec = r_timing != nullptr ? export_timing_get_ticks_usec() : 0;
	collect_empty_line_replacements(r_context);
	if (r_timing != nullptr) {
		r_timing->empty_line_replacements_usec = export_timing_get_ticks_usec() - phase_start_usec;
	}
}

void collect_string_obfuscation_resources(const String &p_source, const String &p_path, ExportContext *p_context) {
	if (p_context == nullptr || !p_context->get_options().obfuscate_strings) {
		return;
	}

	AnalyzedSource analyzed_source;
	String analysis_error;
	if (!analyzed_source.load(p_source, p_path, &analysis_error)) {
		WARN_PRINT("Failed to analyze wgodot-transformed GDScript while collecting string obfuscation resources for '" + p_path + "'. Some string resources may be incomplete for this script.\n" + analysis_error);
		return;
	}

	collect_string_obfuscation_resources_from_tree(p_source, p_path, analyzed_source.parser->get_tree(), p_context);
}

void collect_string_obfuscation_resources_from_tree(const String &p_source, const String &p_path, const GDScriptParser::ClassNode *p_tree, ExportContext *p_context) {
	if (p_context == nullptr || !p_context->get_options().obfuscate_strings || p_tree == nullptr) {
		return;
	}

	TransformOptions options = p_context->get_options();
	// Only string/path literal resource side effects are needed in this prescan pass.
	// Keep de-const enabled so strings introduced by const inlining reserve the same
	// string resources before the export resource map is serialized.
	options.obfuscate_names = false;
	options.obfuscate_builtin_names = false;
	options.strip_comments = false;
	options.strip_empty_lines = false;

	RewriteContext context;
	context.source = p_source;
	context.script_path = p_path;
	context.options = options;
	context.export_context = p_context;
	build_line_offsets(context);
	collect_node_replacements(context, p_tree, false);
}

bool has_obfuscate_path_annotation(const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return false;
	}

	if (p_class->wgodot_obfuscate_path) {
		return true;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS && has_obfuscate_path_annotation(member.m_class)) {
			return true;
		}
	}

	return false;
}

void reserve_script_global_class_name_from_source(ExportContext *p_context, const String &p_source, const String &p_path) {
	if (p_context == nullptr) {
		return;
	}

	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false, false) != OK) {
		return;
	}

	p_context->reserve_script_global_class_name(parser.get_tree());
}

void collect_global_class_rename_request(ExportContext *p_context, const GDScriptParser::ClassNode *p_class, const String &p_path, Vector<GlobalClassRenameRequest> &r_requests) {
	if (p_context == nullptr || p_class == nullptr) {
		return;
	}

	p_context->reserve_script_global_class_name(p_class);
	p_context->reserve_script_declaration_names_for_global_classes(p_class);

	if (p_class->outer != nullptr ||
			p_class->identifier == nullptr ||
			p_class->identifier->name.is_empty() ||
			!p_class->wgodot_obfuscate ||
			p_class->wgodot_no_mangle ||
			p_class->fqcn.begins_with("res://")) {
		return;
	}

	GlobalClassRenameRequest request;
	request.name = p_class->identifier->name;
	request.path = p_path;
	r_requests.push_back(request);
}

} // namespace WGodotGDScriptExportTransform
