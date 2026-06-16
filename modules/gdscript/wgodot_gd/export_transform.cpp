// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "deconst_transform.h"
#include "export_context.h"
#include "name_obfuscation.h"
#include "source_rewrite.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace {

using namespace WGodotGDScriptExportTransform;

struct AnalyzedSource {
	GDScriptParser local_parser;
	Ref<GDScriptParserRef> cached_parser_ref;
	GDScriptParser *parser = nullptr;

	bool load(const String &p_source, const String &p_path) {
		if (load_from_cache(p_source, p_path)) {
			return true;
		}

		return load_local(p_source, p_path);
	}

private:
	bool load_from_cache(const String &p_source, const String &p_path) {
		if (!GDScriptCache::has_parser(p_path)) {
			return false;
		}

		Error err = OK;
		cached_parser_ref = GDScriptCache::get_parser(p_path, GDScriptParserRef::FULLY_SOLVED, err);
		if (err != OK || cached_parser_ref.is_null()) {
			cached_parser_ref.unref();
			return false;
		}

		if (cached_parser_ref->get_source_hash() != p_source.hash()) {
			cached_parser_ref.unref();
			return false;
		}

		parser = cached_parser_ref->get_parser();
		return parser != nullptr;
	}

	bool load_local(const String &p_source, const String &p_path) {
		if (local_parser.parse(p_source, p_path, false) != OK) {
			return false;
		}

		GDScriptAnalyzer analyzer(&local_parser);
		if (analyzer.analyze() != OK) {
			return false;
		}

		parser = &local_parser;
		return true;
	}
};

struct ScriptSource {
	String path;
	String source;
};

struct GlobalClassRenameRequest {
	StringName name;
	String path;
};

void collect_no_mangle_constants(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression, bool p_no_mangle_scope);
void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);

void collect_type_replacements(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	if (!p_type->type_chain.is_empty()) {
		add_global_class_name_reference_replacement(r_context, p_type->type_chain[0]);
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		collect_type_replacements(r_context, container_type);
	}
}

void collect_extends_replacements(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}

	for (const GDScriptParser::IdentifierNode *identifier : p_class->extends) {
		add_global_class_name_reference_replacement(r_context, identifier);
	}
}

bool is_no_mangle_property_scope(const GDScriptParser::VariableNode *p_variable) {
	return p_variable != nullptr && p_variable->wgodot_no_mangle && (p_variable->setter != nullptr || p_variable->getter != nullptr);
}

bool should_strip_export_annotation(const GDScriptParser::AnnotationNode *p_annotation) {
	if (p_annotation == nullptr) {
		return false;
	}

	static const StringName stripped_annotations[] = {
		SNAME("@private"),
		SNAME("@no_mangle"),
		SNAME("@obfuscate"),
	};

	for (const StringName &annotation_name : stripped_annotations) {
		if (p_annotation->name == annotation_name) {
			return true;
		}
	}

	return false;
}

void add_annotation_strip_replacement(RewriteContext &r_context, const GDScriptParser::AnnotationNode *p_annotation) {
	ERR_FAIL_NULL(p_annotation);

	int start = get_offset(r_context, p_annotation->start_line, p_annotation->start_column);
	int end = get_offset(r_context, p_annotation->end_line, p_annotation->end_column);
	if (start < 0 || end < start) {
		return;
	}

	const int line_start = get_line_start_offset(r_context, p_annotation->start_line);
	const int line_end = get_line_end_offset(r_context, p_annotation->end_line);
	if (line_start >= 0 && line_end >= end) {
		const String before = r_context.source.substr(line_start, start - line_start).strip_edges();
		const String after = r_context.source.substr(end, line_end - end).strip_edges();
		if (before.is_empty() && (after.is_empty() || after.begins_with("#"))) {
			start = line_start;
			if (p_annotation->end_line < r_context.line_offsets.size()) {
				end = r_context.line_offsets[p_annotation->end_line];
			} else {
				end = line_end;
			}
		}
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = "";
	r_context.replacements.push_back(replacement);
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
		case GDScriptParser::Node::IDENTIFIER: {
			const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
			if (is_declared_constant_identifier(r_context, identifier)) {
				add_constant_reference_replacement(r_context, identifier, identifier->constant_source);
			} else {
				add_global_class_name_reference_replacement(r_context, identifier);
				add_member_name_reference_replacement(r_context, identifier);
				if (!p_no_mangle_scope) {
					add_local_name_reference_replacement(r_context, identifier);
				}
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
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
			collect_expression_replacements(r_context, binary->left_operand, p_no_mangle_scope);
			collect_expression_replacements(r_context, binary->right_operand, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
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

void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (p_node == nullptr) {
		return;
	}

	bool no_mangle_scope = p_no_mangle_scope;
	if (p_node->type == GDScriptParser::Node::CLASS) {
		no_mangle_scope = no_mangle_scope || static_cast<const GDScriptParser::ClassNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == GDScriptParser::Node::FUNCTION) {
		no_mangle_scope = no_mangle_scope || static_cast<const GDScriptParser::FunctionNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == GDScriptParser::Node::VARIABLE) {
		no_mangle_scope = no_mangle_scope || is_no_mangle_property_scope(static_cast<const GDScriptParser::VariableNode *>(p_node));
	}

	collect_annotation_replacements(r_context, p_node, no_mangle_scope);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			add_class_declaration_name_replacement(r_context, class_node);
			collect_extends_replacements(r_context, class_node);
			bool has_mangled_constants = false;
			bool has_remaining_members = false;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				if (member.type == GDScriptParser::ClassNode::Member::CONSTANT && should_deconst_constant(r_context, member.constant)) {
					has_mangled_constants = true;
				} else {
					has_remaining_members = true;
				}
			}

			bool leave_pass_for_first_constant = class_node->outer != nullptr && has_mangled_constants && !has_remaining_members;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				switch (member.type) {
					case GDScriptParser::ClassNode::Member::CLASS:
						collect_node_replacements(r_context, member.m_class, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::CONSTANT:
						if (should_deconst_constant(r_context, member.constant)) {
							add_constant_declaration_replacement(r_context, member.constant, leave_pass_for_first_constant);
							leave_pass_for_first_constant = false;
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
						for (const GDScriptParser::EnumNode::Value &value : member.m_enum->values) {
							collect_expression_replacements(r_context, value.custom_value, no_mangle_scope);
						}
						break;
					default:
						break;
				}
			}
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
}

void collect_no_mangle_constants_in_expression(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression, bool p_no_mangle_scope) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_no_mangle_constants_in_expression(r_context, element, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, assignment->assignee, p_no_mangle_scope);
			collect_no_mangle_constants_in_expression(r_context, assignment->assigned_value, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_no_mangle_constants_in_expression(r_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, binary->left_operand, p_no_mangle_scope);
			collect_no_mangle_constants_in_expression(r_context, binary->right_operand, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, call->callee, p_no_mangle_scope);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_no_mangle_constants_in_expression(r_context, argument, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, cast->operand, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_no_mangle_constants_in_expression(r_context, pair.key, p_no_mangle_scope);
				collect_no_mangle_constants_in_expression(r_context, pair.value, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::LAMBDA:
			collect_no_mangle_constants(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::PRELOAD:
			collect_no_mangle_constants_in_expression(r_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, subscript->base, p_no_mangle_scope);
			if (!subscript->is_attribute) {
				collect_no_mangle_constants_in_expression(r_context, subscript->index, p_no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_no_mangle_constants_in_expression(r_context, ternary->condition, p_no_mangle_scope);
			collect_no_mangle_constants_in_expression(r_context, ternary->true_expr, p_no_mangle_scope);
			collect_no_mangle_constants_in_expression(r_context, ternary->false_expr, p_no_mangle_scope);
		} break;
		case GDScriptParser::Node::TYPE_TEST:
			collect_no_mangle_constants_in_expression(r_context, static_cast<const GDScriptParser::TypeTestNode *>(p_expression)->operand, p_no_mangle_scope);
			break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_no_mangle_constants_in_expression(r_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand, p_no_mangle_scope);
			break;
		default:
			break;
	}
}

void collect_no_mangle_constants(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (p_node == nullptr) {
		return;
	}

	bool no_mangle_scope = p_no_mangle_scope;
	if (p_node->type == GDScriptParser::Node::CLASS) {
		no_mangle_scope = no_mangle_scope || static_cast<const GDScriptParser::ClassNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == GDScriptParser::Node::FUNCTION) {
		no_mangle_scope = no_mangle_scope || static_cast<const GDScriptParser::FunctionNode *>(p_node)->wgodot_no_mangle;
	} else if (p_node->type == GDScriptParser::Node::VARIABLE) {
		no_mangle_scope = no_mangle_scope || is_no_mangle_property_scope(static_cast<const GDScriptParser::VariableNode *>(p_node));
	}

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				switch (member.type) {
					case GDScriptParser::ClassNode::Member::CLASS:
						collect_no_mangle_constants(r_context, member.m_class, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::CONSTANT:
						collect_no_mangle_constants(r_context, member.constant, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::FUNCTION:
						collect_no_mangle_constants(r_context, member.function, no_mangle_scope);
						break;
					case GDScriptParser::ClassNode::Member::VARIABLE:
						collect_no_mangle_constants(r_context, member.variable, no_mangle_scope);
						break;
					default:
						break;
				}
			}
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			if (no_mangle_scope || constant->wgodot_no_mangle) {
				r_context.no_mangle_constants.insert(constant);
			}
			collect_no_mangle_constants_in_expression(r_context, constant->initializer, no_mangle_scope || constant->wgodot_no_mangle);
		} break;
		case GDScriptParser::Node::FUNCTION:
			collect_no_mangle_constants(r_context, static_cast<const GDScriptParser::FunctionNode *>(p_node)->body, no_mangle_scope);
			break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			for (const GDScriptParser::Node *statement : suite->statements) {
				collect_no_mangle_constants(r_context, statement, no_mangle_scope);
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			collect_no_mangle_constants_in_expression(r_context, variable->initializer, no_mangle_scope);
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
				collect_no_mangle_constants(r_context, variable->setter, no_mangle_scope);
				collect_no_mangle_constants(r_context, variable->getter, no_mangle_scope);
			}
		} break;
		default:
			if (p_node->is_expression()) {
				collect_no_mangle_constants_in_expression(r_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node), no_mangle_scope);
			}
			break;
	}
}

void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (!r_context.options.obfuscate_names || p_node == nullptr) {
		return;
	}

	if (p_node->type == GDScriptParser::Node::CLASS) {
		collect_member_name_obfuscation(r_context, static_cast<const GDScriptParser::ClassNode *>(p_node), p_no_mangle_scope);
	}
}

String get_parser_errors_text(const GDScriptParser &p_parser) {
	const List<GDScriptParser::ParserError> &errors = p_parser.get_errors();
	if (errors.is_empty()) {
		return "no parser/analyzer error details were reported";
	}

	String details;
	const int max_errors = 5;
	int error_count = 0;
	for (const GDScriptParser::ParserError &error : errors) {
		if (error_count >= max_errors) {
			details += "\n  ...";
			break;
		}

		if (!details.is_empty()) {
			details += "\n";
		}
		details += vformat("  line %d, column %d: %s", error.start_line, error.start_column, error.message);
		error_count++;
	}

	return details;
}

bool parse_only(const String &p_source, const String &p_path, String *r_error_details = nullptr) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_text(parser);
		}
		return false;
	}

	return true;
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

} // namespace

namespace WGodotGDScriptExportTransform {

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "debug/gdscript/wgodot/deconst_exports");
	options.obfuscate_names = GLOBAL_GET_CACHED(bool, "debug/gdscript/wgodot/obfuscate_names");

	const int obfuscation_strategy = GLOBAL_GET_CACHED(int, "debug/gdscript/wgodot/obfuscation_strategy");
	if (obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.obfuscation_strategy = static_cast<ObfuscationStrategy>(obfuscation_strategy);
	}

	return options;
}

void prescan_project_scripts(ExportContext *p_context, const HashSet<String> &p_paths) {
	if (p_context == nullptr) {
		return;
	}

	const TransformOptions options = setup_params();
	p_context->set_options(options);
	if (!options.obfuscate_names) {
		return;
	}

	Vector<ScriptSource> scripts;
	Vector<GlobalClassRenameRequest> global_class_rename_requests;
	for (const String &path : p_paths) {
		if (path.get_extension() != "gd") {
			continue;
		}

		const Vector<uint8_t> file = FileAccess::get_file_as_bytes(path);
		if (file.is_empty()) {
			continue;
		}

		const String source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
		ScriptSource script;
		script.path = path;
		script.source = source;
		scripts.push_back(script);

		AnalyzedSource analyzed_source;
		if (!analyzed_source.load(source, path)) {
			reserve_script_global_class_name_from_source(p_context, source, path);
			continue;
		}

		collect_global_class_rename_request(p_context, analyzed_source.parser->get_tree(), path, global_class_rename_requests);
	}

	for (const GlobalClassRenameRequest &request : global_class_rename_requests) {
		(void)p_context->get_or_create_global_class_rename(request.name, request.path);
	}

	for (const ScriptSource &script : scripts) {
		AnalyzedSource analyzed_source;
		if (!analyzed_source.load(script.source, script.path)) {
			continue;
		}

		p_context->index_script(analyzed_source.parser->get_tree(), script.path);
	}
}

void transform_global_class_list(ExportContext *p_context, Array *r_global_class_list) {
	if (p_context == nullptr || r_global_class_list == nullptr) {
		return;
	}

	for (int i = 0; i < r_global_class_list->size(); i++) {
		Dictionary class_dict = (*r_global_class_list)[i];

		if (class_dict.has("class")) {
			const StringName class_name = class_dict["class"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename(class_name)) {
				class_dict["class"] = *obfuscated_name;
			}
		}

		if (class_dict.has("base")) {
			const StringName base_name = class_dict["base"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename(base_name)) {
				class_dict["base"] = *obfuscated_name;
			}
		}

		if (class_dict.has("path")) {
			const String path = class_dict["path"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename_by_path(path)) {
				class_dict["class"] = *obfuscated_name;
			}
		}

		(*r_global_class_list)[i] = class_dict;
	}
}

String transform_source(const String &p_source, const String &p_path, bool *r_changed) {
	return transform_source(p_source, p_path, setup_params(), nullptr, r_changed);
}

String transform_source(const String &p_source, const String &p_path, ExportContext *p_context, bool *r_changed) {
	return transform_source(p_source, p_path, setup_params(), p_context, r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed) {
	return transform_source(p_source, p_path, p_options, nullptr, r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, ExportContext *p_context, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.deconst_exports && !p_options.obfuscate_names) {
		return p_source;
	}

	AnalyzedSource analyzed_source;
	if (!analyzed_source.load(p_source, p_path)) {
		return p_source;
	}

	ExportContext local_context;
	ExportContext *export_context = p_context;
	if (export_context == nullptr && p_options.obfuscate_names) {
		local_context.reset();
		export_context = &local_context;
	}
	if (export_context != nullptr) {
		export_context->set_options(p_options);
	}

	const GDScriptParser::ClassNode *tree = analyzed_source.parser->get_tree();

	RewriteContext context;
	context.source = p_source;
	context.script_path = p_path;
	context.options = p_options;
	context.export_context = export_context;
	context.obfuscation_random.randomize();
	if (context.export_context != nullptr) {
		context.export_context->reserve_script_global_class_name(tree);
		context.export_context->reserve_script_declaration_names_for_global_classes(tree);
		context.export_context->index_global_class_rename(tree, p_path);
		context.export_context->seed_reserved_obfuscated_names(context.reserved_obfuscated_names);
	}
	build_line_offsets(context);
	collect_no_mangle_constants(context, tree, false);
	collect_member_names(context, tree, false);
	collect_node_replacements(context, tree, false);
	if (context.replacements.is_empty()) {
		return p_source;
	}

	const String transformed = apply_replacements(context);
	if (transformed == p_source) {
		return p_source;
	}

	String validation_error;
	if (!parse_only(transformed, p_path, &validation_error)) {
		WARN_PRINT("Failed to validate wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + validation_error);
		return p_source;
	}

	if (r_changed != nullptr) {
		*r_changed = true;
	}
	return transformed;
}

} // namespace WGodotGDScriptExportTransform
