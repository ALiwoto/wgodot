// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "core/string/string_builder.h"

using Parser = GDScriptParser;

namespace {
bool is_integer(const Parser::DataType &p_type) {
	return p_type.is_hard_type() && !p_type.is_meta_type &&
			(p_type.kind == Parser::DataType::ENUM || (p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type == Variant::INT));
}
} // namespace

bool WGodotCppEmitter::integer_match_cases(const Parser::MatchNode *p_match, Vector<Vector<String>> &r_labels) {
	if (!is_integer(expression_type(p_match->test))) {
		return false;
	}
	HashSet<int64_t> seen;
	r_labels.resize(p_match->branches.size());
	for (uint32_t i = 0; i < p_match->branches.size(); i++) {
		const auto *branch = p_match->branches[i];
		// Guards can reject a case and let a later branch match the same value.
		if (branch->guard_body) {
			return false;
		}
		for (const auto *pattern : branch->patterns) {
			if (pattern->pattern_type == Parser::PatternNode::PT_WILDCARD) {
				r_labels.write[i].push_back("default:");
				return true; // An unguarded wildcard makes all later branches unreachable.
			}
			const Parser::ExpressionNode *value = nullptr;
			if (pattern->pattern_type == Parser::PatternNode::PT_LITERAL) {
				value = pattern->literal;
			} else if (pattern->pattern_type == Parser::PatternNode::PT_EXPRESSION) {
				value = pattern->expression;
			}
			if (!value || !value->is_constant || value->reduced_value.get_type() != Variant::INT) {
				return false;
			}
			const int64_t number = value->reduced_value;
			// Enum aliases and repeated patterns must keep first-match semantics.
			if (!seen.has(number)) {
				seen.insert(number);
				r_labels.write[i].push_back("case " + literal(value->reduced_value, pattern) + ":");
			}
		}
	}
	return true;
}

String WGodotCppEmitter::match_condition(const Parser::PatternNode *p_pattern, const String &p_value, const Parser::DataType &p_value_type) {
	const Parser::ExpressionNode *pattern = nullptr;
	switch (p_pattern->pattern_type) {
		case Parser::PatternNode::PT_LITERAL:
			pattern = p_pattern->literal;
			break;
		case Parser::PatternNode::PT_EXPRESSION:
			pattern = p_pattern->expression;
			break;
		case Parser::PatternNode::PT_WILDCARD:
			return "true";
		default:
			unsupported(p_pattern, "destructuring match pattern");
			return String();
	}
	const auto pattern_type = expression_type(pattern);
	if (is_warray(pattern_type) || is_wdictionary(pattern_type)) {
		unsupported(p_pattern, "native container expression in a Variant match pattern");
		return String();
	}
	if (is_integer(p_value_type) && is_integer(pattern_type)) {
		return "(" + p_value + " == " + expression(pattern) + ")";
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	return "WGodotNative::match_value(" + p_value + ", " + expression(pattern) + ")";
}

String WGodotCppEmitter::match_statement(const Parser::MatchNode *p_match, int p_indent) {
	const auto value_type = expression_type(p_match->test);
	if (is_warray(value_type) || is_wdictionary(value_type)) {
		unsupported(p_match, "matching native containers through the current Variant pattern matcher");
		return String();
	}
	StringBuilder code;
	const String indent = String("\t").repeat(p_indent);
	Vector<Vector<String>> labels;
	if (integer_match_cases(p_match, labels)) {
		code += indent + "switch (" + expression(p_match->test) + ") {\n";
		switch_depth++;
		for (uint32_t i = 0; i < p_match->branches.size(); i++) {
			if (labels[i].is_empty()) {
				continue;
			}
			for (const String &label : labels[i]) {
				code += indent + "\t" + label + "\n";
			}
			code += indent + "\t{\n" + suite(p_match->branches[i]->block, p_indent + 2);
			code += indent + "\t\tbreak;\n" + indent + "\t}\n";
		}
		switch_depth--;
		code += indent + "}\n";
		return code.as_string();
	}
	code += indent + "{\n" + indent + "\tauto match_value = " + expression(p_match->test) + ";\n";
	bool first = true;
	for (const auto *branch : p_match->branches) {
		Vector<String> conditions;
		for (const auto *pattern : branch->patterns) {
			conditions.push_back(match_condition(pattern, "match_value", value_type));
		}
		String condition = "(" + String(" || ").join(conditions) + ")";
		if (branch->guard_body) {
			condition += " && " + truth(static_cast<const Parser::ExpressionNode *>(branch->guard_body->statements[0]));
		}
		code += indent + "\t" + (first ? "if" : "else if") + " (" + condition + ") {\n" + suite(branch->block, p_indent + 2) + indent + "\t}\n";
		first = false;
	}
	code += indent + "}\n";
	return code.as_string();
}
