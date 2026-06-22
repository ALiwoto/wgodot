// wgodot-changes::file
/**************************************************************************/
/*  export_no_export_blocks.cpp                                           */
/**************************************************************************/

#include "export_no_export_blocks.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptExportTransform {

static const char *NO_EXPORT_BEGIN_MARKER = "#wgodot::no_export::begin";
static const char *NO_EXPORT_END_MARKER = "#wgodot::no_export::end";

String strip_no_export_blocks(const String &p_source, const String &p_path, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	String stripped;
	bool in_no_export_block = false;
	int line_number = 1;

	for (int line_start = 0; line_start < p_source.length();) {
		int content_end = line_start;
		while (content_end < p_source.length() && p_source[content_end] != '\n' && p_source[content_end] != '\r') {
			content_end++;
		}

		int next_line = content_end;
		String newline;
		if (next_line < p_source.length()) {
			if (p_source[next_line] == '\r') {
				if (next_line + 1 < p_source.length() && p_source[next_line + 1] == '\n') {
					newline = "\r\n";
					next_line += 2;
				} else {
					newline = "\r";
					next_line++;
				}
			} else {
				newline = "\n";
				next_line++;
			}
		}

		const String line = p_source.substr(line_start, content_end - line_start);
		const String trimmed_line = line.strip_edges();
		const bool is_begin_marker = trimmed_line == NO_EXPORT_BEGIN_MARKER;
		const bool is_end_marker = trimmed_line == NO_EXPORT_END_MARKER;

		if (is_begin_marker) {
			if (in_no_export_block) {
				WARN_PRINT("Nested #wgodot::no_export::begin in '" + p_path + "' at line " + itos(line_number) + ". Nested no_export blocks are not supported; ignoring nested begin marker.");
			} else {
				in_no_export_block = true;
			}
			if (r_changed != nullptr) {
				*r_changed = true;
			}
			stripped += newline;
		} else if (is_end_marker) {
			if (!in_no_export_block) {
				WARN_PRINT("Unmatched #wgodot::no_export::end in '" + p_path + "' at line " + itos(line_number) + ". Stripping the directive line anyway.");
			} else {
				in_no_export_block = false;
			}
			if (r_changed != nullptr) {
				*r_changed = true;
			}
			stripped += newline;
		} else if (in_no_export_block) {
			if (r_changed != nullptr) {
				*r_changed = true;
			}
			stripped += newline;
		} else {
			stripped += line;
			stripped += newline;
		}

		line_start = next_line;
		line_number++;
	}

	if (in_no_export_block) {
		WARN_PRINT("Unclosed #wgodot::no_export::begin in '" + p_path + "'. Stripped until end of file.");
	}

	return stripped;
}

} // namespace WGodotGDScriptExportTransform
