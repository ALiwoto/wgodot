// wgodot-changes::file
/**************************************************************************/
/*  source_rewrite.cpp                                                    */
/**************************************************************************/

#include "source_rewrite.h"

#include "core/error/error_macros.h"
#include "core/math/math_funcs.h"

namespace {

struct ReplacementSort {
	bool operator()(const WGodotGDScriptExportTransform::Replacement &p_left, const WGodotGDScriptExportTransform::Replacement &p_right) const {
		if (p_left.start == p_right.start) {
			return p_left.end > p_right.end;
		}
		return p_left.start < p_right.start;
	}
};

} // namespace

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

	return CLAMP(line_start + MAX(p_column - 1, 0), 0, p_context.source.length());
}

void add_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const String &p_text) {
	ERR_FAIL_NULL(p_node);

	const int start = get_offset(r_context, p_node->start_line, p_node->start_column);
	const int end = get_offset(r_context, p_node->end_line, p_node->end_column);
	if (start < 0 || end < start) {
		return;
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_text;
	r_context.replacements.push_back(replacement);
}

String apply_replacements(RewriteContext &r_context) {
	r_context.replacements.sort_custom<ReplacementSort>();

	String result = r_context.source;
	int last_start = result.length() + 1;
	for (int i = r_context.replacements.size() - 1; i >= 0; i--) {
		const Replacement &replacement = r_context.replacements[i];
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

} // namespace WGodotGDScriptExportTransform
