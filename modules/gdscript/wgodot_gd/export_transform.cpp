// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "deadcode_injection.h"
#include "deconst_transform.h"
#include "deenum_transform.h"
#include "export_context.h"
#include "name_obfuscation.h"
#include "obfuscation_names.h"
#include "source_rewrite.h"
#include "string_obfuscation.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"
#include "../gdscript_tokenizer.h"
#include "../gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/char_utils.h"
#include "core/templates/list.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "core/variant/variant_parser.h"

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

void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
void collect_expression_replacements(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_expression, bool p_no_mangle_scope);
void collect_node_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
bool overlaps_existing_replacement(const RewriteContext &p_context, int p_start, int p_end);

bool is_supported_builtin_class_alias_target(const StringName &p_name) {
	if (p_name.is_empty()) {
		return false;
	}

	if (ClassDB::class_exists(p_name) && ClassDB::is_class_exposed(p_name)) {
		return true;
	}

	return GDScriptParser::get_builtin_type(p_name) < Variant::VARIANT_MAX;
}

void get_or_create_builtin_class_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_class_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_class_alias(p_name);
}

bool is_supported_builtin_function_alias_target(const StringName &p_name) {
	return !p_name.is_empty() && (Variant::has_utility_function(p_name) || GDScriptUtilityFunctions::function_exists(p_name));
}

StringName get_builtin_alias_owner_from_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_hard_type() || p_type.is_variant()) {
		return StringName();
	}

	if (p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type < Variant::VARIANT_MAX) {
		const StringName type_name = Variant::get_type_name(p_type.builtin_type);
		return type_name == SNAME("Variant") ? StringName() : type_name;
	}

	if (!p_type.native_type.is_empty() && ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type)) {
		return p_type.native_type;
	}

	return StringName();
}

bool is_supported_builtin_member_alias_target(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return false;
	}

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_owner);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (p_property) {
			if (p_static) {
				return Variant::has_constant(builtin_type, p_name) || Variant::has_enum(builtin_type, p_name) || Variant::get_enum_for_enumeration(builtin_type, p_name) != StringName();
			}

			Callable::CallError err;
			Variant dummy;
			Variant::construct(builtin_type, dummy, nullptr, 0, err);
			if (err.error != Callable::CallError::CALL_OK) {
				return false;
			}

			List<PropertyInfo> properties;
			dummy.get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.name == p_name) {
					return true;
				}
			}
			return false;
		}

		if (!Variant::has_builtin_method(builtin_type, p_name)) {
			return false;
		}

		const MethodInfo method_info = Variant::get_builtin_method_info(builtin_type, p_name);
		return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0);
	}

	if (!ClassDB::class_exists(p_owner) || !ClassDB::is_class_exposed(p_owner)) {
		return false;
	}

	if (p_property) {
		if (ClassDB::has_property(p_owner, p_name)) {
			return true;
		}
		if (p_static) {
			if (ClassDB::has_enum(p_owner, p_name)) {
				return true;
			}
			bool valid = false;
			(void)ClassDB::get_integer_constant(p_owner, p_name, &valid);
			return valid;
		}
		return false;
	}

	MethodInfo method_info;
	if (!ClassDB::get_method_info(p_owner, p_name, &method_info)) {
		return false;
	}

	return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0 || Engine::get_singleton()->has_singleton(p_owner));
}

void get_or_create_builtin_function_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_function_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_function_alias(p_name);
}

void add_builtin_function_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::IDENTIFIER) {
		return;
	}

	const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(p_call->callee);
	if (callee->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_function_alias(p_call->function_name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, callee, String(*alias));
}

void get_or_create_builtin_member_alias(ExportContext *p_context, const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_context == nullptr || !is_supported_builtin_member_alias_target(p_owner, p_name, p_static, p_property)) {
		return;
	}

	(void)p_context->get_or_create_builtin_member_alias(p_owner, p_name, p_static, p_property);
}

void add_builtin_method_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->function_name == SNAME("new") ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::SUBSCRIPT) {
		return;
	}

	const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute || subscript->base == nullptr || subscript->attribute == nullptr) {
		return;
	}

	const GDScriptParser::DataType base_type = subscript->base->get_datatype();
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_call->function_name, is_static, false);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, subscript->attribute, String(*alias));
}

void add_builtin_property_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_base == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType base_type = p_base->get_datatype();
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_identifier->name, is_static, true);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

void add_builtin_class_alias_name_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_identifier == nullptr || !is_supported_builtin_class_alias_target(p_identifier->name)) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_class_alias(p_identifier->name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

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

	if (r_context.options.obfuscate_strings) {
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

	const String text = get_export_string_literal_replacement(r_context, p_literal->value.get_type(), value);
	if (text.is_empty()) {
		return;
	}

	const int start = get_offset(r_context, p_literal->start_line, p_literal->start_column);
	const int end = get_offset(r_context, p_literal->end_line, p_literal->end_column);
	if (start < 0 || end < start || overlaps_existing_replacement(r_context, start, end)) {
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
	if (!r_context.options.obfuscate_strings || r_context.export_context == nullptr || p_binary == nullptr || p_binary->operation != GDScriptParser::BinaryOpNode::OP_ADDITION) {
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

	const String text = r_context.export_context->get_or_create_obfuscated_string_literal(Variant::STRING, value);
	if (text.is_empty()) {
		return false;
	}

	const int start = get_offset(r_context, p_binary->start_line, p_binary->start_column);
	const int end = get_offset(r_context, p_binary->end_line, p_binary->end_column);
	if (start < 0 || end < start || overlaps_existing_replacement(r_context, start, end)) {
		return false;
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = text;
	r_context.replacements.push_back(replacement);
	return true;
}

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
		add_global_class_name_reference_replacement(r_context, p_type->type_chain[0]);
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

bool should_strip_export_annotation(const GDScriptParser::AnnotationNode *p_annotation) {
	if (p_annotation == nullptr) {
		return false;
	}

	static const StringName stripped_annotations[] = {
		SNAME("@private"),
		SNAME("@no_mangle"),
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

bool overlaps_existing_replacement(const RewriteContext &p_context, int p_start, int p_end) {
	for (const Replacement &replacement : p_context.replacements) {
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

bool can_be_raw_string_prefix(const String &p_source, int p_quote_index) {
	if (p_quote_index <= 0 || p_source[p_quote_index - 1] != 'r') {
		return false;
	}

	if (p_quote_index <= 1) {
		return true;
	}

	const char32_t previous = p_source[p_quote_index - 2];
	return !is_unicode_identifier_continue(previous);
}

void collect_string_literal_lines(const String &p_source, HashSet<int> &r_string_lines) {
	bool in_string = false;
	bool is_raw = false;
	bool is_multiline = false;
	char32_t quote_char = 0;
	int line = 1;

	for (int i = 0; i < p_source.length(); i++) {
		const char32_t ch = p_source[i];
		if (in_string) {
			r_string_lines.insert(line);

			if (ch == '\\') {
				if (is_raw) {
					if (i + 1 < p_source.length() && (p_source[i + 1] == quote_char || p_source[i + 1] == '\\')) {
						i++;
					}
				} else if (i + 1 < p_source.length()) {
					i++;
					if (p_source[i] == '\n') {
						line++;
					}
				}
				continue;
			}

			if (ch == quote_char) {
				if (is_multiline) {
					if (i + 2 < p_source.length() && p_source[i + 1] == quote_char && p_source[i + 2] == quote_char) {
						i += 2;
						in_string = false;
					}
				} else {
					in_string = false;
				}
			} else if (ch == '\n') {
				line++;
			}
			continue;
		}

		if (ch == '#') {
			while (i + 1 < p_source.length() && p_source[i + 1] != '\n') {
				i++;
			}
			continue;
		}

		if (ch == '"' || ch == '\'') {
			in_string = true;
			is_raw = can_be_raw_string_prefix(p_source, i);
			is_multiline = i + 2 < p_source.length() && p_source[i + 1] == ch && p_source[i + 2] == ch;
			quote_char = ch;
			r_string_lines.insert(line);
			if (is_multiline) {
				i += 2;
			}
			continue;
		}

		if (ch == '\n') {
			line++;
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
		if (overlaps_existing_replacement(r_context, replacement.start, replacement.end)) {
			continue;
		}
		replacement.text = "";
		r_context.replacements.push_back(replacement);
	}
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

void collect_member_names(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope) {
	if (!r_context.options.obfuscate_names || p_node == nullptr) {
		return;
	}

	if (p_node->type == GDScriptParser::Node::CLASS) {
		collect_member_name_obfuscation(r_context, static_cast<const GDScriptParser::ClassNode *>(p_node), p_no_mangle_scope);
	}
}

String get_source_line_text(const String &p_source, int p_line) {
	if (p_line <= 0) {
		return String();
	}

	int current_line = 1;
	int line_start = 0;
	for (int i = 0; i <= p_source.length(); i++) {
		if (i == p_source.length() || p_source[i] == '\n') {
			if (current_line == p_line) {
				int line_end = i;
				if (line_end > line_start && p_source[line_end - 1] == '\r') {
					line_end--;
				}
				return p_source.substr(line_start, line_end - line_start);
			}
			current_line++;
			line_start = i + 1;
		}
	}

	return String();
}

String make_caret_line(int p_column) {
	const int caret_column = MAX(p_column, 1);
	String caret;
	for (int i = 1; i < caret_column; i++) {
		caret += " ";
	}
	caret += "^";
	return caret;
}

String get_parser_errors_with_source_text(const GDScriptParser &p_parser, const String &p_source) {
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
		const String source_line = get_source_line_text(p_source, error.start_line);
		if (!source_line.is_empty()) {
			details += "\n    " + source_line;
			details += "\n    " + make_caret_line(error.start_column);
		}
		error_count++;
	}

	return details;
}

bool parse_only(const String &p_source, const String &p_path, String *r_error_details = nullptr) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_with_source_text(parser, p_source);
		}
		return false;
	}

	return true;
}

void collect_builtin_class_aliases_from_type(ExportContext *p_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	if (!p_type->type_chain.is_empty() && p_type->type_chain[0] != nullptr) {
		get_or_create_builtin_class_alias(p_context, p_type->type_chain[0]->name);
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		collect_builtin_class_aliases_from_type(p_context, container_type);
	}
}

void collect_builtin_class_aliases_from_expression(ExportContext *p_context, const GDScriptParser::ExpressionNode *p_expression);
void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node);

void collect_builtin_class_aliases_from_identifier(ExportContext *p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_context == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType datatype = p_identifier->get_datatype();
	if (p_identifier->source == GDScriptParser::IdentifierNode::NATIVE_CLASS) {
		get_or_create_builtin_class_alias(p_context, !datatype.native_type.is_empty() ? datatype.native_type : p_identifier->name);
	} else if (p_identifier->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE && datatype.is_meta_type) {
		if (datatype.kind == GDScriptParser::DataType::BUILTIN) {
			get_or_create_builtin_class_alias(p_context, Variant::get_type_name(datatype.builtin_type));
		} else if (datatype.kind == GDScriptParser::DataType::NATIVE) {
			get_or_create_builtin_class_alias(p_context, datatype.native_type);
		}
	}
}

void collect_builtin_class_aliases_from_pattern(ExportContext *p_context, const GDScriptParser::PatternNode *p_pattern) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			collect_builtin_class_aliases_from_expression(p_context, p_pattern->expression);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (const GDScriptParser::PatternNode *sub_pattern : p_pattern->array) {
				collect_builtin_class_aliases_from_pattern(p_context, sub_pattern);
			}
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				collect_builtin_class_aliases_from_expression(p_context, pair.key);
				collect_builtin_class_aliases_from_pattern(p_context, pair.value_pattern);
			}
			break;
		default:
			break;
	}
}

void collect_builtin_class_aliases_from_expression(ExportContext *p_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::IDENTIFIER:
			collect_builtin_class_aliases_from_identifier(p_context, static_cast<const GDScriptParser::IdentifierNode *>(p_expression));
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->attribute != nullptr) {
				const GDScriptParser::DataType base_type = subscript->base->get_datatype();
				const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
				if (!owner.is_empty()) {
					get_or_create_builtin_member_alias(p_context, owner, subscript->attribute->name, base_type.is_meta_type, true);
				}
			}
			collect_builtin_class_aliases_from_expression(p_context, subscript->base);
			if (!subscript->is_attribute) {
				collect_builtin_class_aliases_from_expression(p_context, subscript->index);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_builtin_class_aliases_from_expression(p_context, element);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, assignment->assignee);
			collect_builtin_class_aliases_from_expression(p_context, assignment->assigned_value);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, binary->left_operand);
			collect_builtin_class_aliases_from_expression(p_context, binary->right_operand);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			if (!call->is_super && call->callee != nullptr && call->callee->type == GDScriptParser::Node::IDENTIFIER) {
				const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(call->callee);
				if (callee->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
					get_or_create_builtin_function_alias(p_context, call->function_name);
				}
			} else if (!call->is_super && call->function_name != SNAME("new") && call->callee != nullptr && call->callee->type == GDScriptParser::Node::SUBSCRIPT) {
				const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(call->callee);
				if (subscript->is_attribute && subscript->base != nullptr && subscript->attribute != nullptr) {
					const GDScriptParser::DataType base_type = subscript->base->get_datatype();
					const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
					if (!owner.is_empty()) {
						get_or_create_builtin_member_alias(p_context, owner, call->function_name, base_type.is_meta_type, false);
					}
				}
			}
			collect_builtin_class_aliases_from_expression(p_context, call->callee);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_builtin_class_aliases_from_expression(p_context, argument);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, cast->operand);
			collect_builtin_class_aliases_from_type(p_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_builtin_class_aliases_from_expression(p_context, pair.key);
				collect_builtin_class_aliases_from_expression(p_context, pair.value);
			}
		} break;
		case GDScriptParser::Node::LAMBDA:
			collect_builtin_class_aliases_from_node(p_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function);
			break;
		case GDScriptParser::Node::PRELOAD:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path);
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, ternary->condition);
			collect_builtin_class_aliases_from_expression(p_context, ternary->true_expr);
			collect_builtin_class_aliases_from_expression(p_context, ternary->false_expr);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, type_test->operand);
			collect_builtin_class_aliases_from_type(p_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
			break;
		default:
			break;
	}
}

void collect_builtin_class_aliases_from_annotations(ExportContext *p_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		for (const GDScriptParser::ExpressionNode *argument : annotation->arguments) {
			collect_builtin_class_aliases_from_expression(p_context, argument);
		}
	}
}

void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	collect_builtin_class_aliases_from_annotations(p_context, p_node);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			for (const GDScriptParser::IdentifierNode *identifier : class_node->extends) {
				if (identifier != nullptr) {
					get_or_create_builtin_class_alias(p_context, identifier->name);
				}
			}
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				collect_builtin_class_aliases_from_node(p_context, member.get_source_node());
			}
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, constant->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, constant->initializer);
		} break;
		case GDScriptParser::Node::FUNCTION: {
			const GDScriptParser::FunctionNode *function = static_cast<const GDScriptParser::FunctionNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				collect_builtin_class_aliases_from_node(p_context, parameter);
			}
			collect_builtin_class_aliases_from_node(p_context, function->rest_parameter);
			collect_builtin_class_aliases_from_type(p_context, function->return_type);
			collect_builtin_class_aliases_from_node(p_context, function->body);
		} break;
		case GDScriptParser::Node::PARAMETER: {
			const GDScriptParser::ParameterNode *parameter = static_cast<const GDScriptParser::ParameterNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, parameter->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, parameter->initializer);
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				collect_builtin_class_aliases_from_node(p_context, parameter);
			}
		} break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			for (const GDScriptParser::Node *statement : suite->statements) {
				collect_builtin_class_aliases_from_node(p_context, statement);
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, variable->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, variable->initializer);
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
				collect_builtin_class_aliases_from_node(p_context, variable->setter);
				collect_builtin_class_aliases_from_node(p_context, variable->getter);
			}
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, for_node->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, for_node->list);
			collect_builtin_class_aliases_from_node(p_context, for_node->loop);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, if_node->condition);
			collect_builtin_class_aliases_from_node(p_context, if_node->true_block);
			collect_builtin_class_aliases_from_node(p_context, if_node->false_block);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, match_node->test);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				collect_builtin_class_aliases_from_node(p_context, branch);
			}
		} break;
		case GDScriptParser::Node::MATCH_BRANCH: {
			const GDScriptParser::MatchBranchNode *branch = static_cast<const GDScriptParser::MatchBranchNode *>(p_node);
			for (const GDScriptParser::PatternNode *pattern : branch->patterns) {
				collect_builtin_class_aliases_from_pattern(p_context, pattern);
			}
			collect_builtin_class_aliases_from_node(p_context, branch->guard_body);
			collect_builtin_class_aliases_from_node(p_context, branch->block);
		} break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, assert_node->condition);
			collect_builtin_class_aliases_from_expression(p_context, assert_node->message);
		} break;
		case GDScriptParser::Node::RETURN:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::ReturnNode *>(p_node)->return_value);
			break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, while_node->condition);
			collect_builtin_class_aliases_from_node(p_context, while_node->loop);
		} break;
		default:
			if (p_node->is_expression()) {
				collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node));
			}
			break;
	}
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

} // namespace

namespace WGodotGDScriptExportTransform {

void register_project_settings() {
	GLOBAL_DEF("wgodot/gdscript/disable_embedded_gdscript", true);
	GLOBAL_DEF("wgodot/gdscript/strict_override_checking", true);
	GLOBAL_DEF("wgodot/gdscript/strict_type_checking", true);
	GLOBAL_DEF("wgodot/gdscript/strict_signal_callable_checking", true);
	GLOBAL_DEF("wgodot/export/deconst_exports", true);
	GLOBAL_DEF("wgodot/export/obfuscate_names", true);
	GLOBAL_DEF("wgodot/export/obfuscate_builtin_names", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/obfuscation_strategy", PROPERTY_HINT_ENUM, "Short,Hash,Unicode"), OBFUSCATION_STRATEGY_SHORT);
	GLOBAL_DEF("wgodot/export/obfuscate_file_paths", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/obfuscate_file_paths_strategy", PROPERTY_HINT_ENUM, "Short,Hash,Unicode"), OBFUSCATION_STRATEGY_SHORT);
	GLOBAL_DEF("wgodot/export/obfuscate_strings", true);
	GLOBAL_DEF("wgodot/export/dead_code_injection_enabled", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/min_in_class_dead_code_injection", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), 10);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/max_in_class_dead_code_injection", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), 20);
}

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "wgodot/export/deconst_exports");
	options.obfuscate_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_names");
	options.obfuscate_builtin_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_builtin_names");
	options.obfuscate_file_paths = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_file_paths");
	options.obfuscate_strings = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_strings");
	options.dead_code_injection_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/dead_code_injection_enabled");
	options.min_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/min_in_class_dead_code_injection");
	options.max_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/max_in_class_dead_code_injection");

	const int obfuscation_strategy = GLOBAL_GET_CACHED(int, "wgodot/export/obfuscation_strategy");
	if (obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.obfuscation_strategy = static_cast<ObfuscationStrategy>(obfuscation_strategy);
	}

	const int file_path_obfuscation_strategy = GLOBAL_GET_CACHED(int, "wgodot/export/obfuscate_file_paths_strategy");
	if (file_path_obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && file_path_obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.file_path_obfuscation_strategy = static_cast<ObfuscationStrategy>(file_path_obfuscation_strategy);
	}

	return options;
}

void prescan_project_scripts(ExportContext *p_context, const HashSet<String> &p_paths) {
	if (p_context == nullptr) {
		return;
	}

	const TransformOptions &options = p_context->get_options();
	if (!options.obfuscate_names && !options.obfuscate_builtin_names && !options.obfuscate_file_paths && !options.obfuscate_strings && !options.dead_code_injection_enabled) {
		return;
	}

	Vector<ScriptSource> scripts;
	Vector<GlobalClassRenameRequest> global_class_rename_requests;
	for (const String &path : p_paths) {
		p_context->reserve_script_path(path);

		if (path.get_extension() != "gd") {
			continue;
		}

		const Vector<uint8_t> file = FileAccess::get_file_as_bytes(path);
		if (file.is_empty()) {
			continue;
		}

		const String raw_source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
		const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(raw_source, path, options);
		ScriptSource script;
		script.path = path;
		script.source = source;
		scripts.push_back(script);

		AnalyzedSource analyzed_source;
		if (!analyzed_source.load(source, path)) {
			reserve_script_global_class_name_from_source(p_context, source, path);
			continue;
		}

		if (options.obfuscate_names) {
			collect_global_class_rename_request(p_context, analyzed_source.parser->get_tree(), path, global_class_rename_requests);
		}
		if (options.obfuscate_builtin_names) {
			collect_builtin_class_aliases_from_node(p_context, analyzed_source.parser->get_tree());
		}
		if (options.obfuscate_file_paths && has_obfuscate_path_annotation(analyzed_source.parser->get_tree())) {
			(void)p_context->get_or_create_script_path_rename(path);
		}
	}

	if (options.obfuscate_names) {
		for (const GlobalClassRenameRequest &request : global_class_rename_requests) {
			(void)p_context->get_or_create_global_class_rename(request.name, request.path);
		}
	}

	if (options.obfuscate_names) {
		for (const ScriptSource &script : scripts) {
			AnalyzedSource analyzed_source;
			if (!analyzed_source.load(script.source, script.path)) {
				continue;
			}

			p_context->index_script(analyzed_source.parser->get_tree(), script.path);
		}
	}

	if (options.obfuscate_strings) {
		TransformOptions injected_source_options = options;
		injected_source_options.dead_code_injection_enabled = false;
		for (const ScriptSource &script : scripts) {
			(void)transform_source(script.source, script.path, injected_source_options, p_context, nullptr);
		}
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
				class_dict["class"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
		}

		if (class_dict.has("base")) {
			const StringName base_name = class_dict["base"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename(base_name)) {
				class_dict["base"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
		}

		if (class_dict.has("path")) {
			const String path = class_dict["path"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename_by_path(path)) {
				class_dict["class"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
			const String obfuscated_path = p_context->get_exported_script_path(path);
			if (!obfuscated_path.is_empty()) {
				class_dict["path"] = obfuscated_path;
			}
		}

		(*r_global_class_list)[i] = class_dict;
	}
}

String transform_source(const String &p_source, const String &p_path, bool *r_changed) {
	return transform_source(p_source, p_path, setup_params(), nullptr, r_changed);
}

String transform_source(const String &p_source, const String &p_path, ExportContext *p_context, bool *r_changed) {
	const TransformOptions options = p_context != nullptr ? p_context->get_options() : setup_params();
	return transform_source(p_source, p_path, options, p_context, r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed) {
	return transform_source(p_source, p_path, p_options, nullptr, r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, ExportContext *p_context, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.deconst_exports && !p_options.obfuscate_names && !p_options.obfuscate_builtin_names && !p_options.obfuscate_file_paths && !p_options.obfuscate_strings && !p_options.dead_code_injection_enabled && !p_options.strip_comments && !p_options.strip_empty_lines) {
		return p_source;
	}

	bool dead_code_changed = false;
	const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(p_source, p_path, p_options, &dead_code_changed);

	AnalyzedSource analyzed_source;
	if (!analyzed_source.load(source, p_path)) {
		return p_source;
	}

	ExportContext local_context;
	ExportContext *export_context = p_context;
	const bool using_local_context = export_context == nullptr && (p_options.obfuscate_names || p_options.obfuscate_builtin_names || p_options.obfuscate_file_paths || p_options.obfuscate_strings);
	if (using_local_context) {
		local_context.reset();
		export_context = &local_context;
	}
	if (export_context != nullptr) {
		export_context->set_options(p_options);
	}
	if (using_local_context && p_options.obfuscate_builtin_names) {
		collect_builtin_class_aliases_from_node(export_context, analyzed_source.parser->get_tree());
	}

	const GDScriptParser::ClassNode *tree = analyzed_source.parser->get_tree();

	RewriteContext context;
	context.source = source;
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
	collect_member_names(context, tree, false);
	collect_node_replacements(context, tree, false);
	collect_comment_replacements(context, *analyzed_source.parser);
	collect_empty_line_replacements(context);
	if (context.replacements.is_empty()) {
		if (dead_code_changed && r_changed != nullptr) {
			*r_changed = true;
		}
		return source;
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
