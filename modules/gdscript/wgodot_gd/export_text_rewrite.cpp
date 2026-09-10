// wgodot-changes::file
/**************************************************************************/
/*  export_text_rewrite.cpp                                               */
/**************************************************************************/

#include "../gdscript_tokenizer.h"
#include "export_transform_internal.h"
#include "string_obfuscation.h"

#include "core/error/error_macros.h"
#include "core/variant/variant_parser.h"

namespace WGodotGDScriptExportTransform {

bool get_string_literal_value(const Variant &p_value, String *r_value) {
	ERR_FAIL_NULL_V(r_value, false);

	switch (p_value.get_type()) {
		case Variant::STRING:
			*r_value = p_value;
			return true;
		case Variant::STRING_NAME:
			*r_value = String(StringName(p_value));
			return true;
		case Variant::NODE_PATH:
			*r_value = String(NodePath(p_value));
			return true;
		default:
			return false;
	}
}

String get_export_string_literal_replacement(RewriteContext &r_context, Variant::Type p_type, const String &p_value) {
	if (r_context.export_context == nullptr) {
		return String();
	}

	String value = p_value;
	const bool can_obfuscate_path = p_type == Variant::STRING && r_context.options.obfuscate_file_paths && value.begins_with("res://");
	if (can_obfuscate_path) {
		const String obfuscated_path = r_context.export_context->get_exported_script_path(value);
		if (!obfuscated_path.is_empty()) {
			value = obfuscated_path;
		}
	}

	if (r_context.options.obfuscate_strings && !r_context.no_string_mangle_scope) {
		return r_context.export_context->get_or_create_obfuscated_string_literal(p_type, value);
	}

	if (value != p_value) {
		String text;
		if (VariantWriter::write_to_string(value, text) != OK) {
			return String();
		}
		return text;
	}

	return String();
}

void add_string_literal_replacement(RewriteContext &r_context, const GDScriptParser::LiteralNode *p_literal) {
	if (p_literal == nullptr) {
		return;
	}

	String value;
	if (!get_string_literal_value(p_literal->value, &value)) {
		return;
	}

	const int start = get_offset(r_context, p_literal->start_line, p_literal->start_column);
	const int end = get_offset(r_context, p_literal->end_line, p_literal->end_column);
	if (start < 0 || end < start || overlaps_existing_replacement(r_context, start, end)) {
		return;
	}

	const String text = get_export_string_literal_replacement(r_context, p_literal->value.get_type(), value);
	if (text.is_empty()) {
		return;
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = text;
	r_context.replacements.push_back(replacement);
}

bool expression_reduces_to_string(const GDScriptParser::ExpressionNode *p_expression) {
	return p_expression != nullptr && p_expression->is_constant && p_expression->reduced_value.get_type() == Variant::STRING;
}

bool add_string_concat_replacement(RewriteContext &r_context, const GDScriptParser::BinaryOpNode *p_binary) {
	if (!r_context.options.obfuscate_strings || r_context.no_string_mangle_scope || r_context.export_context == nullptr || p_binary == nullptr || p_binary->operation != GDScriptParser::BinaryOpNode::OP_ADDITION) {
		return false;
	}
	if (!expression_reduces_to_string(p_binary) || !expression_reduces_to_string(p_binary->left_operand) || !expression_reduces_to_string(p_binary->right_operand)) {
		return false;
	}

	String value = p_binary->reduced_value;
	if (r_context.options.obfuscate_file_paths && value.begins_with("res://")) {
		const String obfuscated_path = r_context.export_context->get_exported_script_path(value);
		if (!obfuscated_path.is_empty()) {
			value = obfuscated_path;
		}
	}

	const int start = get_offset(r_context, p_binary->start_line, p_binary->start_column);
	const int end = get_offset(r_context, p_binary->end_line, p_binary->end_column);
	if (start < 0 || end < start || overlaps_existing_replacement(r_context, start, end)) {
		return false;
	}

	const String text = r_context.export_context->get_or_create_obfuscated_string_literal(Variant::STRING, value);
	if (text.is_empty()) {
		return false;
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = text;
	r_context.replacements.push_back(replacement);
	return true;
}

bool should_strip_export_annotation(const GDScriptParser::AnnotationNode *p_annotation) {
	if (p_annotation == nullptr) {
		return false;
	}

	static const StringName stripped_annotations[] = {
		SNAME("@private"),
		SNAME("@no_mangle"),
		SNAME("@no_string_mangle"),
		SNAME("@obfuscate"),
		SNAME("@obfuscate_path"),
		SNAME("@static_class"),
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

bool overlaps_existing_replacement(RewriteContext &r_context, int p_start, int p_end) {
	for (const Replacement &replacement : r_context.replacements) {
		if (p_start < replacement.end && p_end > replacement.start) {
			return true;
		}
	}

	return false;
}

bool is_export_whitespace(char32_t p_char) {
	return p_char == ' ' || p_char == '\t' || p_char == '\r';
}

bool is_whitespace_only_line(const String &p_source, int p_start, int p_end) {
	for (int i = p_start; i < p_end; i++) {
		if (!is_export_whitespace(p_source[i])) {
			return false;
		}
	}

	return true;
}

void collect_string_literal_lines(const String &p_source, HashSet<int> &r_string_lines) {
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);
	for (auto token = tokenizer.scan(); token.type != GDScriptTokenizer::Token::TK_EOF; token = tokenizer.scan()) {
		if (token.type != GDScriptTokenizer::Token::LITERAL) {
			continue;
		}
		const Variant::Type type = token.literal.get_type();
		if (type != Variant::STRING && type != Variant::STRING_NAME && type != Variant::NODE_PATH) {
			continue;
		}
		for (int line = token.start_line; line <= token.end_line; line++) {
			r_string_lines.insert(line);
		}
	}
}

void collect_comment_replacements(RewriteContext &r_context, const GDScriptParser &p_parser) {
	if (!r_context.options.strip_comments) {
		return;
	}

#ifdef TOOLS_ENABLED
	for (const KeyValue<int, GDScriptTokenizer::CommentData> &comment_kv : p_parser.comment_data) {
		const int line = comment_kv.key;
		const GDScriptTokenizer::CommentData &comment = comment_kv.value;
		const int line_start = get_line_start_offset(r_context, line);
		const int line_end = get_line_end_offset(r_context, line);
		if (line_start < 0 || line_end < line_start) {
			continue;
		}

		Replacement replacement;
		if (comment.new_line) {
			replacement.start = line_start;
			if (line < r_context.line_offsets.size()) {
				replacement.end = r_context.line_offsets[line];
			} else {
				replacement.end = line_end;
			}
		} else {
			const String line_text = r_context.source.substr(line_start, line_end - line_start);
			const int comment_offset = line_text.find(comment.comment);
			if (comment_offset < 0) {
				continue;
			}
			replacement.start = line_start + comment_offset;
			replacement.end = line_end;
		}
		if (overlaps_existing_replacement(r_context, replacement.start, replacement.end)) {
			continue;
		}
		replacement.text = "";
		r_context.replacements.push_back(replacement);
	}
#endif
}

void collect_empty_line_replacements(RewriteContext &r_context) {
	if (!r_context.options.strip_empty_lines) {
		return;
	}

	HashSet<int> string_lines;
	collect_string_literal_lines(r_context.source, string_lines);

	for (int line = 1; line <= r_context.line_offsets.size(); line++) {
		if (string_lines.has(line)) {
			continue;
		}

		const int line_start = get_line_start_offset(r_context, line);
		const int line_end = get_line_end_offset(r_context, line);
		if (line_start < 0 || line_end < line_start || !is_whitespace_only_line(r_context.source, line_start, line_end)) {
			continue;
		}

		Replacement replacement;
		replacement.start = line_start;
		if (line < r_context.line_offsets.size()) {
			replacement.end = r_context.line_offsets[line];
		} else {
			replacement.end = line_end;
		}
		if (replacement.end <= replacement.start) {
			continue;
		}
		replacement.text = "";
		r_context.replacements.push_back(replacement);
	}
}

} // namespace WGodotGDScriptExportTransform
