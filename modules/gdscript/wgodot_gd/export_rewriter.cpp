// wgodot-changes::file

#include "export_transform_internal.h"
#include "../gdscript_tokenizer.h"

namespace WGodotGDScriptExportTransform {

void add_extends_path_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (r_context.export_context == nullptr || p_class == nullptr || p_class->extends_path.is_empty()) {
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
			String source_extends_path;
			if (token.type == GDScriptTokenizer::Token::LITERAL && token.literal.get_type() == Variant::STRING) {
				source_extends_path = token.literal;
				if (source_extends_path.is_relative_path()) {
					source_extends_path = r_context.script_path.get_base_dir().path_join(source_extends_path).simplify_path();
				}
			}
			if (source_extends_path == p_class->extends_path) {
				const int start = get_offset(r_context, token.start_line, token.start_column);
				const int end = get_offset(r_context, token.end_line, token.end_column);
				if (start >= 0 && end >= start && !overlaps_existing_replacement(r_context, start, end)) {
					const String text = get_export_string_literal_replacement(r_context, Variant::STRING, p_class->extends_path);
					if (text.is_empty()) {
						return;
					}
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
