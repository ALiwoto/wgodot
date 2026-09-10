// wgodot-changes::file
/**************************************************************************/
/*  source_rewrite.cpp                                                    */
/**************************************************************************/

#include "source_rewrite.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptExportTransform {

void build_line_offsets(RewriteContext &r_context) {
	r_context.line_offsets.clear();
	r_context.line_offsets.push_back(0);

	for (int i = 0; i < r_context.source.length(); i++) {
		if (r_context.source[i] == '\n') {
			r_context.line_offsets.push_back(i + 1);
		}
	}
}

int get_line_start_offset(const RewriteContext &p_context, int p_line) {
	if (p_line <= 0 || p_line > p_context.line_offsets.size()) {
		return -1;
	}

	return p_context.line_offsets[p_line - 1];
}

int get_line_end_offset(const RewriteContext &p_context, int p_line) {
	const int line_start = get_line_start_offset(p_context, p_line);
	if (line_start < 0) {
		return -1;
	}

	if (p_line < p_context.line_offsets.size()) {
		return p_context.line_offsets[p_line] - 1;
	}

	return p_context.source.length();
}

int get_offset(const RewriteContext &p_context, int p_line, int p_column) {
	const int line_start = get_line_start_offset(p_context, p_line);
	if (line_start < 0) {
		return -1;
	}

	return p_column > 0 ? line_start + p_column - 1 : -1;
}

void add_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const String &p_text) {
	ERR_FAIL_NULL(p_node);

	const int start = get_offset(r_context, p_node->start_line, p_node->start_column);
	const int end = get_offset(r_context, p_node->end_line, p_node->end_column);
	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_text;
	r_context.replacements.push_back(replacement);
}

} // namespace WGodotGDScriptExportTransform
