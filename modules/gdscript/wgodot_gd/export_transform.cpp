// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "deconst_transform.h"
#include "name_obfuscation.h"
#include "source_rewrite.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"

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

void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression);
void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node);

void collect_type_replacements(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		collect_type_replacements(r_context, container_type);
	}
}

void collect_parameter_replacements(RewriteContext &r_context, const GDScriptParser::ParameterNode *p_parameter) {
	if (p_parameter == nullptr) {
		return;
	}

	collect_type_replacements(r_context, p_parameter->datatype_specifier);
	collect_expression_replacements(r_context, p_parameter->initializer);
}

void collect_pattern_replacements(RewriteContext &r_context, const GDScriptParser::PatternNode *p_pattern) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			collect_expression_replacements(r_context, p_pattern->expression);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (const GDScriptParser::PatternNode *sub_pattern : p_pattern->array) {
				collect_pattern_replacements(r_context, sub_pattern);
			}
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				collect_expression_replacements(r_context, pair.key);
				collect_pattern_replacements(r_context, pair.value_pattern);
			}
			break;
		default:
			break;
	}
}

void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::IDENTIFIER: {
			const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
			if (is_declared_constant_identifier(r_context, identifier)) {
				add_constant_reference_replacement(r_context, identifier, identifier->constant_source);
			} else {
				add_local_name_reference_replacement(r_context, identifier);
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && is_declared_constant_identifier(r_context, subscript->attribute)) {
				add_constant_reference_replacement(r_context, subscript, subscript->attribute->constant_source);
				break;
			}

			collect_expression_replacements(r_context, subscript->base);
			if (subscript->is_attribute) {
				collect_expression_replacements(r_context, subscript->attribute);
			} else {
				collect_expression_replacements(r_context, subscript->index);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_expression_replacements(r_context, element);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_expression_replacements(r_context, assignment->assignee);
			collect_expression_replacements(r_context, assignment->assigned_value);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_expression_replacements(r_context, binary->left_operand);
			collect_expression_replacements(r_context, binary->right_operand);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			collect_expression_replacements(r_context, call->callee);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_expression_replacements(r_context, argument);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_expression_replacements(r_context, cast->operand);
			collect_type_replacements(r_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_expression_replacements(r_context, pair.key);
				collect_expression_replacements(r_context, pair.value);
			}
		} break;
		case GDScriptParser::Node::GET_NODE:
			break;
		case GDScriptParser::Node::LAMBDA:
			collect_node_replacements(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function);
			break;
		case GDScriptParser::Node::PRELOAD:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path);
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_expression_replacements(r_context, ternary->condition);
			collect_expression_replacements(r_context, ternary->true_expr);
			collect_expression_replacements(r_context, ternary->false_expr);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			collect_expression_replacements(r_context, type_test->operand);
			collect_type_replacements(r_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
			break;
		default:
			break;
	}
}

void collect_annotation_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		for (const GDScriptParser::ExpressionNode *argument : annotation->arguments) {
			collect_expression_replacements(r_context, argument);
		}
	}
}

void collect_constant_contents_replacements(RewriteContext &r_context, const GDScriptParser::ConstantNode *p_constant) {
	if (p_constant == nullptr) {
		return;
	}

	collect_type_replacements(r_context, p_constant->datatype_specifier);
	collect_expression_replacements(r_context, p_constant->initializer);
}

void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	collect_annotation_replacements(r_context, p_node);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
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
						collect_node_replacements(r_context, member.m_class);
						break;
					case GDScriptParser::ClassNode::Member::CONSTANT:
						if (should_deconst_constant(r_context, member.constant)) {
							add_constant_declaration_replacement(r_context, member.constant, leave_pass_for_first_constant);
							leave_pass_for_first_constant = false;
						} else {
							collect_annotation_replacements(r_context, member.constant);
							collect_constant_contents_replacements(r_context, member.constant);
						}
						break;
					case GDScriptParser::ClassNode::Member::FUNCTION:
						collect_node_replacements(r_context, member.function);
						break;
					case GDScriptParser::ClassNode::Member::VARIABLE:
						collect_node_replacements(r_context, member.variable);
						break;
					case GDScriptParser::ClassNode::Member::SIGNAL:
						collect_node_replacements(r_context, member.signal);
						break;
					case GDScriptParser::ClassNode::Member::ENUM:
						for (const GDScriptParser::EnumNode::Value &value : member.m_enum->values) {
							collect_expression_replacements(r_context, value.custom_value);
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
				collect_constant_contents_replacements(r_context, constant);
			}
		} break;
		case GDScriptParser::Node::FUNCTION: {
			const GDScriptParser::FunctionNode *function = static_cast<const GDScriptParser::FunctionNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				collect_parameter_replacements(r_context, parameter);
			}
			collect_type_replacements(r_context, function->return_type);
			collect_node_replacements(r_context, function->body);
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				collect_parameter_replacements(r_context, parameter);
			}
		} break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			collect_suite_local_name_obfuscation(r_context, suite);

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
					collect_node_replacements(r_context, statement);
				}
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			collect_type_replacements(r_context, variable->datatype_specifier);
			collect_expression_replacements(r_context, variable->initializer);
			if (variable->setter != nullptr) {
				collect_node_replacements(r_context, variable->setter);
			}
			if (variable->getter != nullptr) {
				collect_node_replacements(r_context, variable->getter);
			}
		} break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_node);
			collect_expression_replacements(r_context, assert_node->condition);
			collect_expression_replacements(r_context, assert_node->message);
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_node);
			collect_type_replacements(r_context, for_node->datatype_specifier);
			collect_expression_replacements(r_context, for_node->list);
			collect_node_replacements(r_context, for_node->loop);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_node);
			collect_expression_replacements(r_context, if_node->condition);
			collect_node_replacements(r_context, if_node->true_block);
			collect_node_replacements(r_context, if_node->false_block);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match = static_cast<const GDScriptParser::MatchNode *>(p_node);
			collect_expression_replacements(r_context, match->test);
			for (const GDScriptParser::MatchBranchNode *branch : match->branches) {
				collect_node_replacements(r_context, branch);
			}
		} break;
		case GDScriptParser::Node::MATCH_BRANCH: {
			const GDScriptParser::MatchBranchNode *branch = static_cast<const GDScriptParser::MatchBranchNode *>(p_node);
			for (const GDScriptParser::PatternNode *pattern : branch->patterns) {
				collect_pattern_replacements(r_context, pattern);
			}
			collect_node_replacements(r_context, branch->guard_body);
			collect_node_replacements(r_context, branch->block);
		} break;
		case GDScriptParser::Node::RETURN:
			collect_expression_replacements(r_context, static_cast<const GDScriptParser::ReturnNode *>(p_node)->return_value);
			break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_node);
			collect_expression_replacements(r_context, while_node->condition);
			collect_node_replacements(r_context, while_node->loop);
		} break;
		default:
			if (p_node->is_expression()) {
				collect_expression_replacements(r_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node));
			}
			break;
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

bool parse_and_analyze(const String &p_source, const String &p_path, String *r_error_details = nullptr) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_text(parser);
		}
		return false;
	}

	GDScriptAnalyzer analyzer(&parser);
	if (analyzer.analyze() != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_text(parser);
		}
		return false;
	}

	return true;
}

} // namespace

namespace WGodotGDScriptExportTransform {

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "debug/gdscript/wgodot/deconst_exports");
	options.obfuscate_local_variables = GLOBAL_GET_CACHED(bool, "debug/gdscript/wgodot/obfuscate_local_variables");

	const int obfuscation_strategy = GLOBAL_GET_CACHED(int, "debug/gdscript/wgodot/obfuscation_strategy");
	if (obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.obfuscation_strategy = static_cast<ObfuscationStrategy>(obfuscation_strategy);
	}

	return options;
}

String transform_source(const String &p_source, const String &p_path, bool *r_changed) {
	return transform_source(p_source, p_path, setup_params(), r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.deconst_exports && !p_options.obfuscate_local_variables) {
		return p_source;
	}

	AnalyzedSource analyzed_source;
	if (!analyzed_source.load(p_source, p_path)) {
		return p_source;
	}

	RewriteContext context;
	context.source = p_source;
	context.options = p_options;
	build_line_offsets(context);
	collect_node_replacements(context, analyzed_source.parser->get_tree());
	if (context.replacements.is_empty()) {
		return p_source;
	}

	const String transformed = apply_replacements(context);
	if (transformed == p_source) {
		return p_source;
	}

	String validation_error;
	if (!parse_and_analyze(transformed, p_path, &validation_error)) {
		WARN_PRINT("Failed to validate wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + validation_error);
		return p_source;
	}

	if (r_changed != nullptr) {
		*r_changed = true;
	}
	return transformed;
}

} // namespace WGodotGDScriptExportTransform
