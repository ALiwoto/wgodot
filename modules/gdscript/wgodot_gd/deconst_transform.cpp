// wgodot-changes::file
/**************************************************************************/
/*  deconst_transform.cpp                                                 */
/**************************************************************************/

#include "deconst_transform.h"

#include "core/error/error_macros.h"
#include "core/variant/variant_parser.h"

namespace {

String variant_to_source(const Variant &p_value) {
	String text;
	if (VariantWriter::write_to_string(p_value, text) != OK) {
		return String();
	}

	return text;
}

bool should_mangle_constant(const GDScriptParser::ConstantNode *p_constant) {
	return p_constant != nullptr && !p_constant->wgodot_no_mangle;
}

} // namespace

namespace WGodotGDScriptExportTransform {

bool should_deconst_constant(const RewriteContext &p_context, const GDScriptParser::ConstantNode *p_constant) {
	return p_context.options.deconst_exports && should_mangle_constant(p_constant) && !p_context.no_mangle_constants.has(p_constant);
}

bool is_declared_constant_identifier(const RewriteContext &p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	if (p_identifier->source != GDScriptParser::IdentifierNode::LOCAL_CONSTANT &&
			p_identifier->source != GDScriptParser::IdentifierNode::MEMBER_CONSTANT) {
		return false;
	}

	return should_deconst_constant(p_context, p_identifier->constant_source);
}

void add_constant_reference_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const GDScriptParser::ConstantNode *p_constant) {
	ERR_FAIL_NULL(p_constant);
	ERR_FAIL_NULL(p_constant->initializer);

	const String text = variant_to_source(p_constant->initializer->reduced_value);
	if (text.is_empty()) {
		return;
	}

	add_replacement(r_context, p_node, text);
}

void add_constant_declaration_replacement(RewriteContext &r_context, const GDScriptParser::ConstantNode *p_constant, bool p_leave_pass) {
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

	int start = get_offset(r_context, start_line, start_column);
	int end = get_offset(r_context, p_constant->end_line, p_constant->end_column);
	if (start < 0 || end < start) {
		return;
	}

	const int line_end = get_line_end_offset(r_context, p_constant->end_line);
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
		const int line_start = get_line_start_offset(r_context, start_line);
		if (line_start >= 0) {
			start = line_start;
		}
		if (p_constant->end_line < r_context.line_offsets.size()) {
			end = r_context.line_offsets[p_constant->end_line];
		}
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_leave_pass ? "pass" : "";
	r_context.replacements.push_back(replacement);
}

} // namespace WGodotGDScriptExportTransform
