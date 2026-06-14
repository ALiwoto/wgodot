// wgodot-changes::file
/**************************************************************************/
/*  wgodot_deconst_export.cpp                                             */
/**************************************************************************/

#include "wgodot_deconst_export.h"

#include "gdscript_analyzer.h"
#include "gdscript_parser.h"

#include "core/error/error_macros.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant_parser.h"

namespace {

struct WGodotDeconstReplacement {
	int start = 0;
	int end = 0;
	String text;
};

struct WGodotDeconstContext {
	String source;
	WGodotGDScriptDeconstExport::TransformOptions options;
	Vector<int> line_offsets;
	Vector<WGodotDeconstReplacement> replacements;
	HashSet<StringName> reserved_obfuscated_names;
	HashMap<const GDScriptParser::Node *, String> obfuscated_local_names;
	int obfuscated_local_counter = 0;
};

struct WGodotDeconstReplacementSort {
	bool operator()(const WGodotDeconstReplacement &p_left, const WGodotDeconstReplacement &p_right) const {
		if (p_left.start == p_right.start) {
			return p_left.end > p_right.end;
		}
		return p_left.start < p_right.start;
	}
};

void wgodot_build_line_offsets(WGodotDeconstContext &r_context) {
	r_context.line_offsets.clear();
	r_context.line_offsets.push_back(0);

	for (int i = 0; i < r_context.source.length(); i++) {
		if (r_context.source[i] == '\n') {
			r_context.line_offsets.push_back(i + 1);
		}
	}
}

int wgodot_get_line_start_offset(const WGodotDeconstContext &p_context, int p_line) {
	if (p_line <= 0 || p_line > p_context.line_offsets.size()) {
		return -1;
	}

	return p_context.line_offsets[p_line - 1];
}

int wgodot_get_line_end_offset(const WGodotDeconstContext &p_context, int p_line) {
	const int line_start = wgodot_get_line_start_offset(p_context, p_line);
	if (line_start < 0) {
		return -1;
	}

	if (p_line < p_context.line_offsets.size()) {
		return p_context.line_offsets[p_line] - 1;
	}

	return p_context.source.length();
}

int wgodot_get_offset(const WGodotDeconstContext &p_context, int p_line, int p_column) {
	const int line_start = wgodot_get_line_start_offset(p_context, p_line);
	if (line_start < 0) {
		return -1;
	}

	return CLAMP(line_start + MAX(p_column - 1, 0), 0, p_context.source.length());
}

String wgodot_variant_to_source(const Variant &p_value) {
	String text;
	if (VariantWriter::write_to_string(p_value, text) != OK) {
		return String();
	}

	return text;
}

bool wgodot_should_mangle_constant(const GDScriptParser::ConstantNode *p_constant) {
	return p_constant != nullptr && !p_constant->wgodot_no_mangle;
}

bool wgodot_should_deconst_constant(const WGodotDeconstContext &p_context, const GDScriptParser::ConstantNode *p_constant) {
	return p_context.options.deconst_exports && wgodot_should_mangle_constant(p_constant);
}

bool wgodot_is_declared_constant_identifier(const WGodotDeconstContext &p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	if (p_identifier->source != GDScriptParser::IdentifierNode::LOCAL_CONSTANT &&
			p_identifier->source != GDScriptParser::IdentifierNode::MEMBER_CONSTANT) {
		return false;
	}

	return wgodot_should_deconst_constant(p_context, p_identifier->constant_source);
}

void wgodot_add_replacement(WGodotDeconstContext &r_context, const GDScriptParser::Node *p_node, const String &p_text) {
	ERR_FAIL_NULL(p_node);

	const int start = wgodot_get_offset(r_context, p_node->start_line, p_node->start_column);
	const int end = wgodot_get_offset(r_context, p_node->end_line, p_node->end_column);
	if (start < 0 || end < start) {
		return;
	}

	WGodotDeconstReplacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_text;
	r_context.replacements.push_back(replacement);
}

String wgodot_make_short_obfuscated_name(int p_index) {
	static const char *letters = "abcdefghijklmnopqrstuvwxyz";
	const int digit = p_index % 10;
	int group = p_index / 10;

	String prefix;
	do {
		prefix = String::chr(letters[group % 26]) + prefix;
		group = (group / 26) - 1;
	} while (group >= 0);

	return prefix + itos(digit);
}

String wgodot_make_obfuscated_local_name(WGodotDeconstContext &r_context) {
	if (r_context.options.obfuscation_strategy != WGodotGDScriptDeconstExport::OBFUSCATION_STRATEGY_SHORT) {
		WARN_PRINT_ONCE("WGodot local variable obfuscation currently only supports the 'short' strategy. Falling back to 'short'.");
	}

	while (true) {
		const String candidate = wgodot_make_short_obfuscated_name(r_context.obfuscated_local_counter++);
		const StringName candidate_name(candidate);
		if (!r_context.reserved_obfuscated_names.has(candidate_name)) {
			r_context.reserved_obfuscated_names.insert(candidate_name);
			return candidate;
		}
	}
}

const GDScriptParser::Node *wgodot_get_local_declaration_node(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
			return p_local.parameter;
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return p_local.bind;
		default:
			return nullptr;
	}
}

const GDScriptParser::IdentifierNode *wgodot_get_local_declaration_identifier(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable != nullptr ? p_local.variable->identifier : nullptr;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
			return p_local.parameter != nullptr ? p_local.parameter->identifier : nullptr;
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return p_local.bind;
		default:
			return nullptr;
	}
}

bool wgodot_should_obfuscate_local(const GDScriptParser::SuiteNode::Local &p_local) {
	switch (p_local.type) {
		case GDScriptParser::SuiteNode::Local::VARIABLE:
			return p_local.variable != nullptr && !p_local.variable->wgodot_no_mangle;
		case GDScriptParser::SuiteNode::Local::PARAMETER:
		case GDScriptParser::SuiteNode::Local::FOR_VARIABLE:
		case GDScriptParser::SuiteNode::Local::PATTERN_BIND:
			return true;
		default:
			return false;
	}
}

const GDScriptParser::Node *wgodot_get_local_identifier_source(const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return nullptr;
	}

	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			return p_identifier->parameter_source;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			return p_identifier->variable_source;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			return p_identifier->bind_source;
		default:
			return nullptr;
	}
}

void wgodot_add_local_reference_replacement(WGodotDeconstContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_local_variables) {
		return;
	}

	const GDScriptParser::Node *source = wgodot_get_local_identifier_source(p_identifier);
	if (source == nullptr) {
		return;
	}

	const String *obfuscated_name = r_context.obfuscated_local_names.getptr(source);
	if (obfuscated_name == nullptr) {
		return;
	}

	wgodot_add_replacement(r_context, p_identifier, *obfuscated_name);
}

void wgodot_collect_suite_local_obfuscation(WGodotDeconstContext &r_context, const GDScriptParser::SuiteNode *p_suite) {
	if (!r_context.options.obfuscate_local_variables || p_suite == nullptr) {
		return;
	}

	for (const GDScriptParser::SuiteNode::Local &local : p_suite->locals) {
		if (!String(local.name).is_empty()) {
			r_context.reserved_obfuscated_names.insert(local.name);
		}
	}

	for (const GDScriptParser::SuiteNode::Local &local : p_suite->locals) {
		if (!wgodot_should_obfuscate_local(local)) {
			continue;
		}

		const GDScriptParser::Node *declaration = wgodot_get_local_declaration_node(local);
		const GDScriptParser::IdentifierNode *identifier = wgodot_get_local_declaration_identifier(local);
		if (declaration == nullptr || identifier == nullptr || String(identifier->name).is_empty()) {
			continue;
		}

		if (r_context.obfuscated_local_names.has(declaration)) {
			continue;
		}

		const String obfuscated_name = wgodot_make_obfuscated_local_name(r_context);
		r_context.obfuscated_local_names[declaration] = obfuscated_name;
		wgodot_add_replacement(r_context, identifier, obfuscated_name);
	}
}

void wgodot_add_constant_reference_replacement(WGodotDeconstContext &r_context, const GDScriptParser::Node *p_node, const GDScriptParser::ConstantNode *p_constant) {
	ERR_FAIL_NULL(p_constant);
	ERR_FAIL_NULL(p_constant->initializer);

	const String text = wgodot_variant_to_source(p_constant->initializer->reduced_value);
	if (text.is_empty()) {
		return;
	}

	wgodot_add_replacement(r_context, p_node, text);
}

void wgodot_add_constant_declaration_replacement(WGodotDeconstContext &r_context, const GDScriptParser::ConstantNode *p_constant, bool p_leave_pass) {
	ERR_FAIL_NULL(p_constant);

	int start_line = p_constant->start_line;
	int start_column = p_constant->start_column;
	for (const GDScriptParser::AnnotationNode *annotation : p_constant->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		if (annotation->start_line < start_line || (annotation->start_line == start_line && annotation->start_column < start_column)) {
			start_line = annotation->start_line;
			start_column = annotation->start_column;
		}
	}

	int start = wgodot_get_offset(r_context, start_line, start_column);
	int end = wgodot_get_offset(r_context, p_constant->end_line, p_constant->end_column);
	if (start < 0 || end < start) {
		return;
	}

	const int line_end = wgodot_get_line_end_offset(r_context, p_constant->end_line);
	bool remove_full_line = false;
	if (line_end >= end) {
		const String trailing = r_context.source.substr(end, line_end - end).strip_edges();
		if (trailing.begins_with(";")) {
			while (end < line_end && r_context.source[end] != ';') {
				end++;
			}
			if (end < line_end) {
				end++;
			}
			while (end < line_end && (r_context.source[end] == ' ' || r_context.source[end] == '\t')) {
				end++;
			}
		} else {
			end = line_end;
			remove_full_line = true;
		}
	}

	if (!p_leave_pass && remove_full_line) {
		const int line_start = wgodot_get_line_start_offset(r_context, start_line);
		if (line_start >= 0) {
			start = line_start;
		}
		if (p_constant->end_line < r_context.line_offsets.size()) {
			end = r_context.line_offsets[p_constant->end_line];
		}
	}

	WGodotDeconstReplacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_leave_pass ? "pass" : "";
	r_context.replacements.push_back(replacement);
}

void wgodot_collect_expression_replacements(WGodotDeconstContext &r_context, const GDScriptParser::ExpressionNode *p_expression);
void wgodot_collect_node_replacements(WGodotDeconstContext &r_context, const GDScriptParser::Node *p_node);

void wgodot_collect_type_replacements(WGodotDeconstContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		wgodot_collect_type_replacements(r_context, container_type);
	}
}

void wgodot_collect_parameter_replacements(WGodotDeconstContext &r_context, const GDScriptParser::ParameterNode *p_parameter) {
	if (p_parameter == nullptr) {
		return;
	}

	wgodot_collect_type_replacements(r_context, p_parameter->datatype_specifier);
	wgodot_collect_expression_replacements(r_context, p_parameter->initializer);
}

void wgodot_collect_pattern_replacements(WGodotDeconstContext &r_context, const GDScriptParser::PatternNode *p_pattern) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			wgodot_collect_expression_replacements(r_context, p_pattern->expression);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (const GDScriptParser::PatternNode *sub_pattern : p_pattern->array) {
				wgodot_collect_pattern_replacements(r_context, sub_pattern);
			}
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				wgodot_collect_expression_replacements(r_context, pair.key);
				wgodot_collect_pattern_replacements(r_context, pair.value_pattern);
			}
			break;
		default:
			break;
	}
}

void wgodot_collect_expression_replacements(WGodotDeconstContext &r_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::IDENTIFIER: {
			const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
			if (wgodot_is_declared_constant_identifier(r_context, identifier)) {
				wgodot_add_constant_reference_replacement(r_context, identifier, identifier->constant_source);
			} else {
				wgodot_add_local_reference_replacement(r_context, identifier);
			}
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && wgodot_is_declared_constant_identifier(r_context, subscript->attribute)) {
				wgodot_add_constant_reference_replacement(r_context, subscript, subscript->attribute->constant_source);
				break;
			}

			wgodot_collect_expression_replacements(r_context, subscript->base);
			if (subscript->is_attribute) {
				wgodot_collect_expression_replacements(r_context, subscript->attribute);
			} else {
				wgodot_collect_expression_replacements(r_context, subscript->index);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				wgodot_collect_expression_replacements(r_context, element);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, assignment->assignee);
			wgodot_collect_expression_replacements(r_context, assignment->assigned_value);
		} break;
		case GDScriptParser::Node::AWAIT:
			wgodot_collect_expression_replacements(r_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, binary->left_operand);
			wgodot_collect_expression_replacements(r_context, binary->right_operand);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, call->callee);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				wgodot_collect_expression_replacements(r_context, argument);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, cast->operand);
			wgodot_collect_type_replacements(r_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				wgodot_collect_expression_replacements(r_context, pair.key);
				wgodot_collect_expression_replacements(r_context, pair.value);
			}
		} break;
		case GDScriptParser::Node::GET_NODE:
			break;
		case GDScriptParser::Node::LAMBDA:
			wgodot_collect_node_replacements(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function);
			break;
		case GDScriptParser::Node::PRELOAD:
			wgodot_collect_expression_replacements(r_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path);
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, ternary->condition);
			wgodot_collect_expression_replacements(r_context, ternary->true_expr);
			wgodot_collect_expression_replacements(r_context, ternary->false_expr);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			wgodot_collect_expression_replacements(r_context, type_test->operand);
			wgodot_collect_type_replacements(r_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			wgodot_collect_expression_replacements(r_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
			break;
		default:
			break;
	}
}

void wgodot_collect_annotation_replacements(WGodotDeconstContext &r_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		for (const GDScriptParser::ExpressionNode *argument : annotation->arguments) {
			wgodot_collect_expression_replacements(r_context, argument);
		}
	}
}

void wgodot_collect_constant_contents_replacements(WGodotDeconstContext &r_context, const GDScriptParser::ConstantNode *p_constant) {
	if (p_constant == nullptr) {
		return;
	}

	wgodot_collect_type_replacements(r_context, p_constant->datatype_specifier);
	wgodot_collect_expression_replacements(r_context, p_constant->initializer);
}

void wgodot_collect_node_replacements(WGodotDeconstContext &r_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	wgodot_collect_annotation_replacements(r_context, p_node);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			bool has_mangled_constants = false;
			bool has_remaining_members = false;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				if (member.type == GDScriptParser::ClassNode::Member::CONSTANT && wgodot_should_deconst_constant(r_context, member.constant)) {
					has_mangled_constants = true;
				} else {
					has_remaining_members = true;
				}
			}

			bool leave_pass_for_first_constant = class_node->outer != nullptr && has_mangled_constants && !has_remaining_members;
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				switch (member.type) {
					case GDScriptParser::ClassNode::Member::CLASS:
						wgodot_collect_node_replacements(r_context, member.m_class);
						break;
					case GDScriptParser::ClassNode::Member::CONSTANT:
						if (wgodot_should_deconst_constant(r_context, member.constant)) {
							wgodot_add_constant_declaration_replacement(r_context, member.constant, leave_pass_for_first_constant);
							leave_pass_for_first_constant = false;
						} else {
							wgodot_collect_annotation_replacements(r_context, member.constant);
							wgodot_collect_constant_contents_replacements(r_context, member.constant);
						}
						break;
					case GDScriptParser::ClassNode::Member::FUNCTION:
						wgodot_collect_node_replacements(r_context, member.function);
						break;
					case GDScriptParser::ClassNode::Member::VARIABLE:
						wgodot_collect_node_replacements(r_context, member.variable);
						break;
					case GDScriptParser::ClassNode::Member::SIGNAL:
						wgodot_collect_node_replacements(r_context, member.signal);
						break;
					case GDScriptParser::ClassNode::Member::ENUM:
						for (const GDScriptParser::EnumNode::Value &value : member.m_enum->values) {
							wgodot_collect_expression_replacements(r_context, value.custom_value);
						}
						break;
					default:
						break;
				}
			}
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			if (wgodot_should_deconst_constant(r_context, constant)) {
				wgodot_add_constant_declaration_replacement(r_context, constant, false);
			} else {
				wgodot_collect_constant_contents_replacements(r_context, constant);
			}
		} break;
		case GDScriptParser::Node::FUNCTION: {
			const GDScriptParser::FunctionNode *function = static_cast<const GDScriptParser::FunctionNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				wgodot_collect_parameter_replacements(r_context, parameter);
			}
			wgodot_collect_type_replacements(r_context, function->return_type);
			wgodot_collect_node_replacements(r_context, function->body);
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				wgodot_collect_parameter_replacements(r_context, parameter);
			}
		} break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			wgodot_collect_suite_local_obfuscation(r_context, suite);

			bool has_mangled_constants = false;
			bool has_remaining_statements = false;
			for (const GDScriptParser::Node *statement : suite->statements) {
				const GDScriptParser::ConstantNode *constant = statement != nullptr && statement->type == GDScriptParser::Node::CONSTANT ? static_cast<const GDScriptParser::ConstantNode *>(statement) : nullptr;
				if (wgodot_should_deconst_constant(r_context, constant)) {
					has_mangled_constants = true;
				} else {
					has_remaining_statements = true;
				}
			}

			bool leave_pass_for_first_constant = has_mangled_constants && !has_remaining_statements;
			for (const GDScriptParser::Node *statement : suite->statements) {
				const GDScriptParser::ConstantNode *constant = statement != nullptr && statement->type == GDScriptParser::Node::CONSTANT ? static_cast<const GDScriptParser::ConstantNode *>(statement) : nullptr;
				if (wgodot_should_deconst_constant(r_context, constant)) {
					wgodot_add_constant_declaration_replacement(r_context, constant, leave_pass_for_first_constant);
					leave_pass_for_first_constant = false;
				} else {
					wgodot_collect_node_replacements(r_context, statement);
				}
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			wgodot_collect_type_replacements(r_context, variable->datatype_specifier);
			wgodot_collect_expression_replacements(r_context, variable->initializer);
			if (variable->setter != nullptr) {
				wgodot_collect_node_replacements(r_context, variable->setter);
			}
			if (variable->getter != nullptr) {
				wgodot_collect_node_replacements(r_context, variable->getter);
			}
		} break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_node);
			wgodot_collect_expression_replacements(r_context, assert_node->condition);
			wgodot_collect_expression_replacements(r_context, assert_node->message);
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_node);
			wgodot_collect_type_replacements(r_context, for_node->datatype_specifier);
			wgodot_collect_expression_replacements(r_context, for_node->list);
			wgodot_collect_node_replacements(r_context, for_node->loop);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_node);
			wgodot_collect_expression_replacements(r_context, if_node->condition);
			wgodot_collect_node_replacements(r_context, if_node->true_block);
			wgodot_collect_node_replacements(r_context, if_node->false_block);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match = static_cast<const GDScriptParser::MatchNode *>(p_node);
			wgodot_collect_expression_replacements(r_context, match->test);
			for (const GDScriptParser::MatchBranchNode *branch : match->branches) {
				wgodot_collect_node_replacements(r_context, branch);
			}
		} break;
		case GDScriptParser::Node::MATCH_BRANCH: {
			const GDScriptParser::MatchBranchNode *branch = static_cast<const GDScriptParser::MatchBranchNode *>(p_node);
			for (const GDScriptParser::PatternNode *pattern : branch->patterns) {
				wgodot_collect_pattern_replacements(r_context, pattern);
			}
			wgodot_collect_node_replacements(r_context, branch->guard_body);
			wgodot_collect_node_replacements(r_context, branch->block);
		} break;
		case GDScriptParser::Node::RETURN:
			wgodot_collect_expression_replacements(r_context, static_cast<const GDScriptParser::ReturnNode *>(p_node)->return_value);
			break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_node);
			wgodot_collect_expression_replacements(r_context, while_node->condition);
			wgodot_collect_node_replacements(r_context, while_node->loop);
		} break;
		default:
			if (p_node->is_expression()) {
				wgodot_collect_expression_replacements(r_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node));
			}
			break;
	}
}

String wgodot_apply_replacements(WGodotDeconstContext &r_context) {
	r_context.replacements.sort_custom<WGodotDeconstReplacementSort>();

	String result = r_context.source;
	int last_start = result.length() + 1;
	for (int i = r_context.replacements.size() - 1; i >= 0; i--) {
		const WGodotDeconstReplacement &replacement = r_context.replacements[i];
		if (replacement.start < 0 || replacement.end < replacement.start || replacement.end > result.length()) {
			continue;
		}
		if (replacement.end > last_start) {
			continue;
		}

		result = result.substr(0, replacement.start) + replacement.text + result.substr(replacement.end);
		last_start = replacement.start;
	}

	return result;
}

bool wgodot_parse_and_analyze(const String &p_source, const String &p_path) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return false;
	}

	GDScriptAnalyzer analyzer(&parser);
	return analyzer.analyze() == OK;
}

} // namespace

String WGodotGDScriptDeconstExport::transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.deconst_exports && !p_options.obfuscate_local_variables) {
		return p_source;
	}

	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return p_source;
	}

	GDScriptAnalyzer analyzer(&parser);
	if (analyzer.analyze() != OK) {
		return p_source;
	}

	WGodotDeconstContext context;
	context.source = p_source;
	context.options = p_options;
	wgodot_build_line_offsets(context);
	wgodot_collect_node_replacements(context, parser.get_tree());
	if (context.replacements.is_empty()) {
		return p_source;
	}

	const String sanitized = wgodot_apply_replacements(context);
	if (sanitized == p_source) {
		return p_source;
	}

	if (!wgodot_parse_and_analyze(sanitized, p_path)) {
		WARN_PRINT("Failed to validate wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.");
		return p_source;
	}

	if (r_changed != nullptr) {
		*r_changed = true;
	}
	return sanitized;
}

String WGodotGDScriptDeconstExport::sanitize_source(const String &p_source, const String &p_path, bool *r_changed) {
	TransformOptions options;
	options.deconst_exports = true;
	options.obfuscate_local_variables = false;
	options.obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
	return transform_source(p_source, p_path, options, r_changed);
}
